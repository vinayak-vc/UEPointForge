#pragma once

#include "CoreMinimal.h"
#include "PrimitiveSceneProxy.h"

class UPFPointCloudComponent;

/**
 * Scene proxy for a PointForge octree.
 *
 * STUB STAGE: draws the octree's coarse node-cube wireframes (level-capped) so
 * the streaming/traversal wiring is visibly correct in-editor and in a packaged
 * build. The boxes are computed on the game thread (from the resident hierarchy)
 * and handed to the render thread immutably.
 *
 * NEXT MILESTONE (point rendering):
 *   1. CreateRenderThreadResources(): allocate an RHI vertex buffer pool.
 *   2. Async-stream visible node payloads (FPFOctreeStore::ReadNodePoints) on a
 *      worker thread; upload FPFPackedPoint -> VB (RHIAsyncCreateVertexBuffer).
 *   3. GetDynamicMeshElements(): emit FMeshBatch with Type = PT_PointList + a
 *      point vertex factory (port shaders/point.vert/.frag -> HLSL).
 *   4. Per-frame frustum cull + screen-space-error LOD traversal over the node
 *      tree; LRU GPU-byte budget eviction (mirror pfview's PointRenderer).
 */
class FPFPointCloudSceneProxy : public FPrimitiveSceneProxy
{
public:
	SIZE_T GetTypeHash() const override;

	explicit FPFPointCloudSceneProxy(const UPFPointCloudComponent* InComponent);
	virtual ~FPFPointCloudSceneProxy() override;

	virtual void GetDynamicMeshElements(
		const TArray<const FSceneView*>& Views,
		const FSceneViewFamily& ViewFamily,
		uint32 VisibilityMap,
		FMeshElementCollector& Collector) const override;

	virtual FPrimitiveViewRelevance GetViewRelevance(const FSceneView* View) const override;

	virtual uint32 GetMemoryFootprint() const override
	{
		return static_cast<uint32>(sizeof(*this) + GetAllocatedSize());
	}

	uint32 GetAllocatedSize() const { return NodeBoxes.GetAllocatedSize(); }

private:
	/** Local-space (component-relative) cubes for the level-capped node set. */
	TArray<FBox> NodeBoxes;
	FLinearColor WireColor = FLinearColor(0.15f, 0.8f, 1.0f);
};
