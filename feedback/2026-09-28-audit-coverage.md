# Whole-repository audit coverage — 2026-09-28

Inventory of **tracked first-party source at `3094212`**, reconciled against the dated reports in `feedback/`, the five-file read below, and the complete read of `app/BatchExportSession.swift`. Later batch product and probe diffs were separately source-reviewed. `app/Screenshot+Measure.swift` has live product callers and is counted as product-used source; the other 39 app scene/test files are harness. This is a source coverage map, **not a completed whole-codebase behavioral audit**. A finding or a passing gate does not turn a targeted read into a full-file review. Counts exclude ignored build output and installed dependencies.

| Product source | Count | Full source/logic read documented | Targeted read documented | No documented full or targeted audit read |
|---|---:|---:|---:|---:|
| Desktop app and bridge (`app/*.swift`, `app/Bridge.h`; excluding 39 pure test/scene harness files) | 97 | 97 | 0 | 0 |
| Engine C++/ObjC++/headers (`engine/src`, `engine/include`) | 59 | 59 | 0 | 0 |
| First-party Slang shaders (`engine/shaders`, including `ops`) | 64 | 64 | 0 | 0 |
| MCP product server | 1 | 1 | 0 | 0 |
| Website HTML/CSS/JS, excluding vendored JS | 6 | 6 | 0 | 0 |
| Generated design-token Swift source | 1 | 1 | 0 | 0 |
| **Total** | **228** | **228** | **0** | **0** |

“Full” means a prior report explicitly says complete/full source or logic read, or this report read the entire file; it does **not** mean every behavior was tested. The desktop and mask reports, the five-file read below, and the controls, canvas/geometry, GPU/resource, core-shader, engine-contract, filter-support, assistant/agent, pyramid-shader, detail-shader, remaining-surface, app-complete and engine-complete reports supply the 227-file product roster at `dc02241`. The app-harness report then establishes that `Screenshot+Measure.swift` (322 lines) has live calls from `Engine+Render.swift`, `AgentFaces.swift` and `AgentInspect.swift`, making the product-used total **228**. This reclassifies an existing file; it is not a new tracked file. Overlapping reads count once. The website's embedded base64 map bytes were not decoded or visually reviewed, and generated-token reproducibility was not run. More detail: the dated audit reports in this directory.

| Objective | Evidence now | Status / missing proof |
|---|---|---|
| Whole first-party product-used source audit | 228 full, 0 targeted-only, 0 unread at this checkpoint | **Source-read inventory complete; objective still open.** Harness and tool source reads are now also documented below, but behavioral, performance, RAM, UX and competitor proof remain incomplete. A full-file read is not a clean bill of health. |
| Current repository gates | Full `-j2` build and all nine gates run at `3094212`; eight pass | **Open:** `check-modes.py` fails the batch Escape monitor check on the locked desktop (49/50 focused checks pass). See `2026-09-28-batch-export-safety.md`; this does not clear source-only findings in the other reports. |
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

The bounded product reports and the complete app-harness read document **228
product-used files fully read, zero targeted-only and zero unread**. The app and
engine passes supply exact rosters for the former 21 and 15 targeted-only
files. This is source coverage, not a passed review of every behavior: the
reports contain open findings, many paths still lack focused runtime proof, and
new source changes require a fresh inventory. The generated image payload in
`web/js/planet-maps.js` was identified as data, not decoded or visually
reviewed; `DesignTokens.swift` was read and its generator inspected, but byte
reproducibility was not run.

| Separate read boundary | Documented full reads | Not double-counted with |
|---|---:|---|
| Pure app scene/test harness | 39 files, 12,352 lines at `3094212` | `Screenshot+Measure.swift` (322 lines), included once in the 228 product-used files; the 40-file app-harness roster totals 12,674 lines. The app-harness report's 12,654-line roster is dated before the final 20-line `BatchExportProbe.swift` diff (358→378), which was separately read/reviewed |
| `apps/tests` core and mask/I/O harness | 35 files, 16,528 lines | Product source and the app harness |
| Benchmark, standalone diagnostic, MCP test, repro scenarios/index | 79 files, 8,308 lines | Product MCP server, app scenario interpreter, and build definitions; 60 scenario `.txt` files are scripts, not compiled source |
| `tools/` source | 14 files fully read: 12 checker/calibration/fixture files (2,490 lines) here, two packaging/worktree scripts in the prior build-tooling report | The separate 12-file build/packaging roster (1,265 lines) |
| Configuration and entry documents | 6 files, 468 lines | Product source and `mcp/server.ts`; see `2026-09-28-config-docs-audit.md` |
| Standalone darkroom design prototype | 1 HTML file, 1,028 lines | Shipped SwiftUI/AppKit product; see `2026-09-28-prototype-audit.md` |

The two generator inputs (`design/tokens.json`, `design/build-tokens.py`) were
also read, separately. This table counts documented reads, not successful
oracles or executed checks; overlaps between reports appear once per boundary.
Source-derived checker gaps include a benchmark A/B comparison that cannot
fail its gate, nonfinite pixel reductions that can pass GPU tests, and fixed
`/tmp` fixtures that can replace unrelated files. See the five new dated
reports for precise triggers and missing proof. Exclusions remain explicit:
`third_party/SwiftTerm/**` (65 tracked vendored files), `web/js/vendor/**`
(three vendored scripts), binary website assets/fonts and sample/data
fixtures, and untracked/ignored build and package output. No claim is made
about their behavior, security or licenses.

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
