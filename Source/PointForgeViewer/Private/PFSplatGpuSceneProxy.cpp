// Copyright. PointForge Viewer — GPU Gaussian splat path (compute + GPU sort).

#include "PFSplatGpuSceneProxy.h"
#include "PFSplatComponent.h"
#include "PFSplatGpuShaders.h"

#include "GPUSort.h"                     // SortGPUBuffers (engine radix sort)
#include "LocalVertexFactory.h"
#include "MaterialDomain.h"
#include "Materials/Material.h"
#include "Materials/MaterialInterface.h"
#include "PackedNormal.h"
#include "PrimitiveUniformShaderParametersBuilder.h"
#include "RHICommandList.h"
#include "RHIResourceUtils.h"
#include "RenderGraphBuilder.h"
#include "RenderingThread.h"
#include "SceneInterface.h"
#include "SceneManagement.h"
#include "SceneView.h"
#include "SceneViewExtension.h"
#include "Misc/ScopeLock.h"

// ---------------------------------------------------------------------------
// GPU resources
// ---------------------------------------------------------------------------

/** Vertex/index buffer wrappers around RHI buffers created by hand (InitRHI does nothing). */
class FPFRawVertexBuffer final : public FVertexBuffer {};
class FPFRawIndexBuffer final : public FIndexBuffer {};

/** Quad vertex streams for one view (FLocalVertexFactory layout), rewritten by BuildQuads. */
struct FPFSplatViewSlot
{
	explicit FPFSplatViewSlot(ERHIFeatureLevel::Type InFeatureLevel)
		: VertexFactory(InFeatureLevel, "FPFSplatGpuView") {}

	FPFRawVertexBuffer PosVB, UvVB, ColVB;
	FShaderResourceViewRHIRef PosSRV, UvSRV, ColSRV;
	FUnorderedAccessViewRHIRef PosUAV, UvUAV, ColUAV;
	FLocalVertexFactory VertexFactory;

	// Skip the compute when this view's camera and the transform did not change.
	FMatrix LastLocalToView = FMatrix::Identity;
	FMatrix LastProjection = FMatrix::Identity;
	FIntPoint LastViewport = FIntPoint::ZeroValue;

	void Release()
	{
		VertexFactory.ReleaseResource();
		PosVB.ReleaseResource();
		UvVB.ReleaseResource();
		ColVB.ReleaseResource();
	}
};

struct FPFSplatGpuResources
{

	// Splat records (static).
	FBufferRHIRef SplatBuf;
	FShaderResourceViewRHIRef SplatSRV;

	// Sort ping-pong (keys = quantised depth, values = splat index).
	FBufferRHIRef KeyBuf[2], ValBuf[2];
	FShaderResourceViewRHIRef KeySRV[2], ValSRV[2];
	FUnorderedAccessViewRHIRef KeyUAV[2], ValUAV[2];

	// Per-splat projection results + visible counter.
	FBufferRHIRef ConicBuf, RadiusBuf, CountBuf;
	FShaderResourceViewRHIRef ConicSRV, RadiusSRV, CountSRV;
	FUnorderedAccessViewRHIRef ConicUAV, RadiusUAV, CountUAV;

	// Static streams shared by every view slot: constant tangents, quad indices.
	FPFRawVertexBuffer TanVB;
	FShaderResourceViewRHIRef TanSRV;
	FPFRawIndexBuffer IndexBuf;

	// Per-view quad streams (slot 0 always exists once initialised).
	TUniquePtr<FPFSplatViewSlot> Slots[PFSplatMaxViewSlots];

	void Release()
	{
		for (TUniquePtr<FPFSplatViewSlot>& S : Slots)
		{
			if (S)
			{
				S->Release();
				S.Reset();
			}
		}
		TanVB.ReleaseResource();
		IndexBuf.ReleaseResource();
		// The remaining members are ref-counted RHI objects.
	}
};

// ---------------------------------------------------------------------------
// FPFSplatGpuState
// ---------------------------------------------------------------------------

