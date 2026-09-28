# GPU resources and pipeline — source audit, 2026-09-28

Read-only review of `4479927` on `codex/batch-export-safety`. No build, test, GPU run, app launch, or memory stress was performed; another worker held the GPU/build lock. This is a ten-file source batch, not a whole-engine or whole-repository verdict. Sizes and lifetime conclusions below are source-inferred unless attributed to the earlier measured report.

## Full read inventory

Every line of these ten product files was read (1,739 lines total):

| File | Lines |
|---|---:|
| `engine/src/gpu/MetalDevice.h` | 51 |
| `engine/src/gpu/MetalDevice.mm` | 46 |
| `engine/src/gpu/Resources.h` | 151 |
| `engine/src/gpu/Resources.mm` | 278 |
| `engine/src/gpu/TexturePool.cpp` | 65 |
| `engine/src/gpu/TexturePool.h` | 118 |
| `engine/src/ResourcePaths.cpp` | 80 |
| `engine/src/ResourcePaths.h` | 26 |
| `engine/src/pipe/Pipeline.cpp` | 577 |
| `engine/src/pipe/Pipeline.h` | 347 |

This upgrades exactly these ten files to **full source read** in `2026-09-28-audit-coverage.md`'s inventory. `Pipeline.cpp` was previously targeted; the other nine were in its unread batches. No other file was read completely here.

## Targeted caller reads

Searched product references to texture allocation, upload/download, pool ownership, `setPinned`, `nodeOutput`, `resizeAux`, output format changes, command submission, and resource paths. Read only the relevant excerpts of `engine/src/Engine.cpp:180-280`, `engine/src/pipe/DevelopPipeline.cpp:40-95,250-295,470-525`, `DevelopMask.cpp:650-710`, `DevelopLocal.cpp:625-690`, `DevelopOutput.cpp:605-645`, `engine/src/CApi.cpp:530-570`, and `app/Engine+Render.swift:75-165`. Read `feedback/2026-09-28-memory-retention.md` and searched the dated engine, mask, and desktop reports for overlap. These caller files remain **targeted**, not full reads.

## Findings and checks

**No new finding established.** The source trace checked the following named failure modes:

| Risk checked | Source evidence and limit |
|---|---|
| Allocation format and CPU transfer | `Resources.mm:11-38` maps the declared formats and byte sizes; `Pipeline.cpp:345-360` acquires the node's declared shape/format. The sampled display read in `Engine.cpp:212-226` branches on its format. Actual rendered values were not checked here. |
| Pooled ownership and GPU completion | `Pipeline.cpp:340-360,379-413` clears the non-owning pointer with its owner, then reuses textures at the computed last reader. `Resources.mm:261-273` waits for completion and reports command-buffer errors before `Pipeline.cpp:451-457` clears dirty state and shrinks idle storage. Metal execution, peak residency, and output bytes require a GPU run. |
| External node readers | `DevelopPipeline.cpp:44-66` pins six named intermediate outputs; searched `nodeOutput` callers include the fusion, dehaze, reference-image, highlight-stage, and HDR paths. A returned Swift `MTLTexture` is bridged through `CApi.cpp:546-565` and `app/Engine+Render.swift:92-108`; asynchronous canvas lifetime was not exhaustively audited. |
| Auxiliary texture lifetime | The brush accumulator's only resize callers at `DevelopMask.cpp:683-710` reset the component records and parameters when its contents are discarded. Other aux writes use `updateAux`; this review did not prove every shader's full-write invariant. |
| Resource discovery | `ResourcePaths.cpp:16-65` locates bundle resources or the nearest build tree; its product callers are `Engine.cpp:16,25,70,80`. Installed bundle contents and failure paths were not exercised. |

`Pipeline.cpp:393-405` already names a bounded **reuse loss**: a disabled node can remain the static last reader, so an upstream texture stays resident. This is an existing disclosed limitation, not a newly discovered defect. The latest change releases disabled outputs and trims idle pool textures (`Pipeline.cpp:164-175,451-457`). `2026-09-28-memory-retention.md` measured 903.1 → 303.5 MiB of retained texture payload after disabling filters in a controlled 1992×1330 fixture and reports passing GPU assertions. That evidence is authoritative for that fixture; it does not establish 42 MP capacity or active-filter peak footprint.

The next meaningful resource check is a paired full-resolution active-filter peak and repeated open/disable cycle on a memory-constrained Mac, recording both `allocatedBytes()` and process footprint. A small GPU render that toggles a disabled last reader and checks both output bytes and residency would isolate the known reuse loss before changing its schedule. Neither check ran in this source-only audit.
