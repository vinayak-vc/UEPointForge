// Copyright. PointForge Viewer — Gaussian splat scene proxy (billboard path).
#pragma once

#include "CoreMinimal.h"
#include "PrimitiveSceneProxy.h"
#include "LocalVertexFactory.h"
#include "StaticMeshResources.h"   // FStaticMeshVertexBuffers
#include "DynamicMeshBuilder.h"    // FDynamicMeshIndexBuffer32
#include "MaterialShared.h"        // FMaterialRelevance

#include "PFNativeApi.h"           // PFSplatCloud handle type

class UPFSplatComponent;
class UMaterialInterface;

/**
 * One frame's splat quads on the GPU. Built on the render thread from the
 * native DLL's visible-splat list. Released by the proxy through
 * ENQUEUE_RENDER_COMMAND, the same way the point proxy evicts nodes.
 */
struct FPFSplatBatch
{
	FStaticMeshVertexBuffers Buffers;
	FDynamicMeshIndexBuffer32 IndexBuffer;   // 6 indices per splat (quad = 2 tris)
	FLocalVertexFactory VertexFactory;
	int32 NumSplats = 0;

	explicit FPFSplatBatch(ERHIFeatureLevel::Type InFeatureLevel)
		: VertexFactory(InFeatureLevel, "FPFSplatBatch") {}

	void ReleaseResources();
};

/**
 * Splat scene proxy (billboard path).
 *
 * Per view (render thread, GetDynamicMeshElements):
 *   1. Build the native DLL camera in splat metres from the engine view.
 *   2. PF_Splat_Update: frustum cull, screen-size LOD, back-to-front order, 2D Gaussian.
 *   3. Expand each visible splat to a camera-facing quad on the CPU. UV0 = corner
 *      in [-1,1], UV1 = conic.xy, UV2 = (conic.z, radius in px), vertex colour =
 *      RGB + opacity.
 *   4. Draw as one triangle list with the assigned translucent material. The
 *      material evaluates the Gaussian falloff (see Scripts/build_splat_material.py).
 *
 * The proxy owns its PFSplatCloud handle and closes it on destruction (render
 * thread), so no other thread can free it while a view is being built.
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
	PFSplatCloud* Cloud = nullptr;                 // owned; closed in the destructor
	FMaterialRenderProxy* MaterialRenderProxy = nullptr;
	FMaterialRelevance MaterialRelevance;
	float UnitScale = 100.0f;                      // metres -> Unreal units (cm)
	float MinSplatPixels = 0.5f;
	int32 MaxSplatInstances = 300000;

	// Render-thread state (GDME is not parallel for this proxy).
	mutable TUniquePtr<FPFSplatBatch> Batch;
	mutable TArray<PFSplatInstance> Instances;     // reused scratch from the DLL
	mutable TArray<FDynamicMeshVertex> Verts;      // reused scratch
	mutable TArray<uint32> Indices;                // reused scratch
	mutable int32 LastVisibleCount = 0;
};
