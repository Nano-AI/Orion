# Feedback — senior review of the Orion repository

Written 2026-07-28. Two passes: first a verification of PERFORMANCE-AND-QUALITY.md
against the planning docs, research files and pipeline core; then a full
code-quality pass over every file in `app/`, the C API boundary, the engine,
the GPU layer, the export writer, and the math-bearing shaders. Both suites were
run on this machine. Findings are ranked by severity; each has evidence and a
concrete fix, because that is the standard this repository sets for itself.

## What was verified, and coverage

- `orion-tests`: **211 checks, 0 failures**. `orion-viewport-tests`: **2067
  checks, 0 failures**. (The quality doc says 208 — see §14.)
- **Read in full:** every Swift file in `app/` including the screenshot
  harness; the C API and header; all engine `.cpp`/`.mm` (pipeline, white
  balance, tone curve, noise profile, raw decode, Metal device/resources,
  image writer); and the math-bearing shaders (linearize, grade, display,
  linear, tone/hsl ops, geometry, lens, sharpen, highlights, guide_ab,
  denoise_accum, cfa, color_matrix). **Skimmed or skipped:** the four RCD
  demosaic kernels, box blur, guide prep/down, denoise_blur — ported from the
  cited references and covered by GPU tests, judged low-risk — and the two
  viewport test files themselves.
- The closing sweep (highlights, noise profile, raw decode, screenshot
  harness, tone-curve LUT, guide/denoise kernels) found **no new defects** —
  those files are among the best in the repository. `highlights.slang` in
  particular is better than its own reputation: the doc calls it unproven,
  but the shoulder-only fit, extrapolation cap and neighbourhood ratio
  ceiling are exactly the guards its history demanded. What is missing is the
  single-channel-clip test on a real frame, not rigor in the code.
- The claims in PERFORMANCE-AND-QUALITY.md §3 and §7 match the code they cite.
  The linearize clip, the zero-sum grade offsets, the forgiving sidecar decoder
  and the measured `kStrength = 0.03` are all real and implemented as described.
- The self-assessment's honesty is unusually high, and most of the code is
  genuinely good — see §16 before reading the defect list as a verdict.

---

## 1. Edits are lost when the app quits (P1, data loss)

**Evidence:** `saveDevelop()` is called in exactly one place —
`OrionApp.swift:1043`, inside `load(_:)`, when *switching photos*. There is no
`scenePhase` handler, no `willTerminate` observer, no autosave after an edit.

**Impact:** edit a photo, quit Orion (⌘Q, crash, logout), reopen — every
adjustment since the last photo switch is gone, silently. For the common case
of opening one file, editing it and quitting, that is *all* of the work. The
sidecar machinery (`Sidecar.merge`) is correct and ready; nothing invokes it at
the moment that matters. The commit "edits persist per photo" covered switching
and only switching.

**Fix:** debounced autosave (the histogram already shows the pattern —
`scheduleHistogram` at `Engine.swift:644`) plus a save on
`NSApplication.willTerminateNotification`. Then the invariant test: edit,
tear down the Editor, reopen, assert the sidecar carries the edit — the
viewport suite already tests sidecar round-trips, so the seam exists.

---

## 2. Disabled guide chain feeds garbage into whites and blacks (P1, correctness)

**Evidence:** `developLinear` (`develop_linear.slang:60`) always samples
`guideAb` and `guideRaw`. When highlights and shadows are both zero,
`DevelopPipeline::apply` disables all seven guide nodes
(`DevelopPipeline.cpp:659`), and `Pipeline::resolve` (`Pipeline.cpp:111`) walks
each disabled input back to the last live producer — which is the **color
matrix output**. So `rawEv` becomes a linear RGB value read as if it were log2
luminance, and `ab` becomes the pixel's own R and G.

**Impact:** `applyTone` (`ops/tone_ops.slang:66`) normalizes four band weights
to a partition of unity. Two of those weights (`wShadows`, `wHighlights`) are
computed from the garbage `guideEv`. Their *offsets* are zeroed by the zero
sliders, but their weights still sit in the denominator — so a whites-only or
blacks-only edit is diluted by a phantom weight that varies per pixel with the
pixel's own color. For a typical midtone pixel the spurious highlight weight is
large (evLocal lands near +3 EV), so the dilution is not subtle. The bench's
"blacks −1" probe prints `ok` because it only asserts *some* effect, not the
right one.

