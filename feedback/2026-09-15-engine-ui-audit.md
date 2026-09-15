# Engine performance and desktop interaction audit

2026-09-15 · decision #271 · one reliability/performance story. Publication
requested on branch `fix/engine-ui-audit`. Measurements below come from the session logs;
the implementation and regression checks were read back when writing this report.

## Engine findings

MacBook Air M4, 24 GB, Sony 7968 × 5320 (42.4 MP). The focused fusion sweep
uses product contrast 1.45.
The repository's three sample RAWs are moon frames at 42 MP. A separate daylight
frame supplied scene variety. No 24 MP corpus was available, so historical
24 MP timings were not revalidated.

`DevelopPipeline::applyFusion` re-pushed the proxy and pyramid parameters on
every strength change. `setParams` dirties downstream nodes even when the bytes
are unchanged. Strength is consumed only by the final `FuseApply` pass.
`engine/src/pipe/DevelopLocal.cpp` now initializes the chain on entry/reload or
an invalid plan and updates the apply block on strength changes. Upstream edits
still invalidate their dependent pixels.

Paired old/new binaries, alternating runs; each row reports the median and p95
from the round with the lowest p95. Wall time covers apply plus render, with
readback/checksums outside the timed region.

| Fusion strength sweep | Before median / p95 (ms) | New median / p95 (ms) |
|---|---:|---:|
| Daylight full, pair 1 | 35.399 / 36.548 | 24.814 / 25.903 |
| Daylight full, pair 2 | 34.766 / 35.587 | 24.274 / 25.463 |
| Daylight preview, pair 1 (1992 × 1330) | 2.510 / 3.063 | 1.715 / 1.950 |
| Moon full | 33.960 / 34.820 | 23.827 / 24.654 |

Warm strength changes execute 4 nodes instead of 35: `fusion`, `develop:linear`,
`develop:display`, `geometry`. Old/new hashes agree at both resolutions on both
frames. `apps/bench/bench_fusion.cpp` provides the focused `--fusion-sweep` probe;
its named-node invariant fails on the old build and passes on the new one.
`apps/tests/tests_fusion_wiring.cpp` checks byte equality against forced
recomputation, actual strength movement, off/on transitions, upstream edits,
reload and wide output at full and preview scales.

Two limits remain:

- Full-resolution exposure measured 17.67–19.30 ms p95 before, 17.38 ms after.
  It still misses 16 ms. The fusion result does not close that budget gap.
  These are the legacy bench gate's render-reported timings at its default
  contrast 1.0; the wall-time engine sweeps below are a separate measurement.
