#include "PFPointCloudSceneProxy.h"

#include "PFPointCloudComponent.h"
#include "PFOctreeStore.h"

#include "MaterialDomain.h"              // MD_Surface
#include "Materials/Material.h"          // UMaterial::GetDefaultMaterial
#include "Materials/MaterialInterface.h"
#include "SceneManagement.h"
#include "SceneView.h"
#include "ConvexVolume.h"
#include "PrimitiveUniformShaderParametersBuilder.h"

// GPU footprint per source point: 4 verts (FStaticMeshVertexBuffers ~28 B each:
// position 12 + tangents 8 + UV 4 + color 4) + 6 index entries (4 B each).
static constexpr int64 kBytesPerGpuPoint = 4 * 28 + 6 * 4; // = 136

void FPFNodeRender::ReleaseResources()
{
	Buffers.PositionVertexBuffer.ReleaseResource();
	Buffers.StaticMeshVertexBuffer.ReleaseResource();
	Buffers.ColorVertexBuffer.ReleaseResource();
	IndexBuffer.ReleaseResource();
	VertexFactory.ReleaseResource();
}

SIZE_T FPFPointCloudSceneProxy::GetTypeHash() const
{
	static size_t UniquePointer;
	return reinterpret_cast<SIZE_T>(&UniquePointer);
}

FPFPointCloudSceneProxy::FPFPointCloudSceneProxy(const UPFPointCloudComponent* InComponent)
	: FPrimitiveSceneProxy(InComponent)
{
	bWillEverBeLit = false;
	bVerifyUsedMaterials = false;

	Store = InComponent ? InComponent->Store : nullptr;
	Stats = InComponent ? InComponent->Stats : nullptr;
	UnitScale = InComponent ? InComponent->UnitScale : 100.f;

	SseBudgetPx = InComponent ? InComponent->SseBudgetPixels : 1.5f;
	GpuBudgetBytes = InComponent ? static_cast<int64>(InComponent->GpuBudgetMB) * 1024 * 1024 : (1024ll << 20);
	UploadsPerFrame = InComponent ? InComponent->UploadsPerFrame : 32;

	Material = InComponent ? InComponent->PointMaterial : nullptr;
	if (!Material)
	{
		Material = UMaterial::GetDefaultMaterial(MD_Surface);
	}
	MaterialRelevance = Material->GetRelevance_Concurrent(GetScene().GetFeatureLevel());
}

FPFPointCloudSceneProxy::~FPFPointCloudSceneProxy()
{
	for (auto& Pair : Resident)
	{
		if (Pair.Value)
		{
			Pair.Value->ReleaseResources();
		}
	}
	Resident.Empty();
}

void FPFPointCloudSceneProxy::SetTunables_RenderThread(float InSseBudgetPx, int64 InGpuBudgetBytes, int32 InUploadsPerFrame)
{
	SseBudgetPx = InSseBudgetPx;
	GpuBudgetBytes = InGpuBudgetBytes;
	UploadsPerFrame = InUploadsPerFrame;
}

void FPFPointCloudSceneProxy::CreateNode(FPFLoadResult& Load) const
{
	const int32 Idx = Load.NodeIndex;
	if (Resident.Contains(Idx))
	{
		return;
	}

	TUniquePtr<FPFNodeRender> Node = MakeUnique<FPFNodeRender>(GetScene().GetFeatureLevel());
	Node->NumPoints = Load.NumPoints;   // source points (Verts == *4, Indices == *6)
	Node->LastUsedFrame = LastFrameProcessed;

	if (Node->NumPoints > 0 && Load.Verts.Num() == Node->NumPoints * 4 && Load.Indices.Num() == Node->NumPoints * 6)
	{
		Node->Bytes = static_cast<int64>(Node->NumPoints) * kBytesPerGpuPoint;
		// InitFromDynamicVertex fills the vertex buffers + wires the vertex factory.
		Node->Buffers.InitFromDynamicVertex(&Node->VertexFactory, Load.Verts, /*NumTexCoords*/ 1);
		Node->IndexBuffer.Indices = MoveTemp(Load.Indices);
		BeginInitResource(&Node->Buffers.PositionVertexBuffer);
		BeginInitResource(&Node->Buffers.StaticMeshVertexBuffer);
		BeginInitResource(&Node->Buffers.ColorVertexBuffer);
		BeginInitResource(&Node->IndexBuffer);
		BeginInitResource(&Node->VertexFactory);

		ResidentBytesTotal += Node->Bytes;
		PointsOnGpuTotal += Node->NumPoints;
	}
	else
	{
		Node->NumPoints = 0; // empty / malformed → keep resident as a no-draw entry
	}

	Resident.Add(Idx, MoveTemp(Node));
}

