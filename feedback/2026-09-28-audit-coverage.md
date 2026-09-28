# Whole-repository audit coverage — 2026-09-28

Inventory of **tracked first-party source at batch source checkpoint `dc02241`**, reconciled against the dated reports in `feedback/`, one new five-file read below, and a complete read of the new 117-line `app/BatchExportSession.swift`. The one new harness, `app/BatchExportProbe.swift`, is inventoried separately. This is a coverage map, **not a completed whole-codebase audit**. A finding or a passing gate does not turn a targeted read into a full-file review. The July senior review describes a whole-repository pass but does not supply a file-by-file read map; it is valuable prior evidence, not a basis for upgrading every file below. Counts use tracked files at that checkpoint, not ignored build output or installed dependencies.

| Product source | Count | Full source/logic read documented | Targeted read documented | No documented full or targeted audit read |
|---|---:|---:|---:|---:|
| Desktop app and bridge (`app/*.swift`, `app/Bridge.h`; excluding test/scene harnesses and `BatchExportProbe.swift`) | 96 | 96 | 0 | 0 |
| Engine C++/ObjC++/headers (`engine/src`, `engine/include`) | 59 | 59 | 0 | 0 |
| First-party Slang shaders (`engine/shaders`, including `ops`) | 64 | 64 | 0 | 0 |
| MCP product server | 1 | 1 | 0 | 0 |
| Website HTML/CSS/JS, excluding vendored JS | 6 | 6 | 0 | 0 |
| Generated design-token Swift source | 1 | 1 | 0 | 0 |
| **Total** | **227** | **227** | **0** | **0** |

“Full” means a prior report explicitly says complete/full source or logic read, or this report read the entire file; it does **not** mean every behavior was tested. The desktop report names 21 complete files, the mask report names seven complete app files plus three engine/shader files, this report adds five app files, and the controls, canvas/geometry, GPU/resource, core-shader, engine-contract, filter-support, assistant/agent, pyramid-shader, detail-shader, remaining-surface, app-complete and engine-complete reports add 17, ten, ten, 13, ten, 23, seven, 26, 23, 15, 21 and 15 unique files respectively. `mcp/server.ts` and `app/AgentCLIDriver.swift` are among the desktop report's 21; the assistant report read `AgentCLIDriver.swift` again without adding to the full count. Overlaps were counted once; `Pipeline.cpp` moved from targeted to full. “Targeted” means relevant sections, call sites, or specific findings. The batch checkpoint adds one new fully read product file, `BatchExportSession.swift` (117 lines). The app-complete and engine-complete reports fully read the last 36 targeted files, including earlier targeted callers; the canvas review had already promoted `Engine+Geometry.swift` to full. The changed `HdrMergePanel.swift` guard was first read as targeted code and then included in the complete app read. Engine I/O had no explicit whole-file roster by itself; the later engine-complete report supplies the complete reads for its remaining cited implementations. The 2026-09-15 website report checked interactions and fallback behavior; the new remaining-surface report read all six product web source files, except that the embedded base64 map bytes were not decoded or visually reviewed. The generated Swift tokens and two generator inputs were inspected, but generated-byte reproducibility was not run. More detail: the dated audit reports in this directory.

| Objective | Evidence now | Status / missing proof |
|---|---|---|
| Whole first-party source audit | 227 full, 0 targeted-only, 0 unread at the batch source checkpoint | **Source-read inventory complete; objective still open.** Tests/tooling and behavioral, performance, RAM, UX and competitor proof remain incomplete. A full-file read is not a clean bill of health. |
| Latency and physical interaction | #271 measured paired fusion sweeps and 42 MP exposure p95 17.38 ms against 16 ms; preview engine timings exist | **Open.** Physical gestures, focus, cancellation and end-to-end display latency were not exhaustively measured. |
| RAM and capacity | Controlled 1992×1330 inactive texture payload 903.1→303.5 MiB after shrink; 42 MP earlier session reached 9.51 GiB. Source audits derive ~840 MB decimal transient histogram payload and ~42 MB full-frame watermark mask at 42 MP, neither measured RSS | **Open.** Measure histogram and enabled-watermark peaks, then full-resolution active-filter peak and long-session memory. |
| Mask and spot correctness | Eight ranked mask findings and a P1 spot redo state loss from source trace; 21/21 agent checks after fixture correction | **Open.** Layer/raster/async mask cases need render reproductions; spot place-drag/undo/redo needs a state regression. Passing agent checks do not clear them. |
| File and project handling | Desktop and engine I/O reports trace cross-photo autosave, dropped writes, foreign XMP erasure, HDR collisions and export finalization. Later source audits add CFA-phase noise sampling, early HDR Stop, `.cube` input/fidelity, session replay of spaced paths/edited photos, and watermark save dismissal | **Open.** Some pure Swift probes executed; product-level failure/race injection, phase-permutation noise, LUT parser/render, replay and failed-save UI checks remain missing. |
| Agent proposal safety | Assistant/agent audit traces lenient proposed values and commits, same-stem RAW proposal collisions, silent proposed-state fallback and CLI rating coercion | **Open.** Source-only; add malformed-value, byte-preserving commit and two-photo identity checks before marking safe. |
| App state and session safety | Complete app read traces Trash after failed save, incorrect multi-spot diff indices, stale menu count equality, silent preset deletion and empty row after failed matte inference | **Open.** Source-only; focused failure and two-spot/selection checks have not run. |
| Rendering order and pixel math | Pyramid shader audit traces stale dehaze airlight and fusion plan after upstream edits; detail shader audit traces RCD border color-axis and saturated-grain mean changes | **Open.** Source-only. Compare edit orders and border/saturated fixtures through real GPU renders before changing cache or kernels. |
| LUT and C-facade consistency | Complete engine read traces dimension-dependent LUT carryover, partial full/preview LUT upload, nonfinite tone-curve coordinates and exceptional writer context lifetime | **Open.** Source-only; two-photo, fault-injection and nonfinite-input checks have not run. |
| UX and accessibility | 54-scenario earlier baseline: 30 pass, 10 missing fixtures, 3 cold-preview failures fixed, 11 unresolved scene assertions; three asserting screenshot scenes pass. Watermark controls have source-traced name/state gaps | **Open.** No exhaustive VoiceOver, keyboard/focus or physical-device review. |
| Build and packaging | Twelve build/tooling files fully read; source trace finds arbitrary output-directory deletion, relative RAW symlink errors, macOS-floor verification gap, broad shader rebuild dependency | **Open.** These are outside product counts and unexecuted. Preserve existing outputs; test fixes only with isolated sentinels and temporary links when memory permits. |
| Actual Lightroom comparison | `2026-09-28-lightroom-baseline.md` defines criteria and a measurement plan | **Unmeasured.** No side-by-side Lightroom timing, output or workflow result; no parity claim. |

