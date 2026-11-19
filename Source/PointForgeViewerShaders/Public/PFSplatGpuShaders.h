// Copyright. PointForge Viewer — GPU splat compute passes (dispatch API).
#pragma once

#include "CoreMinimal.h"
#include "RHIFwd.h"
#include "RHIDefinitions.h"   // ERHIFeatureLevel

class FRHICommandList;
class FRHIShaderResourceView;
class FRHIUnorderedAccessView;

/**
 * GPU splat pipeline, per view (see Shaders/Private/PFSplatGPU.usf):
 *
 *   1. Project   one thread per splat: view transform, frustum + screen-size cull,
 *                2D covariance -> conic + radius (px), quantised depth key.
 *                Culled splats get key 0xFFFF so they sort last.
 *   2. Sort      engine GPU radix sort (SortGPUBuffers), keys ascending =
 *                nearest first; values = splat indices.
 *   3. BuildQuads one thread per quad slot: takes the nearest min(visible, slots)
 *                splats and writes them far-to-near as camera-facing quads into
 *                the vertex buffers of an FLocalVertexFactory (position, UV0..2,
 *                colour), so the existing translucent splat material draws them.
 *
 * All views are raw RHI views; the caller handles resource transitions.
 * Requires SM5 (compute + typed UAV stores).
 */

/** Quantised depth key range used by the project pass (16 bits). */
static constexpr uint32 PFSplatDepthKeyMask = 0xFFFFu;

struct FPFSplatProjectArgs
{
	uint32 NumSplats = 0;
	float UnitScale = 100.0f;          // record metres -> local units
	float MaxAxisScale = 1.0f;         // largest axis scale of local-to-world (radius cull)
	float MinPixelRadius = 0.5f;       // screen-size LOD (px); 0 = off
	float NearCull = 20.0f;            // view-space depth below which splats are skipped
	uint32 bOrtho = 0;
	float MaxRadiusPx = 4096.0f;       // giant-splat cutoff: larger footprints are dropped

	FMatrix44f LocalToView = FMatrix44f::Identity;   // local -> view (x right, y up, z forward)
	FMatrix44f ViewToClip = FMatrix44f::Identity;    // engine projection matrix
	FVector2f Focal = FVector2f(1, 1);               // pixels per unit at depth 1 (x, y)
	FVector2f Viewport = FVector2f(1, 1);            // pixels
	FVector2f JacobianLimit = FVector2f(1.3f, 1.3f); // 1.3 * tan(half fov), x and y
	FVector2f DepthKeyRange = FVector2f(0, 1);       // x = min depth, y = 1 / (max - min)

	FRHIShaderResourceView* Splats = nullptr;            // StructuredBuffer<FSplatRec> (56 B)
	FRHIUnorderedAccessView* OutKeys = nullptr;          // R32_UINT
	FRHIUnorderedAccessView* OutValues = nullptr;        // R32_UINT
	FRHIUnorderedAccessView* OutConic = nullptr;         // R32G32B32A32_FLOAT: a, b, c, extent (sigmas)
	FRHIUnorderedAccessView* OutWorldRadius = nullptr;   // R32_FLOAT: world units per pixel at the splat
	FRHIUnorderedAccessView* OutVisibleCount = nullptr;  // R32_UINT[1], cleared by the caller
};

struct FPFSplatQuadArgs
{
	uint32 NumQuadSlots = 0;           // quads in the vertex buffers (draw size)
	float UnitScale = 100.0f;
	FVector3f RightLocal = FVector3f(1, 0, 0);   // camera right, local space, world length 1
	FVector3f UpLocal = FVector3f(0, 0, 1);      // camera up, local space, world length 1

	FRHIShaderResourceView* Splats = nullptr;          // StructuredBuffer<FSplatRec>
	FRHIShaderResourceView* SortedValues = nullptr;    // R32_UINT, nearest first
	FRHIShaderResourceView* Conic = nullptr;           // R32G32B32A32_FLOAT
	FRHIShaderResourceView* WorldRadius = nullptr;     // R32_FLOAT
	FRHIShaderResourceView* VisibleCount = nullptr;    // R32_UINT[1]

	FRHIUnorderedAccessView* OutPositions = nullptr;   // R32_FLOAT, 3 per vertex
	FRHIUnorderedAccessView* OutTexCoords = nullptr;   // R32G32_FLOAT, 3 per vertex (UV0..2)
	FRHIUnorderedAccessView* OutColors = nullptr;      // R32_UINT, packed FColor (BGRA)
};

/** True if the GPU splat path can run on this platform / feature level. */
POINTFORGEVIEWERSHADERS_API bool PFSplatGpu_IsSupported(ERHIFeatureLevel::Type FeatureLevel);

/** Render thread. */
POINTFORGEVIEWERSHADERS_API void PFSplatGpu_DispatchProject(FRHICommandList& RHICmdList, const FPFSplatProjectArgs& Args, ERHIFeatureLevel::Type FeatureLevel);

/** Render thread. */
POINTFORGEVIEWERSHADERS_API void PFSplatGpu_DispatchBuildQuads(FRHICommandList& RHICmdList, const FPFSplatQuadArgs& Args, ERHIFeatureLevel::Type FeatureLevel);
