# Orion — Architecture

Constraints driving every choice below: **macOS first, Windows and Linux through Dawn** · **no language required or banned: maintainability and speed decide (#360)** · **speed non-negotiable** · **clean codebase, lean on mature libraries**.

## Language: C++20

Not a compromise — C++ is the *stronger* ecosystem for this domain. Every load-bearing library (LibRaw, OpenColorIO, lensfun, lcms2, Halide, OpenImageIO) is C++-native. Rust would have meant FFI or hand-rolling color management.

Go and Python are wrong here: no GPU story, GC pauses in a real-time pixel loop, and none of the imaging libraries. Python stays useful for tooling — profile generation, test harnesses, benchmark scripts.

## GPU: Metal, shaders authored in Slang

Metal wins on macOS: native f16, best-in-class tooling (Xcode GPU capture, Instruments), free MPS primitives, direct Core ML/Neural Engine path, and zero translation layers. Vulkan-via-MoltenVK would cost debuggability and add a translation layer for a portability benefit not needed yet.

The lock-in objection is solved by **[Slang](https://github.com/shader-slang/slang)** (Khronos-hosted, Apache-2.0): author each kernel once, compile to MSL for Metal and WGSL for Dawn. A thin C++ GPU abstraction (`Device` / `Texture` / `Kernel` / `CommandBuffer`) holds both backends behind one interface.

### Two backends: Metal and Dawn (issue 24, decisions #317, #375, #376)

Windows and Linux run the engine on **Dawn** alone (WebGPU in C++, #317, built from source by `tools/build-dawn.sh`, #361): D3D12 on Windows, Vulkan on Linux (#376). On macOS Metal is the default and Dawn runs on Metal behind `ORION_GPU=dawn`, so the port happened on this machine with Metal as the reference.

| Piece | How |
|---|---|
| Seam | The existing `gpu::Device / Texture / Library / Kernel / CommandBuffer` *are* the interface; each owns an abstract `Impl` (`gpu/Backend.h`) with a Metal subclass (`Metal*.mm`) and a Dawn subclass (`Dawn*.cpp`; `DawnMetal.mm` and `DawnNative.cpp` hold what differs per system). No node, graph or call site changes. `Gpu.cpp` holds the neutral half (formats, sizes, forwarding). |
| Choosing | `Device::create()` reads `ORION_GPU` (`metal` default, `dawn`); `Device::create(Backend)` for a test that wants both. So the whole suite, the bench and the app run on Dawn with no rebuild. Built without `third_party/dawn-build`, Dawn throws and Metal is unaffected. |
| Shaders | The same Slang source compiles twice: `.metallib` as before, and `.wgsl` (slangc → `wgsl_prep.py` → `build/shaders/<entry>.wgsl`). The prep script reads both targets' reflection and writes a header the backend reads: each texture's Metal slot → WGSL binding; the parameter block's copies from Metal's packed layout to WGSL's uniform layout (arrays of scalars get a 16-byte stride there); storage textures with a format token and `write` access unless read back. The host keeps writing Metal-layout structs. |
| Formats | WGSL fixes a storage texture's format in the shader, Metal does not. The Dawn kernel keeps one pipeline per combination of bound storage formats, made on first use, so a node that changes format (`setNodeFormat`) just gets a second pipeline. |
| Display | `Texture::displayHandle()` is what a shell draws: on Metal the texture; on Dawn an RGBA8 texture lives in an IOSurface (`SharedTextureMemory`, as in `spikes/dawn-avalonia/`) and the handle is an `id<MTLTexture>` over the same memory. The facade's texture getters return it, so `present.mm` takes it unchanged. On Windows and Linux there is no shared surface yet: the shell reads every render back through the facade (`orion_engine_read_pixels`). |
| Parity | `tests_dawn_parity.cpp` renders the develop graph on both backends with every node pinned and replays every node's kernel alone on Dawn on Metal's inputs (54 of 56 bit-identical, 2 within one fp16 step), then the whole graph on each (bit-identical output); the whole suite also runs with `ORION_GPU=dawn`. |
| Dawn settings | One device per process (Metal's system default is one). `disable_robustness` (Metal never bounds-checked these shaders; on, it doubled GPU time). `RenderAttachment` on every texture so Dawn's zero-fill is a cheap clear. Prepared shaders and their pipelines cached per device. |
| Default | On macOS, **Metal** until Dawn's first render after an open stops costing more (49 ms against 96-507 ms: WebGPU's zero-fill of new textures). Steady state matches: fit drag, fit settled, 100%, pan all within noise. |

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
   - **Built, partly (issue 22, decision #371).** A third graph, the *region graph*, is the unchanged develop graph built over a **window of the mosaic**: the bounding box of the visible rectangle mapped back through crop, straighten, perspective (the geometry node's own map) and then the lens (`lens::fetchPoint`), plus a 192-pixel halo, origin on a 16-pixel grid, size bucketed to 256 so a pan reloads rather than rebuilds. The window is decimated by a power of two `step` (1–8) chosen so one region pixel ≤ one screen pixel: never softer than the screen, and the cost is the screen's, not the sensor's. Region pixel *i* is exactly pixel *i* + origin of the whole frame decimated by `step` (the preview graph at step 4), so only three places learn the offset: the lens shifts its two centres, the perspective and the geometry node use the whole frame's size, and the geometry node subtracts the origin at its one fetch. Radii in frame pixels (sharpening; grain already) divide by `step`. The noise measurement is the frame's, not the window's. Whatever knows where it is (masks, spots, grain, the creative vignette) or reads more than a halo (dehaze's airlight, the clarity and fusion pyramids, the highlight fill, the guided filter) is **refused** by `DevelopPipeline::regionSupports` and the caller renders the whole graph — each is a follow-up. The preview graph is the small whole-frame pass: the histogram and the eyedropper read it while the full graph is older than the edit.
2. **Per-node output caching** — moving a slider recomputes only its downstream subgraph.
3. **Degrade-then-refine** during drags: skip expensive nodes mid-gesture, refine on idle.
4. Async thumbnail workers + LRU cache at app level.

## Library stack

| Need | Choice | License |
|---|---|---|
| RAW decode | **LibRaw** — Sony ARW well covered, 1000+ cameras | LGPL-2.1 / CDDL / commercial |
| Shaders | **Slang** → MSL, WGSL | Apache-2.0 |
| GPU off macOS | **Dawn** (D3D12, Vulkan), statically linked | BSD-3 (third parties in `NOTICE`) |
| Color management | **OpenColorIO** + **lcms2** for ICC | BSD-3 / MIT |
| EXIF read | **Apple ImageIO** (native, free on macOS) | — |
| XMP sidecars | **Adobe XMP Toolkit** | BSD |
| Lens corrections | **lensfun** (DB is CC-BY-SA) | LGPL-3 |
| Index/cache | none since #380: the SQLite index was the Swift app's | — |
| ML inference | **Core ML** (ANE) now; ONNX Runtime when porting | — |

⚠️ **Avoid exiv2** — GPL-2.0, which a source-available, never-redistributed work (#359, `LICENSE`) cannot carry. Apple's ImageIO covers metadata reads on macOS; revisit when porting.

## Library, not catalog — answering "why a DB at all?"

You're right that a catalog is overkill. The design:

- **XMP sidecars are the source of truth.** One `.xmp` next to each RAW holding the edit stack + rating + label. Portable, diffable, survives moves and backups, readable by other tools. Delete Orion and your edits still exist.
- **No index of record.** The Swift app kept a disposable SQLite index for filtering large folders; it retired with that app (#380), and the Avalonia app reads the sidecars directly. If one comes back, it stays rebuildable from the sidecars at any time.

So: folder-first browsing exactly as you work now.

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

The engine ships as a standalone C++ library with a narrow C API. The UI is a *client*. That is what let the SwiftUI app be replaced (#380) without touching the engine.

## UI shell: Avalonia 12 on .NET 10 (#315, #316, #380)

`shell/`, one C# app for macOS, Windows and Linux; it replaced the SwiftUI app, whose history is in git (`UI-DECISION.md` has the original evaluation). It ships as `Orion.app` on macOS; Windows and Linux packages are CI artifacts (#376).

| Piece | How |
|---|---|
| Boundary | Two hand-written C surfaces, POD only: the facade `engine/include/orion/orion.h`, and the shim over it, `shell/native/shim.h` / `shim.cpp` (`shim_*`: adjustments by C name through a table generated from `orion.h`, brush strokes by revision, readback), built with the engine into `liborion_shim` and called by P/Invoke (`shell/Shared/Native.cs`). |
| Exceptions | **C# cannot catch a C++ exception; an escaped one terminates the process.** Every facade entry point runs in `guard()` (`engine/src/CApiGuard.h`); every shim entry point that allocates runs in `shim_guard()` (`shim.h`). |
| Canvas, macOS | Zero-copy (#354, #357): `present.mm` copies each render on the GPU into an IOSurface that Avalonia's Metal renderer imports, synchronised by two `MTLSharedEvent`s. Headless, or when the import fails, the shell copies through the CPU. |
| Canvas, Windows and Linux | A CPU copy of every render (`shim_read` → `orion_engine_read_pixels`) for now; zero-copy there is open (#355 is Linux's allocator). |
| Commands | Every command is registered by name (`--commands`), so it can be driven without the UI; `--selftest`, `--screenshot`, `--batch-export` and the agent run headless. |
| Debug tooling | Dear ImGui for internal inspectors only, never product UI. |

## Licence

Source-available under PolyForm Internal Use 1.0.0 with added permissions for personal use and pull requests (#359): never redistributed or sold. A work under it cannot carry GPL code; LGPL only dynamically linked. Revisions up to `abf3a51` remain Apache-2.0 (`NOTICE`).

## Open decisions

- **Distribution** — direct download + notarization assumed; Mac App Store sandboxing complicates folder-based workflow.