## Source-read boundary

The twelve bounded reports plus the earlier desktop/mask audits and the five-file
read below document a complete read of all **227 tracked first-party product
files at this checkpoint**. The final app and engine passes each supply an exact
roster for the former 21 and 15 targeted-only files. This is source coverage,
not a passed review of every behavior: the reports contain open findings,
none of their new paths was reproduced under the current memory pressure, and
new source changes require a fresh inventory. The generated image payload in
`web/js/planet-maps.js` was identified as data, not decoded or visually
reviewed; `DesignTokens.swift` was read and its generator inspected, but byte
reproducibility was not run.

Tests and tooling are inventoried separately, **not counted as product review**: 39 app Swift files under `ViewportTests*`, `Scenario*`, `Screenshot*` plus the new `BatchExportProbe.swift` (358 lines at `dc02241`, partially read); 35 `apps/tests` C++ sources/headers; 12 `apps/bench` C++ sources/headers; `mcp/server.test.ts` and `mcp/test/*`; 14 tracked source files under `tools/` (`.py`, `.sh`, `.cpp`); plus `apps/{pixstat,probe,rawstat}` utilities, `design/build-tokens.py`, build definitions and `repro/` scripts. Twelve build/packaging files were fully read in `2026-09-28-build-tooling-audit.md` (1,265 lines), and `design/tokens.json` plus `design/build-tokens.py` were read in the remaining-surface audit; all remain separate from product counts. Other assertions/gates were inspected only where stated. Exclusions: `third_party/SwiftTerm/**` (65 tracked vendored files), `web/js/vendor/**` (three vendored scripts), binary website assets/fonts and sample/data fixtures, untracked/ignored build and package output. Exclusion means outside this first-party manual source count, not a claim of security or license review.

A separate streaming line-count sweep of 311 tracked first-party
`.swift/.cpp/.h/.hpp/.mm/.c/.m/.slang` files found none at or above 1,000
lines; the largest was `ShaderParams.h` at 990. Vendored `third_party/` was excluded.
This checks the maintenance size limit, not source behavior or test coverage.

## New five-file read

Read all lines of `app/AdjustmentCatalogue.swift` (238), `AdjustmentGroup.swift` (180), `AdjustmentMath.swift` (24), `GalleryLayout.swift` (78), and `Navigator.swift` (39). Searched product callers in `app/` and read the relevant caller/test excerpts in `OrionApp+Commands.swift:387-397` and `ViewportTests+Gallery.swift:26-48`; did not read those entire caller/test files or run tests. The catalogue and group depend on engine defaults and panel context previously only targeted, so this read does not establish end-to-end slider correctness. No product code changed.

| Finding | Certainty and implication |
|---|---|
| `GalleryLayout.swift:47-49` says Right at a row end stays in that row, but `:63-64` advances from index 3 to 4 in a four-column grid. `OrionApp+Commands.swift:387-396` calls this for gallery focus; `ViewportTests+Gallery.swift:33-35` explicitly expects the transition. | **Definite stale comment, not a proven navigation defect.** Test and implementation agree; intended UX would need a product decision before changing behavior. Fix the comment when this file is next edited. |

No other finding is established from these five files alone. In particular, their static specs and arithmetic cannot prove the GPU output, panel accessibility, or interaction latency.