**Fix:** tell the shader the guide is off — reuse the dead `guideEpsilonUnused`
slot as a flag, and when it is clear compute `evLocal = ev` (the pixel's own
EV), which is also the correct semantic fallback. Then a GPU test: blacks −1
with shadows at zero must produce the same dark-patch delta whether the guide
chain is enabled or not.

---

## 3. Active lens corrections destroy incremental invalidation (P1, performance)

**Evidence:** `DevelopPipeline.cpp:544` —

```cpp
if (first || correctingLens ||
    adj.lensDistortion != lastAdj_.lensDistortion || ...)
```

`correctingLens` is true whenever any lens slider is *nonzero*, not when one
*changed*. `Pipeline::setParams` (Pipeline.cpp:94) has no value comparison and
unconditionally dirties downstream.

**Impact:** with a vignette applied, every exposure tick re-pushes the lens
params and recomputes lens (a full resample), sharpen, matrix, develop:linear,
display and geometry — roughly 7 full-resolution passes instead of 3. The
headline "exposure drag 11.5 ms, 3 nodes" holds only with all lens sliders at
zero, which is the only state the bench measures. Once the lens database lands,
corrections-on becomes the *normal* state.

**Fix:** delete `correctingLens ||`; the changed-comparisons already cover
enable/disable transitions. Add a bench probe with `lensVignette = 0.5` that
drags exposure and asserts the node count is still 3. Every other block in
`apply()` is guarded correctly; lens is the one outlier.

---

## 4. The grading zones are mislabeled by the linear-luma partition (P2, usability)

With the partition at 0.0/0.5/1.0 in **linear** luma (`color_grade.slang:57`):
middle gray (Y = 0.18) weighs **0.70 shadows / 0.30 midtones / 0.00
highlights**; the midtone weight peaks at Y = 0.5 linear, about +1.5 stops above
middle gray (sRGB ~0.74 on screen); the highlights wheel touches little but
speculars. On a normal exposure the shadow wheel grades nearly the whole image
and the other two barely act — against Lightroom/Resolve expectations the
wheels will feel broken, not different.

**Fix:** keep the CDL math and scene-linear application (both correct and well
argued), but compute the *weights* on a perceptual axis — smoothstep over
`log2(Y / 0.18)` with knees around ±2 EV, or over AgX-mapped luma. Also: the
partition is Orion's own formulation, not part of ASC CDL — by the repo's own
rule it belongs in `research/UNSOURCED.md`, and it is not there.

---

## 5. The newest engine node has no GPU test, and the bench cannot fail (P2)

- `research/color-grading.md` admits the grade shader's effect "is a check
  somebody ran once." No `testColorGradeGpu` (identity at zero; a shadow push
  moves a dark patch, leaves a bright one). No grading probe in the bench.
- The bench's thirteen control probes print `NO EFFECT` but the exit code
  ignores them (`apps/bench/main.cpp:310`) — a silently dead slider still
  exits 0.
- `main.cpp:264` hardcodes `"(1 of 8 nodes)"` for the curve drag; the graph has
  27 nodes. PERFORMANCE-AND-QUALITY.md §2 repeats the stale label as if
  measured.

---

## 6. Sidecar escaping compounds on every merge (P2, data corruption)

**Evidence:** `Sidecar.swift` — `write` escapes (`escape()`, line 115) but
`read`/`value(of:)` never unescapes. `Library.persist` reads-modifies-writes
the whole sidecar on every rating change.

**Impact:** a color label containing `&`, `<` or `"` gains one escape layer per
merge: `R&D` → `R&amp;D` → `R&amp;amp;D`. Today `colorLabel` has no UI writer
so nothing hits it — but the field is read from foreign sidecars, and Lightroom
writes labels like "Second". Fix now while it is one function: unescape in
`value(of:)`. (The string-matching parser itself is acceptable for the current
interop bar, but it is the same class of shortcut — say so in a comment.)

---

## 7. Every export publishes the photographer's location (P2, privacy)

`ImageWriter.mm:80` copies the **GPS dictionary** onto every export,
unconditionally. ROADMAP's own export-panel spec lists "Keep all · Strip
location · Strip everything"; the built panel has no metadata control at all. A
photo taken at home and exported for the web carries the home coordinates. This
is the one gap in the feature set with real-world consequences for a user who
has no way to know. **Fix:** default to stripping GPS unless the user opts in,
or land the promised three-way control — either is fine, silent inclusion is
not.

---

## 8. The design-token system exists and nothing uses it (P2, UI architecture)

`Sources/OrionUI/DesignTokens.swift` is generated from `design/tokens.json`
("do not edit by hand") — and **no file imports it**. `OrionApp.swift:135`
re-declares `Palette` by hand:

