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
- ✅ Color grading wheels (ASC CDL) — three `ColorWheel` controls, shadows/midtones/highlights. ⚠ **Split toning and a creative vignette are not built**; the wheels subsume most of what split toning was for
- ✅ Creative LUTs (.cube, tetrahedral) *(Adobe Cube LUT Specification 1.0; Sakamoto & Itooka 1981)*
- Segmentation-based highlight reconstruction

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
- Snapshots/versions, perspective correction. ✅ **Film grain shipped 2026-08-01** (#81, #82)

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
| ⚠ **Brush cost is linear in accumulated dabs, forever** | Full resolution it crosses 16 ms at **~500 dabs** — 1.4 strokes across the frame. Armed it crosses at ~12,300, then steps to a 27 ms plateau at ~13,400 that nobody has explained. Arming bought 14× and moved the wall; it did not remove it. The fix is ROADMAP's incremental accumulation |
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

## Incremental brush accumulation — costed, not started

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

**Session one — the predicate, alone, with no accumulator behind it.**

| Piece | Where | Note |
|---|---|---|
| Keep the previous upload's post-transform texels per component | `DevelopPipeline`, beside `brushDabs_` | 256 KB a component, already the size of the buffer `apply` builds and throws away every event |
| `int unchangedPrefix(component, const float* texels, int count)` | new, next to `buildDabBounds` in `ShaderParams.h` | returns how many leading texels are bit-identical to the stored ones, `0` if the geometry, the nib or the kind moved |
| Tests that a *plausible* stroke is caught | `orion-tests` | see the mutations below |

⚠ The predicate compares the **post-transform** texels — the floats actually
uploaded — not the displayed-coordinate dabs. A crop, a straighten or a quarter
turn moves every centre through `mask::toFrame`, and a predicate reading the
pre-transform list would call that prefix unchanged. `buildDabBounds` already
takes the uploaded texels for exactly this reason.

**Named mutations, because a predicate that always returns 0 is correct and
useless, and one that always returns `count` is fast and wrong:**

| Mutation | Must fail |
|---|---|
| `unchangedPrefix` returns `count` whenever `count` grew | *undo three dabs, paint three different ones* — same count, different prefix, stale coverage, plausible picture |
| `unchangedPrefix` ignores `brushErase` | paint then erase over the same spot: source-over and destination-out do not commute |
| `unchangedPrefix` compares pre-transform dabs | rotate the frame mid-stroke; every centre moves and the prefix must go to 0 |
| `unchangedPrefix` returns 0 always | a *speed* check, not a correctness one — the accumulator must be measurably used, or session two is a no-op that passes everything |

That last row is the one this repository keeps learning: the fast path has to be
asserted to have been *taken*. `dehaze-reaches-the-picture.txt` and the bench's
`dehaze drag` node-count invariant are the pattern — count named nodes, not
milliseconds.

**Session two — the accumulator, behind the predicate.**

| Piece | Where |
|---|---|
| Persistent R32F texture per brush component, lazily allocated | `DevelopPipeline` ctor / `addAuxTexture` |
| `firstDab` into `MaskComponent`'s spare `_pad3` slot | `ShaderParams.h`, no size change, static-assert the offset |
| Kind 3 starts from the accumulator when `firstDab > 0`, writes it back | `mask_component.slang`, ~25 lines, one branch |
| Fall back to a full evaluation when the predicate says the prefix moved | `DevelopPipeline::apply` |

⚠ **The bit-identity invariant is the acceptance test, and it already exists.**
A stroke rendered incrementally must equal the same stroke rendered whole, texel
for texel — that is what #80's test asserts and what R32F is for. Add the
incremental path to that comparison rather than writing a second one.

⚠ **Budget check before starting session two:** ~97 MB a component at 24 Mpx, on
top of 6878 MiB of intermediates, and the grain node's +194 MB has already
landed. Four brush components is ~388 MB. Lazily allocated on first brush dab,
released when a component stops being kind 3 — otherwise a group of four
gradients pays for four accumulators nothing reads.

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
