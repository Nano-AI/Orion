# Orion — Status

**Update this at the end of every session.** It is the recovery point when context is cleared.

---

**Last updated:** 2026-07-27
**Phase:** M0 done. M1 ~60%, M2 ~55%. See the status table in `ROADMAP.md`.
**Next story:** crop/rotate and XMP sidecars (M1) · denoise and lens corrections (M2)

---

## Where we are

**M0 is done and the gate passed with room to spare.** A 24 MP Sony ARW goes
through a seven-node GPU pipeline and an exposure change re-renders in
**8.15 ms at p95 — at full resolution**, against a 16 ms budget. The preview-ROI
optimisation the architecture assumes we would need is not needed yet.

```
Source          Sony ILCE-7M3, 6024 x 4024 (24.2 MP, RGGB)
  decode        48 ms   (504 MP/s, LibRaw)
Pipeline        7 nodes, 971 MiB of intermediates
  full render   45.8 ms  (every node)
  exposure drag  6.7 ms median, 7.9 p95  (2 of 7 nodes)
  curve drag     3.7 ms median, 4.6 p95  (1 of 7 nodes)
  WB drag       26.5 ms                  (7 of 7 nodes)  <- over budget
M0 gate         PASS
```

**The pipeline is bandwidth-bound, not compute-bound.** An `rgba16f` texture at
24 MP is 194 MB. Adding tone and colour as separate pointwise nodes pushed
exposure drag to 19 ms and *failed the gate*; fusing all the scene-linear
pointwise work into one kernel, and AgX + curve into another, brought it back to
6.7 ms. Each operation still lives in its own function in
`shaders/ops/tone_ops.slang` — one file per adjustment, but one dispatch.

**Do not split pointwise operations back into separate nodes.** The
maintainability rule is about readable code, not one kernel per slider, and
every extra pointwise pass costs a 194 MB round trip for nothing.

⚠️ **White balance is over budget at 26.5 ms** and always will be: it rewrites
the linearize block at the head of the graph, so the demosaic has to rerun —
the demosaic interpolates white-balanced data. The fix is darktable's
degrade-then-refine (cheap demosaic mid-drag, full quality on release), or the
preview-ROI path. Neither is built yet.

Per-node caching works: moving exposure dirties only exposure + AgX, so
linearize, all three RCD passes and the colour matrix are served from cache.

### Dev machine (measured, not assumed)
Apple **M4**, macOS 26.4.1, arm64 · Xcode 26.6 / clang 21 · 17.8 GiB recommended working set · 13.3 GiB max buffer · **unified memory** · Apple7 GPU family supported.

Unified memory is a real advantage: CPU↔GPU transfers are free, so LibRaw can decode straight into a shared buffer with no staging copy.

### Toolchain installed
`cmake` · `ninja` · `libraw 0.22.2` · `little-cms2` (was already present)
Still needed: **Slang** (S0.3, not in Homebrew — grab a GitHub release).

### Build
```
cmake -S . -B build -G Ninja
cmake --build build
./build/apps/probe/orion-probe
```

### What exists
```
engine/include/orion/orion.h        C facade — POD only, no exceptions cross it
engine/src/CApi.cpp                 exception firewall; guard() turns throws into status
engine/src/Engine.{h,cpp}           engine proper, RAII
engine/src/gpu/MetalDevice.{h,mm}   device + queue
engine/src/gpu/Resources.{h,mm}     Texture, Library, Kernel, CommandBuffer
engine/src/raw/RawImage.{h,cpp}     LibRaw decode -> untouched CFA mosaic
engine/src/pipe/Pipeline.{h,cpp}    the DAG: Kahn topo sort, per-node dirty caching
engine/src/pipe/DevelopPipeline.*   the standard 7-node graph + adjustments
engine/src/pipe/ShaderParams.h      host mirrors of shader structs, static_assert'd
engine/src/util/ImageWriter.mm      PNG out via ImageIO
engine/shaders/*.slang              7 kernels, one file each
app/*.swift                         SwiftUI shell, MTKView canvas, zero-copy
apps/probe, apps/bench              C-API smoke test, and the M0 gate
design/                             tokens.json -> CSS + Swift; darkroom mockup
```

### Bugs worth remembering
1. **Slang binding indices are cumulative across a module.** Compiling all
   kernels into one metallib gave kernel 2 textures at index 2/3 and kernel 3 at
   4/5, while the host binds from 0 every dispatch — so every kernel after the
   first read unbound slots and produced black. Fix: **one metallib per kernel**.
   Do not "optimise" that back into a single module.
2. **The camera matrix must be row-normalised.** Without it, white balance and
   the colour matrix fight: the data is already neutral after WB, and an
   unnormalised matrix re-tints it (we had a magenta cast). dcraw normalises
   rgb_cam for the same reason.
3. **One geometry, one function.** The renderer, the crop overlay and hit
   testing each computed the photo's on-screen rectangle for themselves, and
   drifted apart — handles landed on a rectangle the pixels were not drawn in.
   `app/CanvasLayout.swift` is now the only copy, and the *engine is given*
   the preview canvas rather than deriving a second one. Same class of bug in
   the shader: the straighten pivot was derived from cropOrigin/cropSize,
   which describe the canvas rather than the user's rectangle, so the preview
   turned about the frame centre and the committed render about the crop
   centre. Pass the pivot, do not derive it.