void FPFPointCloudSceneProxy::EvictToBudget(uint32 FrameNumber) const
{
	if (ResidentBytesTotal <= GpuBudgetBytes)
	{
		return;
	}

	// Evict cheapest-to-lose first: combine staleness (frames since drawn) with
	// node depth, so deep leaves go before coarse ancestors. Root is sticky.
	const TArray<FPFNodeRecord>& Nodes = Store.IsValid() ? Store->GetNodes() : TArray<FPFNodeRecord>();
	const int32 RootIdx = Store.IsValid() ? Store->GetRootIndex() : INDEX_NONE;

	auto NodeLevel = [&](int32 Idx) -> uint8
	{
		return (Idx >= 0 && Idx < Nodes.Num()) ? Nodes[Idx].Level : 0;
	};

	TArray<int32> Keys;
	Resident.GetKeys(Keys);
	Keys.Sort([this, &NodeLevel, FrameNumber](const int32& A, const int32& B)
	{
		const FPFNodeRender* RA = Resident[A].Get();
		const FPFNodeRender* RB = Resident[B].Get();
		const uint32 StaleA = FrameNumber - (RA ? RA->LastUsedFrame : 0);
		const uint32 StaleB = FrameNumber - (RB ? RB->LastUsedFrame : 0);
		const uint8 LA = NodeLevel(A);
		const uint8 LB = NodeLevel(B);
		// Composite key: staleness dominates; ties broken by deeper level = evict first.
		const uint64 KA = (static_cast<uint64>(StaleA) << 8) | LA;
		const uint64 KB = (static_cast<uint64>(StaleB) << 8) | LB;
		return KA > KB;  // larger composite = evict sooner
	});

	for (int32 Key : Keys)
	{
		if (ResidentBytesTotal <= GpuBudgetBytes)
		{
			break;
		}
		TUniquePtr<FPFNodeRender>& Node = Resident[Key];
		if (!Node || Node->LastUsedFrame == FrameNumber)
		{
			continue;
		}
		if (Key == RootIdx)
		{
			continue;   // root is precious — keep resident
		}
		ResidentBytesTotal -= Node->Bytes;
		PointsOnGpuTotal -= Node->NumPoints;
		Node->ReleaseResources();
		Resident.Remove(Key);
		if (Store.IsValid())
		{
			Store->MarkEvicted(Key);
		}
	}
}

void FPFPointCloudSceneProxy::ProcessFrame(FMeshElementCollector& Collector, uint32 FrameNumber) const
{
	LastFrameProcessed = FrameNumber;

	// Absorb finished async loads (capped).
	if (Store.IsValid())
	{
		int32 Uploaded = 0;
		FPFLoadResult Result;
		while (Uploaded < UploadsPerFrame && Store->PopResult(Result))
		{
			CreateNode(Result);
			++Uploaded;
		}
	}

	EvictToBudget(FrameNumber);

	if (Stats.IsValid())
	{
		Stats->ResidentNodes.store(Resident.Num());
		Stats->ResidentBytes.store(ResidentBytesTotal);
		Stats->PointsOnGpu.store(PointsOnGpuTotal);
		Stats->PendingLoads.store(Store.IsValid() ? Store->PendingRequests() : 0);
	}
}