FPFSplatGpuState::FPFSplatGpuState(ERHIFeatureLevel::Type InFeatureLevel, FRecords InRecords,
	int32 InMaxQuads, float InUnitScale, float InMinPixelRadius, float InMaxScreenFraction,
	const FSceneInterface* InScene)
	: FeatureLevel(InFeatureLevel)
	, Records(MoveTemp(InRecords))
	, NumSplats(Records.IsValid() ? Records->Num() : 0)
	, NumQuads(FMath::Clamp(InMaxQuads, 1, FMath::Max(1, NumSplats)))
	, UnitScale(InUnitScale)
	, MinPixelRadius(InMinPixelRadius)
	, MaxScreenFraction(FMath::Max(InMaxScreenFraction, 0.05f))
	, LocalBoundsUU(ForceInit)
	, Scene(InScene)
{
	// Depth-key range comes from the data, never from component bounds (which
	// can be stale): wrong bounds collapse every key to one value and the sort
	// then leaves splats in file order (see-through surfaces).
	if (Records.IsValid())
	{
		for (const PFSplatGpuRecord& R : *Records)
		{
			LocalBoundsUU += FVector(R.position[0], R.position[1], R.position[2]) * UnitScale;
		}
	}
	if (!LocalBoundsUU.IsValid)
	{
		LocalBoundsUU = FBox(FVector(-1.0), FVector(1.0));
	}
}

FPFSplatGpuState::~FPFSplatGpuState()
{
	// May run on any thread (last reference). GPU wrappers must be released on
	// the render thread; ENQUEUE_RENDER_COMMAND runs inline when already there.
	if (Res)
	{
		FPFSplatGpuResources* R = Res.Release();
		ENQUEUE_RENDER_COMMAND(PFSplatGpuRelease)(
			[R](FRHICommandListImmediate&)
			{
				R->Release();
				delete R;
			});
	}
}

void FPFSplatGpuState::SetLocalToWorld(const FMatrix& InLocalToWorld)
{
	FScopeLock Lock(&TransformLock);
	LocalToWorld = InLocalToWorld;
}

bool FPFSplatGpuState::IsSlotReady(int32 SlotIndex) const
{
	return SlotIndex >= 0 && SlotIndex < PFSplatMaxViewSlots && SlotReady[SlotIndex].load();
}

const FLocalVertexFactory* FPFSplatGpuState::GetVertexFactory(int32 SlotIndex) const
{
	if (!IsSlotReady(SlotIndex))
	{
		SlotIndex = 0;   // this view's slot is not built yet: draw the first view's quads
	}
	if (!IsSlotReady(SlotIndex) || !Res || !Res->Slots[SlotIndex])
	{
		return nullptr;
	}
	return &Res->Slots[SlotIndex]->VertexFactory;
}

const FIndexBuffer* FPFSplatGpuState::GetIndexBuffer() const
{
	return Res ? &Res->IndexBuf : nullptr;
}

