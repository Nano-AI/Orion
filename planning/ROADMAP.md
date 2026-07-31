# Orion — Roadmap

AGILE structure: **Milestones** (releasable increments) → **Epics** → **Stories** (one Opus session each, ideally).
Rule: every milestone ends with something you can actually shoot with. No milestone is pure infrastructure.

---

## M0 — Prove the budget (risk spike)

**This milestone is a benchmark, not a feature.** Everything after it depends on <16 ms being real. Find out now, not in month four.

**Definition of done:** a 24 MP Sony ARW decodes, demosaics, and renders to screen through a ≥5-node GPU pipeline, with an exposure slider that responds in **under 16 ms at preview resolution**, measured and logged.

| Story | Notes |
|---|---|
| S0.1 Skeleton: C++20 project, CMake, Metal device init | |
| S0.2 Decode one ARW with LibRaw → GPU texture | Bayer data, no processing |
| S0.3 Slang→MSL shader build step in CMake | Proves the portability plan works |
| S0.4 RCD demosaic as a compute kernel | Port the MIT reference |
| S0.5 Minimal DAG: node graph, topological sort, `rgba16f` buffers | The engine's spine |
| S0.6 Exposure + AgX display transform nodes | Simplest end-to-end pipeline |
| S0.7 Display the output texture on screen, zero copies | `MTKView` in `NSViewRepresentable`; POD facade at the C++↔Swift boundary |
| S0.8 **Benchmark harness + latency numbers** | Gate: pass or rethink the stack |

**Kill criteria:** if <16 ms is unreachable on your Mac after optimization, stop and revisit the GPU/UI architecture before building features on it.

---

## M1 — Usable editor

**Definition of done:** you edit a real shoot in Orion start to finish and export JPEGs you're happy with. No other tool involved.

- **Epic: Browse** — folder open, grid + filmstrip, async thumbnails, LRU cache
- **Epic: Cull** — star ratings, reject flags, color labels; filter and sort; SQLite index
- **Epic: Core develop** — exposure, contrast, highlights/shadows, whites/blacks, WB (temp/tint + eyedropper), vibrance/saturation
- **Epic: Geometry** — crop, rotate, straighten
- **Epic: Edit model** — op stack → XMP sidecar, undo/redo, history panel
- **Epic: Export** — full-res tiled render path, JPEG/TIFF, resize, color space
- ✅ **Epic: Interaction** — per-node caching, and degrade-then-refine during
  drags (2026-07-30). A second graph over a quarter-linear mosaic renders while
  a control moves; the full one renders when the hand stops. Clarity 57.2 → 5.1
  ms a tick, dehaze 115.4 → 7.1. ⚠ Only the canvas reads the preview: export,
  the histogram and the eyedropper all take the full graph
- **Epic: Look** — neutral-gray dark theme, panel layout

### Export panel — modeled on macOS Preview's export sheet

Basic export works (JPEG/PNG/TIFF, quality, longest-edge resize) but has no UI
beyond a save dialog. Preview's sheet is the right reference because it is the
one every Mac user already understands.

| Control | Behavior |
|---|---|
| **Format** | JPEG · PNG · TIFF · HEIF. Changing it swaps the options below and updates the extension in the filename. |
| **Quality** | JPEG and HEIF only. Slider with a live **estimated file size** beside it — the number is why the slider is legible. |
| **Resolution** | Preset menu (Full · 4096 · 2048 · 1024 px long edge · Custom) plus explicit width/height fields that respect the aspect ratio. Shows the resulting pixel dimensions. |
| **Color space** | sRGB · Display P3 · Adobe RGB. sRGB default, since it is what survives the web. |
| **Bit depth** | 8-bit · 16-bit, TIFF and PNG only. ⚠️ Needs the pipeline to end in `rgba16f` rather than `rgba8`. |
| **Metadata** | Keep all · Strip location · Strip everything. |
| **Output sharpening** | None · Screen · Print. Resampling softens; this is the standard correction. |

**Live estimate** matters more than it sounds: quality sliders are meaningless
without one, which is exactly why Preview shows it.

