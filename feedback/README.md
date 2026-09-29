# feedback/

Every critique of this repository, and the investigations that came out of them.

Kept together, and kept in the repository rather than in a chat log, for one
reason: **the criticism should be as findable as the plan.** `planning/` says
what Orion is meant to be; this says where it is not that yet, who noticed, and
what the measurement showed.

Nothing here is deleted when it is fixed. A finding that was closed still records
how a defect got past every test — which is usually the more useful half.

## Contents

| File | What it is |
|---|---|
| `2026-09-28-verification.md` | Integrated post-merge build and nine passing gates, including the initial agent fixture failure, its correction, and memory limits. |
| `2026-09-28-memory-retention.md` | Reduced-resolution controlled comparison of disabled GPU output release and idle pool shrink; export output regression and coverage. |
| `2026-09-28-repository-cleanup.md` | Confirmed generated artifacts removed; originals, sidecars and website assets checked. |
| `2026-09-28-masking-audit.md` | Eight ranked, source-traced mask correctness/performance findings and coverage limits. |
| `2026-09-28-desktop-io-audit.md` | Nine ranked desktop, sidecar, batch and library findings; five pure Swift probes plus post-upstream source revalidation. |
| `2026-09-28-engine-io-audit.md` | Engine file, HDR and C facade findings with evidence and untested failure paths. |
| `2026-09-28-lightroom-baseline.md` | Source-backed comparison criteria and measurement plan; no invented Lightroom benchmark result. |
| `2026-09-28-audit-coverage.md` | Exact first-party source coverage and still-open performance, RAM, UX, masking, file and Lightroom verification. |
| `2026-09-28-app-harness-audit.md` | All 40 app scene/test files read; one product-used measurement file, 39 pure harness files, and source-only harness findings. |
| `2026-09-28-core-tests-audit.md` | Complete core GPU/math test-harness read, including oracle and fixture-safety gaps. |
| `2026-09-28-mask-io-tests-audit.md` | Complete mask/I/O test-harness read; fixed `/tmp` fixtures and blind HDR alignment pixels are open. |
| `2026-09-28-bench-mcp-repro-audit.md` | Benchmark, MCP test, standalone diagnostic and repro-script reads; A/B benchmark and test-oracle gaps. |
| `2026-09-28-checker-tools-audit.md` | Remaining checker/calibration tool reads; timeout, sample-preservation and calibration-evidence gaps. |
| `2026-09-28-config-docs-audit.md` | Six configuration and entry-document reads; dependency, proxy guidance and stale-count findings. |
| `2026-09-28-prototype-audit.md` | Complete 1,028-line design prototype read; findings apply to the prototype, not product UI. |
| `2026-09-28-controls-audit.md` | Complete read of 17 develop controls/panels; lens state/undo, curve first-tick and keyboard findings. |
| `2026-09-28-canvas-geometry-audit.md` | Complete read of ten canvas/geometry files; spot redo, Compare sampling and histogram findings. |
| `2026-09-28-core-shader-audit.md` | Complete read of 13 core shaders; grading luminance and optional roll-off findings, source-only. |
| `2026-09-28-gpu-resource-audit.md` | Complete read of ten GPU/resource and pipeline files; lifetime trace and measurement limits. |
| `2026-09-28-engine-contracts-audit.md` | Complete read of ten engine API/merge/raw/writer contract files; CFA noise and early HDR Stop findings, source-only. |
| `2026-09-28-filter-support-audit.md` | Complete read of 23 engine filter-support files; LUT input bounds/fidelity and developer override findings, source-only. |
| `2026-09-28-assistant-agent-audit.md` | Eight complete assistant/agent reads, seven newly counted; proposed-edit type, commit, photo identity and state/flag contract findings, source-only. |
| `2026-09-28-pyramid-shader-audit.md` | Complete read of 26 dehaze, local contrast and fusion shaders; two CPU reduction cache-invalidation findings, source-only. |
| `2026-09-28-detail-shader-audit.md` | Complete read of 23 detail and imaging shaders; RCD Bayer border and saturated-grain findings, source-only. |
| `2026-09-28-remaining-surface-audit.md` | Remaining app, generated-token and website source reads; replay, watermark, accessibility and stale-site findings, source-only. |
| `2026-09-28-build-tooling-audit.md` | Twelve build/packaging files read separately from product counts; output deletion, sample link, macOS floor and shader dependency findings, source-only. |
| `2026-09-28-app-complete-audit.md` | Complete read of the remaining 21 targeted app files; Trash, spot diff, menu count, preset deletion and failed selection findings, source-only. |
| `2026-09-28-engine-complete-audit.md` | Complete read of the remaining 15 targeted engine files; LUT lifecycle, tone-curve input and writer lifetime findings, source-only. |
| `2026-09-28-batch-export-safety.md` | Batch export safety evidence: eight gates pass; the locked desktop blocks key-window Escape proof, and other file-handling gaps remain open. |
| [Batch branch review](2026-09-28-batch-branch-review.md) | 2026-09-28 | Independent full-branch source review: no additional Critical/Important source defect; integration waits for actual key-window mutation proof. |
| `2026-09-24-compare-overwrites-color.md` | Old installed app reproduced overwriting saved color edits when Compare opens; reinstalled #271 fix preserves them. Recovery limits recorded. |
| `2026-09-15-engine-ui-audit.md` | Paired fusion optimization, desktop interaction fixes, nine-gate results and remaining exposure, memory and UI-coverage gaps (#271). |
| `2026-09-15-website-audit.md` | Website interaction/fallback bugs, responsive polish and local browser verification on `web/site-audit-polish`. |
| `2026-07-28-senior-review.md` | Outside senior review of the whole repository. 17 findings, ranked, each with file:line evidence and a concrete fix. The three P1s and finding 4 are closed; 6–14 are open. |
| `2026-07-28-performance-and-quality.md` | Self-assessment written for a reviewer who has not seen the repository: what to run, the latency table, how correctness is defended, and a plainly stated list of known weaknesses. Contains corrections to its own earlier claims. |
| `2026-07-28-colour-investigation.md` | The purple sky. Full thread: measurements against three independent renderers, what was ruled out and how, two outside AI reviews that corrected the approach, the licensing analysis, and what is still undecided. |
| `2026-07-28-colour-investigation-response.md` | Outside review of the colour investigation, with the web research the original session ran out of budget for. Confirms the diagnosis against the DNG spec and profiling literature; adds two findings the plan needed: the HueSatMap must apply in linear ProPhoto HSV, and the 1.3× darkness matches the DNG BaselineExposure mechanism. |

## How to read these

Start with the senior review — it is the widest net. The quality doc is the
self-assessment it was checking, including the places where the self-assessment
was too kind. The colour investigation is the deepest single thread and is the
best example of the pattern that keeps recurring here:

> The code was fine wherever it was measured.

Every serious defect in this project so far has lived in a state nobody pointed
an instrument at. A purple sky survived two test suites, a benchmark and a full
review because the entire sample corpus was two night frames.

## Where the fixes live

Findings are closed in code, and the reasoning is recorded in:

- `planning/DECISIONS.md` — every settled choice, numbered, with its reason
- `planning/HISTORY.md` — the archived session log, 50 sessions and counting
- `planning/STATUS.md` — current state plus the recent session log, including corrections to overstated
  commit messages
- `research/UNSOURCED.md` — the honest register of what is our own formulation
  rather than a published algorithm
- `research/*.md` — the algorithm sources themselves