void FPFSplatGpuState::InitGpu(FRHICommandListImmediate& RHICmdList)
{
	check(IsInRenderingThread());
	if (NumSplats <= 0)
	{
		bInitFailed = true;
		return;
	}

	TUniquePtr<FPFSplatGpuResources> R = MakeUnique<FPFSplatGpuResources>();
	const uint32 N = static_cast<uint32>(NumSplats);
	const uint32 Q = static_cast<uint32>(NumQuads);
	const uint32 V = Q * 4u;

	// ---- splat records, uploaded once ----
	R->SplatBuf = UE::RHIResourceUtils::CreateBufferFromArray(RHICmdList, TEXT("PFSplatRecords"),
		EBufferUsageFlags::StructuredBuffer | EBufferUsageFlags::ShaderResource | EBufferUsageFlags::Static,
		sizeof(PFSplatGpuRecord), Records->GetData(), N * sizeof(PFSplatGpuRecord));
	R->SplatSRV = RHICmdList.CreateShaderResourceView(R->SplatBuf);

	// ---- typed scratch buffers (GPU written) ----
	auto MakeBuf = [&RHICmdList](const TCHAR* Name, uint32 Bytes)
	{
		FRHIResourceCreateInfo CreateInfo(Name);
		return RHICmdList.CreateVertexBuffer(Bytes,
			EBufferUsageFlags::ShaderResource | EBufferUsageFlags::UnorderedAccess,
			ERHIAccess::UAVCompute, CreateInfo);
	};

	for (int32 b = 0; b < 2; ++b)
	{
		R->KeyBuf[b] = MakeBuf(TEXT("PFSplatSortKeys"), N * 4);
		R->KeySRV[b] = RHICmdList.CreateShaderResourceView(R->KeyBuf[b], 4, PF_R32_UINT);
		R->KeyUAV[b] = RHICmdList.CreateUnorderedAccessView(R->KeyBuf[b], PF_R32_UINT);
		R->ValBuf[b] = MakeBuf(TEXT("PFSplatSortValues"), N * 4);
		R->ValSRV[b] = RHICmdList.CreateShaderResourceView(R->ValBuf[b], 4, PF_R32_UINT);
		R->ValUAV[b] = RHICmdList.CreateUnorderedAccessView(R->ValBuf[b], PF_R32_UINT);
	}
	R->ConicBuf = MakeBuf(TEXT("PFSplatConic"), N * 16);
	R->ConicSRV = RHICmdList.CreateShaderResourceView(R->ConicBuf, 16, PF_A32B32G32R32F);
	R->ConicUAV = RHICmdList.CreateUnorderedAccessView(R->ConicBuf, PF_A32B32G32R32F);
	R->RadiusBuf = MakeBuf(TEXT("PFSplatWorldRadius"), N * 4);
	R->RadiusSRV = RHICmdList.CreateShaderResourceView(R->RadiusBuf, 4, PF_R32_FLOAT);
	R->RadiusUAV = RHICmdList.CreateUnorderedAccessView(R->RadiusBuf, PF_R32_FLOAT);
	R->CountBuf = MakeBuf(TEXT("PFSplatVisibleCount"), 16);
	R->CountSRV = RHICmdList.CreateShaderResourceView(R->CountBuf, 4, PF_R32_UINT);
	R->CountUAV = RHICmdList.CreateUnorderedAccessView(R->CountBuf, PF_R32_UINT);

	// ---- static streams: constant tangents, quad index pattern ----
	{
		TArray<FPackedNormal> Tangents;
		Tangents.SetNumUninitialized(V * 2);
		const FPackedNormal TX(FVector3f(1, 0, 0));
		const FPackedNormal TZ(FVector4f(0, 0, 1, 1));
		for (uint32 v = 0; v < V; ++v)
		{
			Tangents[v * 2 + 0] = TX;
			Tangents[v * 2 + 1] = TZ;
		}
		R->TanVB.VertexBufferRHI = UE::RHIResourceUtils::CreateVertexBufferFromArray(RHICmdList, TEXT("PFSplatTangents"),
			EBufferUsageFlags::Static | EBufferUsageFlags::ShaderResource, MakeConstArrayView(Tangents));
		R->TanSRV = RHICmdList.CreateShaderResourceView(R->TanVB.VertexBufferRHI, 4, PF_R8G8B8A8_SNORM);
	}
	{
		TArray<uint32> Indices;
		Indices.SetNumUninitialized(Q * 6);
		for (uint32 q = 0; q < Q; ++q)
		{
			const uint32 v = q * 4;
			uint32* I = Indices.GetData() + q * 6;
			I[0] = v; I[1] = v + 1; I[2] = v + 2;
			I[3] = v; I[4] = v + 2; I[5] = v + 3;
		}
		R->IndexBuf.IndexBufferRHI = UE::RHIResourceUtils::CreateIndexBufferFromArray(RHICmdList, TEXT("PFSplatIndices"),
			MakeConstArrayView(Indices));
	}

	if (!R->SplatBuf || !R->TanVB.VertexBufferRHI || !R->IndexBuf.IndexBufferRHI)
	{
		UE_LOG(LogTemp, Error, TEXT("PFSplatGpu: GPU buffer creation failed (%d splats)"), NumSplats);
		bInitFailed = true;
		return;
	}

	// Wrappers: InitRHI is a no-op, InitResource just marks them initialised.
	R->TanVB.InitResource(RHICmdList);
	R->IndexBuf.InitResource(RHICmdList);

	Res = MoveTemp(R);
	if (!InitSlot(RHICmdList, 0))
	{
		Res->Release();
		Res.Reset();
		bInitFailed = true;
		return;
	}
	Records.Reset();   // drop our reference; the component keeps its cached copy

	UE_LOG(LogTemp, Log, TEXT("PFSplatGpu: uploaded %d splats, %d draw slots (%.1f MB GPU, +%.1f MB per extra view)"),
		NumSplats, NumQuads,
		(N * (sizeof(PFSplatGpuRecord) + 4 * 4 + 16 + 4) + V * (12 + 8 + 24 + 4) + Q * 24) / (1024.0 * 1024.0),
		V * (12 + 24 + 4) / (1024.0 * 1024.0));
}