**Blocked on:** 16-bit export needs the final node's format changed and the
orientation node widened to match. Not hard, but it touches the pipeline tail.

### Extending format support

| Format | Notes |
|---|---|
| HEIF | Free via ImageIO — smaller than JPEG at equal quality, and native on Apple platforms |
| JPEG XL | Better still, but ImageIO support needs verifying before promising it |
| AVIF | Same caveat |
| DNG | *Export* as DNG is a different problem — writing a raw container, not an image. Later. |

---

## M2 — Depth

**Definition of done:** Orion handles a difficult image (harsh light, high ISO, bad lens) as well as Lightroom would.

- ✅ Tone curve (monotone cubic Hermite, per-channel LUT)
- ✅ HSL / color mixer, 8 hue bands, with targeted adjustment
- ✅ Sharpening (amount / radius / masking)
- ✅ Guided filter → local highlights and shadows *(He, Sun & Tang)*
- ✅ Profiled wavelet denoise + per-camera noise profile *(Starck et al.)*
- ✅ Lens corrections, with lensfun's database vendored and read directly
- ✅ Broader camera support via LibRaw — and unsupported sensors refused by name
- ✅ Before/after split — the mockup's Compare interaction
- ✅ Keyboard-first workflow and control accessibility
- ✅ Camera profile beyond the 3×3: BaselineExposure and HueSatMap *(DNG spec)*

---

## M3 — The "180°" milestone

**Definition of done:** one click makes a flat RAW look striking.

- ✅ Local Laplacian clarity *(Paris/Hasinoff/Kautz; Aubry et al.)* — Texture, the fine-level variant, is not built
- ✅ Single-image exposure fusion *(Hessel & Morel; Mertens et al.)*
- ✅ Dehaze (dark channel prior) *(He, Sun & Tang; guided-filter refinement)*
- ✅ **Auto-enhance** — percentile auto-levels driving the above *(Simplest Color Balance; CIPA DC-004)*
- Color grading wheels (ASC CDL), split toning, vignette
- ✅ Creative LUTs (.cube, tetrahedral) *(Adobe Cube LUT Specification 1.0; Sakamoto & Itooka 1981)*
- Segmentation-based highlight reconstruction

---

## M4 — Local edits & workflow

**Definition of done:** you can dodge, burn, and fix a specific area — then apply it across a shoot.

- ✅ Gradient masks (linear + radial) — parametric, applied to the parameter
- ✅ **Luminance** range mask — 2026-07-30, `research/masking.md` §4b. ⚠ The
  old note here said this was cheap given M1's bilateral grid; M1 never built
  one, and a range mask is pointwise so it would not have helped
- Colour range mask — needs a colour distance and so a colour space
- ✅ Mask combine operators (add/subtract/intersect) — 2026-07-29, decision #62
- ✅ Guided feathering of the mask group — 2026-07-29, `research/masking.md`
  §4 *(He, Sun & Tang)*. The mask's edge is pulled onto the photograph's,
  and left alone where there is none to find
- ✅ AI **subject and person** selection — 2026-07-30, Apple Vision, no bundled
  model and no new dependency. **Sky is not done and is not cheap**: Apple has
  no API for it (its matte is capture-time metadata only), so it needs a
  bundled BiRefNet or U²-Net — both MIT/Apache. ⚠ RMBG is the trap every
  tutorial reaches for and its licence forbids commercial use
- ✅ Spot removal — 2026-07-30, clone and heal. `research/spot-removal.md`;
  the zeroth-order term of Farbman et al.'+chr(39)+'s interpolant, with the bounded
  failure across a hard edge recorded in `UNSOURCED.md` §21 and stated in the
  panel
- Brush masks — **reinstated 2026-07-29**, see DECISIONS #54 (was cut from v1)
- ✅ Presets — 2026-07-30. A **patch**, not a state: only the groups it carries
  are applied, and the crop, the dust and the masks are never among them