4. **The crop must stay inside the turned frame.** Nothing enforced it, so a
   straightened export had transparent wedges in its corners — the crop is
   what gets sampled, and it reached past the picture. `constrainedCrop`
   shrinks and recentres it, which is what Lightroom does. A fixed preview
   canvas could not hold a steep angle either: a 3:2 frame at 45 degrees
   reaches 1.77x its short side, so the old constant 1.42 clipped corners past
   about 17 degrees. The canvas is now computed per angle and aspect, and
   sampled into a frame-sized texture so its cost stays flat.
5. **A `Path` view takes the size it is offered.** An unsized one inside a
   `.position()` grows to the whole overlay, and `.position` then centres
   *that* — which threw the crop corner marks into the middle of the window.
   Give hand-drawn marks a fixed `.frame`.

## Settled

See `DECISIONS.md` for the full list with reasoning. Headlines:

- C++20 engine, Metal GPU, Slang shaders. **No Rust, no Vulkan.**
- Compute DAG, one shader per node, `rgba16f` linear Rec.2020, scene-referred, pixels stay on GPU.
- XMP sidecars = truth, SQLite = disposable rebuildable index. Folder-based, no catalog.
- macOS first. Sony ARW only for v1.
- RCD demosaic, AgX-family sigmoid tone mapper, profiled wavelet denoise.
- Maintainability is a hard constraint (solo dev): small shaders, 3-file feature changes.

## In flight

**Nothing in flight.** UI shell decision is closed — see `UI-DECISION.md`. Planning is complete enough to start coding.

⚠️ Session limit and the 200-call web-search budget were both exhausted on 2026-07-27. **Do research inline and sparingly** — the developer asked for fewer subagents, and they proved fragile at this scale.

## Blocked / needs a decision from the developer

1. ~~UI shell~~ ✅ **Resolved: SwiftUI/AppKit + C++ engine** (decision #25). Qt was picked then reversed — see `UI-DECISION.md` for why.
2. **License / business model** — undecided by choice. Building to keep both doors open: avoid GPL libraries, dynamically link LGPL ones. Revisit before v1 ships.

## Scope — locked 2026-07-27

Every feature now has a milestone. Notable calls:
- **Cut from v1:** card import (point at a folder instead), brush masking, keywords/search.
- **Local edits land in M4**, gradient + luminance/color-range masks + AI subject/sky. Spot removal kept.
- **Bilateral grid + BGU pulled forward to M1** — built before needed, as the escape hatch for the latency budget.
- No tethered shooting.

## M1 progress

Done: white balance (real Kelvin, as-shot on open), exposure, highlights,
shadows, whites, blacks, vibrance, saturation, contrast, tone curve, and
export (JPEG/PNG/TIFF with quality and resize). The app has all of them.

Remaining in M1: crop/rotate/straighten · XMP sidecars and the non-destructive
op stack · undo/redo and history · folder browse, filmstrip, ratings and
filtering · the SQLite index.

## M2 progress

1. ✅ **Tone curve** — `pipe/ToneCurve.{h,cpp}` evaluates the same monotone cubic
   Hermite spline as the mockup into a 256x4 LUT (master, R, G, B); the shader
   samples it. Runs after AgX, in display space.
2. ✅ **Colour mixer** — eight hue bands with hue/saturation/luminance each,
   in `shaders/ops/hsl_ops.slang`. Weights overlap smoothly (60° falloff, squared)
   so a gradient crossing between bands does not band. Folded into the fused
   scene-linear kernel, so it costs no extra pass.
3. ✅ **Sharpening** — unsharp mask with detail masking, placed immediately after
   the demosaic. Upstream position is deliberate: dirt only flows downstream, so
   an exposure drag never recomputes it.
4. Profiled wavelet denoise + a per-camera noise profile
5. Lens corrections via lensfun
6. Before/after split — the mockup's Compare interaction

### Known gaps to close in M2
- Demosaic is **RCD-family, not a faithful RCD port** — directional +
  gradient-corrected + clamped, which is genuinely good but not the reference
  algorithm. Revisit against https://github.com/LuisSR/RCD-Demosaicing
- Highlight reconstruction is not implemented at all (clip only)
- The pipeline runs at full resolution; the preview-ROI path in ARCHITECTURE.md
  is designed but unbuilt. Not needed yet on an M4 — will be on lesser GPUs
- Black level ignores LibRaw's 2D cblack pattern (averaged instead)
- AgX output is sRGB-encoded; the EDR/P3 path is not wired up

## Culling — where the controls are

Rejection was reported broken three times and was never reproducible from the
code, because the failure was focus, not logic: the `x` handler was an
`onKeyPress` on the editor's root view, and the Metal canvas takes first
responder on any click. Culling now lives in a **Photo menu** (`PhotoCommands`
in `OrionApp.swift`), published through `@FocusedValue(\.cull)`. Menu shortcuts
route through the responder chain, so they work wherever focus sits — and the
shortcut is written next to its name instead of having to be known in advance.

R rejects · 1–5 rate · ⌘0 clears · ← → browse · 0 fits · 9 is actual size ·
⏎ applies a crop · ⎋ cancels one · ⌘R resets adjustments.

## Notes for whoever picks this up

- The developer wants **evidence, not agreement**. When they express skepticism about a technology, research it honestly — they explicitly asked to have their assumptions tested.
- Keep planning docs concise. Dense tables, not essays.
- The most important research finding is **Bilateral Guided Upsampling** (`RESEARCH.md` §4) — it is the general solution to "this algorithm is too slow to be interactive" and should be a DAG node type built in M1, before it's needed.