bool FPFSplatGpuState::InitSlot(FRHICommandListImmediate& RHICmdList, int32 SlotIndex)
{
	check(IsInRenderingThread());
	check(Res && SlotIndex >= 0 && SlotIndex < PFSplatMaxViewSlots);
	if (Res->Slots[SlotIndex])
	{
		return true;
	}

	const uint32 V = static_cast<uint32>(NumQuads) * 4u;
	auto MakeBuf = [&RHICmdList](const TCHAR* Name, uint32 Bytes)
	{
		FRHIResourceCreateInfo CreateInfo(Name);
		return RHICmdList.CreateVertexBuffer(Bytes,
			EBufferUsageFlags::ShaderResource | EBufferUsageFlags::UnorderedAccess,
			ERHIAccess::UAVCompute, CreateInfo);
	};

	TUniquePtr<FPFSplatViewSlot> S = MakeUnique<FPFSplatViewSlot>(FeatureLevel);
	S->PosVB.VertexBufferRHI = MakeBuf(TEXT("PFSplatPositions"), V * 12);
	S->UvVB.VertexBufferRHI = MakeBuf(TEXT("PFSplatTexCoords"), V * 24);
	S->ColVB.VertexBufferRHI = MakeBuf(TEXT("PFSplatColors"), V * 4);
	if (!S->PosVB.VertexBufferRHI || !S->UvVB.VertexBufferRHI || !S->ColVB.VertexBufferRHI)
	{
		UE_LOG(LogTemp, Error, TEXT("PFSplatGpu: vertex buffer creation failed for view slot %d"), SlotIndex);
		return false;
	}
	S->PosSRV = RHICmdList.CreateShaderResourceView(S->PosVB.VertexBufferRHI, 4, PF_R32_FLOAT);
	S->PosUAV = RHICmdList.CreateUnorderedAccessView(S->PosVB.VertexBufferRHI, PF_R32_FLOAT);
	S->UvSRV = RHICmdList.CreateShaderResourceView(S->UvVB.VertexBufferRHI, 8, PF_G32R32F);
	S->UvUAV = RHICmdList.CreateUnorderedAccessView(S->UvVB.VertexBufferRHI, PF_G32R32F);
	S->ColSRV = RHICmdList.CreateShaderResourceView(S->ColVB.VertexBufferRHI, 4, PF_R8G8B8A8);
	S->ColUAV = RHICmdList.CreateUnorderedAccessView(S->ColVB.VertexBufferRHI, PF_R32_UINT);

	S->PosVB.InitResource(RHICmdList);
	S->UvVB.InitResource(RHICmdList);
	S->ColVB.InitResource(RHICmdList);

	// ---- vertex factory: mirrors FStaticMeshVertexBuffers' bind functions ----
	FLocalVertexFactory::FDataType Data;
	Data.PositionComponent = FVertexStreamComponent(&S->PosVB, 0, 12, VET_Float3);
	Data.PositionComponentSRV = S->PosSRV;
	Data.TangentBasisComponents[0] = FVertexStreamComponent(&Res->TanVB, 0, 8, VET_PackedNormal, EVertexStreamUsage::ManualFetch);
	Data.TangentBasisComponents[1] = FVertexStreamComponent(&Res->TanVB, 4, 8, VET_PackedNormal, EVertexStreamUsage::ManualFetch);
	Data.TangentsSRV = Res->TanSRV;
	Data.TextureCoordinates.Add(FVertexStreamComponent(&S->UvVB, 0, 24, VET_Float4, EVertexStreamUsage::ManualFetch));   // UV0 + UV1
	Data.TextureCoordinates.Add(FVertexStreamComponent(&S->UvVB, 16, 24, VET_Float2, EVertexStreamUsage::ManualFetch));  // UV2
	Data.TextureCoordinatesSRV = S->UvSRV;
	Data.NumTexCoords = 3;
	Data.LightMapCoordinateIndex = 0;
	Data.LightMapCoordinateComponent = FVertexStreamComponent(&S->UvVB, 0, 24, VET_Float2, EVertexStreamUsage::ManualFetch);
	Data.ColorComponent = FVertexStreamComponent(&S->ColVB, 0, 4, VET_Color, EVertexStreamUsage::ManualFetch);
	Data.ColorComponentsSRV = S->ColSRV;
	Data.ColorIndexMask = ~0u;
	S->VertexFactory.SetData(RHICmdList, Data);
	S->VertexFactory.InitResource(RHICmdList);

	Res->Slots[SlotIndex] = MoveTemp(S);
	if (SlotIndex > 0)
	{
		UE_LOG(LogTemp, Log, TEXT("PFSplatGpu: added view slot %d (%.1f MB)"), SlotIndex, V * (12 + 24 + 4) / (1024.0 * 1024.0));
	}
	return true;
}

