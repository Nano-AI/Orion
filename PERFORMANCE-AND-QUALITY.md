# Orion — Performance and Quality Check

Written 2026-07-28, at the end of an overnight session plus a working day, for a
reviewer who has not seen the repository before. It is meant to be checkable:
every claim below is either a number you can reproduce with a command in this
file, or a file and line you can read.

---

## 1. What to run first

```sh
cmake -S . -B build -G Ninja && cmake --build build

./build/apps/tests/orion-tests            # 208 checks — engine maths + real GPU renders
./build/orion-viewport-tests              # 2067 checks — canvas geometry, curve, sidecar
./build/apps/bench/orion-bench file.ARW   # latency gate + per-control effect checks
```

Both suites are green as of the last commit. The bench writes its outputs to the
working directory and `.gitignore` covers them.

---

## 2. Performance

Sony ILCE-7M3, 6024 × 4024 (24.2 MP, RGGB), Apple M4.

| Interaction | Nodes recomputed | Time |
|---|---|---|
| Exposure drag | 3 | 11.5 ms |
| Highlights / shadows | 10 | 23.6 ms |
| Color mixer | 5 | 19.2 ms |
| Curve drag | 1 of 8 | 8.2 ms median, 9.4 p95 |
| Temperature / tint / sharpen | 11 | **~43–50 ms** |

**M0 gate: 13.90 ms p95 against a 16 ms budget**, with the grading node in
the graph (12.70 without it). Full resolution, no preview
proxy — which is why zooming to 100% shows real pixels rather than an upscale.

Two honest caveats:

- **Temperature is over budget and structurally so.** White balance rewrites the
  head of the graph, so the demosaic reruns. The fix is degrade-then-refine or
  the preview-ROI path in `ARCHITECTURE.md`. Neither is built and neither is
  small.
- **16-bit output costs 2.6 ms** of the budget (9.04 → 11.67 ms when it landed).
  It is a capability nobody sees on screen. If the budget ever gets tight, the
  fix is a second display path used only for export.

Export is off the interaction path: 140–350 ms depending on format, color
conversion and metadata. The EXIF read alone is ~90 ms because ImageIO opens the
RAW again; caching the property dictionary at open would give it straight back.

---

## 3. How correctness is defended

The rule that shapes this repository is in `CLAUDE.md`: **every non-trivial
filter cites a published reference in `research/`.** It exists because
plausible-looking constants once shipped a purple cast on every image.

The second rule is *measure, don't look*. Three of the worst bugs this week were
invisible to inspection and obvious to a five-line assertion:

| Bug | Why looking failed | What caught it |
|---|---|---|
| Blown highlights rendered magenta | The image looked plausible; the lights just *were* pink | `--measure` on the blown sign: saturation 0.242 |
| Denoise did nothing | Output looked identical, because it was | Channel standard deviations, unchanged |
| Guided filter cost 90 ms | A comment claimed the blur was O(1) in radius; the loop was not | The bench |
| Resized exports dropped to 8-bit | A PNG of a ramp compresses to nothing either way | Reading the written file back with ImageIO |

### Bugs found and fixed in this session, with the invariant now pinning each

| Bug | Root cause | Test |
|---|---|---|
| **Every clipped light rendered magenta** | `linearize` scaled each channel by its white-balance gain and never clipped, so a blown pixel left the node wearing the gains as a color: about (2.2, 1.0, 1.6). Every later stage preserves ratios | `testLinearizeClipsToWhite` — reads the mosaic *per CFA channel*, because the cast is a difference between channels and an average hides it |
| Compare came apart on zoom | The split runs across the drawn quad; the panel drew the divider against the *fit* rectangle | `testDrawnRectFollowsTheZoom`, 80 checks |
| Top/bottom compare was upside down | The shader recovered its position by unpicking `uv`, which is flipped in y | Quad coordinate is a varying now |
| Renaming any field would have destroyed **every** sidecar | Swift's synthesized decoder throws on a missing key; `Engine.restore` swallows it with `try?` | `testSidecarSurvivesAMissingField`, 13 checks |
| Resized exports lost 16-bit | The resize context was 8 bits | Reads the file back with ImageIO |
| The bench measured a write the product never performs | It passed a bare options struct | It builds the app's options now |
| Rating a photo erased its edits | Two writers each rebuilt the sidecar from their own half | `Sidecar.merge` |

---

## 4. Where I would look first if I were reviewing this

Ranked by how much damage a defect there would do.

1. **`engine/shaders/linearize.slang`** — the white clip. It is three lines and
   it decides whether every highlight in the application is the right color. The
   trade it makes (the white point moves with white balance; reconstruction
   headroom is spent) is deliberate and stated, but it is a trade.
2. **`engine/src/pipe/DevelopPipeline.cpp`** — the graph, node ordering, and the
   incremental-invalidation logic that makes the DAG worth having. If a node is
   invalidated too rarely, you get stale pixels that look like a caching bug and
   are nearly impossible to reproduce.
3. **`app/EditHistory.swift`** — the forgiving decoder. It is the only thing
   standing between a schema change and silent, total data loss.
4. **`app/CanvasLayout.swift`** — one place that answers "where is the photo on
   screen". It exists because three independent derivations drifted apart and
   the handles landed on one rectangle while the pixels were drawn on another.
5. **`engine/shaders/highlights.slang`** — off by default, and the only shader
   here I would still call unproven on real frames. It drew halos once.

---

## 5. Known weaknesses, stated plainly

