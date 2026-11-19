// Copyright. PointForge Viewer — Gaussian splat scene proxy (billboard path).
#pragma once

#include "CoreMinimal.h"
#include "PrimitiveSceneProxy.h"
#include "LocalVertexFactory.h"
#include "StaticMeshResources.h"   // FStaticMeshVertexBuffers
#include "DynamicMeshBuilder.h"    // FDynamicMeshIndexBuffer32
#include "MaterialShared.h"        // FMaterialRelevance
#include "HAL/CriticalSection.h"
#include <atomic>

#include "PFNativeApi.h"           // PFSplatCloud handle type

class UPFSplatComponent;
class UMaterialInterface;
class FPFSplatWorker;

/**
 * One set of splat quads.
 *
 * Lifetime (same split as the engine's mesh streaming, FSkeletalMeshStreamIn):
 *   worker  : fill the CPU buffers, then (if GRHISupportsAsyncTextureCreation)
 *             create + upload the GPU buffers on a private FRHICommandList
 *             (UploadCmdList) and finish recording it.
 *   render  : submit UploadCmdList (SubmitUpload), bind the vertex factory
 *             (InitFactory), draw until a newer batch replaces it, then drop the
 *             GPU references (ReleaseResources) and hand the batch back.
 *   worker  : delete it (frees the large CPU-side arrays off the render thread).
 * Without async resource creation the render thread calls InitBuffers itself.
 */
struct FPFSplatBatch
{
	FStaticMeshVertexBuffers Buffers;
	FDynamicMeshIndexBuffer32 IndexBuffer;   // 6 indices per splat (quad = 2 tris)
	FLocalVertexFactory VertexFactory;
	int32 NumSplats = 0;

	FRHICommandList* UploadCmdList = nullptr; // recorded by the worker, submitted by the render thread
	bool bBuffersInit = false;                // InitBuffers ran (worker or render thread)
	bool bGpuReady = false;                   // render thread only: safe to draw

	explicit FPFSplatBatch(ERHIFeatureLevel::Type InFeatureLevel)
		: VertexFactory(InFeatureLevel, "FPFSplatBatch") {}

	/** Create + upload the vertex/index buffers on RHICmdList (any thread with its own list). */
	void InitBuffers(FRHICommandListBase& RHICmdList);
	/** Render thread: queue the worker's recorded upload ahead of this frame's draws. */
	void SubmitUpload();
	/** Render thread: bind the streams and init the vertex factory (cheap). */
	void InitFactory(FRHICommandListBase& RHICmdList);
	/** Render thread: drop the GPU resources (RHI objects are ref-counted). */
	void ReleaseResources();
};

/** Camera for one worker update: the DLL camera plus the billboard axes. */
struct FPFSplatViewRequest
{
	PFSplatCamera Cam;
	FVector3f RightL = FVector3f(1, 0, 0);   // camera right, splat-local (unit)
	FVector3f UpL = FVector3f(0, 0, 1);      // camera up, splat-local (unit)
};

/**
 * Splat scene proxy (billboard path), with the heavy work off the render thread.
 *
 * Render thread (GetDynamicMeshElements, every frame):
 *   1. Build the native DLL camera in splat metres from the engine view and post
 *      it to the worker (latest wins; cheap).
 *   2. If the worker finished a batch: submit its recorded GPU upload, bind its
 *      vertex factory, and retire the old batch (GPU refs dropped here, memory
 *      freed by the worker).
 *   3. Draw the newest batch with the translucent splat material.
 *
 * Worker thread (FPFSplatWorker), whenever the camera changed:
 *   PF_Splat_Update (cull, screen-size LOD, back-to-front order, 2D Gaussian),
 *   then expand each splat to a camera-facing quad straight into the batch's
 *   CPU buffers, then create + upload the GPU buffers on the batch's own RHI
 *   command list. UV0 = corner in [-1,1], UV1 = conic.ab, UV2 = (conic.c,
 *   radius px), vertex colour = sRGB + opacity.
 *
 * The drawn batch can be a frame or two behind the camera. Splat centres are in
 * world space, so they never drift; only the billboard facing lags while the
 * camera turns.
 *
 * Only the worker calls into the DLL with the cloud handle. The proxy joins the
 * worker before it closes the handle (destructor, render thread).
 */
class FPFSplatSceneProxy final : public FPrimitiveSceneProxy
{
public:
	SIZE_T GetTypeHash() const override;

	/** Takes ownership of OwnedCloud (may be null: nothing is drawn). */
	FPFSplatSceneProxy(const UPFSplatComponent* InComponent, PFSplatCloud* OwnedCloud);
	virtual ~FPFSplatSceneProxy() override;

	virtual void GetDynamicMeshElements(
		const TArray<const FSceneView*>& Views,
		const FSceneViewFamily& ViewFamily,
		uint32 VisibilityMap,
		FMeshElementCollector& Collector) const override;

	virtual FPrimitiveViewRelevance GetViewRelevance(const FSceneView* View) const override;
	virtual uint32 GetMemoryFootprint() const override { return sizeof(*this); }

private:
	friend class FPFSplatWorker;

	/** Worker: one update for Req; returns the filled batch (null if nothing visible). */
	FPFSplatBatch* BuildBatch(const FPFSplatViewRequest& Req);

	PFSplatCloud* Cloud = nullptr;                 // owned; closed in the destructor
	FMaterialRenderProxy* MaterialRenderProxy = nullptr;
	FMaterialRelevance MaterialRelevance;
	ERHIFeatureLevel::Type FeatureLevel = ERHIFeatureLevel::SM5;
	float UnitScale = 100.0f;                      // metres -> Unreal units (cm)
	float MinSplatPixels = 0.5f;
	int32 MaxSplatInstances = 300000;

	// ---- render thread <-> worker hand-off (guarded by Mutex) ----
	mutable FCriticalSection Mutex;
	mutable FPFSplatViewRequest PendingRequest;    // latest camera from the render thread
	mutable bool bHasPendingRequest = false;
	mutable FPFSplatBatch* ReadyBatch = nullptr;   // finished by the worker, not yet adopted
	mutable TArray<FPFSplatBatch*> StaleBatches;   // superseded before adoption: render thread submits + releases
	mutable TArray<FPFSplatBatch*> DeleteQueue;    // released by the render thread: worker deletes

	FPFSplatWorker* Worker = nullptr;              // owned; joined in the destructor

	// ---- worker-only scratch ----
	TArray<PFSplatInstance> Instances;

	// ---- render-thread-only state (GDME is not parallel for this proxy) ----
	mutable TUniquePtr<FPFSplatBatch> Batch;       // currently drawn
	mutable std::atomic<int32> LastVisibleCount{ 0 };
};