void FPFSplatGpuState::UpdateForView(FRHICommandListImmediate& RHICmdList, const FPFSplatViewInputs& View, int32 SlotIndex)
{
	check(IsInRenderingThread());
	if (bInitFailed || SlotIndex < 0 || SlotIndex >= PFSplatMaxViewSlots)
	{
		return;
	}
	if (!Res)
	{
		InitGpu(RHICmdList);
		if (!Res)
		{
			return;
		}
	}
	if (!InitSlot(RHICmdList, SlotIndex))
	{
		return;   // out of memory for this view: it keeps drawing slot 0
	}
	FPFSplatViewSlot& Slot = *Res->Slots[SlotIndex];

	FMatrix L2W;
	{
		FScopeLock Lock(&TransformLock);
		L2W = LocalToWorld;
	}

	// Local -> translated world -> view, in double, then narrowed (view-relative,
	// so float precision holds for large worlds).
	const FMatrix LocalToView = L2W.ConcatTranslation(View.PreViewTranslation) * View.TranslatedViewMatrix;
	const FMatrix& Proj = View.ProjectionMatrix;
	const FIntPoint Vp(FMath::Max(1, View.ViewportSize.X), FMath::Max(1, View.ViewportSize.Y));

	if (SlotReady[SlotIndex].load() && Vp == Slot.LastViewport
		&& LocalToView.Equals(Slot.LastLocalToView, 0.0) && Proj.Equals(Slot.LastProjection, 0.0))
	{
		return;   // same camera and transform: this view's vertex buffers are still correct
	}
	Slot.LastLocalToView = LocalToView;
	Slot.LastProjection = Proj;
	Slot.LastViewport = Vp;

	const bool bOrtho = Proj.M[3][3] >= 1.0;
	const float MaxAxisScale = static_cast<float>(FMath::Max(L2W.GetMaximumAxisScale(), UE_SMALL_NUMBER));
	const float NearCull = 0.2f * UnitScale;   // 20 cm at UnitScale 100 (same cut as the CPU path)

	// Depth key range from the cloud's bounding sphere in view space.
	const FVector CentreView = LocalToView.TransformPosition(LocalBoundsUU.GetCenter());
	const double RadiusView = LocalBoundsUU.GetExtent().Size() * MaxAxisScale;
	const double ZMin = FMath::Max<double>(NearCull, CentreView.Z - RadiusView);
	const double ZMax = FMath::Max<double>(ZMin + 1.0, CentreView.Z + RadiusView);

	// Camera right/up (world, unit) -> local, so quads keep world size under scale.
	const FMatrix WorldToLocal = L2W.Inverse();
	const FVector RightW(View.InvViewMatrix.M[0][0], View.InvViewMatrix.M[0][1], View.InvViewMatrix.M[0][2]);
	const FVector UpW(View.InvViewMatrix.M[1][0], View.InvViewMatrix.M[1][1], View.InvViewMatrix.M[1][2]);
	const FVector RightL = WorldToLocal.TransformVector(RightW.GetSafeNormal());
	const FVector UpL = WorldToLocal.TransformVector(UpW.GetSafeNormal());

	FPFSplatGpuResources& R = *Res;

	// ---- 1. project ----
	RHICmdList.Transition({
		FRHITransitionInfo(R.KeyUAV[0], ERHIAccess::Unknown, ERHIAccess::UAVCompute),
		FRHITransitionInfo(R.ValUAV[0], ERHIAccess::Unknown, ERHIAccess::UAVCompute),
		FRHITransitionInfo(R.ConicUAV, ERHIAccess::Unknown, ERHIAccess::UAVCompute),
		FRHITransitionInfo(R.RadiusUAV, ERHIAccess::Unknown, ERHIAccess::UAVCompute),
		FRHITransitionInfo(R.CountUAV, ERHIAccess::Unknown, ERHIAccess::UAVCompute)
	});
	RHICmdList.ClearUAVUint(R.CountUAV, FUintVector4(0, 0, 0, 0));
	RHICmdList.Transition(FRHITransitionInfo(R.CountUAV, ERHIAccess::UAVCompute, ERHIAccess::UAVCompute));

	FPFSplatProjectArgs PA;
	PA.NumSplats = static_cast<uint32>(NumSplats);
	PA.UnitScale = UnitScale;
	PA.MaxAxisScale = MaxAxisScale;
	PA.MinPixelRadius = MinPixelRadius;
	PA.NearCull = NearCull;
	PA.bOrtho = bOrtho ? 1u : 0u;
	PA.MaxRadiusPx = MaxScreenFraction * static_cast<float>(FMath::Max(Vp.X, Vp.Y));
	PA.LocalToView = FMatrix44f(LocalToView);
	PA.ViewToClip = FMatrix44f(Proj);
	PA.Viewport = FVector2f(Vp.X, Vp.Y);
	PA.Focal = FVector2f(Proj.M[0][0] * Vp.X * 0.5, Proj.M[1][1] * Vp.Y * 0.5);
	PA.JacobianLimit = FVector2f(1.3 / FMath::Max(Proj.M[0][0], UE_SMALL_NUMBER), 1.3 / FMath::Max(Proj.M[1][1], UE_SMALL_NUMBER));
	PA.DepthKeyRange = FVector2f(ZMin, 1.0 / (ZMax - ZMin));
	PA.Splats = R.SplatSRV;
	PA.OutKeys = R.KeyUAV[0];
	PA.OutValues = R.ValUAV[0];
	PA.OutConic = R.ConicUAV;
	PA.OutWorldRadius = R.RadiusUAV;
	PA.OutVisibleCount = R.CountUAV;
	PFSplatGpu_DispatchProject(RHICmdList, PA, FeatureLevel);

	RHICmdList.Transition({
		FRHITransitionInfo(R.KeyUAV[0], ERHIAccess::UAVCompute, ERHIAccess::SRVCompute),
		FRHITransitionInfo(R.ValUAV[0], ERHIAccess::UAVCompute, ERHIAccess::SRVCompute),
		FRHITransitionInfo(R.ConicUAV, ERHIAccess::UAVCompute, ERHIAccess::SRVCompute),
		FRHITransitionInfo(R.RadiusUAV, ERHIAccess::UAVCompute, ERHIAccess::SRVCompute),
		FRHITransitionInfo(R.CountUAV, ERHIAccess::UAVCompute, ERHIAccess::SRVCompute)
	});

	// ---- 2. sort (engine GPU radix sort, 16-bit keys = 4 passes) ----
	FGPUSortBuffers SortBuffers;
	for (int32 b = 0; b < 2; ++b)
	{
		SortBuffers.RemoteKeySRVs[b] = R.KeySRV[b];
		SortBuffers.RemoteKeyUAVs[b] = R.KeyUAV[b];
		SortBuffers.RemoteValueSRVs[b] = R.ValSRV[b];
		SortBuffers.RemoteValueUAVs[b] = R.ValUAV[b];
	}
	const int32 Sorted = SortGPUBuffers(RHICmdList, SortBuffers, 0, PFSplatDepthKeyMask, NumSplats, FeatureLevel);

	// ---- 3. quads into the vertex buffers ----
	RHICmdList.Transition({
		FRHITransitionInfo(R.ValUAV[Sorted], ERHIAccess::Unknown, ERHIAccess::SRVCompute),
		FRHITransitionInfo(Slot.PosUAV, ERHIAccess::Unknown, ERHIAccess::UAVCompute),
		FRHITransitionInfo(Slot.UvUAV, ERHIAccess::Unknown, ERHIAccess::UAVCompute),
		FRHITransitionInfo(Slot.ColUAV, ERHIAccess::Unknown, ERHIAccess::UAVCompute)
	});

	FPFSplatQuadArgs QA;
	QA.NumQuadSlots = static_cast<uint32>(NumQuads);
	QA.UnitScale = UnitScale;
	QA.RightLocal = FVector3f(RightL);
	QA.UpLocal = FVector3f(UpL);
	QA.Splats = R.SplatSRV;
	QA.SortedValues = R.ValSRV[Sorted];
	QA.Conic = R.ConicSRV;
	QA.WorldRadius = R.RadiusSRV;
	QA.VisibleCount = R.CountSRV;
	QA.OutPositions = Slot.PosUAV;
	QA.OutTexCoords = Slot.UvUAV;
	QA.OutColors = Slot.ColUAV;
	PFSplatGpu_DispatchBuildQuads(RHICmdList, QA, FeatureLevel);

	const ERHIAccess DrawAccess = ERHIAccess::VertexOrIndexBuffer | ERHIAccess::SRVMask;
	RHICmdList.Transition({
		FRHITransitionInfo(Slot.PosUAV, ERHIAccess::UAVCompute, DrawAccess),
		FRHITransitionInfo(Slot.UvUAV, ERHIAccess::UAVCompute, DrawAccess),
		FRHITransitionInfo(Slot.ColUAV, ERHIAccess::UAVCompute, DrawAccess)
	});

	SlotReady[SlotIndex].store(true);
}

