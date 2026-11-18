#pragma once
// ---------------------------------------------------------------------------
// PointForgeSplatC.h — flat C API for 3D Gaussian Splat clouds, exported from
// PointForgeNative.dll next to the octree API in PointForgeC.h.
//
// Accepted inputs (PF_Splat_Open):
//   *.ply   3DGS point_cloud format (x,y,z, scale_0..2, rot_0..3, opacity, f_dc_0..2)
//   *.splat 32-byte antimatter15 binary records
//   *.vxpc  package containing "splat.bin" (32-byte .splat records)
//
// Contract:
//  - Coordinates: "centred" space, metres, Z-up right-handed, relative to the
//    cloud centre reported by PF_Splat_GetInfo. The caller supplies view/proj
//    matrices that map this space (column-major, GL/glm convention).
//  - PF_Splat_Update does the per-view work on the CALLING thread: frustum cull,
//    screen-size LOD, back-to-front ordering and the 2D Gaussian (conic) for each
//    visible splat. The back-to-front order is re-sorted only when the view
//    direction changes (translation alone reuses it), so flying is cheap.
//  - Output instances are back-to-front. Draw them with "over" alpha blending
//    (premultiplied: colour * alpha, alpha), depth test on, depth write off.
//  - Only POD crosses the boundary. Single-threaded use per handle.
// ---------------------------------------------------------------------------
#include "PointForgeC.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct PFSplatCloud PFSplatCloud;   // opaque handle

typedef struct PFSplatInfo {
    uint64_t count;            // number of splats
    double   center[3];        // source-space centre (subtracted from positions)
    float    boundsMin[3];     // AABB in centred space
    float    boundsMax[3];
    uint32_t hasHigherSH;      // 1 if the file had f_rest_* (ignored, colour is SH0 only)
    uint32_t pad;
} PFSplatInfo;

typedef struct PFSplatCamera {
    float    view[16];         // column-major: centred space -> view space
    float    proj[16];         // column-major: view space -> clip (perspective or ortho)
    float    viewportWidth;    // pixels
    float    viewportHeight;   // pixels
    float    minPixelRadius;   // LOD: skip splats whose projected radius is smaller (px); 0 = off
    uint32_t maxInstances;     // hard cap on output count (0 = limited by capacity only)
    uint32_t isOrtho;          // 1 = orthographic projection
    uint32_t pad;
} PFSplatCamera;

// One visible splat (68 bytes, 17 floats).
//   position / worldRadius : centred space (metres). worldRadius is the quad
//                            half-size at the splat's depth, for camera-facing
//                            billboards (corner * worldRadius * camera right/up).
//   ndc / depth / radiusNdc : screen-space quad, for hosts that draw in clip space.
// Quad corner c in [-1,1]^2:
//   fragment coord (px) = c * radiusPx     (evaluate the Gaussian there)
// Gaussian: d2 = conic.x*x*x + 2*conic.y*x*y + conic.z*y*y,
//           alpha = min(0.99, opacity * exp(-0.5*d2)).
typedef struct PFSplatInstance {
    float position[3];         // splat centre, centred space (metres)
    float worldRadius;         // quad half-size in metres at this depth
    float ndc[2];              // quad centre in NDC
    float depth;               // NDC z
    float conic[3];            // inverse 2D covariance in pixels: a, b, c
    float radiusPx;            // quad half-size in pixels (3 sigma, clamped to viewport)
    float radiusNdc[2];        // quad half-size in NDC (x, y)
    float color[4];            // r, g, b, opacity (0..1)
} PFSplatInstance;

// Opens a splat cloud. Returns NULL on failure (bad path, unsupported file, empty).
PF_API PFSplatCloud* PF_Splat_Open(const char* pathUtf8);
PF_API void          PF_Splat_Close(PFSplatCloud* cloud);
PF_API int32_t       PF_Splat_GetInfo(PFSplatCloud* cloud, PFSplatInfo* out);   // 1 ok, 0 fail

// Culls, LOD-filters and orders the splats for this camera, then writes up to
// min(capacity, cam->maxInstances if non-zero) instances to `out` in back-to-front
// order. Returns the number written, or 0 on invalid input.
PF_API int32_t PF_Splat_Update(PFSplatCloud* cloud, const PFSplatCamera* cam,
                               PFSplatInstance* out, int32_t capacity);

// Number of splats that passed culling in the most recent PF_Splat_Update.
PF_API int32_t PF_Splat_LastVisibleCount(PFSplatCloud* cloud);

#ifdef __cplusplus
} // extern "C"
#endif
