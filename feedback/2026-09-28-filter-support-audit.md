# Filter support source audit — 2026-09-28

Source-only pass over the 23 files marked unread in `2026-09-28-audit-coverage.md` (5,595 lines). I read the complete inventory below, the relevant `research/` entries, and narrow callers and existing checks. No product code changed. No build, compiler, app, GPU, stress test or executable check was run: the host was at kernel memory pressure warning 2. Findings are source-derived; no new render, latency or RSS claim follows from this pass. Existing mask, engine-I/O, GPU-resource and shader findings remain with their dated reports.

## Ranked findings

| Rank | Source-derived finding | Trigger and consequence | Smallest fix direction and missing evidence |
|---|---|---|---|
| P1 | A finite `.cube` number may become infinity on narrowing. `CubeLut.cpp:49-58` checks the `double` from `strtod`, then casts to `float` without checking the result. `parseCube` accepts the resulting value at `:198-209`, and `DevelopOutput.cpp:530-570` uploads it into the LUT texture. | A data row containing `1e100` is finite as `double`, exceeds `float` range, and is accepted as `+inf`. A downloaded look can then produce nonfinite pixels rather than a named load error. This is a file-input boundary. | Reject values outside the representable finite `float` range (and validate the cast) in the existing `toFloat`. Add one parser check for a huge finite literal and, when execution is safe, a small GPU render check confirming rejected data never reaches the texture. Current parser tests in `tests_display.cpp:108-195` cover malformed text, not this case. |
| P2 | Lifting an arbitrary 1D LUT to the fixed 33³ grid loses narrow features. `CubeLut.cpp:88-114` samples the input curve only at the 33 grid abscissae; `:242-251` uses this for files up to 65,536 entries (`:173-180`). The shader then interpolates the lifted grid. | A 1D table with a narrow peak between adjacent 1/32 samples can lose that peak entirely, even though the original 1D table contains it. The comment at `:83-86` correctly claims exactness *at grid nodes* but the product accepts the whole source table and does not warn about detail between them. This is a color-output fidelity gap, not a provenance concern. | Either preserve the 1D table through a native 1D sampling path, or explicitly bound/decline input resolution that cannot be represented under a measured error limit. Do not silently call the 33³ lift lossless. `tests_display.cpp:182-195` checks only the two endpoints of a two-entry linear LUT; it cannot see this. A narrow-peak CPU assertion plus render oracle is needed before changing the path. |
| P2 | LUT loading has no input-size bound before allocation. `CApi.cpp:484-491` copies the entire chosen file into a `std::string`; `CubeLut.cpp:198-210` appends every numeric row to `values`, and only compares the count with the declared maximum after the full file at `:230-249`. | A malformed or oversized local `.cube` can make two large resident copies and grow the float vector until memory exhaustion, then fail for excess rows. With the current host pressure this is especially undesirable, but no memory measurement was run. | Check file size before reading, and stop parsing rows once they exceed the declared bounded count. Use the existing 65³ / 65,536 limits, with a modest text-size allowance for headers/comments; no new parser layer is needed. Existing short-table tests do not cover overlong input. |
| P3 | The fit-only saturation override does not enforce its documented grammar. `HueSatMap.h:273-289` says exactly 90 comma-separated scales and malformed input is ignored, but the loop accepts whitespace separators, a trailing suffix after the 90th parsed value, and `strtof` nonfinite results. `DevelopCapture.cpp:365-368` sends the result into the profile table. | A malformed `ORION_HUESAT_CURVE` environment value can change the fitted profile while appearing accepted. This is a developer measurement hook, not an ordinary UI input; the risk is a misleading fit or test run, not a known shipped-image defect. | Require each expected comma/end and finite scale in the existing loop. A small pure check suffices when execution is safe. `tests_grade.cpp:267` exercises the default fitted curve, not malformed overrides. |

## Exact full-read inventory

Counts are current `wc -l` source lines, not executable statements. All paths in this table have prefix `engine/src/pipe/` except `util/Half.h`, whose prefix is `engine/src/`.

| File | Lines | File | Lines | File | Lines |
|---|---:|---|---:|---|---:|
| `Adjustments.h` | 378 | `AutoEnhance.h` | 227 | `CubeLut.cpp` | 260 |
| `CubeLut.h` | 67 | `Dehaze.h` | 92 | `DevelopCapture.cpp` | 802 |
| `DevelopInternal.h` | 45 | `ExposureFusion.h` | 300 | `GrainPlate.h` | 182 |
| `HighlightFill.h` | 359 | `HueSatMap.h` | 370 | `LensDatabase.cpp` | 374 |
| `LensDatabase.h` | 120 | `LensGeometry.h` | 116 | `LocalLaplacian.h` | 218 |
| `MaskGeometry.h` | 738 | `Perspective.h` | 354 | `Pyramid.h` | 91 |
| `ToneCurve.cpp` | 123 | `ToneCurve.h` | 47 | `WhiteBalance.cpp` | 215 |
| `WhiteBalance.h` | 37 | `util/Half.h` | 80 | **Total: 23** | **5,595** |

## Scope and assumptions

I used the local published-source notes in `research/{auto-enhance,dehaze,exposure-fusion,film-grain,highlight-reconstruction,lens-corrections,local-laplacian,luts,masking,perspective,tone-and-local-contrast}.md` and `research/UNSOURCED.md` for algorithm context. No algorithm replacement or GPL implementation is proposed. The P1 and P2 LUT findings follow from the parser and sampled-grid arithmetic; they have not been reproduced in a running product. The P3 override is conditional on a developer-set environment variable. `CApi.cpp`, `DevelopOutput.cpp`, `tests_display.cpp` and `tests_grade.cpp` were targeted caller/check reads, not promoted to full-file audits. This batch does not clear the remaining unread shaders or the 42 targeted product files in the coverage map, nor the pending nine gates and full-resolution memory/latency measurements.
