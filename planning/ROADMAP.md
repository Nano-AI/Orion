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

- ✅ **Epic: Browse** — folder open, grid + filmstrip, async thumbnails, LRU cache
- **Epic: Cull** — ✅ star ratings, reject flags, color labels, filter and sort. ⚠ **The SQLite index was never built** — `Library.scan` rescans the folder and re-reads every sidecar on every open. It is also the last unbuilt piece of M1 and a performance item
- ✅ **Epic: Core develop** — exposure, contrast, highlights/shadows, whites/blacks, WB (temp/tint + eyedropper), vibrance/saturation
- ✅ **Epic: Geometry** — crop, rotate, straighten
- ✅ **Epic: Edit model** — op stack → XMP sidecar, undo/redo, history panel
- **Epic: Export** — ✅ full-res render path, JPEG/PNG/TIFF, resize, color space. ⚠ Three of the panel's seven specified controls are missing: bit depth, metadata policy, output sharpening
- **Epic: Browse** — folder open, grid + filmstrip, async thumbnails, LRU cache
- ✅ **Epic: Cull** — star ratings, reject flags, color labels; filter and sort;
  **SQLite index + persistent thumbnail cache landed 2026-08-01**, decision #91.
  300 frames, page cache warm: **454–688 ms cold against 28–54 ms warm, 12.9–17.2×**,
  and an indexless open lands on the cold number, so the win is the index rather
  than the disk. `Orion --library-open <folder>` is the paired measurement and
  the wiring check
- **Epic: Core develop** — exposure, contrast, highlights/shadows, whites/blacks, WB (temp/tint + eyedropper), vibrance/saturation
- **Epic: Geometry** — crop, rotate, straighten
- **Epic: Edit model** — op stack → XMP sidecar, undo/redo, history panel
- **Epic: Export** — full-res tiled render path, JPEG/TIFF, resize, color space
- ✅ **Epic: Interaction** — per-node caching, and degrade-then-refine during
  drags (2026-07-30). A second graph over a quarter-linear mosaic renders while
  a control moves; the full one renders when the hand stops. Clarity 57.2 → 5.1
  ms a tick, dehaze 115.4 → 7.1. ⚠ Only the canvas reads the preview: export,
  the histogram and the eyedropper all take the full graph
- ✅ **Epic: Look** — neutral-gray dark theme, panel layout

### Export panel — modeled on macOS Preview's export sheet

Basic export works (JPEG/PNG/TIFF, quality, longest-edge resize) but has no UI
beyond a save dialog. Preview's sheet is the right reference because it is the
one every Mac user already understands.

| Control | Behavior |
|---|---|
| **Format** | JPEG · PNG · TIFF · HEIF. Changing it swaps the options below and updates the extension in the filename. |
| **Quality** | JPEG and HEIF only. Slider with a live **estimated file size** beside it — the number is why the slider is legible. |
| **Resolution** | Preset menu (Full · 4096 · 2048 · 1024 px long edge · Custom) plus explicit width/height fields that respect the aspect ratio. Shows the resulting pixel dimensions. |
| **Color space** | ✅ sRGB · Display P3 · Adobe RGB. sRGB default, since it is what survives the web. |
| **Bit depth** | ✅ 8-bit · 16-bit, TIFF and PNG only, greyed out for JPEG *(2026-08-01)* |
| **Metadata** | ✅ Keep all · Strip location · Strip everything |
| **Output sharpening** | ✅ None · Screen · Print *(Fraser & Schewe; the amounts are in `UNSOURCED.md` §2)* |

**Live estimate** matters more than it sounds: quality sliders are meaningless
without one, which is exactly why Preview shows it.

**All seven controls are built.** What is left on this panel is HEIF, which is a
format question rather than a control — see below.

Two things turned up while finishing it:

- **Every export was sixteen bits, always.** The writer had no other mode, so
  every PNG Orion wrote was about twice the size it needed to be and nothing
  said so. The “blocked on the pipeline tail” note this section carried was
  stale — the tail had been widened long ago and only the *wide* direction ever
  existed. The work was adding the narrow one.
- **An 8-bit export renders through the narrow graph on purpose**, because that
  is the path that dithers (`ops/dither_ops.slang`). Rounding a smooth 16-bit
  sky down to eight bits without one is exactly the banding the dither exists to
  prevent, so the depth the file will hold decides which graph renders it.
  DECISIONS #90.

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
- ✅ Color grading wheels (ASC CDL) — three `ColorWheel` controls, shadows/midtones/highlights
- ✅ **Creative vignette** — 2026-08-01, `research/vignette.md`, decision #103.
  cos⁴ natural falloff *(Reiss 1945; Kingslake 1992)*, an exposure change in
  scene-linear light, centred on the crop. ⚠ Not the lens vignette
  **correction**, which is a different control in a different node — asserted in
  both directions
- ❌ **Split toning — refused, decision #97.** It is the wheels with fewer
  controls, and Adobe retired the panel it copies in October 2020 in favour of
  exactly what Orion already ships. **Its one real remainder was Balance** — ✅
  **built 2026-08-01, decision #104.** A rigid shift of all three zone centres,
  ±1.25 EV at full travel, on the grading panel under the wheels it moves.
  Centred is bit-identical to the fixed −2.5 / 0 / +2.5 EV partition, and with
  the wheels centred it does not run the node at all
- ✅ Creative LUTs (.cube, tetrahedral) *(Adobe Cube LUT Specification 1.0; Sakamoto & Itooka 1981)*
- Highlight reconstruction beyond the window fit — **renamed from
  "segmentation-based"; solver, mask and node chain built and wired 2026-08-01,
  off by default.** Rouf, Lau & Heidrich §3.2 as pull-push. §3.3's detail
  transfer and §3.4's falloff remain. Costed below

---

## M4 — Local edits & workflow

**Definition of done:** you can dodge, burn, and fix a specific area — then apply it across a shoot.

- ✅ Gradient masks (linear + radial) — parametric, applied to the parameter
- ✅ **Luminance** range mask — 2026-07-30, `research/masking.md` §4b. ⚠ The
  old note here said this was cheap given M1's bilateral grid; M1 never built
  one, and a range mask is pointwise so it would not have helped
