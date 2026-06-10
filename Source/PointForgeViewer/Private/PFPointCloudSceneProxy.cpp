#include "PFPointCloudSceneProxy.h"

#include "PFPointCloudComponent.h"
#include "PFOctreeStore.h"
#include "SceneManagement.h"      // FPrimitiveDrawInterface, DrawWireBox
#include "SceneView.h"

namespace
{
	// Recursively collect each node's cube in component-local space:
	//   local = (worldSourceCoord - cubeCentre) * UnitScale
	// Capped at MaxLevel so we draw a few hundred boxes, not millions.
	void BuildBoxesRecursive(
		const FPFOctreeStore& Store,
		int32 NodeIdx,
		const double Min[3],
		double Size,
		const FVector& CubeCentre,
		double UnitScale,
		int32 Level,
		int32 MaxLevel,
		TArray<FBox>& Out)
	{
		const TArray<FPFNodeRecord>& Nodes = Store.GetNodes();
		if (NodeIdx < 0 || NodeIdx >= Nodes.Num())
		{
			return;
		}

		const FVector LoSrc(Min[0], Min[1], Min[2]);
		const FVector HiSrc(Min[0] + Size, Min[1] + Size, Min[2] + Size);
		const FVector Lo = (LoSrc - CubeCentre) * UnitScale;
		const FVector Hi = (HiSrc - CubeCentre) * UnitScale;
		Out.Add(FBox(Lo, Hi));

		if (Level >= MaxLevel)
		{
			return;
		}

		const FPFNodeRecord& N = Nodes[NodeIdx];
		for (int32 O = 0; O < 8; ++O)
		{
			if (N.Children[O] != PF_NO_CHILD)
			{
				double ChildMin[3];
				double ChildSize;
				PFChildCube(Min, Size, O, ChildMin, ChildSize);
				BuildBoxesRecursive(Store, static_cast<int32>(N.Children[O]),
					ChildMin, ChildSize, CubeCentre, UnitScale, Level + 1, MaxLevel, Out);
			}
		}
	}
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
	bVerifyUsedMaterials = false; // we draw wireframes via PDI, no registered materials

	if (InComponent && InComponent->Store.IsValid() && InComponent->Store->IsValid())
	{
		const FPFOctreeStore& Store = *InComponent->Store;
		const FPFFileMetadata& M = Store.GetMeta();

		const FVector CubeCentre(
			M.CubeMin[0] + M.CubeSize * 0.5,
			M.CubeMin[1] + M.CubeSize * 0.5,
			M.CubeMin[2] + M.CubeSize * 0.5);

		const double UnitScale = static_cast<double>(InComponent->UnitScale);
		double RootMin[3] = { M.CubeMin[0], M.CubeMin[1], M.CubeMin[2] };

		const int32 MaxLevel = 3; // stub: draw coarse cubes only
		BuildBoxesRecursive(Store, Store.GetRootIndex(), RootMin, M.CubeSize,
			CubeCentre, UnitScale, 0, MaxLevel, NodeBoxes);
	}
}

FPFPointCloudSceneProxy::~FPFPointCloudSceneProxy()
{
}

void FPFPointCloudSceneProxy::GetDynamicMeshElements(
	const TArray<const FSceneView*>& Views,
	const FSceneViewFamily& ViewFamily,
	uint32 VisibilityMap,
	FMeshElementCollector& Collector) const
{
	const FMatrix& LocalToWorld = GetLocalToWorld();

	for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ++ViewIndex)
	{
		if (!(VisibilityMap & (1 << ViewIndex)))
		{
			continue;
		}

		FPrimitiveDrawInterface* PDI = Collector.GetPDI(ViewIndex);
		for (const FBox& Box : NodeBoxes)
		{
			DrawWireBox(PDI, LocalToWorld, Box, WireColor, SDPG_World, 1.0f);
		}
	}

	// TODO(point rendering): stream + upload visible node payloads here and emit
	// PT_PointList mesh batches. See header for the milestone breakdown.
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
	Result.bEditorPrimitiveRelevance = false;
	return Result;
}