void FPFPointCloudSceneProxy::GetDynamicMeshElements(
	const TArray<const FSceneView*>& Views,
	const FSceneViewFamily& ViewFamily,
	uint32 VisibilityMap,
	FMeshElementCollector& Collector) const
{
	if (!Store.IsValid() || !Store->IsValid() || Material == nullptr)
	{
		return;
	}

	const uint32 FrameNumber = ViewFamily.FrameNumber;
	const bool bNewFrame = (FrameNumber != LastFrameProcessed);
	if (bNewFrame)
	{
		ProcessFrame(Collector, FrameNumber);
	}

	FMaterialRenderProxy* MaterialProxy = Material->GetRenderProxy();
	const FMatrix& LocalToWorldMatrix = GetLocalToWorld();
	const FVector WorldScale = LocalToWorldMatrix.GetScaleVector();
	const double Scale = FMath::Max3(FMath::Abs(WorldScale.X), FMath::Abs(WorldScale.Y), FMath::Abs(WorldScale.Z));
	const FVector Centre = Store->GetCubeCentre();

	const TArray<FPFNodeRecord>& Nodes = Store->GetNodes();

	for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ++ViewIndex)
	{
		if (!(VisibilityMap & (1 << ViewIndex)))
		{
			continue;
		}
		const FSceneView* View = Views[ViewIndex];
		const bool bPrimaryView = (ViewIndex == 0);

		const FConvexVolume& Frustum = View->ViewFrustum;
		const FVector ViewOrigin = View->ViewMatrices.GetViewOrigin();
		const double ViewportH = FMath::Max(1, View->UnscaledViewRect.Height());
		const double TanHalfFovInv = View->ViewMatrices.GetProjectionMatrix().M[1][1]; // = 1/tan(fovY/2)
		const double SsFactor = ViewportH * 0.5 * TanHalfFovInv;

		int32 VisibleNodes = 0;
		int32 DrawnNodes = 0;
		int64 DrawnPoints = 0;

		TArray<int32, TInlineAllocator<256>> Stack;
		Stack.Push(Store->GetRootIndex());

		while (Stack.Num() > 0)
		{
			const int32 Idx = Stack.Pop(EAllowShrinking::No);
			if (Idx < 0 || Idx >= Nodes.Num())
			{
				continue;
			}
			const FPFNodeRecord& Rec = Nodes[Idx];
			const FPFNodeCube& Cube = Store->GetCube(Idx);

			// Node cube in component-local space, then world.
			const FVector LocalMin(
				(Cube.Min[0] - Centre.X) * UnitScale,
				(Cube.Min[1] - Centre.Y) * UnitScale,
				(Cube.Min[2] - Centre.Z) * UnitScale);
			const double LocalSize = Cube.Size * UnitScale;
			const FVector LocalCentre = LocalMin + FVector(LocalSize * 0.5);
			const FVector WorldCentre = LocalToWorldMatrix.TransformPosition(LocalCentre);
			const FVector WorldExtent = FVector(LocalSize * 0.5 * Scale);

			if (!Frustum.IntersectBox(WorldCentre, WorldExtent))
			{
				continue;
			}
			++VisibleNodes;

			// Draw this node (with ancestors) if resident & GPU-ready.
			if (const TUniquePtr<FPFNodeRender>* Found = Resident.Find(Idx))
			{
				FPFNodeRender* Node = Found->Get();
				if (Node && Node->NumPoints > 0 && Node->VertexFactory.IsInitialized()
					&& Node->IndexBuffer.IndexBufferRHI.IsValid())
				{
					Node->LastUsedFrame = FrameNumber;

					FMeshBatch& Mesh = Collector.AllocateMesh();
					Mesh.VertexFactory = &Node->VertexFactory;
					Mesh.MaterialRenderProxy = MaterialProxy;
					Mesh.bWireframe = false;
					Mesh.ReverseCulling = IsLocalToWorldDeterminantNegative();
					Mesh.Type = PT_TriangleList;          // quad sprites (2 tris/point)
					Mesh.DepthPriorityGroup = SDPG_World;
					Mesh.bCanApplyViewModeOverrides = false;
					Mesh.CastShadow = false;

					FDynamicPrimitiveUniformBuffer& DUB = Collector.AllocateOneFrameResource<FDynamicPrimitiveUniformBuffer>();
					FPrimitiveUniformShaderParametersBuilder Builder;
					BuildUniformShaderParameters(Builder);
					DUB.Set(Collector.GetRHICommandList(), Builder);

					FMeshBatchElement& Element = Mesh.Elements[0];
					Element.PrimitiveUniformBufferResource = &DUB.UniformBuffer;
					Element.IndexBuffer = &Node->IndexBuffer;
					Element.FirstIndex = 0;
					Element.NumPrimitives = Node->NumPoints * 2;          // 2 triangles per point
					Element.MinVertexIndex = 0;
					Element.MaxVertexIndex = Node->NumPoints * 4 - 1;     // 4 verts per point
					Collector.AddMesh(ViewIndex, Mesh);

					++DrawnNodes;
					DrawnPoints += Node->NumPoints;
				}
			}
			else if (Store.IsValid())
			{
				Store->RequestLoad(Idx);
			}

			// Screen-space error: descend while projected spacing exceeds the budget.
			double Dist = (WorldCentre - ViewOrigin).Size();
			if (Dist < 1.0) Dist = 1.0;
			const double SpacingWorld = Store->GetNodeSpacing(Rec.Level) * UnitScale * Scale;
			const double Pixels = SpacingWorld * SsFactor / Dist;

			if (Pixels > SseBudgetPx && Rec.ChildMask != 0)
			{
				for (int32 O = 0; O < 8; ++O)
				{
					if (Rec.Children[O] != PF_NO_CHILD)
					{
						Stack.Push(static_cast<int32>(Rec.Children[O]));
					}
				}
			}
		}

		if (bPrimaryView && Stats.IsValid())
		{
			Stats->VisibleNodes.store(VisibleNodes);
			Stats->DrawnNodes.store(DrawnNodes);
			Stats->DrawnPoints.store(DrawnPoints);
		}
	}
}

FPrimitiveViewRelevance FPFPointCloudSceneProxy::GetViewRelevance(const FSceneView* View) const
{
	FPrimitiveViewRelevance Result;
	Result.bDrawRelevance = IsShown(View);
	Result.bDynamicRelevance = true;
	Result.bStaticRelevance = false;
	Result.bShadowRelevance = false;
	Result.bRenderInMainPass = ShouldRenderInMainPass();
	Result.bRenderCustomDepth = ShouldRenderCustomDepth();
	Result.bTranslucentSelfShadow = false;
	MaterialRelevance.SetPrimitiveViewRelevance(Result);
	Result.bVelocityRelevance = false;
	return Result;
}