// ---------------------------------------------------------------------------
// View extension: one compute update per view family, before it renders
// ---------------------------------------------------------------------------

namespace
{
	FCriticalSection GRegistryLock;
	TArray<TWeakPtr<FPFSplatGpuState, ESPMode::ThreadSafe>> GRegistry;

	void RegisterState(const TSharedPtr<FPFSplatGpuState, ESPMode::ThreadSafe>& State)
	{
		FScopeLock Lock(&GRegistryLock);
		GRegistry.Add(State);
	}

	void UnregisterState(const FPFSplatGpuState* State)
	{
		FScopeLock Lock(&GRegistryLock);
		GRegistry.RemoveAllSwap([State](const TWeakPtr<FPFSplatGpuState, ESPMode::ThreadSafe>& W)
		{
			const TSharedPtr<FPFSplatGpuState, ESPMode::ThreadSafe> P = W.Pin();
			return !P.IsValid() || P.Get() == State;
		});
	}
}

class FPFSplatGpuViewExtension final : public FSceneViewExtensionBase
{
public:
	explicit FPFSplatGpuViewExtension(const FAutoRegister& AutoRegister)
		: FSceneViewExtensionBase(AutoRegister) {}

	virtual void SetupViewFamily(FSceneViewFamily& InViewFamily) override {}
	virtual void SetupView(FSceneViewFamily& InViewFamily, FSceneView& InView) override {}
	virtual void BeginRenderViewFamily(FSceneViewFamily& InViewFamily) override {}