- ✅ Copy/paste/sync — 2026-07-30. Sync edits sidecars **without opening the
  photographs**, at the level of the JSON keys, so a photo that has never been
  edited keeps its as-shot white balance. ⚠ "Across a selection" is across
  every photo in view; the filmstrip has no multi-selection yet
- ✅ Batch export — 2026-07-30. One engine reused across the list (466 ms a
  photo, peak RSS flat); each photo'+chr(39)+'s own sidecar restored before it is
  exported; nothing overwritten and no two sources colliding
- Snapshots/versions, perspective correction, film grain

---


## Per-layer adjustments — the decomposition, costed

Decision #75 settled *what* (pointwise adjustments per layer, pyramids stay
global) and #76 settled *which* (exposure, contrast, saturation, warmth, tint).
#77 removed the UI cost — the catalogue renders N layers with no new controls.
What follows is the engine work, measured rather than guessed, so it can start
clean rather than be discovered mid-change.

### ⚠ The constraint that shapes it: the graph is static

Nodes and their inputs are fixed at construction. Which component *ends* a layer
is a runtime property, so a layer's coverage cannot be a node chosen per render.

The way through: **`develop:linear` binds all four component outputs**, and a
per-layer parameter says which index carries that layer's coverage. Components
group into layers by a `startsLayer` flag — a layer is a run of consecutive
rows, which is how the row list already reads.

### The pieces, in order

