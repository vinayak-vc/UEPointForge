# PointForge Viewer — Feature Overview

Runtime, out-of-core point-cloud viewer for Unreal Engine 5.5. Loads arbitrary
large clouds (LAS/LAZ/E57/PLY/PTS/XYZ — billions of points, larger than RAM) by
**converting once** to a streamable on-disk octree, then **streaming** it from
disk every load after. Same "convert once, open instantly" idea as Euclideon's
UDS, but in your own open format and rendered natively in UE.

---

## 1. The big picture

```
LoadPointCloudFile("E:/Model/NTPC.laz")
   │
   ├─ cache HIT  ──────────────────────────────►  stream octree, fly around
   │   (octree dir already exists for this file)
   │
   └─ cache MISS ──►  pfconvert.exe builds octree   ──►  stream octree
        (slow, one time: ~minutes for 16 GB LAZ)        (seconds, every time after)
```

Two halves:

| Half | What | Where |
|------|------|-------|
| **pfconvert** | Importer. Source cloud → streamable on-disk octree (LOD pyramid). Out-of-core: bounded RAM regardless of input size. | The separate **PointForge** C++ repo (`pfconvert.exe`). Unchanged by this plugin. |
| **PointForge Viewer** | This UE plugin. The runtime viewer — streams octree nodes by screen-space-error LOD, LRU GPU budget, async loads. Replaces the standalone `pfview`. | This plugin. |

The plugin **shells out** to `pfconvert.exe` for the convert step (default), so a
20–90 min batch job runs in its own process (cancellable, can't corrupt the UE
heap). Optionally it can link `pfcore` and convert in-process (`bLinkPfcoreInProcess`).

---

## 2. End-to-end flow

1. **`APFPointCloudActor::LoadPointCloudFile(path)`** — call from Blueprint/C++/UI.
2. **`FPFConvert`** computes a cache dir keyed by a hash of the source file
   (path + size + mtime). If `meta.bin` exists there → cache hit. Else it runs
   `pfconvert.exe --out <cacheDir>` on a background thread.
3. On success (game thread) → **`UPFPointCloudComponent::OpenOctreeDir(cacheDir)`**.
4. **`FPFOctreeStore`** loads `meta.bin` + `hierarchy.bin` (resident, small),
   precomputes per-node cubes, and starts a worker thread for payload streaming.
5. **`FPFPointCloudSceneProxy`** renders: each frame it traverses the octree,
   frustum-culls, picks LOD by screen-space error, draws resident nodes, requests
   missing ones, and LRU-evicts to the GPU budget.
6. **`UPFViewerPanel`** (auto-shown) displays live stats and drives the tunables.

---

## 3. Components

| File | Role |
|------|------|
| `PFOctreeFormat.h` | Mirror of the on-disk structs (`FileMetadata`, `NodeRecord`, `PackedPoint`) — size-asserted; keep in sync with the PointForge repo. |
| `PFOctreeStore.{h,cpp}` | Loads hierarchy (resident), precomputes cubes + per-level spacing, runs a worker thread (`FRunnable`) that reads node payloads off `octree.bin` and builds GPU vertices. Request/result queues + LRU re-request support. |
| `PFConvert.{h,cpp}` | Convert-once cache: locate `pfconvert.exe`, hash→cache dir, run conversion async (or in-process if `PF_LINK_PFCORE`). |
| `PFPointCloudComponent.{h,cpp}` | `UPrimitiveComponent` owning the store + tunables + live stats. Ticks to push tunables to the proxy and expose `GetStats()`. |
| `PFPointCloudSceneProxy.{h,cpp}` | The render-thread streaming engine (see §4). Per-node `FStaticMeshVertexBuffers` + `FLocalVertexFactory`, SSE-LOD traversal, LRU eviction. |
| `PFViewerPanel.{h,cpp}` | Code-built UMG overlay (no `.uasset`): live stats + tunable sliders/checkboxes. |
| `PFPointCloudActor.{h,cpp}` | Drop-in actor: `LoadPointCloudFile`, auto-shows the panel. |

---

## 4. Streaming engine (the core)

A direct port of `pfview`'s loop, onto UE 5.5's `FPrimitiveSceneProxy`. All of
this runs on the **render thread** inside `GetDynamicMeshElements`, reading the
store's immutable hierarchy/cubes (safe — const after load).

