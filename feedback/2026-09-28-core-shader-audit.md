# Core shader source audit — 2026-09-28

Source-only review: **13 product files / 1,903 lines fully read**. Two new
correctness findings below; neither was rendered or benchmarked in this pass.
No product changes, builds, tests, application launches or GPU work were run.
The numbers below are algebraic consequences of the source, not measurements.

## Findings

| ID / priority | Reachable trigger and evidence | Consequence | Minimum repair / regression |
|---|---|---|---|
| CS1 / P2 — grading wheels preserve channel mean, not luminance | Set all three grading wheels to green (`x = -0.5`, `y = sqrt(3)/2`), brightness tracks zero, on a neutral patch. `engine/src/pipe/DevelopPipeline.cpp:188` subtracts the arithmetic RGB mean. The only product caller, `engine/src/pipe/DevelopOutput.cpp:223`, supplies these offsets to `engine/shaders/color_grade.slang:151`; line 178 adds them scaled by **Rec.2020 luminance**. | Offset is approximately `(-0.125, +0.25, -0.125)` in every zone. At middle gray the zone sum is approximately one, no channel clips, and `dot(offset, (0.2627, 0.6780, 0.0593)) = 0.12925`: **linear luminance rises about 12.925%** while arithmetic mean stays fixed. This violates the stated promise that a wheel changes color and its separate brightness track controls luminance. It is separate from #41's repaired constant-offset deep-shadow clipping bug. | First add a GPU neutral-wedge regression using weighted Rec.2020 luminance, including the green direction and zero brightness tracks. The existing check at `apps/tests/tests_grade.cpp:445` measures `(R+G+B)/3`, so it cannot catch this. The small shared repair is subtracting weighted luminance instead of channel mean in `gradeOffsets`; document/version the changed look for old grades, and separately test clipping on saturated colors before claiming general luminance preservation. Reuse the weights already cited in `research/color-pipeline.md`; do not invent a new color model. |
| CS2 / P2, developer override only — BT.2390 adaptation can reverse its shoulder | Select `ORION_ROLLOFF=2` or batch export `--rolloff 2`, set contrast to **2** (within the UI's `0.5...2` range), and lift a neutral bright patch into the shoulder. `engine/shaders/ops/rolloff_ops.slang:76-78` expands the Hermite interval to the contrast-scaled endpoint but keeps a unit derivative at its knee. `engine/src/pipe/DevelopOutput.cpp:49-55` makes the override reachable; `app/BatchExportDriver.swift:37` accepts the CLI selector. | Write `t = kRollOffHi`, `h = yMax-t`, `d = 1-t`. The shoulder polynomial's derivative with respect to its normalized coordinate is `(1-s) * [h + (6*d-3*h)*s]`. If `h > 3*d`, it becomes negative before the endpoint. At contrast 2, source constants give `t ≈ 0.92893`, `h ≈ 0.46501`, `3*d ≈ 0.21322`: the shoulder rises above 1 and then falls back. Even contrast 1.45 meets the same inequality (`h ≈ 0.24834`). Passing that axis to `agxCurve` exceeds its documented fit domain; source alone does not quantify the final displayed reversal/clipping. | Keep mode 1 as the shipping default. Repair mode 2's adapted knee/interval using monotonic-Hermite constraints, or reject unsafe override combinations pending that repair. Do not silently cap the spline's output: that recreates the hard plateau it exists to avoid. Add a real GPU rising neutral ramp through the full reachable shoulder, at 1.45 and 2, with bounds/order assertions; the two deep-shadow samples documented for `testDisplayRollOffIsInjective` do not cover this shoulder. Record the adaptation in the existing research entry. |

CS2 does **not** implicate the default ACES mode. This is a defect in Orion's
domain adaptation, not a claim that the published BT.2390 method is defective.
Its trigger needs an exposure/brightening edit: the default unedited sample
ceiling used in #223 does not exercise the whole shoulder at contrast 1.45.

## Existing findings and limits kept separate

| Area | Review disposition |
|---|---|
| Camera JPEG versus neutral RAW saturation | #229 is settled; no renewed global color-parity defect. #230's two high-key frames remain unresolved. The fitted #232 curve is a camera look, not proof of colorimetric accuracy. |
| Default roll-off | Host default is mode **1** (#224), despite stale mode-0-default comments in `develop_display.slang`. The old hard-clamp report is not reopened. |
| Process-1 broad tone bands | #276 provides process 2; legacy process-1 behavior is intentional and must not be silently retuned. |
| Negative channels / gamut clipping | Known clamp limitations are documented in `research/tone-and-gamut-findings.md`; this pass supplies no new image evidence or attribution for #230. |
| Highlight recovery | The fixed 12-pixel/stride-3 fit and the common pre-demosaic clip are documented choices. The later region-fill shaders are outside this batch. No new defect is established in the assigned recovery kernel. |
| Masks | The local loop and selected-overlay reads were inspected; the existing `2026-09-28-masking-audit.md` owns layer/coverage failures. They are not counted again here. |
| Performance / RAM | No new measured finding. Per-pixel reads, loops and fused passes were inspected, but latency, compilation behavior, pool lifetime and peak memory cannot be inferred from this batch. Existing #271/#286 limits remain. |

## Exact full-read inventory

Paths relative to the repository; counts are `wc -l` at review time.

| Product file | Lines |
|---|---:|
| `engine/shaders/develop_linear.slang` | 318 |
| `engine/shaders/develop_display.slang` | 368 |
| `engine/shaders/linearize.slang` | 60 |
| `engine/shaders/linear_source.slang` | 41 |
| `engine/shaders/color_matrix.slang` | 34 |
| `engine/shaders/huesat.slang` | 129 |
| `engine/shaders/highlights.slang` | 197 |
| `engine/shaders/color_grade.slang` | 193 |
| `engine/shaders/ops/tone_ops.slang` | 159 |
| `engine/shaders/ops/rolloff_ops.slang` | 156 |
| `engine/shaders/ops/hsl_ops.slang` | 135 |
| `engine/shaders/ops/dither_ops.slang` | 31 |
| `engine/shaders/ops/vignette_ops.slang` | 82 |
| **Total** | **1,903** |

**Targeted only, not full-read coverage:** `engine/src/pipe/DevelopPipeline.cpp`
(`gradeOffsets` and beginning of composition geometry), `DevelopPipeline.h`
(declaration), `DevelopOutput.cpp` (roll-off selection, grade parameter push,
references to linear/display parameter pushes), `app/Engine.swift` (contrast
and state wiring search), `app/Engine+Document.swift` / `Engine+Mask.swift`
(search hits), `app/DevelopPanels+Light.swift` and `app/AgentKeys.swift`
(contrast ranges), `app/BatchExportDriver.swift` (override parsing search),
`apps/tests/tests_grade.cpp` (grading oracle excerpts). These files must not
be promoted to fully read in the whole-source coverage map.

Research context consulted: `research/README.md`, relevant entries and later
corrections in `color-pipeline.md`, `camera-profiles.md`,
`tone-and-local-contrast.md`, `highlight-reconstruction.md`, `color-grading.md`,
`vignette.md`, `tone-and-gamut-findings.md`, and relevant `UNSOURCED.md` entries
(tone versions, vibrance, HSL, grade, roll-off). Planning/status and existing
feedback were checked for settled behavior. Research/planning files are context,
not part of the 13-file full-read claim. No external implementation was copied.

**Remaining verification:** reproduce CS1/CS2 through the real shader and
product parameter path, then run required gates after any repair. This pass
does not certify rendering correctness, speed, memory capacity or all caller
validation for these files.
