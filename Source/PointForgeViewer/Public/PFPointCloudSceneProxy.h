#pragma once

#include "CoreMinimal.h"
#include "PrimitiveSceneProxy.h"
#include "LocalVertexFactory.h"
#include "StaticMeshResources.h"   // FStaticMeshVertexBuffers
#include "DynamicMeshBuilder.h"    // FDynamicMeshIndexBuffer32
#include "MaterialShared.h"        // FMaterialRelevance

class UPFPointCloudComponent;
class UMaterialInterface;
class FPFOctreeStore;
struct FPFViewerStats;
struct FPFLoadResult;

/** One resident node's GPU buffers + vertex factory (render-thread owned). */
struct FPFNodeRender
{
	FStaticMeshVertexBuffers Buffers;
	FDynamicMeshIndexBuffer32 IndexBuffer;   // 6 indices/point (quad = 2 tris)
	FLocalVertexFactory VertexFactory;
	int32 NumPoints = 0;                     // source point count (verts = *4, indices = *6)
	SIZE_T Bytes = 0;
	uint32 LastUsedFrame = 0;

	explicit FPFNodeRender(ERHIFeatureLevel::Type InFeatureLevel)
		: VertexFactory(InFeatureLevel, "FPFNodeRender") {}

	void ReleaseResources();
};

/**
 * MILESTONE #2A — out-of-core streaming scene proxy.
 *
 * Per frame (in GetDynamicMeshElements, render thread): drains finished async
 * loads into per-node GPU buffers (capped by UploadsPerFrame), traverses the
 * octree top-down with frustum cull + screen-space-error LOD (descend while a
 * node's projected spacing exceeds the pixel budget), emits a PT_TriangleList
 * batch (one camera-facing quad per point) for each resident node, requests
 * missing nodes, and LRU-evicts to the GPU byte budget.
 *
 * Reads the Store's const hierarchy/cubes directly (immutable after load → safe
 * on the render thread). Tunables pushed from the component via a render command.
 * Point size / round / attenuate are done in the assigned billboard MATERIAL
 * (World Position Offset reads the per-vertex corner sign in UV0) — points are
 * invisible unless that material is assigned.
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
	virtual uint32 GetMemoryFootprint() const override { return sizeof(*this); }

	/** Push live panel tunables (called via ENQUEUE_RENDER_COMMAND from the component). */
	void SetTunables_RenderThread(float InSseBudgetPx, int64 InGpuBudgetBytes, int32 InUploadsPerFrame, float InPointCountLimit);

private:
	// Mutable streaming state (touched only on the render thread).
	void ProcessFrame(FMeshElementCollector& Collector, uint32 FrameNumber) const;
	void CreateNode(FPFLoadResult& Load) const;
	void EvictToBudget(uint32 FrameNumber) const;

	TSharedPtr<FPFOctreeStore> Store;
	TSharedPtr<FPFViewerStats, ESPMode::ThreadSafe> Stats;

	UMaterialInterface* Material = nullptr;
	FMaterialRelevance MaterialRelevance;
	float UnitScale = 100.f;

	// All of the following are mutated from the (single) render thread only.
	mutable TMap<int32, TUniquePtr<FPFNodeRender>> Resident;
	mutable int64 ResidentBytesTotal = 0;
	mutable int64 PointsOnGpuTotal = 0;
	mutable uint32 LastFrameProcessed = 0xFFFFFFFFu;

	// Tunables (defaults mirror pfview; updated via SetTunables_RenderThread).
	mutable float SseBudgetPx = 1.5f;
	mutable int64 GpuBudgetBytes = 1024ll * 1024ll * 1024ll;
	mutable int32 UploadsPerFrame = 32;
	mutable float PointCountLimit = 0.0f;
};