**Per frame:**
1. **Upload** up to `Uploads/frame` finished async loads → create a per-node
   `FStaticMeshVertexBuffers` + `FLocalVertexFactory` (via
   `InitFromDynamicVertex`), add to the resident map.
2. **Evict** (LRU): if resident GPU bytes exceed `GPU budget`, release the
   least-recently-drawn nodes until under budget.
3. **Traverse** the octree top-down (per view):
   - **Frustum cull** each node's world cube (`View->ViewFrustum`).
   - **Draw** the node if resident & GPU-ready (`PT_PointList` batch). A node is
     drawn together with its ancestors → partial loads still look correct.
   - **Request** an async load if the node is visible but not resident.
   - **Screen-space error**: `pixels = nodeSpacing · (viewportH·0.5/tan(fovY/2)) / distance`.
     Descend into children while `pixels > LOD budget`. Closer/denser → more detail.

**Threads:** a worker (`FPFOctreeStore::Run`) reads `octree.bin` payloads and
builds vertex arrays off the game/render threads; results are drained on the
render thread. Stats are written via atomics, read by the component for the panel.

---

## 5. The panel

Auto-shown top-left when a cloud loads (PIE/packaged only — needs a game viewport).

**Stats (read-only):**
- **Cloud** — total points + nodes in the whole on-disk octree.
- **Visible nodes** — passed frustum + SSE this frame.
- **Drawn nodes** — of visible, how many are resident & drawn (visible==drawn → no holes).
- **Points on GPU** — total across all resident buffers (incl. cached off-screen).
- **Drawn points** — points actually rendered this frame.
- **GPU resident** — GPU memory used by point buffers.
- **Load queue** — pending async load requests (0 = caught up).
- **FPS**.

**Controls:**
- **LOD budget (px)** — SSE threshold. Lower = more detail + more VRAM/loading. *(live)*
- **GPU budget (MB)** — LRU memory cap. *(live)*
- **Uploads/frame** — max node uploads per frame; higher fills faster, bigger hitches. *(live)*
- **Point size / Round points / Attenuate** — wired but **no visual effect yet**
  (needs milestone 2B; UE `PT_PointList` is fixed 1px).

---

## 6. On-disk octree format (v1)

Produced by `pfconvert`, consumed here. A converted cloud is a directory:
`meta.bin` (FileMetadata, magic "PFO1"), `metadata.json`, `hierarchy.bin`
(NodeRecord array, 52 B each), `octree.bin` (per-node `PackedPoint` payloads,
20 B each). Positions are quantized (LAS-style scale/offset); the viewer uploads
positions **relative to the cube centre** as float for GPU precision. See the
PointForge repo's `docs/ARCHITECTURE.md` and `src/common/OctreeFormat.h` (the
single source of truth — `PFOctreeFormat.h` mirrors it).

---

## 7. Status

| Milestone | State |
|-----------|-------|
| Convert-once cache + async pfconvert | ✅ done |
| Streaming engine (SSE-LOD, LRU, async, per-node buffers) — **2A** | ✅ done |
| Live stats panel + tunable controls — **2C** | ✅ done |
| Sized / round / attenuated points (quad vertex factory + `.usf`) — **2B** | ⛔ pending |

Points currently render as fixed **1px** `PT_PointList`. Milestone 2B adds a
quad-expanding custom vertex factory + shaders (ported from the LidarPointCloud
plugin) to make Point size / Round / Attenuate functional.

---

## 8. Build & run

1. Build `pfconvert.exe` from the PointForge repo (LASzip enabled for LAS/LAZ;
   ensure `laszip3.dll` sits beside it).
2. Enable **PointForge Viewer** in the `.uproject` (already added). Build the UE project.
3. Place a **PointForge Point Cloud** actor (or BP subclass), set `PfConvertExePath`
   if not auto-located.
4. **PIE / packaged:** call `LoadPointCloudFile("E:/Model/scan.laz")`. First run
   converts (watch `LogPointForgeConvert`), then streams; the panel appears.

**Color:** assign an **Unlit** material reading `VertexColor → Emissive` to the
component's `PointMaterial` for RGB; otherwise points render grey (default material).

---

## 9. Known limits / next

- **2B** sized/round/attenuated points (the 3 dead controls).
- Convert is single-process (single-threaded indexer in pfconvert). Phase C
  parallelises well — future work.
- Per-node payload reads open `octree.bin` per request on the worker — fine, could
  use a persistent handle / memory-map for higher throughput.
- Multi-view (nDisplay/stereo) stats reflect the primary view only.
