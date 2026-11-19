# PointForge Viewer (Unreal Engine 5.5 plugin)

Loads and renders **3D Gaussian Splats** and **very large point clouds** inside
Unreal Engine, in the editor and in packaged (Win64) builds.

| Content | Formats | How it renders |
|---|---|---|
| Gaussian splats | 3DGS `.ply`, `.splat`, `.vxpc` (with `splat.bin`) | GPU path: compute-shader projection + GPU radix sort every frame, one translucent draw |
| Point clouds | `.las` `.laz` `.e57` `.ply` `.pts` `.xyz` | Converted once to a streamable octree (`pfconvert.exe`), then streamed with screen-space-error LOD |

The native work (file parsing, octree format, CPU splat fallback) lives in
`PointForgeNative.dll` and `pfconvert.exe`, built from the **PointForge** C++
repository. This plugin is the Unreal side: components, scene proxies, shaders
and a Blueprint API.

---

## 1. Requirements

- Unreal Engine **5.5**, Windows 64-bit.
- Shader Model 5 (D3D11/D3D12) for the GPU splat path. Below SM5 the plugin
  falls back to the CPU path automatically.
- **A C++ project to package.** Unreal packages Blueprint-only projects with its
  stock prebuilt game executable, which does not contain project plugins. Add a
  C++ module to the project (an empty one is enough) so packaging builds a
  project-specific executable that includes this plugin.

## 2. Install

1. Copy the plugin to `<YourProject>/Plugins/PointForgeViewer/`.
2. Make sure the native binaries are present. They are built from the
   PointForge repo and are not stored in git (`*.dll` is ignored):

   | File | Where | Used for |
   |---|---|---|
   | `PointForgeNative.dll` | `ThirdParty/PointForgeNative/bin/` and `Binaries/ThirdParty/` | splat parsing, CPU splat path, octree API |
   | `zstd.dll` (+ `zstd.lib`, `zstd.h`, `zstd_errors.h`) | `ThirdParty/zstd/{bin,lib,include}/` | compressed octree nodes |
   | `pfconvert.exe` + `laszip3.dll`, `E57Format.dll`, `xerces-c_3_3.dll` | `Binaries/ThirdParty/` | point-cloud conversion |

   `PointForgeViewer.Build.cs` stages these next to the game executable when
   you package (RuntimeDependencies).
3. List the plugin in your `.uproject` and build the editor target
   (`<Project>Editor`).
4. Create the splat material once: run
   `Scripts/build_splat_material.py` in the editor (Tools > Execute Python
   Script). It creates `/Game/PointCloudData/Materials/M_PointForgeSplat`.

## 3. Gaussian splats

Add a **PF Splat** component (`UPFSplatComponent`) to any actor, set
**Splat Material** to `M_PointForgeSplat`, then set **Splat File** or call
`LoadSplatFile` / `LoadSplatFileAsync`.

```cpp
UPFSplatComponent* Splat = NewObject<UPFSplatComponent>(Actor, TEXT("Splats"));
Splat->SetSplatMaterial(SplatMaterial);
Actor->SetRootComponent(Splat);
Splat->RegisterComponent();
Splat->OnSplatLoaded.AddDynamic(this, &AMyActor::HandleLoaded);
Splat->LoadSplatFileAsync(TEXT("D:/Scans/room.ply"));
```

### Settings

| Property | Default | Notes |
|---|---|---|
| `SplatFile` | — | Absolute path. Parsed once and cached in memory (about 56 bytes per splat). |
| `UpAxis` | **Y down (3DGS / COLMAP)** | Also Y up (glTF / OpenGL), Z up, As stored. Converts right-handed files to Unreal's axes, so scenes are upright and not mirrored. GPU path only. |
| `UnitScale` | 100 | File metres → Unreal units (cm). |
| `MinSplatPixels` | 0.5 | Screen-size LOD; 0 draws every splat. |
| `MaxSplatInstances` | 300000 | Draw cap per view. When more are visible, the **nearest** are kept. |
| `MaxSplatScreenFraction` | 1.0 | Splats larger than this fraction of the screen are skipped: blurry blobs right at the camera that dominate frame time. |
| `bUseGpuSplats` | true | Off = CPU path (native DLL sorts on a worker thread). |
| `SplatMaterial` | — | Material slot 0. |

Bounds are computed from the splats and saved with the level, so culling is
correct as soon as the level opens.

### Blueprint API

| Group | Nodes |
|---|---|
| Loading | `Load Splat File` (blocking), `Load Splat File Async` (background; only the latest request is applied), `Unload Splats`, `Is Splat Loaded`, `Is Loading`, `Get Splat Count`, `Get Splat File` |
| Events | `On Splat Loaded` (component, count), `On Splat Load Failed` (component, path) |
| Settings | Set Up Axis / Unit Scale / Min Splat Pixels / Max Splat Instances / Max Splat Screen Fraction / Use Gpu Splats / Splat Material. Setting the variables in Blueprint calls these setters, which rebuild the render state. |
| Queries | `Get Splat Bounds` (all splats), `Get Core Bounds` (2nd–98th percentile, ignores floaters), `Get Suggested Viewpoint` (eye height inside the scene) |
| Material | Standard slot 0: `Create Dynamic Material Instance`, then `Set Scalar Parameter Value` on **Brightness** and **OpacityScale** (live, no GPU re-upload). |

