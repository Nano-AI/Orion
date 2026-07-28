# Orion — Status

**Update this at the end of every session.** It is the recovery point when context is cleared.

---

**Last updated:** 2026-07-28 (overnight run, in progress)
**Phase:** M0 done. M1 ~98%, M2 ~97%.
**Next story:** the color-space picker, then a lens database

## Overnight run — 2026-07-28

Working agreement for this run: commit and push per feature, screenshot every
major feature, measure the engine's output rather than eyeballing it, and only
reach for Gemini if `research/` genuinely does not cover something. It has not
been needed so far.

**Done**

| | Commit |
|---|---|
| Crop constrained to the turned frame; straighten opens to ±90; pivot is the frame center; corner marks in a fixed box; culling moved to a Photo menu | `28ca074` |
| Tone curve panel — the engine's spline had been unreachable through the facade since M2 | `829e565` |
| Profiled wavelet denoise, with a per-frame Poisson–Gaussian fit | `bb06700` |
| Highlight reconstruction; fast guided filter (90 ms → 19.6 ms); a bench that stops crying wolf | `a4ac2fa` |
| Lens corrections — distortion, TCA, vignetting | `bd8c23c` |
| Export panel: measured file size, typed dimensions | `06fff34` |
| 16-bit output end to end; red/blue swap in the screenshot harness | `a50908c` |
| Edits persist per photo; keys work; compare survives a rotation | `02ad412` |
| **A blown highlight came out magenta** — linearize never clipped | see below |
| Every adjustment resets from its own readout | see below |
| Compare came apart on zoom; the top/bottom split was upside down | see below |
| Analog track controls; American spelling; a sidecar that survives a rename | see below |
| Export color space, EXIF and rating; a resize that keeps its depth | see below |

### Export, finished

sRGB, Display P3 and Adobe RGB, converted by ColorSync rather than by a matrix
typed in here — CLAUDE.md's "prefer mature libraries", and a hand-rolled
chromatic adaptation is a cast waiting to happen. The pixels are tagged sRGB
where they are made, because that is what they are, and converted from there;
tagging them as the destination would relabel without moving them, which is how
a file comes to open oversaturated.

EXIF, lens, date and the star rating are carried onto the file, read with
ImageIO rather than exiv2 — DECISIONS #10, and it reads the RAW's own blocks
straight out of the container. Orientation and the RAW's pixel dimensions are
dropped deliberately: the geometry node has already applied the rotation, so
copying the tag would tell every viewer to turn the picture again.

Verified end to end on `_PIC8220.ARW`: Make SONY, Model ILCE-7M3, lens
"24mm F1.4 DG DN | Art 022", ISO 3200, 1/80 at f/1.4, the capture date, Software
Orion, rating 4, and the three profiles each landing on the right file.

**A resize was dropping to eight bits.** The 16-bit output path shipped the
night before survived exactly as far as the first resize, and only for exports
with a size limit — the ones nobody re-checks. The test now reads the written
file back, because a PNG of a smooth ramp compresses to almost nothing at either
depth and byte counts cannot tell them apart.

The bench was passing a bare options struct, so it measured a write the product
never performs. It builds the same options the app does now.

### Compare and zoom

The split happens across the drawn quad in the canvas shader; the panel was
drawing the divider, the labels and the grab band against the *fit* rectangle.
Same rectangle only at fit. `CanvasLayout.drawnRect` is where the picture
actually is. The top/bottom split was also upside down — the fragment shader
recovered its position by unpicking `uv`, which is flipped in y and scaled into
a sub-rectangle of the texture. The quad coordinate is a varying now.

### The sidecar could not survive a rename

Swift's synthesised decoder throws on a missing key rather than falling back to
a property's default, and `Engine.restore` swallows it with a `try?`. Renaming
`denoiseColour` would have silently discarded **every** adjustment in **every**
sidecar on disk, and the photo would have opened unedited with nothing said —
and that was already true of adding any field at all. Decoding is field-by-field
and forgiving now, and still reads the old spelling.

### The magenta highlights

The one that mattered. `linearize` scaled each channel by its white-balance gain
and clamped only at zero, so a blown pixel — which the sensor delivers as
(S, S, S) — left the node as the gains themselves, about (2.2, 1.0, 1.6) on a
warm frame. Everything downstream preserves ratios, so the tone curve, the
color matrix and AgX all carried it faithfully to the screen. Every clipped
light in a night shot rendered magenta.

Clipping all three to one ceiling is dcraw's default, and it belongs in the
mosaic for dcraw's reason: RCD interpolates across an unclipped neighbour, so
clipping afterwards leaves a fringe instead of a clean edge. Written up in
`research/color-pipeline.md`.

Measured over the blown sign in `_PIC8220.ARW`: mean saturation **0.242 → 0.015**,
R/G/B 0.878/0.677/0.896 → 0.800/0.809/0.811.

A side effect worth knowing: highlight recovery is now a measured no-op on that
frame (saturation 0.0146 → 0.0147 at full strength), because a fully blown pixel
is already white and there is nothing to correlate. It still earns its place
where one channel clipped alone. Left off by default, but the panel copy no
longer promises to fix a magenta the pipeline no longer produces.

⚠️ The clip moves with white balance, by design — the white point does. It also
spends the headroom a reconstruction could have used. Both are the right trade
against a cast that was on every frame.

**Still to do, in order**

1. **A lens database.** The corrections are built and manual. lensfun would set
   the coefficients from what the EXIF names; the maths does not change. This is
   the largest remaining item — a dependency plus an XML database, not an
   afternoon.
