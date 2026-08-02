# Orion — Architecture

Constraints driving every choice below: **macOS first** · **no Rust** · **C++/C, Go, Python comfortable** · **speed non-negotiable** · **clean codebase, lean on mature libraries**.

## Language: C++20

Not a compromise — C++ is the *stronger* ecosystem for this domain. Every load-bearing library (LibRaw, OpenColorIO, lensfun, lcms2, Halide, OpenImageIO) is C++-native. Rust would have meant FFI or hand-rolling color management.

Go and Python are wrong here: no GPU story, GC pauses in a real-time pixel loop, and none of the imaging libraries. Python stays useful for tooling — profile generation, test harnesses, benchmark scripts.

## GPU: Metal, shaders authored in Slang

Metal wins on macOS: native f16, best-in-class tooling (Xcode GPU capture, Instruments), free MPS primitives, direct Core ML/Neural Engine path, and zero translation layers. Vulkan-via-MoltenVK would cost debuggability and add a translation layer for a portability benefit not needed yet.

The lock-in objection is solved by **[Slang](https://github.com/shader-slang/slang)** (Khronos-hosted, Apache-2.0): author each kernel once, compile to MSL today and SPIR-V/HLSL when Windows/Linux land. Pair it with a thin C++ GPU abstraction (`Device` / `Texture` / `Kernel` / `CommandQueue`) so a Vulkan backend slots in behind the same interface.

## Engine: compute DAG, vkdt-shaped

The single most important structural decision, taken straight from the research:

- Edits form a **DAG of nodes**, topologically sorted — not a linear chain.
- **One compute shader per node.** No dual CPU/GPU implementations (darktable's biggest maintenance tax).
- Working format **`rgba16f`, linear Rec.2020**, scene-referred. No Lab anywhere.
- One large GPU allocation, sub-allocated for all intermediate buffers.
- **Pixels never leave the GPU** until export. The viewport displays the pipeline's output texture directly.

### Pipeline order
```
decode → white balance → demosaic → camera matrix → linear working space
  → [exposure, denoise, local contrast, grading, masks]   ← all scene-linear
  → ONE display transform (AgX sigmoid)
  → [creative LUT, output encode]
```

### Hitting <16 ms
1. **Preview pipe** runs at screen resolution over the visible ROI only. Full-res tiled pipe reserved for export and 100% zoom.
2. **Per-node output caching** — moving a slider recomputes only its downstream subgraph.
3. **Degrade-then-refine** during drags: skip expensive nodes mid-gesture, refine on idle.
4. Async thumbnail workers + LRU cache at app level.

## Library stack

| Need | Choice | License |
|---|---|---|
| RAW decode | **LibRaw** — Sony ARW well covered, 1000+ cameras | LGPL-2.1 / CDDL / commercial |
| Shaders | **Slang** → MSL | Apache-2.0 |
| Color management | **OpenColorIO** + **lcms2** for ICC | BSD-3 / MIT |
| EXIF read | **Apple ImageIO** (native, free on macOS) | — |
| XMP sidecars | **Adobe XMP Toolkit** | BSD |
| Lens corrections | **lensfun** (DB is CC-BY-SA) | LGPL-3 |
| Index/cache | **SQLite** | Public domain |
| ML inference | **Core ML** (ANE) now; ONNX Runtime when porting | — |

⚠️ **Avoid exiv2** — GPL-2.0, which would force Orion open-source or a paid commercial license. Apple's ImageIO covers metadata reads on macOS for free; revisit when porting.

## Library, not catalog — answering "why a DB at all?"

You're right that a catalog is overkill. The design:

- **XMP sidecars are the source of truth.** One `.xmp` next to each RAW holding the edit stack + rating + label. Portable, diffable, survives moves and backups, readable by other tools. Delete Orion and your edits still exist.
- **SQLite is a disposable index**, not a database of record. It exists purely so that filtering 5,000 photos by rating is instant instead of a 5,000-file disk read. It also caches thumbnails.
- **Fully rebuildable at any time.** Corrupt it, delete it, move the folder — Orion re-scans the sidecars and reconstructs. No lock-in, no "where did my catalog go" failure mode.

So: folder-first browsing exactly as you work now, with an index that makes rating/rejecting/filtering large shoots fast.

## Maintainability — a hard constraint, not a preference

Orion is built by **one developer with a Claude subscription**, who must be able to hand-edit any file six months from now. Code that can only be written, never modified, is a failure regardless of how fast it runs.

**This is a second, independent reason to choose Metal over Vulkan.** Vulkan famously needs ~1000 lines of boilerplate to draw a triangle; Metal needs roughly 100. Vulkan's explicitness buys control that matters for an engine team and costs debuggability that a solo developer can't absorb.

Rules the codebase holds to:

- **One node = one small shader.** Target 50–150 lines per kernel, each doing exactly one thing. No mega-shaders combining exposure, curves, and grading into one unreadable blob. If a node's shader passes ~200 lines, it's two nodes.
- **The GPU abstraction is written once, early, and then left alone.** `Device`, `Texture`, `Kernel`, `CommandQueue`. All the fiddly API surface lives here so no feature code ever touches it.
- **Adding a feature is a repeatable 3-file change:** one `.slang` kernel, one params struct, one UI panel. If adding a slider requires touching the scheduler, the abstraction is wrong.
  - ⚠ The engine-side wiring was quietly a *fourth* and *fifth* place for a long time — a node declared in `DevelopPipeline`'s constructor and its parameters pushed twelve hundred lines further down the same file. Since decision #113 it is one file: the graph's four regions each own a `.cpp` (`DevelopCapture`, `DevelopLocal`, `DevelopMask`, `DevelopOutput`) holding **both** the nodes it builds and the blocks it pushes, and `DevelopPipeline.cpp` is two ordered call lists. The table on `DevelopPipeline.h`'s private section says which region is which. ⚠ Neither call list may be sorted: node indices are held in members, so a reordered `build` compiles and rewires the graph.
- **Prefer a mature library over hand-rolled code** every time, even at some performance cost. LibRaw over a custom ARW parser; lensfun over custom distortion math.
- **Reference-image tests.** Each node gets a known-input → known-output test, so a refactor that breaks color science fails loudly instead of silently shifting every photo.
- **Readable beats clever.** Optimize only where the benchmark says to, and leave a comment saying what the benchmark said.

## Engine/UI separation

The engine ships as a standalone C++ library with a narrow C API. The UI is a *client*. This is what makes the UI decision reversible and the eventual Windows port tractable — the hard 80% is already portable.

## UI shell: SwiftUI/AppKit

Decided after evaluating Qt 6/QML, bespoke Skia/Metal, Dear ImGui, and C++ webview shells — full comparison in `UI-DECISION.md`. Chosen because the two stated priorities are **stunning look** and **minimal bloat**, and native wins hardest on both: Apple's materials, vibrancy, typography and animation curves come free, and the bundle is ~5–15 MB against Qt's 40–60 MB.

- **Canvas:** engine renders into the drawable's texture; `MTKView`/`CAMetalLayer` wrapped in `NSViewRepresentable`. Genuinely zero-copy — Swift never touches pixels. Pass `MTLDevice`/`MTLCommandQueue` across as opaque handles.
  ⚠️ SwiftUI's `.layerEffect`/`.colorEffect`/`.drawingGroup` **cannot** display an `MTLTexture` — they rasterize SwiftUI's own content. UI garnish only.
- **HDR:** `wantsExtendedDynamicRangeContent = true`, `pixelFormat = .rgba16Float`, `colorspace = extendedLinearDisplayP3`. Matches the engine's working format exactly; better than 10-bit integer.
- **State:** view models are plain `@Observable` objects with **zero SwiftUI types inside**, so any panel can be re-hosted in AppKit when SwiftUI hits a density cliff. `@Observable` is mandatory — `ObservableObject` would repaint all 40 panels on every slider tick. Sets a **macOS 14+ floor**.
- **Expect to fall back to AppKit** for the filmstrip and grid (`NSCollectionView` — SwiftUI has no real cell reuse) and for pinch-zoom (SwiftUI exposes no direct trackpad magnification events).
- **Debug tooling:** Dear ImGui for the internal pipeline inspector, node graph, and frame timings. Never product UI.

### C++ ↔ Swift boundary
Swift 5.9+ imports C++ headers directly, no shim required — but still hand-author a **narrow POD facade**: plain structs, int/float handles, no templates, no move-only types. It's the only boundary stable across Swift releases.

⚠️ **C++ exceptions cannot be caught by Swift — an uncaught one terminates the process.** The engine must never let one escape the facade.

## Open decisions
- **License strategy** — free/open vs paid closed-source. Currently building to keep both open. (Note: no UI-framework licensing obligations now that Qt is out.)
- **Distribution** — direct download + notarization assumed; Mac App Store sandboxing complicates folder-based workflow.