	virtual void PreRenderViewFamily_RenderThread(FRDGBuilder& GraphBuilder, FSceneViewFamily& InViewFamily) override
	{
		if (InViewFamily.Views.Num() == 0 || !InViewFamily.Views[0] || !InViewFamily.Scene)
		{
			return;
		}

		TArray<TSharedPtr<FPFSplatGpuState, ESPMode::ThreadSafe>> Active;
		{
			FScopeLock Lock(&GRegistryLock);
			for (const TWeakPtr<FPFSplatGpuState, ESPMode::ThreadSafe>& W : GRegistry)
			{
				TSharedPtr<FPFSplatGpuState, ESPMode::ThreadSafe> S = W.Pin();
				if (S.IsValid() && S->GetScene() == InViewFamily.Scene)
				{
					Active.Add(MoveTemp(S));
				}
			}
		}
		if (Active.Num() == 0)
		{
			return;
		}

		// One update per view, each into its own slot: stereo eyes and nDisplay
		// viewports are culled and sorted for their own camera. Views past the slot
		// limit draw slot 0. Families render one after another (each with its own
		// graph), so slot i always belongs to view i of the family being rendered.
		TArray<FPFSplatViewInputs> Inputs;   // heap: RDG caps the size captured by a pass lambda
		Inputs.Reserve(PFSplatMaxViewSlots);
		const int32 NumViews = FMath::Min(InViewFamily.Views.Num(), PFSplatMaxViewSlots);
		for (int32 i = 0; i < NumViews; ++i)
		{
			const FSceneView* View = InViewFamily.Views[i];
			if (!View)
			{
				break;   // keep slot index == view index
			}
			FPFSplatViewInputs& In = Inputs.AddDefaulted_GetRef();
			In.TranslatedViewMatrix = View->ViewMatrices.GetTranslatedViewMatrix();
			In.ProjectionMatrix = View->ViewMatrices.GetProjectionMatrix();
			In.InvViewMatrix = View->ViewMatrices.GetInvViewMatrix();
			In.PreViewTranslation = View->ViewMatrices.GetPreViewTranslation();
			In.ViewportSize = View->UnconstrainedViewRect.Size();
		}

		// Untracked raw-RHI work (the passes do their own transitions). Added first
		// in the family's graph, so it executes before the translucency pass that
		// reads the vertex buffers.
		GraphBuilder.AddPass(RDG_EVENT_NAME("PFSplatGPU (%d clouds, %d views)", Active.Num(), Inputs.Num()), ERDGPassFlags::None,
			[Active = MoveTemp(Active), Inputs = MoveTemp(Inputs)](FRHICommandListImmediate& RHICmdList)
			{
				for (const TSharedPtr<FPFSplatGpuState, ESPMode::ThreadSafe>& S : Active)
				{
					for (int32 i = 0; i < Inputs.Num(); ++i)
					{
						S->UpdateForView(RHICmdList, Inputs[i], i);
					}
				}
			});
	}
};

static TSharedPtr<FPFSplatGpuViewExtension, ESPMode::ThreadSafe> GPFSplatGpuExtension;
static std::atomic<bool> GPFSplatGpuExtensionReady{ false };

void PFSplatGpu_EnsureViewExtension()
{
	check(IsInGameThread());
	if (!GPFSplatGpuExtension.IsValid() && GEngine)
	{
		GPFSplatGpuExtension = FSceneViewExtensions::NewExtension<FPFSplatGpuViewExtension>();
		GPFSplatGpuExtensionReady.store(true);
	}
}

bool PFSplatGpu_HasViewExtension()
{
	return GPFSplatGpuExtensionReady.load();
}

void PFSplatGpu_ReleaseViewExtension()
{
	GPFSplatGpuExtensionReady.store(false);
	GPFSplatGpuExtension.Reset();
}

// ---------------------------------------------------------------------------
// FPFSplatGpuSceneProxy
// ---------------------------------------------------------------------------

SIZE_T FPFSplatGpuSceneProxy::GetTypeHash() const
{
	static size_t UniquePointer;
	return reinterpret_cast<SIZE_T>(&UniquePointer);
}

