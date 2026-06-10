# PointForge Viewer (UE5.5 plugin)

Runtime out-of-core streaming viewer for **PointForge** on-disk octrees.

Workflow (the "convert once, load instant" pattern):

```
LoadPointCloudFile("D:/scans/site.laz")
  first time : pfconvert builds an octree, cached at
               <ProjectSaved>/PointForgeCache/<hash>   (slow, one time)
  after that : cache hit -> streams from disk in seconds
```

## Files
| File | Role |
|------|------|
| `PointForgeViewer.uplugin` | plugin descriptor (Runtime module, Win64) |
| `…/PointForgeViewer.Build.cs` | deps + optional pfcore static-link toggle |
| `…/PFOctreeFormat.h` | **mirror** of the on-disk structs (size-asserted) |
| `…/PFOctreeStore.{h,cpp}` | loads meta.bin + hierarchy.bin; reads node payloads |
| `…/PFConvert.{h,cpp}` | convert-cache (shell-out to pfconvert.exe, or in-proc) |
| `…/PFPointCloudComponent.{h,cpp}` | `UPrimitiveComponent`, makes the proxy |
| `…/PFPointCloudSceneProxy.{h,cpp}` | `FPrimitiveSceneProxy` (STUB: node-cube wireframes) |
| `…/PFPointCloudActor.{h,cpp}` | Blueprint-callable `LoadPointCloudFile` |

## Status
- **Working now:** opens a converted octree, builds the scene proxy, draws coarse
  node-cube wireframes (proves hierarchy parse + traversal + transform).
- **Stub / next milestone:** actual point rendering — async payload streaming,
  RHI vertex buffers, `PT_PointList` mesh batches, screen-space-error LOD, LRU
  GPU budget. Hooks are in `FPFPointCloudSceneProxy` and `FPFOctreeStore`.

## Setup
1. Build PointForge so `pfconvert.exe` exists (`build/Release/pfconvert.exe`),
   and ensure `laszip3.dll` sits beside it.
2. Either:
   - leave `bLinkPfcoreInProcess = false` (default) and bundle/locate
     `pfconvert.exe` — the plugin auto-finds `build/Release/pfconvert.exe`, or
     drop it in `Plugins/PointForgeViewer/Binaries/ThirdParty/`, or set
     `PfConvertExePath` on the actor; **or**
   - set `bLinkPfcoreInProcess = true` in `PointForgeViewer.Build.cs` to link
     `pfcore.lib` + `laszip3.lib` and convert in-process.
3. Regenerate project files, rebuild, enable **PointForge Viewer** (already added
   to the `.uproject`), drop a `PFPointCloudActor` in a level, call
   `LoadPointCloudFile` from Blueprint/C++.

## Notes
- `PFOctreeFormat.h` is a hand-mirror of `OctreeFormat.h` / `PointFormat.h`.
  If the on-disk format version bumps, update both (the `static_assert`s catch
  size drift, not field drift).
- `UnitScale` (component) converts source units → UE cm (default 100 = metres).
- Positions are rendered relative to the octree cube centre (matches pfview's
  large-coordinate precision convention).