| # | Piece | Cost |
|---|---|---|
| 1 | `startsLayer` on `MaskComponentEdit`; kernel starts its fold from zero rather than from `src` | one kernel branch |
| 2 | **Four refine chains, one per component slot** rather than one on the fold | **+21 nodes** (7 each), ~138 MiB more R16F |
| 3 | `develop:linear` takes four coverages, loops layers per pixel | 4 bindings; one pass, not four |
| 4 | `LinearAdjust` carries four local sets plus each layer's coverage index | ~30 more floats |
| 5 | Sidecar: per-layer local sets, legacy single set migrates to layer 1 | schema version |
| 6 | Panel: layer boundaries in the row list, `AdjustmentGroup` per layer | no new controls (#77) |

Measured against today: **127 nodes → 148**, 6427 MiB → about **6565 MiB** (+2%).
Both affordable. The refine chains disable at strength zero exactly as the
single one does, so an unrefined stack costs their textures and none of their
time.

### ⚠ What must not be done along the way

- **Raising the four-component cap.** Each live component is a full-resolution
  R16F pass; the cap is a memory number and layers do not change it. Four
  components split across four layers is the same 184 MiB as four in one group.
- **Per-layer clarity, dehaze or fusion.** #75. Not pointwise, so the rule that
  the coverage scales the parameter is not even defined for them.
- **Blend modes over rendered frames.** #75. Layer opacity is a scalar into α.

## Slider latency, end to end — reported, not yet measured

**Reported live 2026-07-31: "it starts to get slow when I adjust it."** One cause
was found the same afternoon and fixed — see below — but it does not close this,
because the instrument that found it cannot see most of the path.

### What is measured today, and what is not

`orion-bench` drives `DevelopPipeline` directly. It reports an exposure tick at
**3 nodes and 8.7–13.7 ms p95** at full resolution, and session `2026-07-31c`
measured **1.7 ms** with `interact on`, which is where a real drag runs. By that
instrument there is no problem.

⚠ **The instrument stops at the C++ boundary.** A tick in the app is a SwiftUI
gesture → `Engine.edit` → `EditHistory.record` → `InteractionLog.committed` →
`cAdjustments()` → the POD facade → `apply` → `render` → `MTKView`. The bench
runs the last two. **Nothing in this repository times the first five**, and two
of them do per-tick work that grows with the edit rather than staying flat:

| Per tick, on the main thread | Why it is a candidate |
|---|---|
| `EditHistory.record` copies the whole `DevelopState` | value type carrying the mask components and the spot list |
| `InteractionLog.committed` runs `diff(from:to:)` over every field and formats strings | allocation per tick, and the log only grows |
| `cAdjustments()` assigns ~80 fields into the C block | flat, but it is on the same thread as the frame |

⚠ **Candidates, not causes.** This project has twice recorded a performance
claim that turned out to be about the fixture rather than the product — the
120-dab stroke figure in `2026-07-30p`, and my own "12 ms is 75% of the budget"
in `2026-07-31b`, which was wrong because degrade-then-refine already paid it.
Do not optimise any row of that table before it has a number.

### The pieces, in order

| # | Piece | Cost |
|---|---|---|
| 1 | A scenario verb or bench mode that times **a tick as the app issues it**, from the gesture callback to the frame being presented. Without this the rest is guesswork | the story |
| 2 | Attribute the tick: history, log, facade, engine, present | — |
| 3 | Fix whatever piece 2 names, and only that | unknown by construction |
| 4 | A floor in the bench, so it cannot regress silently the way it just did | small |

### ✅ One cause found and fixed, 2026-07-31

The film-grain node was added **enabled**, so it ran at Amount 0 on every frame
of every drag, and `develop:display` was writing `RGBA16Float` to feed it. The
exposure slider went from **3 nodes and 10.6 ms p95 to 4 and 17.03 ms** — past
the 16 ms M0 gate. `DevelopPipeline::retargetOutputChain` now disables the node
and moves the Bayer dither back, and `testGrainWiring` pins that a tick costs
the same as it did before grain existed.

⚠ **That this shipped into a working tree at all is the argument for piece 1.**
The regression was in the engine, where the instrument *does* reach — and it was
still reported by a person using the app before the bench was next run.

## Incremental brush accumulation — costed, not started

Decision #80 made each re-evaluation of a stroke ~30× cheaper. It did **not**
make them fewer: painting appends dabs, and every appended dab still re-runs the
loop over every dab laid so far. Rejection means each of those passes is cheap;
the count of passes is still quadratic in the stroke.

⚠ **A frame-filling scribble defeats the boxes entirely** — every run's box spans
the frame, no reject fires, and the cost returns to the old one. This is the fix
for that case, and it is the only one.

### The shape

Keep a persistent accumulator per brush component. On an append, the kernel
starts from the stored coverage, composites only dabs `[firstDab, count)`, and
writes it back. `_pad3` in `MaskComponent` has a spare slot for `firstDab`, so
the struct does not change size.

| Piece | Cost |
|---|---|
| Persistent R32F accumulator per brush component | **~97 MB each at 24 Mpx**, lazily allocated |
| `firstDab` in the params, kernel starts from the accumulator | one branch, ~25 shader lines |
| Host predicate deciding when the prefix is unchanged | ~35 lines, and the whole risk |

**R32F, not R16F.** The accumulator has to round-trip the coverage exactly or the
result stops being bit-identical to a full evaluation, which is the invariant
#80's test asserts.

### ⚠ Why it is not started

The predicate is the danger. It must compare the **post-transform** texels of the
new stroke against the previous upload, prefix for prefix, and fall back to a
full re-evaluation on any mismatch — geometry change, nib change, undo, row
reorder. The tempting cheap version is "did the count grow?", and its failure
mode is the worst kind this repository knows: undo three dabs, paint three
different ones, and a stale accumulator keeps the old coverage and renders **a
completely plausible brushstroke**. Every screenshot passes. Every perceptual
check passes. Only a test that returns the count to a previously seen value with
a different prefix can see it.

That is a session with its own tests, not an addition to one that has already
landed.

## Film grain — costed, not started

`research/film-grain.md` is written and settles the method: a precomputed
correlated grain plate (AV1's architecture, Norkin & Birkbeck 2018) carrying
Newson, Delon & Galerne's (CGF 2017) `√(Y(1−Y))` variance law, applied by one
pointwise node after the display transform.

### ⚠ The constraint that shapes it, found while costing

**`develop:display` outputs `RGBA8Unorm`.** Grain has to be added to unquantised
values, so a grain node reading that output would be adding noise to values that
are already 8-bit — banding, and the dither ordering becomes meaningless.

So the quantisation boundary has to move: `develop:display` becomes
`RGBA16Float` always, and the **grain node becomes the one that quantises**,
carrying the Bayer dither block that currently ends `develop_display.slang`.
`setWideOutput` then toggles the grain and geometry nodes rather than display
and geometry.

That is not free: one more full-resolution `RGBA16Float` intermediate is
**+194 MB** (24.2 Mpx × 8 B), about 3% on 6878 MiB, plus ~12 MB on the preview
graph. 148 nodes → 149.

⚠ At Amount 0 the grain node must be a **bit-exact copy plus the dither it
inherited**, or every existing `identical` baseline silently rebases.

### The pieces, in order

| # | Piece | Cost |
|---|---|---|
| 1 | ✅ **Done 2026-07-31.** `grain.slang` — plate fetch, hand-rolled trilinear, `σ(Y)` weight, dither | 131 lines |
| 2 | ✅ **Done 2026-07-31.** `GrainPlate.h`: PCG32 + Box–Muller, band-limiting blur, CPU box-filtered chain. ⚠ Stacked **vertically into one 2048×4096 R32F** rather than real mip levels — the aux-texture API has none, and adding them would change the GPU layer for nothing, since the shader must filter by hand anyway. `levelOffset(l)` is the closed form both sides use | 182 lines, 33 MB, 14 checks |
| 3 | ✅ **Done 2026-07-31.** New node; `setWideOutput` retargeted. ⚠ **Not** `develop:display` → `RGBA16Float` unconditionally, which is what the costing above assumed and what shipped first: a node that runs at Amount 0 is a full-resolution pointwise pass on every frame of every drag, and it took the M0 gate from 10.63 to **17.03 ms**. `retargetOutputChain` disables the node and hands the dither back to the display node instead, so the +194 MB and the +1 node are paid **only while the slider is up** | +93 MB idle, +194 MB on |
| 4 | ✅ **Done 2026-07-31.** `GrainParams` + offset asserts; `gridStep` uniform so both graphs sample one field | small |
| 5 | `amount` / `size` through `Adjustments` → `orion.h` → `CApi.cpp` → Swift | ~10 files |
| 6 | Catalogue entry, two sliders, sidecar fields | ~4 files |
| 7 | `testGrainGpu` ✅ and `testGrainWiring` ✅ (the node's *wiring*, which the kernel test cannot see); bench probe on **mean absolute difference** ✅, floor 0.06 of the exposure reference. Wiring scenario in `repro/` still to do | 26 checks |

### ⚠ What must not be done along the way

- **A hardware sampler.** Filtering precision is unspecified across GPU
  families, so export and preview could differ by device. Hand-rolled trilinear.
- **`std::normal_distribution` or `generateMipmaps`.** Both are
  implementation-defined; the plate would differ between toolchains.
- **A hash of the pixel coordinate.** The preview graph is a different grid, so
  preview and export become different *realisations* rather than different
  resolutions of one — and the preview reads an order of magnitude grainier.
- **Per-channel grain.** That is sensor noise, not film.

## M5 — Advanced

- ML denoise (NAFNet-class via Core ML) as a **background pass**, not a live slider
- Edge-aware brush refinement beyond M4's guided-filter pass
- User-loadable DCP profiles
- X-Trans support (Markesteijn)
- Customizable tool panels / saved workspaces
- Windows port (engine already portable; UI is the work)

---

## Where things actually stand

**`planning/STATUS.md`.** There is no second copy here on purpose: this file
had one, it disagreed with STATUS on the milestone percentages, on whether
16-bit export existed and on which story was next, and two copies of "where we
are" guarantee that outcome.

This file says what the milestones *are*. STATUS says where they stand.

## Working agreement

- **One story per Opus session**, with `planning/` as the brief. Update `DECISIONS.md` whenever a choice is made mid-build.
- **Benchmark every milestone.** Latency regressions get fixed before new features land, not after.
- **Dogfood M1 onward.** If you avoid using Orion for a real shoot, that's the bug report.
- Open questions live at the bottom of `ARCHITECTURE.md` until resolved.