- **The wide-gamut export is honest but hollow.** The picker offers Display P3
  and Adobe RGB and converts correctly through ColorSync, but the display
  transform ends in Rec.709 primaries and saturates there — so **nothing Orion
  renders today falls outside sRGB**. P3 buys correct tagging for a managed
  workflow, not more saturation. The panel and the C header both say so.
- **Highlight recovery is off by default** and is now a measured no-op on a
  frame with fully blown lights (saturation 0.0146 → 0.0147 at full strength),
  because the white clip already made those pixels white. It still earns its
  place where one channel clipped alone, but that case is untested on real
  frames.
- **Two things the screenshot harness cannot see**: the Metal canvas (AppKit
  cannot capture a Metal layer) and any 3D transform (`cacheDisplay` skips
  them). The second is why the camera-style mode dial built this session was
  deleted rather than kept — a control whose whole point could not be checked by
  the suite, on a widget with one job.
- **The filmstrip is not screenshot-verified.** The harness renders one photo
  and never scans a folder, so the strip does not appear in any capture. Its
  changes are colors and Canvas drawing, which is the low-risk end, but it is
  the one piece of this session's UI work checked only by reading.
- **No lens database.** The corrections are built and correct; the coefficients
  are entered by hand. See §8.
- **No SQLite index.** The folder scan is live and fine to a few hundred frames.
- **`app/OrionApp.swift` is the largest file** and is close to the 1000-line
  limit `CLAUDE.md` sets. Two components have already been lifted out of it
  (`AdjustmentSlider`, `ColorWheel`); the panel bodies should be next.

---

## 6. Features that are genuinely missing

Not "not polished" — absent.

| Missing | Notes |
|---|---|
| **Masking** | No linear, radial, brush or subject masks. Every adjustment is global. **This is now the largest gap against the reference product** |
| **Healing / clone** | Nothing for dust spots |
| **Lens database** | **Not built — see §8.** The corrections work; the coefficients are typed in by hand |
| **Perspective / keystone** | Straighten only |
| **Panorama / HDR merge** | Out of scope for M3 |
| **Print** | No print pipeline |

---

## 7. Color grading — built this session

Three-way wheels, in `Color`. Worth a reviewer's attention because it is the
newest engine node and the one whose constant was hardest to get right.

- **Algorithm**: ASC CDL v1.2 slope/offset per tonal zone, blended by a
  smoothstep partition of Rec.2020 luma. `research/color-grading.md`.
- **Position**: after the tone controls, before the display transform, in
  scene-linear light. Grading after a filmic shoulder makes the same offset
  behave differently in the highlights for reasons unrelated to the zone.
- **The invariant**: each wheel's RGB offset is **zero-sum**, so a wheel changes
  hue and not brightness, and the slider beneath it is the only thing that
  moves luminance. 40 checks in `testGradeOffsets` cover every angle and radius.
  Nothing in the picture would announce this being wrong; the image would just
  drift brighter as you graded.
- **The constant was measured.** `kStrength` started at 0.15, which is sensible
  in a display-referred space and wrong in linear light where a dark patch sits
  around 0.005. Because the offset is zero-sum it always has a negative
  component, so 0.15 pushed two channels of every shadow through zero and the
  clamp held them there. The shadow patch measured luma **0.12 at k = 0.15 and
  0.22 at k = 0.03** — the larger setting was *darker*. It ships at 0.03.

A reviewer could reasonably challenge the zone boundaries (0.0/0.5 and 0.5/1.0
in linear luma). They are a plain reading of "shadows, midtones, highlights"
rather than a measured perceptual partition, and the research file says so.

---

## 8. What I was asked for and did not deliver

**The lens database.** Asked for in the same session as the grading and not
built. The correction shaders are finished and correct — distortion, TCA and
vignetting, against lensfun's published models — but the coefficients are still
entered by hand rather than looked up from what the EXIF names.

What it needs, so the next session does not have to rediscover it:

1. `lensfun` as a dependency. **LGPL-3** for the library, so dynamic linking
   keeps the application's own license free — which matters for the
   source-open / binaries-paid model this project is considering. Its database
   is **CC-BY-SA 3.0**: shippable with attribution, and share-alike applies to
   the data, not to the application.
2. darktable is **not** a source for this. Its database *is* lensfun's; its own
   code is GPL and copying it would force the whole application GPL.
3. The lookup: `TIFF Make`/`Model` and `Exif LensModel` are already read on
   export, so the strings are in hand. lensfun matches on those, then gives
   poly3 `k1`, TCA and vignetting terms for the focal length and aperture.
4. The maths does not change. `lens.slang` already takes exactly these
   parameters, normalized against lensfun's own `R_norm = ½√(w²+h²)`.

I ran out of session before starting it rather than half-building it. The honest
state is: the hard part (the corrections, correctly sourced) is done; the
plumbing (a dependency, a string match, a parameter fill) is not.

---

## 9. Working agreements a reviewer should know

- **No Rust, no Vulkan.** Both are hard constraints in `CLAUDE.md`, for
  maintainability by a solo developer. Suggestions that route through either are
  not useful here.
- **One node, one small shader** (50–150 lines). Adding a feature should be a
  repeatable three-file change.
- **Prefer mature libraries** over hand-rolled code, even at some performance
  cost. ColorSync does the color conversion and CoreGraphics does the resampling
  for exactly this reason.
- **Avoid GPL libraries** until the license model is settled.
- `planning/STATUS.md` is updated every session and is the recovery point when
  context is lost. It is the most useful file in the repository for someone
  picking this up cold.