- The mirror constructs colors in **sRGB** (`Color(red:green:blue:)`); the
  generated tokens are **Display P3** (`Color(hex:)` with `.displayP3`). Same
  numbers, different primaries — the app renders slightly different colors than
  the token file specifies.
- Constants renamed in transit (`reject`→`rejected`, `star`→`rated`), `rail`
  dropped, `filmBase`/`filmHole` added only to the mirror — drift is designed in.
- Hardcoded literal copies elsewhere: `Navigator.swift:30` (accent),
  `Histogram.swift:51` (channel colors that exist as `chanR/G/B` tokens),
  `ImageCanvas` clear color and shader surround (0.165…), spacing numbers that
  duplicate `Orion.Space`/`Orion.Layout` (322, 14, 44, 98).

**Fix:** make the app link the generated module, delete the hand mirror, add
the film colors to `tokens.json`, and regenerate. One source, as the file's own
header demands.

---

## 9. Main-thread stalls and small leaks (P3 cluster)

- **Every slider tick renders synchronously on the main thread** —
  `didSet → pushAndRender → orion_engine_render → commitAndWait`. At 12 ms
  that fits a frame; the 43–50 ms temperature drag means dropped frames by
  construction. Known and accepted (degrade-then-refine is the planned fix);
  stated here so it is on the record as a structural choice, not an oversight.
- `Engine.exportedSize` (a real ~150–300 ms encode) runs on the main actor via
  the export sheet's `measure` closure — the sheet hitches on every debounced
  remeasure. Detach it.
- `installKeyMonitor` (`OrionApp.swift:1101`) adds an `NSEvent` local monitor
  that is never removed — no `onDisappear`, no stored teardown.
- `captureOriginal` (`Engine.swift:509`) creates a fresh `MTLCommandQueue` per
  capture; reuse one.
- `Library.open` streams metadata **serially** — one detached task at a time,
  awaited in order (`Library.swift:98`). A 500-frame folder trickles in;
  a bounded `TaskGroup` would load it in a fraction of the time. And
  `openFolder` busy-polls `library.loading` every 30 ms.

---

## 10. Copy and behavior mismatches (P3, all small, all user-visible)

| Where | Says | Does |
|---|---|---|
| `OrionApp.swift:631` hint | "X rejects" | R rejects (handler, menu, STATUS all agree on R) |
| `ExportPanel.swift:301` | "Color space is sRGB." | shown even when Display P3 / Adobe RGB is selected |
| `ExportPanel.swift:359` comment | commits "on Return or on losing focus" | `onSubmit` only — click Export with an uncommitted field and it exports stale dimensions |
| `OrionApp.swift` empty state | "Sony ARW today" | the open panel accepts 8 raw extensions |
| Export save dialog | `export.jpg` | should default to the photo's own name |

---

## 11. Dead and inconsistent state (P3)

- `Library.minimumRating` has no UI writer, and couples oddly into
  `visible`'s `.all` case (`!photo.rejected || minimumRating == 0`) — either
  finish the rating filter or delete the state.
- `ToneCurve.cpp`'s `evaluateCurve` re-sorts the points and recomputes the
  Fritsch–Carlson tangents on every call; `buildCurveLut` calls it 1024 times
  per rebuild. Hoist both per channel. Harmless today (curve drag is within
  budget), noted so it is a choice rather than an accident.
- `DevelopPipeline.cpp:217` computes `swaps` and voids it. `LinearAdjustParams`
  carries `guideEpsilonUnused` (a candidate for the §2 fix). `CApi.cpp` uses
  `std::clamp` without including `<algorithm>`.
- `ImageCanvas.Renderer.transform` mutates `viewport.fitScale` and clamps the
  model *inside* `draw(in:)` — a model write during a render pass. It works,
  but it is the pattern that produces "view didn't update until the next tick"
  bugs; compute fit scale where the viewport changes instead.

---

## 12. Accessibility is uneven across the custom controls (P3)

`AnalogTrack` does it right: `accessibilityElement`, value, adjustable actions,
reduced-motion respected. The others don't follow: `ColorWheel` has a label but
no value and no adjustable action (a VoiceOver user cannot grade at all);
`CurveEditor` is mouse-only; `Filmstrip` cells are tap-only with no keyboard
path and no accessibility identity beyond `.help`. The platform bar — and this
repo's own best control — is the standard; bring the other three up to it.

---

## 13. The 1000-line rule is broken, and the doc understates it (P3)