FPFSplatGpuSceneProxy::FPFSplatGpuSceneProxy(const UPFSplatComponent* InComponent, FPFSplatGpuState::FRecords InRecords)
	: FPrimitiveSceneProxy(InComponent)
{
	bWillEverBeLit = false;
	bVerifyUsedMaterials = false;

	const ERHIFeatureLevel::Type FeatureLevel = GetScene().GetFeatureLevel();
	const float UnitScale = InComponent ? InComponent->UnitScale : 100.0f;

	UMaterialInterface* Mat = InComponent ? InComponent->SplatMaterial.Get() : nullptr;
	if (!Mat)
	{
		Mat = UMaterial::GetDefaultMaterial(MD_Surface);   // opaque: wrong look until SplatMaterial is set
	}
	MaterialRenderProxy = Mat->GetRenderProxy();
	MaterialRelevance = Mat->GetRelevance_Concurrent(FeatureLevel);

	State = MakeShared<FPFSplatGpuState, ESPMode::ThreadSafe>(FeatureLevel, MoveTemp(InRecords),
		InComponent ? InComponent->MaxSplatInstances : 300000, UnitScale,
		InComponent ? InComponent->MinSplatPixels : 0.5f,
		InComponent ? InComponent->MaxSplatScreenFraction : 1.0f, &GetScene());
}

FPFSplatGpuSceneProxy::~FPFSplatGpuSceneProxy()
{
	if (State.IsValid())
	{
		UnregisterState(State.Get());
		State.Reset();   // GPU resources are released on the render thread
	}
}

void FPFSplatGpuSceneProxy::CreateRenderThreadResources(FRHICommandListBase& RHICmdList)
{
	if (State.IsValid())
	{
		State->SetLocalToWorld(GetLocalToWorld());
		RegisterState(State);
	}
}

void FPFSplatGpuSceneProxy::DestroyRenderThreadResources()
{
	if (State.IsValid())
	{
		UnregisterState(State.Get());
	}
}

void FPFSplatGpuSceneProxy::OnTransformChanged(FRHICommandListBase& RHICmdList)
{
	if (State.IsValid())
	{
		State->SetLocalToWorld(GetLocalToWorld());
	}
}

void FPFSplatGpuSceneProxy::GetDynamicMeshElements(
	const TArray<const FSceneView*>& Views,
	const FSceneViewFamily& ViewFamily,
	uint32 VisibilityMap,
	FMeshElementCollector& Collector) const
{
	if (!State.IsValid() || !State->IsDrawable() || !MaterialRenderProxy)
	{
		return;   // first frame: the update pass has not filled the buffers yet
	}
	const FIndexBuffer* IB = State->GetIndexBuffer();
	if (!IB)
	{
		return;
	}
	const int32 Quads = State->GetNumQuads();

	for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ++ViewIndex)
	{
		if (!(VisibilityMap & (1u << ViewIndex)) || !Views[ViewIndex])
		{
			continue;
		}

		// Each view draws the quads built for its own camera (slot == view index).
		const FLocalVertexFactory* VF = State->GetVertexFactory(ViewIndex < PFSplatMaxViewSlots ? ViewIndex : 0);
		if (!VF)
		{
			continue;
		}

		FMeshBatch& Mesh = Collector.AllocateMesh();
		Mesh.VertexFactory = VF;
		Mesh.MaterialRenderProxy = MaterialRenderProxy;
		Mesh.bWireframe = false;
		Mesh.ReverseCulling = IsLocalToWorldDeterminantNegative();
		Mesh.Type = PT_TriangleList;
		Mesh.DepthPriorityGroup = SDPG_World;
		Mesh.bCanApplyViewModeOverrides = false;
		Mesh.CastShadow = false;

		FDynamicPrimitiveUniformBuffer& DUB = Collector.AllocateOneFrameResource<FDynamicPrimitiveUniformBuffer>();
		FPrimitiveUniformShaderParametersBuilder Builder;
		BuildUniformShaderParameters(Builder);
		DUB.Set(Collector.GetRHICommandList(), Builder);

		FMeshBatchElement& Element = Mesh.Elements[0];
		Element.PrimitiveUniformBufferResource = &DUB.UniformBuffer;
		Element.IndexBuffer = IB;
		Element.FirstIndex = 0;
		Element.NumPrimitives = Quads * 2;        // unused slots are zero-area quads
		Element.MinVertexIndex = 0;
		Element.MaxVertexIndex = Quads * 4 - 1;
		Collector.AddMesh(ViewIndex, Mesh);
	}
}

FPrimitiveViewRelevance FPFSplatGpuSceneProxy::GetViewRelevance(const FSceneView* View) const
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
