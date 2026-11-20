// Copyright. PointForge Viewer — GPU Gaussian splat path (compute + GPU sort).
#pragma once

#include "CoreMinimal.h"
#include "PrimitiveSceneProxy.h"
#include "MaterialShared.h"        // FMaterialRelevance
#include "Templates/SharedPointer.h"
#include "HAL/CriticalSection.h"
#include <atomic>

#include "PFNativeApi.h"           // PFSplatGpuRecord

class UPFSplatComponent;
class FSceneInterface;
class FRHICommandListImmediate;
class FLocalVertexFactory;
class FIndexBuffer;
struct FPFSplatGpuResources;
struct FPFSplatViewSlot;

/** Views per family that get their own quad buffers (stereo eyes, nDisplay viewports). */
static constexpr int32 PFSplatMaxViewSlots = 4;

/** View data captured on the render thread for one compute update (copied by value). */
struct FPFSplatViewInputs
{
	FMatrix TranslatedViewMatrix = FMatrix::Identity;   // translated world -> view
	FMatrix ProjectionMatrix = FMatrix::Identity;
	FMatrix InvViewMatrix = FMatrix::Identity;          // view -> world (rotation only matters)
	FVector PreViewTranslation = FVector::ZeroVector;
	FIntPoint ViewportSize = FIntPoint(1, 1);
};

/**
 * GPU state for one splat cloud: the splat records (uploaded once), sort and
 * projection scratch, and FLocalVertexFactory-layout vertex buffers that a
 * compute pass rewrites per view. Shared between the scene proxy (draws it) and
 * the view extension (updates it), so neither outlives the other's use.
 *
 * Each view of a family (left/right eye, nDisplay viewports) gets its own
 * vertex buffers ("view slot"), culled and sorted for that view's camera. The
 * projection and sort scratch is shared because views update one after another.
 *
 * Threading: everything except construction runs on the render thread. GPU
 * resources are created lazily inside the first update pass, and released on
 * the render thread when the last reference goes away.
 */
class FPFSplatGpuState : public TSharedFromThis<FPFSplatGpuState, ESPMode::ThreadSafe>
{
public:
	using FRecords = TSharedPtr<const TArray<PFSplatGpuRecord>, ESPMode::ThreadSafe>;

	/** Bounds for the depth-sort keys are computed from the records themselves. */
	FPFSplatGpuState(ERHIFeatureLevel::Type InFeatureLevel, FRecords InRecords,
		int32 InMaxQuads, float InUnitScale, float InMinPixelRadius, float InMaxScreenFraction,
		const FSceneInterface* InScene);
	~FPFSplatGpuState();

	/** Render thread: the owning primitive moved. */
	void SetLocalToWorld(const FMatrix& InLocalToWorld);

	/**
	 * Render thread, inside an RDG pass: create GPU resources on first use, then
	 * project, sort and build quads into the vertex buffers of view slot SlotIndex.
	 */
	void UpdateForView(FRHICommandListImmediate& RHICmdList, const FPFSplatViewInputs& View, int32 SlotIndex);

	/** Any thread: true once slot 0 has been filled (something can be drawn). */
	bool IsDrawable() const { return IsSlotReady(0); }
	/** Any thread: true once SlotIndex has its buffers and was filled at least once. */
	bool IsSlotReady(int32 SlotIndex) const;

	/** Vertex factory of a ready slot (falls back to slot 0), or null. */
	const FLocalVertexFactory* GetVertexFactory(int32 SlotIndex) const;
	const FIndexBuffer* GetIndexBuffer() const;
	int32 GetNumQuads() const { return NumQuads; }
	const FSceneInterface* GetScene() const { return Scene; }

private:
	void InitGpu(FRHICommandListImmediate& RHICmdList);
	bool InitSlot(FRHICommandListImmediate& RHICmdList, int32 SlotIndex);

	const ERHIFeatureLevel::Type FeatureLevel;
	FRecords Records;                          // shared with the component; dropped after upload
	const int32 NumSplats;
	const int32 NumQuads;                      // draw slots = min(splats, cap)
	const float UnitScale;
	const float MinPixelRadius;
	const float MaxScreenFraction;             // giant cutoff, fraction of the larger viewport side
	FBox LocalBoundsUU;                       // local space, Unreal units (from the records)
	const FSceneInterface* Scene;              // identity only, never dereferenced

	// Written from OnTransformChanged, read by the update pass. Guarded because the
	// renderer may call proxy hooks from task threads.
	mutable FCriticalSection TransformLock;
	FMatrix LocalToWorld = FMatrix::Identity;

	TUniquePtr<FPFSplatGpuResources> Res;      // render thread
	bool bInitFailed = false;

	// Per-view vertex buffers live in Res (created on the render thread, freed
	// with Res). A slot's ready flag is set after its buffers exist and were filled.
	std::atomic<bool> SlotReady[PFSplatMaxViewSlots] = {};
};

/**
 * Scene proxy for the GPU path. Draws the vertex buffers of its FPFSplatGpuState
 * (one translucent mesh element, quads already in back-to-front order) with the
 * same M_PointForgeSplat material as the CPU path.
 */
class FPFSplatGpuSceneProxy final : public FPrimitiveSceneProxy
{
public:
	FPFSplatGpuSceneProxy(const UPFSplatComponent* InComponent, FPFSplatGpuState::FRecords InRecords);
	virtual ~FPFSplatGpuSceneProxy() override;

	virtual SIZE_T GetTypeHash() const override;
	virtual void CreateRenderThreadResources(FRHICommandListBase& RHICmdList) override;
	virtual void DestroyRenderThreadResources() override;
	virtual void OnTransformChanged(FRHICommandListBase& RHICmdList) override;

	virtual void GetDynamicMeshElements(
		const TArray<const FSceneView*>& Views,
		const FSceneViewFamily& ViewFamily,
		uint32 VisibilityMap,
		FMeshElementCollector& Collector) const override;

	virtual FPrimitiveViewRelevance GetViewRelevance(const FSceneView* View) const override;
	virtual uint32 GetMemoryFootprint() const override { return sizeof(*this); }

private:
	TSharedPtr<FPFSplatGpuState, ESPMode::ThreadSafe> State;
	FMaterialRenderProxy* MaterialRenderProxy = nullptr;
	FMaterialRelevance MaterialRelevance;
};

/** Game thread: make sure the view extension that drives GPU splat updates is registered. */
void PFSplatGpu_EnsureViewExtension();
/** Any thread: true once the view extension exists (proxies fall back to the CPU path until then). */
bool PFSplatGpu_HasViewExtension();
/** Module shutdown. */
void PFSplatGpu_ReleaseViewExtension();