- ✅ Color range mask — shipped in 0.4.0-alpha.3. Oklab chromaticity distance, mask kind 6
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
- ✅ Brush masks — reinstated 2026-07-29 (DECISIONS #54, was cut from v1) and shipped: dabs walked at fixed spacing, erase polarity per dab, run-of-64 bounding-box rejection (#80)
- ✅ Presets — 2026-07-30. A **patch**, not a state: only the groups it carries
  are applied, and the crop, the dust and the masks are never among them
- ✅ Copy/paste/sync — 2026-07-30. Sync edits sidecars **without opening the
  photographs**, at the level of the JSON keys, so a photo that has never been
  edited keeps its as-shot white balance. ⚠ "Across a selection" is across
  every photo in view; the filmstrip has no multi-selection yet
- ✅ Batch export — 2026-07-30. One engine reused across the list (466 ms a
  photo, peak RSS flat); each photo'+chr(39)+'s own sidecar restored before it is
  exported; nothing overwritten and no two sources colliding
- ✅ **Snapshots / versions** — 2026-08-01, decision #99. This photograph's whole
  state under a name, in a sibling `PHOTO.orion-snapshots.json` rather than in
  the sidecar autosave rewrites every 900 ms. ⚠ The hard half was the mattes: a
  raster mask is a file the sweep deletes when the *sidecar* stops naming it, so
  a version could restore a mask covering nothing. Pinned rather than copied,
  because matte files are already immutable (#79), and what a pin cannot cover
  is reported on the row before it is pressed
- Perspective correction. ✅ **Film grain shipped 2026-08-01** (#81, #82)
- ✅ **Perspective correction** — 2026-08-01, decision #100.
  `research/perspective.md`; the 4-point DLT homography (Hartley & Zisserman,
  2nd ed., 2004, §4.1) folded into the geometry node's **existing** sampling
  pass, so the picture is still resampled once. Vertical, horizontal and
  aspect; auto-scale to fill, as the lens corrections do. Masks, brush dabs and
  spots go through the same matrix
- Snapshots/versions. ✅ **Film grain shipped 2026-08-01** (#81, #82)

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

## Interaction latency, end to end — instrument built, two gestures fixed

**Reported live 2026-07-31: "it starts to get slow when I adjust it."** Then
2026-08-01: **"forced updates per stroke."** Both were right, and the second
named the mechanism exactly.

⚠ **The root cause was not slowness anywhere. It was that the canvas never told
the engine a gesture was in progress.** `AdjustmentSlider` arms
degrade-then-refine through `AnalogTrack`; no gesture *on the picture* did. So a
slider tick rendered a quarter-linear preview and a brush stroke rendered the
**full graph at full resolution**, once per pointer event, sixty times a second,
against a photograph that gets more expensive with every dab already laid.

Measured with the new `paint` verb on `_PIC8220`:

| Stroke, in dabs | Unarmed (was) | Armed (now) |
|---|---|---|
| 41 | 7.6 ms/event | **0.7 ms** |
| 246 | 27.3 ms/event — 37 fps | **1.9 ms** |
| 784 | — | 1.8 ms |

and on placing a radial mask carrying a local exposure, `drag maskCentreX`:
**13.0 ms a tick → 1.3 ms**.

✅ Fixed 2026-08-01 in `MaskOverlay`: both the paint gesture and the placement
drag now arm. ⚠ **Four gestures still do not** — see the audit below.

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
| 1 | ✅ **Done 2026-08-01.** `scenario paint <x,y> <x,y> <n>` — a stroke as the *canvas* issues it, one push per pointer event, reporting ms per event. ⚠ Deliberately does **not** arm the preview graph itself: a verb that did would report the same number whether `MaskOverlay` still called `beginInteraction` or not | one verb |
| 2 | Attribute the tick: history, log, facade, engine, present. Still open — piece 1 times the whole thing, not its parts | — |
| 3 | Fix whatever piece 2 names, and only that | unknown by construction |
| 4 | A floor in the bench, so it cannot regress silently the way it just did | small |

⚠ **What piece 1 does not cover.** `Scenario` drives `Engine` and `CanvasLayout`,
never a SwiftUI view, so **nothing asserts that a gesture arms**. The two lines
that matter in `MaskOverlay` are reachable only by reading them, and the four
gestures below were found by grepping for `beginInteraction`, not by a red test.

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

## ⚠ ACTION ITEM — a full performance audit of the application

**Asked for directly, 2026-08-01.** Not a story about one slider: a pass over
everything a photographer touches, with a number against each.

### Why it is its own item rather than more of the section above

Two gestures were fixed on 2026-08-01 by adding one line each. Both were found by
`grep beginInteraction`, and **four more came back in the same grep**:

| Gesture | Arms the preview graph? |
|---|---|
| Every slider, via `AnalogTrack` | ✅ |
| `MaskOverlay` paint | ✅ 2026-08-01 |
| `MaskOverlay` placement drag | ✅ 2026-08-01 |
| `CropOverlay` | ❌ |
| `SpotOverlay` | ❌ |
| `CurveEditor` | ❌ |
| `ColorWheel` | ❌ |

⚠ **They were not fixed in the same breath, on purpose.** Each swaps the canvas
to a differently-sized texture mid-gesture while an overlay is drawn over it,
and this codebase has already shipped exactly that bug once — the compare split
sampling two textures through one set of UVs. The crop overlay is the riskiest
of the four, because its rectangle *is* the geometry being changed. Each needs
its own before/after and its own look at the screen. That is the audit, not a
sed.

### What the audit must cover

| Area | The question |
|---|---|
| ✅ **The four gestures above** | Done 2026-08-01. All six canvas gestures arm now, and `repro/gesture-preview-agrees.txt` pins that the settled picture is identical either way — the mutation that stops `endInteraction` settling fails all five controls. ⚠ The grading wheel's arming is still unmeasured: `Scenario`'s control table is scalar and the wheels write three-component tuples |
| ✅ **Autorelease pools** | Done 2026-08-01, decision #86. ~1.3 MB per photo open, invisible to `leaks` by construction. +8.92 MB → +0.58 MB over 15 pipeline builds |
| ✅ **Orphan matte files** | Done 2026-08-01, decision #87. 26 orphans, 512 KB, beside one sample frame |
| ✅ **Dehaze's drag cost — root-caused and fixed 2026-08-01, decision #92** | `DevelopPipeline.cpp:1325` re-pushed the whole dehaze parameter set on every tick, and `Pipeline::setParams` dirties the downstream subgraph **without comparing the bytes**. Only omega moves with the slider; the dark channel, the six rank passes and the candidate pooling are functions of the frame's size, the paper's constants and A — nine nodes, six of them full resolution over 24 MP, redone for a value none of them read. Latched behind `hazeShapeValid_`. **Paired, interleaved, two builds, two rounds: 147.3/146.4 → 102.7/100.6 ms and 127.1/120.6 → 87.0/87.7 ms (0.69–0.71×)**, with exposure and clarity unmoved in the same process, and all 33 bench control probes moving the picture by identical amounts. Pinned by the bench's `dehaze drag` invariant, which counts *named* nodes; restoring the old guard prints `DEHAZE REDOES THE DARK CHANNEL` and exits 1. ⚠ **Two claims here were wrong and both were about the fixture:** the "~50 ms outside node dispatch" compared a tick against the bench's dehaze section alone, when clarity's input *is* `nDehaze_` and a dehaze tick runs its 39 nodes too (55 measured, 73.6 + 65.5 ≈ 139); and `estimateAirlight` fires **once** in a 40-tick drag, exactly as its comment claims. The recorded 67.3 ms does not reproduce at `6fd4e59` either, so the doubling was a fixture artifact — the waste it pointed at was not. No bisect was needed |
| ⚠ **`reopen` grows 25–49 KB per cycle; plain `open` is flat** | 300 iterations of each, monotonic, and it resumes at the same slope after the allocator releases. A mask component roughly doubles the rate. The difference between the two paths is `Sidecar.read` → `restore` → `restoreMattes` → `sweep`. `InteractionLog` is capped at 2000 lines and ruled out. ~240 MB over a 5,000-photo cull |
| ~~⚠ **Brush cost is linear in accumulated dabs, forever**~~ | ✅ **Fixed 2026-08-01, #108.** It was linear because every appended dab re-laid the whole stroke. The marginal cost of a pointer event is now flat in what is already painted — `mask:0` 5.20 ms appending 49 dabs to 294 against 36.46 ms re-laying them. The **first** evaluation of a component is still linear, which is once after a reload or a geometry change rather than once an event |
| **The tick, attributed** | `EditHistory.record` copies the whole `DevelopState`; `InteractionLog.committed` diffs every field and formats strings; `setBrushStroke` re-flattens the entire stroke per pointer event. All three are per-tick and O(size of the edit). ⚠ Candidates — the armed stroke is still linear (0.4 ms at 46 dabs, 1.8 at 784), which is 200 fps at the dab cap and may simply not be worth touching |
| **Cold open** | decode 36 ms + full render 72–92 ms. What does the photographer see in between? |
| ✅ **Library** | Done 2026-08-01, decision #91. Was: every open re-opened every raw through LibRaw, re-extracted every embedded preview and re-read every XMP. **300 frames: 454–688 ms cold → 28–54 ms warm, 12.9–17.2×.** ⚠ The listing is still the filesystem's — `plan` is *handed* the directory contents and can only answer about files in it, so a photograph deleted outside Orion cannot be resurrected by a cache. What is left is named and costed under **Library index — what is not done** below |
| **Scroll, zoom, pan, filmstrip** | never measured at all |
| **Memory** | 6971 MiB of intermediates on a 24 MP frame. What happens on 8 GB, or on a GPU that is not an M4? |
| **Export** | 345 ms JPEG, 571 ms TIFF. Fine, but unwatched |
| **The measuring protocol itself** | ⚠ 2026-07-31 recorded the M0 gate swinging **8.97 → 44.53 ms on an identical binary** with GUI load. Any number this audit produces is worthless unpaired. Decide the protocol first |

### ⚠ The rule that governs it

`CLAUDE.md`: latency regressions block new features. Twice now a performance
claim in this repository turned out to be about the fixture rather than the
product, and once — the 120-dab stroke — it was off by fifteen times. **Measure,
then fix, then measure again.** Nothing in the table above gets touched on the
strength of looking wrong.

## Library index — what is not done, costed

The index and the thumbnail cache both shipped whole (decision #91). These are
the edges that were deliberately left, each with a number rather than a shrug.

| Piece | Why it was left | Cost |
|---|---|---|
| **Rows for a folder you never open again are never collected.** The prune runs inside `plan`, against the listing it was given, so it only ever cleans a folder you are looking at. Delete a shoot from disk and its rows sit in the database forever | The database is bounded in *bytes* by the thumbnail budget, and a metadata row is ~200 B — 5,000 dead frames is ~1 MB. It is untidy, not a leak with teeth, and a sweep that walks every indexed folder statting for existence is a background job with its own failure modes | ~half a session: one `SELECT DISTINCT dir`, one existence check per folder, run on launch |
| **The thumbnail budget is a constant with no way to see it or clear it.** 512 MB, no readout, no button | Nothing in the app yet has a preferences surface to put it on, and inventing one for a single number is worse than the number | ~half a session, and it wants the settings panel that does not exist |
| **Nothing watches a folder while it is open.** Rate a photograph in Lightroom with Orion showing the same folder and Orion keeps the rating it read | True before this change too; the index does not make it worse, because every stamp is re-taken on the next open. An `FSEventStream` on the open folder is the fix and it is a live-update story, not a cache story | ~1 session |
| **A `SQLITE_BUSY` from a second Orion process is untested.** The code excludes `BUSY`/`LOCKED` from the corrupt path deliberately — those mean somebody else holds the file, and discarding it would be deleting a live database — but the mutation that widens that list to every error code passes every test | Simulating cross-process lock contention needs a second process and a held transaction; the honest note is that this one rule is reasoned rather than pinned | ~half a session for a two-process fixture |
| **`Library.swift` is now 400 lines and holds both the view model and the loading policy** | Under the 1000-line rule with room, and splitting it now would move code that has just changed | — |

## Perspective — what is not done, costed

The geometry shipped whole (decision #100): the homography, the composition into
the existing sampling pass, the auto-scale, and the centre of every mask, brush
dab and spot going through the same matrix. One edge was deliberately left with
a number rather than a shrug — **and it was closed on 2026-08-01, decision
#102.** A mask's extent is now the ellipse the map's derivative makes of it,
read off a symmetric 2×2 in closed form (Golub & Van Loan §2.4.1, §8.5.2).

What that bought, and what it did not:

| Piece | State |
|---|---|
| A radial mask's **semi-axes and angle** under an anisotropic map | ✅ **exact under the derivative.** The whole first-order error is gone. Visible where it is the *only* error: an aspect squeeze is exactly linear and exactly area-preserving, so √\|det J\| was exactly 1 and a mask came out **round under a two-to-one squeeze** — 4 of 84 clear cells leaked at **0.1461** luma, and none do now |
| The map's **curvature** across a large mask | ❌ **still there, now costed — 2026-08-02, decision #136.** No derivative at a point can see it. ⚠ **It is larger than "about a fifth off" made it sound**: measured against the exact answer over a 600 × 600 grid, a 0.34 mask under vertical 1.00 differs by **1.0000 coverage at worst** — pixels the render covers completely and the interface draws clear — mean 0.039, over **5.8%** of the frame. It reads as a fifth through `maskcheck` because that counts *cells*, and most of the disagreement is inside cells the overlay already calls covered. The error is quadratic in mask size: at 0.10 it is 0.25% of the frame. An aspect squeeze measures **exactly 0.0000**, which is the harness checking itself against a map that has no curvature |
| A linear gradient's **ramp length** | ✅ **anisotropy removed 2026-08-02, decision #134** — `mask::lengthAlong` gives \|J·u\| along the ramp's own pre-image direction. ⚠ Worth **0.0018 luma** at worst under a keystone and **0.0003** under an aspect squeeze, over a 6 × 6 patch grid — too small for any cell check, so the call site ships knowingly unpinned. ⚠ **The row here previously said "nothing at all" under a squeeze, because `Placement::jac` is the identity there. Both halves were false** (#136): that Jacobian is diag(0.500050, 1.000100) and the frames do differ. The claim had reached five documents |
| A linear gradient's **level sets** | ❌ **wrong today, first order, and nothing runs it — found 2026-08-02, decision #136.** The kernel projects onto the segment between two endpoints, so its level sets are perpendicular to that segment in *frame* coordinates; the drawn mask's are perpendicular in *display* coordinates. t is a covector — it goes through **J⁻ᵀ** while the endpoints go through J, and the two agree only when J is conformal. Worst coverage difference **1.0000** under an aspect squeeze, 0.9548 under vertical 1.00. Red in the existing instrument: `maskcheck 20 -2.0` on the shipping build reports **3 of 27 clear cells leaked, worst 0.1300 luma**. No `repro/` section runs a *linear* mask under a squeeze, which is why it survived |

⚠ `repro/perspective-carries-the-mask.txt` sits at **0.34** now, and its
keystone half passes there before and after the fix — the section that goes red
when the ellipse is reverted is the **aspect** one. A scenario that cannot fail
is not a scenario.

### Both remaining terms, costed — 2026-08-02, decision #136

The curvature row and the level-set row have **the same first piece**, which is
why they are costed together rather than separately. Everything below is a
consequence of one observation: the host transforms a mask's *parameters* once,
and the exact answer is to transform the *point* — crop, straighten, quarter
turns and the homography all compose into a single 3 × 3.

| Piece | What it is | Cost |
|---|---|---|
| **1. `mask::displayMatrix`** | Fold crop, straighten, quarter turns and the perspective into one frame → display 3 × 3, in normalized coordinates. Pinned against `fromFrame` pointwise, which is the existing step-by-step path — they must agree or one of them is wrong. **Pure addition; no pixel moves**, so it is provable by byte comparison | ~half a session |
| **2. The linear gradient, exactly** | t(q) = ⟨n, (q,1)⟩ / (\|u\|··⟨M₃, (q,1)⟩), n = uₓ·M₁ + uᵧ·M₂ − ⟨z,u⟩·M₃. **Six floats, two dots, one divide** against the four floats and one dot it replaces — the exact form is *cheaper to justify* than the approximation. Verified to 2.2 × 10⁻⁶ on a 400 × 400 grid. ⚠ A red fixture already exists (`maskcheck 20 -2.0` under an aspect squeeze), so this one starts with a test that fails | ~half a session |
| **3. The radial mask, exactly** | Two options, and the choice is a measurement rather than a preference. **(a) The conic**: r²(q) = ⟨q̃, C q̃⟩/⟨M₃, q̃⟩² with C = Mᵀ·Cₑ·M — exact, six floats, but **only for roundness = 2**, since a superellipse's level sets are not conics. **(b) The point map**: carry q through M per pixel and evaluate the mask exactly as drawn — exact for *every* kind, every roundness, every feather, and it deletes `radiusToFrame`, `lengthAlong` and `Placement::scale` outright. Costs one 3 × 3 and one divide **per pixel per component** on 24 MP, which is the open question | ~1 session, and it is the one with a performance answer attached |

⚠ **(b) is the honest end state and (a) is the cheap one.** (b) makes the shader
agree with the overlay *by construction* rather than by two derivations kept in
step — which is the failure class this whole area exists to prevent — but it
moves per-pixel work into a kernel that already runs full-resolution over every
component. Nobody has measured that, so nobody should promise it.

⚠ **What none of this removes:** the mask is still *applied* in `develop:linear`,
which sees the uncorrected frame. Every piece here changes where the coverage is
computed, not where it lands.

## Incremental brush accumulation — ✅ shipped 2026-08-01, #102 and #108

Decision #80 made each re-evaluation of a stroke ~30× cheaper. It did **not**
make them fewer: painting appends dabs, and every appended dab still re-runs the
loop over every dab laid so far. Rejection means each of those passes is cheap;
the count of passes is still quadratic in the stroke.

⚠ **A frame-filling scribble defeats the boxes entirely** — every run's box spans
the frame, no reject fires, and the cost returns to the old one. This is the fix
for that case, and it is the only one.

### ✅ Confirmed by measurement 2026-08-01 — and the two rule-outs were both wrong

The paragraph above was the prediction. It is now the finding, and both standing
rule-outs against it have been withdrawn.

What the kernel pays is `Σ over blocks of (pixels in that block's box) × 64`.
Appending grows the block count and leaves the box sizes alone, so it is linear.
`orion-bench` grew the dab count by *subdividing a stroke of fixed extent*, which
grows the block count and shrinks every box in exact proportion — a constant, for
any block size, any nib, any frame. **The flat 29.00 → 21.96 ms that ruled out
the mask kernel was the fixture, not the kernel.** The bench now runs both
shapes; `research/brush-acceleration.md` carries the argument and the table.

| Stroke shape | `mask:0`, full graph | `mask:0`, preview graph |
|---|---|---|
| refined, 60 → 960 dabs (the old fixture) | 24.16 → 19.35 ms | 1.54 → 1.31 ms |
| appended, 49 → 294 dabs (what `paint` lays) | 2.65 → **34.88 ms** | 0.17 → **2.23 ms** |

- **Not resolution.** Both graphs have the same slope; the preview is 1/16 the
  pixels and 1/16 the milliseconds. The quarter-linear graph is not special.
- **Not the host.** At either stroke length `setBrushStroke` ×2 costs 0.001 ms
  and `apply` ×2 costs 0.057 ms — flat, and three orders below the render.
  `orion_engine_set_brush_stroke` uploading the whole dab list per event is
  measured and is not the cost.
- **Not anything else going dirty.** Four nodes run per event at both lengths —
  `mask:0`, `develop:linear`, `develop:display`, `geometry` — and only `mask:0`
  moves. The other three are 0.60 ms of preview render at any stroke length.

⚠ **And a second case defeats the boxes, not only the scribble.** 64 dabs is
longer than most strokes, so a block straddles a pen-up and its box spans the
gap between two strokes. Holding the dab count, the block count and the painted
area identical and only moving six strokes apart: **0.4 / 0.6 / 0.8 / 0.9 ms** an
event for gaps of 0 / 0.02 / 0.10 / 0.15 frame heights. Padding strokes out to a
block boundary would recover that constant factor; it does not touch the slope,
so it waits for this story rather than pre-empting it.

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

### The decomposition — two sessions, in this order

Written 2026-08-01 alongside the measurement above, so that the next session
starts from a plan rather than from the investigation again. **Session one ships
nothing user-visible and that is deliberate:** the predicate is the whole risk,
so it gets built and attacked before anything depends on being able to trust it.

**Session one — the predicate, alone, with no accumulator behind it. ✅ done
2026-08-01, decision #102.**

| Piece | Where | Note |
|---|---|---|
| ✅ Keep the previous upload's post-transform texels per component | `DevelopPipeline::brushPrev_`, beside `brushDabs_` | Only the live prefix is stored, so a short stroke is kilobytes and the 16,384-dab cap is the 256 KB the buffer `apply` throws away every event anyway. Invalidated on a reload and when a component stops being kind 3 — both are cases where session two's accumulator will not have survived |
| ✅ `int unchangedPrefix(const BrushPrefixState&, const BrushShape&, const float* texels, int count)` | next to `buildDabBounds` in `ShaderParams.h` | `memcmp` per dab, capped at the shorter of the two strokes; `0` if the nib, the flow, the hardness or the kind moved. The geometry needs no case of its own — it moves every texel |
| ✅ Tests that a *plausible* stroke is caught | `testBrushPrefixPredicate` and `testBrushPrefixWiring`, 13 checks each | the unit half on hand-built texels, the wiring half through the real `DevelopPipeline` where the texels come out of `mask::toFrame` |

⚠ **No pixel moved, and it is asserted rather than argued.** The wiring test
renders a 160-dab stroke built by appending — the predicate answering 80 — then
reloads, which throws the stored texels away, uploads the same stroke whole with
the predicate answering 0, and compares the two frames byte for byte.

⚠ **`BrushPrefixStat::evaluations` is why the fast-path check is not vacuous.**
`apply` skips a component whose edit did not change, so an answer left behind by
an earlier event reads exactly like a fast path that was taken. Making the
predicate run only when the count grew — a mutation nothing else in the file
notices — turns five checks red through that counter.

⚠ The predicate compares the **post-transform** texels — the floats actually
uploaded — not the displayed-coordinate dabs. A crop, a straighten or a quarter
turn moves every centre through `mask::toFrame`, and a predicate reading the
pre-transform list would call that prefix unchanged. `buildDabBounds` already
takes the uploaded texels for exactly this reason.

**Named mutations, because a predicate that always returns 0 is correct and
useless, and one that always returns `count` is fast and wrong:**

| Mutation | Must fail | ✅ measured 2026-08-01 |
|---|---|---|
| `unchangedPrefix` returns `count` whenever `count` grew | *undo three dabs, paint three different ones* — same count, different prefix, stale coverage, plausible picture | **7 red**, including the undo-and-repaint case in both halves |
| `unchangedPrefix` ignores `brushErase` | paint then erase over the same spot: source-over and destination-out do not commute | **2 red** — comparing 2 floats a dab instead of 4 |
| `unchangedPrefix` compares pre-transform dabs | rotate the frame mid-stroke; every centre moves and the prefix must go to 0 | **1 red** — and only one, which is the argument for the wiring test existing at all: the unit half cannot see this |
| `unchangedPrefix` returns 0 always | a *speed* check, not a correctness one — the accumulator must be measurably used, or session two is a no-op that passes everything | **9 red**, one of them "80 of the 160 dabs are the stroke already on the GPU" |
| *(added)* the shape guard dropped — nib, flow, hardness, kind stop counting | one radius covers the whole stroke, so a wider nib re-lays every dab already down | **4 red** |
| *(added)* the predicate is only *asked* when the count grew | the vacuity guard on the guard: a stale answer reads exactly like a fast path | **5 red**, four of them through `evaluations` |

That last row is the one this repository keeps learning: the fast path has to be
asserted to have been *taken*. `dehaze-reaches-the-picture.txt` and the bench's
`dehaze drag` node-count invariant are the pattern — count named nodes, not
milliseconds.

**Session two — the accumulator, behind the predicate. ✅ done 2026-08-01,
decision #108.**

| Piece | Where | ✅ |
|---|---|---|
| ✅ Persistent R32Float texture, **one**, lazily allocated | `auxBrushAccum_`, registered at 1×1 and grown by `ensureBrushAccum` | Not one per component — see the budget below, which is where the plan changed |
| ✅ `firstDab` and `accumUse` into `MaskComponent`'s spare `_pad3` slots | `ShaderParams.h`, no size change, offsets 108 and 112 static-asserted | |
| ✅ Kind 3 starts from the accumulator when `firstDab > 0`, writes it back | `mask_component.slang`, ~20 lines, one branch | The block walk starts at the block *holding* `firstDab` and clamps to it inside — rounding down to the boundary composites up to 63 dabs twice, and does |
| ✅ Fall back to a full evaluation when the prefix moved | `DevelopPipeline::apply`, plus `reconcileBrushAccum` immediately before the render | The second one is the piece this plan did not have. See below |

**Measured** — `mask:0` alone, appending 49 dabs against re-laying the same
stroke because one dab of its head moved. Same dab count, same host work, same
blocks and boxes, interleaved rep by rep in one process, two runs
(`orion-bench` block **3d**):

| Dabs already down | Append 49 | Head moved: re-lay all |
|---|---|---|
| 49 | 4.66 / 9.19 ms | 7.65 / 14.53 ms |
| 294 | 5.20 / 5.80 ms | **36.46 / 46.87 ms** |

Flat in what is already painted, against linear. The right column is also the
cost before the change, which is what says the fixture did not move.

### ⚠ The budget check changed the plan, which is what it was for

| | Full graph | Preview | Both | On 7186 MiB |
|---|---|---|---|---|
| **One accumulator, for the live component** | 92.47 MiB | 5.78 MiB | **98.25 MiB** | **+1.37%** |
| One per component, as costed above | 370 MiB | 23 MiB | 393 MiB | +5.5% |

Painting is a single-component gesture, so a per-component accumulator's only
purchase over a shared one is the **first event after the photographer moves to
a different row** — one out of a gesture's hundreds. Its price is 393 MiB paid
by every photograph with four brush rows whether or not any is being painted.
One texture, and the loser of the swap re-lays once.

Half-resolution accumulation was the other candidate and is **not available**:
the acceptance test is bit-identity with a full evaluation, and a
half-resolution accumulator is a different computation, so the comparison the
design rests on could not be made at all. R16Float fails for the same reason and
it is measured — switching the format turns the ten-event comparison red.

⚠ **The bench's 7186 MiB does not include any of this.**
`Pipeline::intermediateBytes()` sums node outputs only, so every aux texture is
outside it — the dab textures, the bounds, the mattes, both LUTs, the grain
plate, and now the accumulator. Pre-existing, stated so that "173 nodes,
7186 MiB, unchanged" is not read as "nothing was allocated".

### ⚠ Two things this plan did not know, both found in the building

1. **Session one's `brushPrev_` answers a different question.** It advanced
   when a stroke was *uploaded*. An accumulator is a texture and a texture
   changes when the graph is **rendered** — and `Engine::setAdjustments` applies
   to both graphs on every pointer event while only the preview renders, so
   during a gesture the full graph is handed a hundred strokes and renders once.
   The claim is now advanced from `Pipeline::lastRun()`, from what the graph
   reports having executed.
2. **A kernel that accumulates is not idempotent and nothing else here is.**
   Run it twice on one parameter block and the new dabs go down twice. `apply`
   cannot see that coming — white balance moves and the reference behind every
   mask component changes. `reconcileBrushAccum` runs immediately before the
   render, where nothing can intervene afterwards, and refuses the fast path to
   any node about to run on parameters this `apply` did not push.

Ten mutations, six red on the first pass and **three that passed everything and
were defects in the checks**; the table is in `research/brush-acceleration.md`.

## Film grain — ✅ shipped 2026-08-01

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
| 5 | ✅ **Done 2026-08-01.** `amount` / `size` through `Adjustments` → `orion.h` → `CApi.cpp` → `DevelopState` → `Engine` → `cAdjustments` | 8 files |
| 6 | ✅ **Done 2026-08-01.** Catalogue entry, two sliders, sidecar fields, presets, sync, the interaction log and the scenario's control table. ⚠ `repro/grain-survives-a-reopen.txt` exists because `Engine.state` builds `DevelopState` with the **memberwise initializer**, and when the two fields were added to the struct but not to that call Swift filled them with defaults and compiled silently — grain rendered on screen and reached the sidecar as 0, with 569 + 3449 checks and 31 scenarios all green | 6 files |
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

## Highlight reconstruction beyond the window fit — costed, piece 1 done

`research/highlight-reconstruction.md` settles the method: Rouf, Lau & Heidrich
(PROCAMS 2012), gradient-domain restoration — Laplace-interpolate a smooth color
over the clipped region from its own boundary, then transfer detail from the
unclipped channels by a Poisson solve.

**Renamed.** The item read "segmentation-based highlight reconstruction" for
three milestones and that name is why it looked un-buildable. See below.

### ⚠ The constraint that shapes it, found while costing

**A Dirichlet solve is already region-scoped, so there is no segmentation pass.**
Nothing in `∇²ρ = 0 over Ω^∪ with ρ|∂Ω^∪ = f|∂Ω^∪` crosses a pixel outside
`Ω^∪`, so each connected blown region is solved on its own, from its own
boundary, without being labelled. Labelling would give the same answer more
slowly.

That is the whole reason this was stuck. Connected components is the one shape
that does not fit here: union-find is a CPU pass over 24 Mpx behind a readback
stall, and the GPU alternatives are iterative label propagation whose **pass
count depends on the picture**, against a static graph.

### What the shipping node does not cover, measured

| Region | Today |
|---|---|
| Some channels clipped, within 12 px of valid data | Recovered by `highlights.slang` |
| Some channels clipped, beyond 12 px | Declined — `n < kMinSamples` |
| **Every channel clipped, at any distance** | **Untouched.** Under #29 the `count == 3` branch is a literal identity |

Measured on a 140 px blown disc: `highlightRecover` returns the core at
R/B = 1.000, its input unchanged. A blown lamp or window on a 6024×4024 frame is
hundreds of pixels across, so this is the common case, not the corner.

### The pieces, in order

| # | Piece | Cost |
|---|---|---|
| 1 | ✅ **Done 2026-08-01.** `hl_pull.slang` + `hl_push.slang` — the Dirichlet fill, as Gortler et al.'s (SIGGRAPH 1996 §3.5.1) pull-push. `pipe/HighlightFill.h` carries the host twin **and** a Gauss-Seidel reference run to convergence, so the approximation error is printed every run rather than assumed: **6.1% of rim span**. Not wired to the graph | 87 + 82 lines, 9 checks |
| 2 | ✅ **Done 2026-08-01.** `hl_mask.slang` — `Ω^∩` as the hole, the picture as the Dirichlet data, box-restricted onto the solve grid in the same pass. Reads the same `whiteClipFor(m)` the linearize node used. ⚠ **`Ω^∩`, not `Ω^∪`**: the union's partial case already has a node, and replacing Masood et al.'s per-pixel cross-channel fit with a smooth interpolant would be strictly worse. Known also requires the shoulder (`kShoulder`, cited to the same place), which is what stops the night sky being read as evidence about the lamp | 101 lines |
| 3 | ✅ **Done 2026-08-01.** The node chain: mask, 11 pulls, 11 pushes, `hl_apply.slang`. ⚠ **Costed wrong, and the number was the decision** — see below. Disables to nothing at `highlightRecovery` 0, asserted by name in `apps/bench` | **+24 nodes, +215 MiB** |
| 4 | ✅ **Done 2026-08-01, decision #109.** §3.3's cross-channel detail transfer — as its **model** and not its solve, in two shader edits. ⚠ **The estimate beside this row was wrong and is kept below with the reason.** Built: `hl_mask.slang`'s hole becomes the part of `Ω^∪` the window fit did not recover (read off the node as `rec > raw`, not off a threshold), and `hl_apply.slang` writes `f*_k = (ρ_k/ρ_j)·f_j` where some channel never clipped. It is a **correction to piece 3** before it is a feature: the ring supplying `ρ` for every blown core was itself still clipped over 58%/69% of its length | **+0 nodes, +0 MiB** |
| 5 | §3.4's log-space gradient fill-in for `Ω^∩`, which is what gives a blown core its falloff back. ⚠ **This is now the visible gap**: piece 3 leaves a plateau. No Mach band at the rim — `ρ|∂Ω = f|∂Ω` makes the join continuous by construction — but a blown lamp comes back with its rim's color and none of its shape | 2 more solves |
| 6 | Its own control, if it wants one. ⚠ **Piece 3 did not add one**: the fill runs on `highlightRecovery`, which is already plumbed end to end, and one control for both halves of one coverage is the honest default — a photograph that wants its highlights left alone wants both left alone. Splitting them is a slider, eight files and an argument that the two are separable, and nothing so far says they are | 0 files, until it is wanted |

### ⚠ What piece 3 actually cost, and why the estimate was 16× out

The estimate above was **+25 nodes and ~516 MB**, at full resolution. Measured:

| | Estimate | Built |
|---|---|---|
| Nodes | +25 | **+24** (149 → 173) |
| Memory | ~516 MB, pyramid only | **+215 MiB total** — pyramid **30 MiB**, apply node **185 MiB** |

Two things the estimate got wrong, in opposite directions.

**The pyramid runs at a quarter resolution and the estimate never asked.** `ρ` is
harmonic — no detail to lose — so the solve was swept against the Gauss-Seidel
reference before a node was written: 6.1% of rim span at full, **6.9% at 1/4**,
12.6% at 1/16. Decision #102. Sixteen times less memory for 0.8 points on top of
an approximation already worth 6.1.

**Subsampling does not save nodes**, which is the half the estimate had right for
the wrong reason. The level count is logarithmic in the frame, so 1/4 removes two
levels and nothing else. If a chain of this shape is ever the problem, the fix is
fewer levels, not a smaller grid.

**And the apply pass is the real cost.** One full-resolution `RGBA16Float` node,
185 MiB, six times the whole pyramid, and no factor subsamples it away. Decision
#96 measured the same 194 MB for the creative vignette and fused it into the
grade rather than pay it; there is nothing to fuse into here, because the fill
has to land after `highlights` and before the denoise.

### ⚠ What piece 4 cost, and why "reuse the pyramid" was wrong

The row above read **+23 nodes and ~30 MiB, reusing piece 3's pyramid**, and that
is what made piece 4 look like the cheaper of the two remaining pieces. Measured:
**+0 and +0.** Neither half of the estimate survived being checked against the
shipped code.

| | Estimate | Built |
|---|---|---|
| Nodes | +23 | **0** (173 → 173) |
| Memory | ~30 MiB | **0** (7186 → 7186 MiB) |

**"Reuse" was the wrong word, not the wrong number.** `hl_mask.slang` wrote `Ω^∩`
as the hole, so every pixel outside it and above the shoulder was *known* with
`rgb = f` — which makes **`ρ ≡ f` over `Ω^∪ \ Ω^∩`, §3.3's whole domain**, and
`f*_k = (ρ_k/ρ_j)·f_j` the identity. `hl_apply.slang`'s own comment already said
so. There was nothing to reuse; §3.3 wanted a `ρ` over a *different hole*.

**And pull-push cannot solve §3.3 in any case.** §3.2 is Laplace, which a
pull-push interpolant approximates. §3.3 is Poisson with a source, and pull-push
has no residual and no relaxation to put one in. What ships is §3.3's model with
the integration replaced by a clamp — `research/UNSOURCED.md` §28.

**The census is what changed the answer.** `apps/bench` block 3e, on real frames:
the set §3.3 newly serves is 0.023%–0.068% of a frame, which does not buy 23
nodes. But the ring supplying `ρ` for **every** blown core is 11,901 / 20,563 px
and 58% / 69% of it comes back untouched by the window fit, so piece 3 was
solving every core from a rim that was itself clipped. That is a correction, it
needed none of the nodes, and no estimate made from the outside would have found
it.

**Revised total: one more session, for piece 5**, which is now the only visible
gap — a blown lamp comes back with the right color and no falloff.

### ⚠ What must not be done along the way

- **A connected-component labelling pass.** It is not needed (above), and its
  pass count is content-dependent, which a static graph cannot express.
- **Zhang & Brainard's global prior.** A channel-correlation prior fitted over a
  night frame is fitted on the dark warm background — the exact shape of the
  purple halo `kShoulder` exists to prevent. Rouf et al.'s Figures 4 and 7 show
  it failing on a neon sign for the same reason.
- **Copying darktable or RawTherapee.** Both are GPL and both have this feature.
  The published descriptions are fair game and the paper is the source used.
- **Wiring a piece without its measurement.** The solver approximates a
  multigrid solve; the number that says how well is a test, not a memory.
- **Quietly reopening decision #29.** ✅ Argued 2026-08-01 as **decision #103**,
  on the day the node landed. #29's magenta was the white-balance gains — the
  same magenta on every blown pixel of every frame, evidence of nothing — while
  `ρ` is the harmonic interpolant of the region's own rim and by the maximum
  principle cannot leave that rim's range. A neutral rim still gives a neutral
  core, so #29's outcome is reached by evidence rather than by decree, and the
  clip itself is untouched.

## M5 — Advanced

- ML denoise (NAFNet-class via Core ML) as an **on-demand pass**, not a graph
  node and not a live slider — **researched and costed 2026-08-01, not built.**
  `research/denoise-learned.md`, decision #111, the piece table below
- Edge-aware brush refinement beyond M4's guided-filter pass
- User-loadable DCP profiles
- X-Trans support (Markesteijn)
- Customizable tool panels / saved workspaces
- Windows port (engine already portable; UI is the work)

---

## Core ML denoise — costed, nothing built

`research/denoise-learned.md` settles what this line can and cannot be. **It was
research only, by design.** No node was written, no model was converted, no
build was run and no gate was claimed.

**Renamed from "background pass" to "on-demand pass"**, because "background"
suggests it runs on its own and it must not: it runs when the photographer asks,
and its result is cached against the frame.

### ⚠ The three constraints that shape it, found while costing

**1. Orion's insertion point is a third domain, and no published checkpoint is
trained for it.** The premise this line was written under — that Orion's noise
handling is pre-demosaic — is half wrong, and the wrong half decides everything.
The *fit* (`raw/NoiseProfile.cpp`, `estimateNoise(const BayerImage&)`) runs on
the mosaic. The *filter* (`denoise:blur 0..3`, `denoise:shrink 3..0`) runs
**after RCD and before `camera->working`**, in linear camera RGB, because
`var = a·x + b` only holds there and the matrix would mix the variances with the
channels.

Published denoisers are trained either on sRGB (gamma-encoded, tone-mapped,
8-bit — this is what SIDD's leaderboard measures) or on the Bayer mosaic (Brooks
et al., CVPR 2019). Orion's point is neither. **An sRGB checkpoint applied there
would look plausible and be wrong for a reason invisible to inspection**, which
is the purple cast's exact shape.

**2. It cannot be a graph node, and the arithmetic is one line.** At 24 Mpx one
fp16 32-channel activation is **1,480 MiB — precisely what the whole existing
8-node denoise chain costs**, since 32 channels × 2 bytes and 8 nodes × 8 bytes
are both 64 B/px. A four-level U-net at width 32 is ~2,868 MiB for one
activation per level and a guessed 4–8 GiB in practice, on top of the graph's
7,186 MiB. **It tiles or it does not run**, and a tiled network is not one node.

**3. The blocker is not the facade.** Core ML is **Objective-C** — verified in
the SDK headers, `MLModel.h`, `MLFeatureValue.h:60`, `MLMultiArray.h:178` — so
it is callable from the engine's existing `.mm` files without Swift and without
crossing the POD facade at all. ⚠ It fails by `NSError`, so the wrapper returns
a status code; an exception crossing the facade terminates the process.

### The latency, computed rather than measured

NAFNet's own Table 6 gives 65 GMAC at 256×256 = 0.992 MMAC/px → **48.1 TFLOP**
for a 24 Mpx frame, or **65.5 TFLOP** tiled at 512 with a 32 px halo (126 tiles,
1.36× the pixels). ⚠ No sustained fp16 throughput was measured on this machine,
so it stops there: at 5 TFLOP/s that is 13.1 s, at 40 TFLOP/s it is 1.6 s.

**The conclusion does not depend on which.** M0's target is a sub-16 ms drag and
the optimistic row is 1.6 s — **100× the whole frame budget.**

### The pieces, in order

| # | Piece | Cost |
|---|---|---|
| 1 | **Measure the gap.** A paired fixture: a clean synthetic frame, `estimateNoise`'s own Poisson–Gaussian model applied *forward* to make its noisy twin, both through a real `DevelopPipeline`. PSNR/SSIM **and** a detail metric, denoiser off / luma 2.0 / luma 4.0. ⚠ **This session decides whether the rest happens.** Orion has no clean reference and no full-reference metric today, so the size of the gap is currently unknown and no dB figure for the shipped denoiser exists | **+0 nodes, +0 MiB.** ~1 session |
| 2 | **Measure the round trip with no model in it.** ObjC++ in the engine: `MTLTexture` → IOSurface-backed `CVPixelBuffer` → a trivial **identity** `.mlpackage` → back. Time at 24 Mpx, assert bit-identical. ⚠ **This is where "zero-copy" is proved or disproved** — `MLMultiArray initWithPixelBuffer:` exists (macOS 12) but whether Core ML honours it without an internal copy is the largest unknown in the write-up. If it copies, the answer switches to `MPSGraph`, whose `MPSGraphTensorData initWithMTLBuffer:` takes the buffer directly — at the cost of hand-building the network in ObjC, which is a thousand-line file and against the maintainability constraint | **+0 nodes, +0 MiB.** ~1 session |
| 3 | ⚠ **DECISION POINT — the domain. A written argument, not code.** Move the model upstream to the mosaic (matches the literature, reopens decision #29 and the demosaic order), downstream past the display transform (cheap, **violates decision #6**), or train for Orion's own domain (correct, weeks). **"None of the three, stop here" is a real outcome** and must be allowed to win | **0 files.** ~half a session |
| 4 | **The tiler, with no network in it.** Split → identity → reassemble at 512 with a parameterised halo. ⚠ **Assert the reassembly is bit-identical** — a tile seam is invisible to inspection and obvious to a five-line assertion, which is this repo's own lesson twice over. ⚠ **Set the halo from the chosen model's receptive field, measured.** 32 px is a guess and is almost certainly too small for a four-level U-net | **+0 nodes** in the develop graph. ~100 MiB working set *(guess)*. ~1 session |
| 5 | **The model, whichever piece 3 chose.** Convert or train, bundle the `.mlpackage`, run it through piece 4's tiler, composite the result in as an **uploaded texture** — the structural position `mask:0`'s raster already occupies (decision #79) | **+1 node, +185 MiB**, plus the tiler. ⚠ **Session count unknown by construction:** downstream ~1, upstream 2–3, **train-our-own weeks** |
| 6 | **The control and the cache rule.** A button, not a slider. Cached against the frame and the model version; **no** parameter change re-runs it. ⚠ `unchangedPrefix` (decision #102) is about parameters not changing, not about a pass being too expensive to run — this wants its own concept | ~1 session, ~10 files |

**Total: 4.5 sessions to a decision point that may say stop, plus an unbounded
tail.** ⚠ **Do not start at piece 5.** Pieces 1–4 are worth doing whether or not
a model ever ships — piece 1 gives Orion the paired fixture and the
full-reference metric it does not have, and piece 4's tiler is what a
full-resolution export path wants anyway.

### ⚠ Which of these numbers are guesses

`research/highlight-reconstruction.md`'s estimate was **16× out** because nobody
measured before costing. Marked here so the same mistake is at least visible:

| Number | Status |
|---|---|
| 185 MiB a full-res `RGBA16Float` node; 1,480 MiB the denoise chain; 173 nodes / 7,186 MiB | **Measured / read out of the source.** Solid |
| 65 GMAC at 256×256; 40.30 dB SIDD; DND non-commercial; NAFNet and Restormer MIT; DnCNN and FFDNet **unlicensed** | **Verified against the papers and the GitHub licence API** |
| 2,868 MiB for one activation per level; 48.1 / 65.5 TFLOP; 126 tiles | **Arithmetic** from the verified figures. Solid given the architecture assumption |
| 4–8 GiB in practice; ~100 MiB tile working set; the 32 px halo; every session count in the table | ⚠ **Guesses.** Pieces 1, 2 and 4 exist to replace them |
| Orion's own denoiser's dB gap; sustained fp16 throughput on this machine; whether Core ML's pixel-buffer path is genuinely zero-copy | ⚠ **Unknown.** No measurement exists |

### ⚠ What must not be done along the way

- **Shipping an sRGB-domain checkpoint at the current insertion point.** The
  domain error above. Plausible, wrong, invisible.
- **Bundling weights whose training data's licence has not been read.** Decision
  #78 settled this for segmentation and it transfers unchanged: weights inherit
  their data's terms however permissive the architecture's code is. DND is
  explicitly non-commercial; SIDD's MIT claim traces to one project page —
  corroborated by a second search, but that is one source seen twice — and is
  unverified in the distributed archive.
- **Using DnCNN or FFDNet's original repositories.** Neither has a licence file
  at all, which is no grant rather than a permissive one. `cszn/KAIR` is MIT and
  re-implements both if either is ever wanted.
- **Hand-building the network in `MPSGraph` calls** unless piece 2 forces it.
- **Making it a slider, or making it a node.** The two arithmetic findings above.

---

## X-Trans — costed, nothing built

`research/demosaic-xtrans.md` settles what this line can and cannot be. **It was
research only, by design.** No node was written, no `throw` was lifted, no build
was run and no gate was claimed — and the tree could not have been built anyway,
since `third_party/slang` was destroyed the same day.

### ⚠ The premise the line was written under is half wrong

The roadmap says "X-Trans support (**Markesteijn**)", and everyone reads that as
"port Markesteijn to Slang". Two findings kill that reading and replace it.

**1. There is no published description of Markesteijn's algorithm.** Searched:
dcraw carries the code with an attribution and no derivation; darktable's
`xtrans.c` and RawTherapee's `xtransdemosaic.cc` are **GPL-3**; the manuals
describe which variant to pick, not how either works; Fujifilm's patents cover
the **array**, not the interpolation, and Markesteijn is not Fujifilm; the
`xtransdemosaicking.blogspot.com` write-up (darktable's Markesteijn+FDC) is an
*addition to* Markesteijn that calls it as a black box. **The only extant
description is source code, and the accessible copies are GPL.** Reading one and
re-typing it in Slang is copying, not implementing from a description, and
`CLAUDE.md` forbids it. That route is closed and stays closed.

**2. ⚠ The same code also ships under LGPL-2.1 / CDDL-1.0, inside LibRaw.**
Read out of this machine's installed headers rather than remembered:
`xtrans_interpolate(int)` at `libraw.h:451` in LibRaw 0.22.2, dual-licensed by
the COPYRIGHT file, credited to *"Frank Markesteijn's algorithm"* in its own
source header. The **GPL** interpolators — AMaZE, LMMSE, AFD — live in the
separate, abandoned `LibRaw-demosaic-pack-GPL2/GPL3` repositories, which
Homebrew does not build; `xtrans_interpolate` is in the LGPL/CDDL core because
it came through dcraw's non-RESTRICTED code. **Orion already links `libraw_r`
dynamically** (`engine/CMakeLists.txt:49-61`), so nothing about the licence
model changes. `dcraw_process()` is public (`libraw.h:274`) and `user_qual > 2`
selects Markesteijn 3-pass.

### ⚠ What actually makes it expensive, and it is decision #29

`DevelopPipeline.cpp:1269` states it in a comment written for Bayer: *"White
balance rewrites the linearize block, which sits at the head of the graph — so
moving temperature legitimately recomputes everything, including the demosaic.
That is inherent."* It is inherent because **#29 clips all three channels to a
common ceiling after white balance and before demosaic**, so RCD never
interpolates across an unclipped 2.2× red neighbour.

**So a demosaic that leaves the GPU takes the temperature slider with it.**
That, not the licence and not the memory, is this feature's price.

### The counter-intuitive part: the GPU graph gets *smaller*

Arithmetic from the measured 185 MiB full-resolution `RGBA16Float` node, at a
26 MP X-Trans frame. Removing `linearize` and the four `rcd:*` nodes and
uploading a demosaiced `RGBA16Float` source in their place:

| | Nodes | GPU MiB |
|---|---|---|
| Remove `linearize` + 4 `rcd:*` | **−5** | −594.1 |
| Remove the `R16Uint` mosaic source | — | −49.5 |
| Add the uploaded `RGBA16Float` source | 0 | +198.1 |
| **Net** | **−5** | **−445.6** |

**168 nodes on an X-Trans frame against 173 on a Bayer one** — a load-immune
number a bench probe can assert by name, the way `dehaze drag` does.

⚠ **And a constraint that has nothing to do with the demosaic:** every
full-resolution node scales with pixel count, so the existing 173-node graph at
7,186 MiB is **~7,700 MiB at 26 MP and ~11,800 MiB at 40 MP** (X-H2 / X-T5)
before any of this. That is a 40 MP problem, and it arrives with the first Fuji
file whatever the demosaic is.

### The pieces, in order — and the order is the decision

| # | Piece | Cost |
|---|---|---|
| 0 | **The frames, and they are free.** `raw.pixls.us` publishes community raw samples under **CC0** — its own upload wording is *"I hereby release it under the cc0 license into the public domain"* — and actively solicits Fujifilm RAF in **both** uncompressed and compressed forms. Five files: uncompressed and compressed RAF from one body, an X-Trans I (X-Pro1) and an X-Trans IV/V (X-H2, 40 MP), and ⚠ **one Bayer Fujifilm** (X-A or GFX) as the control — without it, `filters == 9` selection is untested and a branch that takes the X-Trans path on every Fuji file passes every X-Trans test | **0 code, 0 nodes.** ~half a session |
| 1 | **Decode only, and nothing renders.** Lift `RawImage.cpp:165`'s `throw`, carry `imgdata.idata.xtrans[6][6]` / `xtrans_abs[6][6]` and the 6×6 `cblack` through the image struct, and assert the pattern, dimensions and black levels against a real `.RAF`. ⚠ **The session ends with the frame decoded and the pipeline still refusing it** — a decode that half-feeds a Bayer graph is the failure mode this repo has recorded by name | **+0 nodes, +0 MiB.** ~1 session |
| 2 | ⚠ **DECISION POINT — measure Markesteijn's wall clock.** `dcraw_process` with `user_qual = 1` and `= 3` on a 26 MP and a 40 MP RAF, timed. **This is the number the feature turns on and nobody in this repository has it.** If 3-pass is seconds, X-Trans is an open-time cost and not a slider, and piece 3's answer is forced. **"Too slow, stop here" is a real outcome** | **0 nodes.** ~half a session |
| 3 | ⚠ **DECISION POINT — white balance. A written argument, not code.** Four options, all in `research/demosaic-xtrans.md` §4.2: (a) demosaic neutral and clip afterwards — **reopens #29 and loses**, and highlight pieces 2–4 all assume the clip happened; (b) temperature re-decodes on X-Trans, honestly and visibly; (c) a cheap GPU demosaic for the preview — **circular**, that is the thing which does not exist licence-clean; (d) re-apply gains as a ratio — **do not**, Markesteijn's direction decisions are not linear and the error would look right. ⚠ **"None of the four" must be allowed to win** | **0 files.** ~half a session |
| 4 | **The graph branches at its head.** Source becomes `RGBA16Float` on X-Trans; `linearize` and the four `rcd:*` nodes are skipped. ⚠ **Verify the −5 / −445.6 from the inside** rather than trusting the row above, and ⚠ **assert every Bayer number is bit-identical** — 173 nodes, 7,186 MiB, and the M0 gate's exposure path must not move by a texel. ⚠ Also verify what `dcraw_process` with `output_color = 0`, `no_auto_bright = 1`, `gamm = {1,1}` actually leaves in `imgdata.image`; "linear camera RGB" is an assumption here, not a reading | **−5 nodes, −446 MiB** *(arithmetic; piece 4 measures it)*. ~1–2 sessions |
| 5 | **The two CPU paths that fail silently.** `estimateNoise` samples `row[x]`, `row[x+2]`, `row[x+4]` under a comment that says *"all three are the same color in any 2x2 CFA"* — a stride of 2 crosses colours in a 6×6, so the second difference measures the scene's own R–G–B steps as noise, returns a plausible inflated `a` and `b`, sets `measured = true`, and the denoiser over-smooths. `decimate` builds the preview mosaic in 2×2 units under a comment warning that an off-cell stride "looks like a color-swapped nightmare". ⚠ **Both are tested against a synthetic field with a known answer**, because both are invisible on a real frame | **+0 nodes.** ~1 session |
| 6 | **The preview graph and the cache rule.** The 1/16 preview decimates the mosaic today, which piece 5 removes on X-Trans; it decimates the demosaiced RGB instead. Markesteijn's output is cached against the frame **and the white-balance state piece 3 chose** — structurally the same concept Core ML denoise piece 6 needs and neither has. ⚠ `unchangedPrefix` (#102) is about parameters not changing, not about a pass being too expensive to re-run | ~1 session |

**Total: ~5–6 sessions, through two decision points either of which may say
stop.** ⚠ **Do not start at piece 4.** Pieces 0, 1 and 5 are worth doing whether
or not X-Trans ever ships — piece 0 gives the repository its first non-Sony,
non-Bayer fixtures, and piece 5 fixes two CPU bugs that are latent on Bayer
today only because the assumption happens to hold.

⚠ **And do not start any of it before `DevelopPipeline.cpp` is split.** It is
2,549 lines against a stated ceiling of 1,000, it is the largest standing
violation of a hard constraint, and this feature branches the **head** of that
graph.

### ⚠ Which of these numbers are guesses

| Number | Status |
|---|---|
| `xtrans_interpolate` in LibRaw 0.22.2 under LGPL-2.1/CDDL-1.0; `LIBRAW_XTRANS == 9`; `dcraw_process` public; `user_qual > 2` → 3-pass; the CC0 wording at raw.pixls.us | ✅ **Read out of the installed headers, the COPYRIGHT file, LibRaw's source and the site itself** |
| Every breakage in piece 1/4/5's description | ✅ **Read out of Orion's own source**, with file and line |
| 185 MiB a full-res `RGBA16Float`; 173 nodes / 7,186 MiB | ✅ **Measured**, already in `STATUS.md` |
| −5 nodes / −445.6 MiB; ~7,700 and ~11,800 MiB at 26 and 40 MP | ⚠ **Arithmetic** from the row above, assuming proportionality. Piece 4 verifies |
| 26 MP and 40 MP sensor dimensions | ⚠ **Manufacturer spec.** There is no X-Trans file in this repository to read them off |
| **Markesteijn's wall clock at any resolution** | ⚠⚠ **UNKNOWN, and it decides the feature.** Piece 2 exists for it |
| Every session count in the table; the LSLCD node/memory table in the research file | ⚠ **Guesses** |
| Whether Markesteijn is actually better than the published alternative | ⚠ **Nobody has measured it.** Rafinazari & Dubois (ICIP 2014; uOttawa thesis 2017, opened and read) is the only peer-reviewed X-Trans method, and its own chapter opens by naming the literature gap. It reports 36.50 dB against Bayer LSLCD's 39.8 on simulated Kodak mosaics — ⚠ which are gamma-corrected and white-balanced, i.e. **not Orion's domain**, the same class of error decision #111 found one milestone earlier |

### ⚠ What must not be done along the way

- **Porting `xtrans.c` from darktable or RawTherapee.** GPL-3, and there is no
  published description to legitimise a reimplementation. This is the one thing
  this whole write-up exists to rule out.
- **Assuming LibRaw's demosaics are GPL.** The GPL ones are in the separate
  demosaic packs. The X-Trans one is not, and the difference is the feature.
- **Re-applying white balance as a ratio after the demosaic** (option d). The
  error would be invisible and unbounded.
- **Deleting `RawImage.cpp:165`'s `throw` before piece 4.** It is a *good* error
  message — it names the sensor, the reason and where the work is tracked — and
  every CPU path behind it fails silently rather than loudly.
- **Costing this from the node table above without running piece 2.**
  `research/highlight-reconstruction.md`'s estimate was 16× out for exactly that,
  and it was wrong in one word.
- **Building it at all, if piece 2 or 3 says stop.** Declining X-Trans with the
  reason written down is a first-class outcome and is cheaper than a
  half-threaded one.

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