From Python, `get_suggested_viewpoint()` returns `(location, rotation)` or
`None`.

### How the GPU path works

1. On load, the component copies the splat records (position, 3σ radius, 3D
   covariance, colour) from `PointForgeNative.dll` and converts them to Unreal
   axes. The GPU proxy uploads them once.
2. Before each view family renders, a scene view extension runs three passes
   (skipped when neither camera nor transform changed):
   - **Project** (compute): view transform, frustum and LOD cull, 2D
     covariance → conic, visible extent, 16-bit depth key.
   - **Sort**: the engine's GPU radix sort (`SortGPUBuffers`), nearest first.
   - **Build quads** (compute): the nearest splats far-to-near as camera-facing
     quads, sized tightly to each ellipse, written into
     `FLocalVertexFactory`-layout vertex buffers.
3. The proxy draws one translucent mesh element with `M_PointForgeSplat`. The
   material uses the *After Motion Blur* translucency pass, so TSR does not
   smear splats.

Shaders: `Shaders/Private/PFSplatGPU.usf`, registered by the
`PointForgeViewerShaders` module (loads at `PostConfigInit`).

GPU memory is roughly 290 MB per million splats.

## 4. Point clouds

Place an `APFPointCloudActor` and call **`LoadPointCloudFile(path)`**:

```
LoadPointCloudFile("D:/scans/site.laz")
  first time : pfconvert.exe builds an octree in
               <source folder>/PointForgeCache/<hash>/   (minutes, once)
  after that : cache hit -> streams from disk in seconds
```

The cache key is a hash of path, size, modification time and convert settings.
Converted octrees (a folder with `meta.bin`) open directly with
`UPFPointCloudComponent::OpenOctreeDir`. Stream tuning lives on the component:
`SseBudgetPixels`, `GpuBudgetMB`, `UploadsPerFrame`, `PointCountLimit`,
`PointSize`, `bRoundPoints`, `bAttenuate`, `UnitScale`. Read live numbers with
`GetStats()`.

Conversion runs out of process by default (cancellable; a crash cannot take
down the editor). Set `bLinkPfcoreInProcess = true` in
`PointForgeViewer.Build.cs` to link `pfcore.lib` and convert in-process
instead.

## 5. Modules and files

| Module / file | Role |
|---|---|
| `PointForgeViewer` (Runtime, Default) | Components, actors, proxies, native-DLL loader, convert cache, UI panels |
| `PointForgeViewerShaders` (Runtime, PostConfigInit) | Global compute shaders + `/Plugin/PointForgeViewer` shader mapping |
| `PFSplatComponent` | Splat component: loading, caching, axis conversion, Blueprint API |
| `PFSplatGpuSceneProxy` | GPU splat state, view extension, proxy |
| `PFSplatSceneProxy` | CPU splat path (native DLL on a worker thread) |
| `PFPointCloudActor` / `PFPointCloudComponent` / `PFPointCloudSceneProxy` | Point-cloud streaming |
| `PFOctreeStore`, `PFOctreeFormat.h` | Octree reader; mirror of the on-disk format |
| `PFConvert`, `PFConvertPanel`, `PFConvertSettings` | Convert cache, convert UI, saved settings |
| `PFNativeApi` | Loads `PointForgeNative.dll`; optional exports resolve to null on older DLLs |
| `Scripts/build_splat_material.py` | Builds `M_PointForgeSplat` |
| `Scripts/build_edl_material.py`, `build_billboard_material.py` | Point-cloud materials |

## 6. Troubleshooting

| Symptom | Cause / fix |
|---|---|
| Splats black or missing after reopening a level (older plugin versions) | Bounds were not saved. Reload the file once, or re-save the level with this version. |
| Scene tilted or upside down | Set `UpAxis`. 3DGS training output is Y down; glTF exports are Y up. |
| Packaged game: "module 'PointForgeViewerShaders' could not be found" | The project is Blueprint-only. Add a C++ module (see Requirements). |
| Low FPS close to geometry | Fill rate from huge near-camera splats. Lower `MaxSplatScreenFraction`, or raise `MinSplatPixels`. |
| Packaging fails with "HttpListener unable to bind to 127.0.0.1:3000" | An editor plugin in the cook process clashes with a running editor. Close the editor before packaging. |

## 7. Notes

- `PFOctreeFormat.h` mirrors `OctreeFormat.h` / `PointFormat.h` from the
  PointForge repo. Update both if the on-disk format changes (the
  `static_assert`s catch size drift, not field drift).
- Point positions are rendered relative to the octree cube centre, and splat
  positions relative to the cloud centre, for float precision far from the
  origin.
- Out of scope for the plugin for now: measure, clip, slices, exports, web
  remote, video, photogrammetry. Those live in the standalone PointForge viewer.