2. **A real wide gamut.** The export picker offers sRGB, Display P3 and Adobe
   RGB, and converts correctly — but the display transform ends in Rec.709
   primaries and saturates there, so **nothing Orion renders yet falls outside
   sRGB**. Choosing P3 today buys correct tagging for a managed workflow, not
   more saturation. Widening it for real means giving `develop_display.slang`
   its output primaries as a parameter and moving the sRGB encode with them.
   The panel and the C header both say this plainly rather than implying
   otherwise.
3. **The EXIF read costs ~90 ms per export.** `writeImage` opens the RAW with
   ImageIO on every write to lift its metadata. Caching the property dictionary
   at open would give it straight back; export is off the interaction path, so
   it has not been worth doing yet.
4. **Temperature drag is 43 ms.** Structural: white balance rewrites the head of
   the graph, so the demosaic reruns. The fix is degrade-then-refine (a cheap
   demosaic mid-drag) or the preview-ROI path in `ARCHITECTURE.md`. Neither is
   built, and neither is small.

Verified 2026-07-28: a TIFF export reports `bitsPerSample: 16`, 6024×4024,
145 MB — which is exactly 6024·4024·3·2 bytes.

### Latency, re-measured 2026-07-28 (Sony ILCE-7M3, 6024×4024, M4)

**27 nodes, 4027 MiB of intermediates** — up from 16 nodes and 2.6 GiB. The M4
recommends a 17.8 GiB working set so this is comfortable, but it is the number
to watch on a lesser GPU.

| Drag | Nodes | Time |
|---|---|---|
| Exposure | 3 | 11.5 ms |
| Highlights / shadows | 10 | **23.6 ms** (was 90.1) |
| Color mixer | 5 | 19.2 ms |
| Temperature / tint / sharpen | 11 | ~50 ms |

**M0 gate passes at 11.67 ms p95**, against 16 ms. Re-measured 2026-07-28 after
the highlight clip: **12.70 ms p95** on `_PIC8220.ARW`. The clip is one
instruction in `linearize`; the difference is the frame, not the change.

⚠️ It was 9.04 ms before 16-bit output. Writing `RGBA16Float` from the display
and geometry nodes doubles the bytes those two move, and that is 2.6 ms of the
budget spent on a capability nobody sees on screen. It is a deliberate trade —
4.3 ms of headroom is still real headroom — but if the budget ever gets tight
this is the first place to look, and the fix is a second display path used only
for export rather than a wider one used always.

Temperature is over budget and always will be: it rewrites the head of the
graph, so the demosaic reruns. The fix is degrade-then-refine or the preview-ROI
path, neither of which is built.

### The screenshot harness

```
./build/Orion.app/Contents/MacOS/Orion --screenshot out.png --photo x.ARW \
    --scene crop-angle [--measure x,y,w,h] [--size 1680x1050]
```

Renders the real view hierarchy offscreen — no Screen Recording permission,
which a terminal does not have. Scenes live in `app/Screenshot.swift`; add one
there when you add a feature. `--measure` prints mean and standard deviation
for a region of the engine's output, which is the only way to tell whether a
filter did anything: noise that is obvious at 100% vanishes into a screenshot
scaled to fit a review pane. It is what caught the denoiser doing nothing, and the export panel's size
estimate reading twenty percent high.

`--measure` also prints mean saturation, which is what turned "there is purple
in my photo" into a number that could be watched going down.

A hover cannot be staged in an offscreen render, so `AdjustmentSlider.previewHover`
forces it for the `reset-hover` scene. Whether a control shifts sideways when its
hover state appears is exactly the kind of question a screenshot answers and
reading the code does not — this codebase has shipped that bug three times.

**What it does not prove:** the canvas is drawn as a still read off the GPU, not
through `MTKView` — AppKit cannot capture a Metal layer. Canvas geometry stays
the viewport suite's job.

---

## Where we are

**M0 is done and the gate passed with room to spare.** A 24 MP Sony ARW goes
through a seven-node GPU pipeline and an exposure change re-renders in
**8.15 ms at p95 — at full resolution**, against a 16 ms budget. The preview-ROI
optimization the architecture assumes we would need is not needed yet.

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
24 MP is 194 MB. Adding tone and color as separate pointwise nodes pushed
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
linearize, all three RCD passes and the color matrix are served from cache.

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
   Do not "optimize" that back into a single module.
2. **The camera matrix must be row-normalized.** Without it, white balance and
   the color matrix fight: the data is already neutral after WB, and an
   unnormalized matrix re-tints it (we had a magenta cast). dcraw normalizes
   rgb_cam for the same reason.
3. **One geometry, one function.** The renderer, the crop overlay and hit
   testing each computed the photo's on-screen rectangle for themselves, and
   drifted apart — handles landed on a rectangle the pixels were not drawn in.
   `app/CanvasLayout.swift` is now the only copy, and the *engine is given*
   the preview canvas rather than deriving a second one. Same class of bug in
   the shader: the straighten pivot was derived from cropOrigin/cropSize,
   which describe the canvas rather than the user's rectangle, so the preview
   turned about the frame center and the committed render about the crop
   center. Pass the pivot, do not derive it.
4. **The crop must stay inside the turned frame.** Nothing enforced it, so a
   straightened export had transparent wedges in its corners — the crop is
   what gets sampled, and it reached past the picture. `constrainedCrop`
   shrinks and recenters it, which is what Lightroom does. A fixed preview
   canvas could not hold a steep angle either: a 3:2 frame at 45 degrees
   reaches 1.77x its short side, so the old constant 1.42 clipped corners past
   about 17 degrees. The canvas is now computed per angle and aspect, and
   sampled into a frame-sized texture so its cost stays flat.
5. **A `Path` view takes the size it is offered.** An unsized one inside a
   `.position()` grows to the whole overlay, and `.position` then centers
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
2. ✅ **Color mixer** — eight hue bands with hue/saturation/luminance each,
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