- Session footprint rose from 2850 MiB (2.78 GiB) at first default render to
  about 9736 MiB (9.51 GiB) after exercising filters. Turning all filters off
  and reopening retained it. The pool high-water and process footprint are
  different measurements; the original 1560 MiB cold-render pool result (#219)
  does not bound a session. Cache retention needs a separately scoped policy.

The initial daylight sweeps also distinguish drag from settle: preview p95 was
1.71–1.86 ms for exposure, 7.43–7.70 for clarity, 3.34–3.47 for dehaze and
9.77–12.27 for denoise; full-render p95 was 18.08–18.33, 108.80–111.93,
58.15–59.74 and 153.33–171.80 ms respectively. These engine timings exclude
physical input delivery and display presentation. No competitor was benchmarked.

## Desktop fixes

| Finding | Implementation |
|---|---|
| Compare's temporary neutral render could enter autosave | `Engine+Render.swift` suppresses `onEdit` during original capture. `Autosave.swift` cancels an obsolete pending write when the state returns to the saved edit. |
| The first gesture required a preview texture that lazy allocation had never produced | `beginInteraction` materializes the preview before switching to it; failure falls back to full output. |
| Rotation's undo depended on which control invoked it | `Engine+Geometry.swift` owns the rotation transaction; toolbar/key callers use it directly. |
| Proposal review allowed document mutations outside the disabled panel | `documentEditsLocked` guards edit/history/reset paths, menus and canvas overlays. The watcher waits for active drags and detaches the outgoing review before another photo opens. |
| Proposal comparison used an as-shot reference | `comparisonReference` holds the committed edit, including white balance; labels read “Current edit” and “Proposed.” Both halves use proposed framing, disclosed in the footer. Ordinary Compare retains #269's as-shot baseline. |
| Proposal keys and enabled navigation were hard to read | Product labels replace persistence keys in the summary; engraved labels have a 10-point minimum; tabs, hints, gallery filenames and export notes use readable secondary ink. |
| Export options could crowd out actions; successful batches looked like errors | Options scroll above fixed Cancel/Export actions. Successful export and sync use `notice`; partial failures use the error message. |

`repro/desktop-interaction-reliability.txt` runs 27 disk/GPU checks through
`app/Scenario+Workflow.swift`, including the real proposal watcher, approval,
rejection, undo and autosave. All 27 pass.

Baseline: 54 scenarios, 30 pass and 24 fail/stop. Of the 24, ten need missing
fixtures, three failed on cold-preview availability (fixed here), and eleven
have unresolved scene assertions involving masks, Auto, dehaze, perspective or
selection. The targeted after-run passes all 12 scenarios, including the new
27-check regression. This was not a complete rerun of the baseline sweep.

The screenshot records cover ordinary and small windows, compare, proposal,
gallery, panels, empty state and both ends of export. They support layout review.
Physical gestures, focus behavior and VoiceOver were not exhaustively tested;
the gesture and wiring gates are source checks. All-UI verification remains open.

## Verification and evidence

The recorded `nine-gates-20260915` run passed all nine gates:

| Gate | Recorded result |
|---|---|
| Engine | 1104 checks, zero failures |
| Viewport | 4210 checks, zero failures |
| Decisions, before #271 | 268 rows, 1–270; three declared gaps; all references resolve |
| Gestures | 6 |
| Screens | 3 asserting scenes + 1 byte-stable scene |
| Modes | 13 library checks, 2 batch exports, HDR merge |
| Wiring | 472 product functions / 8 accounted-for harness-only |
| Agent | 14/14 |
| Site | 14 assertions |

Evidence basenames under the session's temporary `opencode/` directory:
`perf-engine-day-a.log`, `perf-engine-day-b.log`, `perf42-a.log`, `perf42-b.log`,
`perf42-day.log`, `fusion-paired-day.log`, `fusion-paired-moon.log`,
`fusion-regression.log`; `desktop-ui-audit/` and `desktop-ui-after/` contain
`sweep.json`, per-scenario logs and captures; `nine-gates-20260915/` contains
the gate logs. This report preserves the findings because those files are temporary.

## References and scope

- Apple [WWDC21 10153](https://developer.apple.com/videos/play/wwdc2021/10153/)
  and [WWDC20 10632](https://developer.apple.com/videos/play/wwdc2020/10632/),
  plus [Metal persistent objects](https://developer.apple.com/library/archive/documentation/3DDrawing/Conceptual/MTLBestPracticesGuide/PersistentObjects.html): profiling and resource reuse.
- [MetalPetal](https://github.com/MetalPetal/MetalPetal) (MIT), transient/persistent
  image cache policy; [Halide scheduling](https://people.csail.mit.edu/jrk/halide-pldi13.pdf)
  (Ragan-Kelley et al., PLDI 2013): references for a future retention decision.
  Neither was added as a dependency.
- Apple [HIG accessibility](https://developer.apple.com/design/human-interface-guidelines/accessibility),
  Capture One's [interface](https://support.captureone.com/hc/en-us/articles/360002468797-User-interface-overview)
  and [workspace customization](https://support.captureone.com/hc/en-us/articles/8861094993949-Customizing-Capture-One),
  Adobe's October 2020 Color Grading release and
  [Photomator](https://www.pixelmator.com/photomator/): desktop hierarchy,
  legibility and photo-first workspace references, without performance rankings.
- Fusion's published algorithm and Orion's deviations remain documented in
  [`research/exposure-fusion.md`](../research/exposure-fusion.md). This change
  adjusts invalidation; it introduces no filter mathematics.

Gaussian splatting, Marigold and Qwen were discussion ideas only. They require
explicit scope before being queued or built.