| File | Lines | Note |
|---|---|---|
| `apps/tests/main.cpp` | 1791 | tests are still code the developer must hand-edit |
| `app/OrionApp.swift` | 1205 | the quality doc says "close to the limit" — it is 20% past it |
| `engine/shaders/highlights.slang` | 197 | the 50–150 shader rule's own text says ~200 is two nodes |

Lift the panel bodies out of `OrionApp.swift`; split the test file by subject.
And a quality report should not soften a violated hard constraint into
"close to" — state the number and let it be red.

---

## 14. Documentation drift — the recovery-point system contradicts itself (P3)

| Where | Says | Reality |
|---|---|---|
| `ROADMAP.md` "Where things actually stand" | M1 ~60%, "Export is 8-bit" | STATUS: M1 ~98%; 16-bit export shipped and verified |
| `STATUS.md` header | "Next story: the color-space picker" | shipped in `dca7d28` |
| `STATUS.md` latency | gate 12.70 ms p95 | quality doc: 13.90 with grading in graph |
| Quality doc §1 | 208 checks | 211 on this machine |
| `UNSOURCED.md` "just absent" list | highlight recon, denoise, lens corrections, 16-bit export absent | all four are built |
| `UNSOURCED.md` §1 | tone masks are smoothstep knees at −4…+1 EV | `tone_ops.slang` now uses Gaussian bands at ±2.5/±5.5 EV, citing deep-research §3 — the register describes code that no longer exists |
| Masking milestone | `masking.md`/STATUS say M3 | ROADMAP/FEATURES put masks in M4 |

**Fix:** delete ROADMAP's status section in favor of a pointer to STATUS.md —
two copies of "where we are" guarantees this. Strike the four built features
from UNSOURCED, rewrite its §1 against the current implementation, and add the
grading partition (§4 above).

---

## 15. Frontend design — assessment against the craft bar

The interface has a real, defensible point of view: an analog-instrument
language (engraved tracks, milled thumb, index detents, the film strip with
KS-1870 sprocket pitch, serif wordmark against a sans instrument) executed
consistently and *argued for* in comments. The copy is genuinely good — plain
verbs, explanatory footnotes under controls, errors that name the problem.
Restraint is real: the mode dial was deleted for being a costume. This is far
above template quality, and the histogram-as-signature reading is correct.

What still falls short of "best standards":

1. The token system bypass (§8) — the design has one voice but two sources of
   truth, in different color spaces.
2. Accessibility unevenness (§12).
3. The copy mismatches (§10) — small, but copy is design material here and the
   rest of it is too good to leave these in.
4. Keyboard reachability: sliders and wheels are pointer-only outside of
   VoiceOver; a keyboard-first workflow is an M2 roadmap item and none of the
   custom controls accept focus yet.

---

## 16. What is excellent, and should be defended

- The exception firewall (`CApi.cpp`) is exactly right: every entry guarded,
  fixed buffers copied safely, malformed curves degrade to identity.
- `ImageWriter.mm` tags pixels as what they are (sRGB) and converts through
  ColorSync — the correct answer, with the reasoning in place.
- `CanvasLayout` as the single geometry authority, and `CurveMath` deliberately
  duplicating the engine's spline *with a pinning test* — both are the mature
  handling of a real risk.
- `EditHistory` (snapshot, coalesce, bound), the forgiving sidecar decoder, the
  `assign()` single-field-list rule, `AnalogTrack`'s interaction detail.
- The measure-don't-look culture: `--measure`, per-CFA-channel assertions, the
  k = 0.03 story. It found three invisible bugs this week; §2 above shows the
  next place it needs to reach (disabled-node resolve paths).

---

## 17. For the loop — process feedback

1. **Benchmark and test the adversarial state.** Every latency number and every
   control probe runs with optional nodes off. §2 and §3 both lived in the
   gap between "the happy path is measured" and "the normal user state is not."
   One probe per switchable node, asserting node count and effect magnitude.
2. **Wire the lifecycle before the feature.** The sidecar work was done and
   correct; the save-on-quit hook was nobody's story. When a feature's value
   depends on *when* it runs, the trigger is part of the feature.
3. **One source per fact** — for documents (ROADMAP vs STATUS), for constants
   (Palette vs DesignTokens), for registers (UNSOURCED vs the shaders). Every
   duplicated fact in this repo rotted within days. Enforce by deletion.
4. **Numbers in documents should be pasted from output, not recalled** (208 vs
   211, "1 of 8 nodes").
5. **When a hard constraint is violated, say "violated"** — not "close to."
