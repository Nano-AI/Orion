# Pyramid and local-operator shader audit — 2026-09-28

Source-only review of the 26 shaders listed below, their relevant host wiring and existing checks. No build, compiler, application, test, GPU, network or install operation ran under host memory pressure. No product code changed. Findings are path-dependent cache errors established by the source graph; their image magnitude, timing and RAM impact were **not measured**.

## Findings

| Rank | Trigger and source evidence | Consequence and smallest repair/check |
|---|---|---|
| **P1 — atmospheric light remains stale after an upstream edit** | With dehaze enabled, change `highlightRecovery` on a RAW frame whose restored highlights affect the airlight candidates. `DevelopCapture.cpp:479-567` changes the highlight and fill nodes; `:118-259` places them before `nHueSat_`. The dehaze candidate shader reads that output (`DevelopLocal.cpp:44-50`; `dehaze_peak.slang:34-60`). Yet `applyDehaze` clears `airlightValid_` only on first apply or a white-balance change (`DevelopLocal.cpp:326-335`), and `render()` estimates A only if that flag is false (`DevelopPipeline.cpp:514-522`). | The graph updates the pixels and candidate texture but uses the old airlight in `dehaze_chan.slang:27-35` and `dehaze_recover.slang:60-66`; the same final edit can render differently depending on edit order. Clear A's validity for **every adjustment that changes `nHueSat_`**, including highlight recovery, denoise, lens, spot and sharpening changes, without invalidating it for downstream tone edits or the dehaze strength alone. Reuse the existing flag and two-pass reduction. Add one real-pipeline order-independence GPU check: warm dehaze, change a candidate-affecting upstream control, then compare its output to a fresh pipeline applied directly to the final state. Use a synthetic bright candidate that the chosen control actually changes. |
| **P1 — fusion's median-derived plan remains stale after an upstream edit** | With fusion enabled, change dehaze or clarity, keeping white balance and fusion strength fixed. Fusion's proxy reads `nClarity_` (`DevelopLocal.cpp:182-201`), and `fuse_proxy.slang:49-75` computes its mapped luminance. The plan is computed from that proxy's median (`DevelopLocal.cpp:634-653`), yet `applyFusion` clears `fusePlanValid_` only on first apply or a white-balance change (`:434-442`). `render()` therefore skips `estimateFusionPlan()` (`DevelopPipeline.cpp:514-522`) while the graph recomputes its proxy and split shaders with the previous plan (`fuse_split.slang:31-54`). | A final dehaze/clarity/fusion state can use a plan selected for an earlier frame, so edit order can change the rendered fusion. Invalidate the plan for changes to **any stage upstream of `nFuseProxy_`** that can move its median, including dehaze and clarity, while preserving the existing fast path for a fusion-strength drag. Check warm sequence A→B against a fresh B pipeline on a synthetic frame whose mapped median crosses a plan boundary; assert both plan selection and output bytes, and retain the named-node warm-strength invariant. |

These are two instances of the same root cause: the DAG dirties dependent textures, while its CPU reductions keep independent validity flags. Invalidation must follow the reduction input's dependency set. The line references above name exact paths, not a claim that every listed adjustment visibly changes every photograph.

## Performance and numerical disposition

- **No new timing or RAM finding.** `dehaze_rank.slang` already runs separable 15-tap min/max passes; `research/dehaze.md` records their measured cost and the unattempted van Herk/Gil-Werman alternative. `research/local-laplacian.md` records the measured 70→58 ms separable remap trade and the failed branchy collapse optimization. Recommending either change without a new paired measurement would repeat settled work.
- Source allocations during the whole-frame reductions are visible in `DevelopLocal.cpp:634-646,672-685`: the fusion proxy is downloaded to half floats and copied to floats for `nth_element`; pooled dehaze candidates are downloaded to half floats and copied to `Candidate` objects. This is transient payload, not measured process footprint, and the existing 42 MP active-filter capacity gap remains open.
- The shaders clamp dark-channel division, transmission and fusion gain (`dehaze_chan.slang:29-35`, `dehaze_recover.slang:60-66`, `fuse_apply.slang:70-77`). This source read did not establish a distinct finite-input NaN, overflow or out-of-bounds path. It cannot certify full image behavior without GPU renders.
- **Existing finding left intact:** decision #227 measured a 61–95 px fusion halo already present in the proxy pyramid and falsified guided coefficient lifting as a fix. This audit does not count it again. The earlier masking and GPU-resource audits own their respective mask and pool findings.

## Coverage and exact full-read roster

Counts are `wc -l` at review time. These are 26 **complete shader reads / 1,537 lines**; host and check files named above were **targeted reads only** and must not be promoted to full-file coverage.

| Shader (`engine/shaders/`) | Lines |
|---|---:|
| `box_blur.slang` | 40 |
| `box_blur4.slang` | 37 |
| `dehaze_ab.slang` | 37 |
| `dehaze_chan.slang` | 37 |
| `dehaze_peak.slang` | 62 |
| `dehaze_prep.slang` | 72 |
| `dehaze_rank.slang` | 46 |
| `dehaze_recover.slang` | 69 |
| `guide_ab.slang` | 47 |
| `guide_down.slang` | 56 |
| `guide_prep.slang` | 43 |
| `llf_apply.slang` | 36 |
| `llf_collapse.slang` | 89 |
| `llf_collapse0.slang` | 87 |
| `llf_down.slang` | 40 |
| `llf_down4.slang` | 39 |
| `llf_down_v.slang` | 34 |
| `llf_luma.slang` | 44 |
| `llf_remap.slang` | 67 |
| `llf_remap_h.slang` | 55 |
| `ops/llf_ops.slang` | 93 |
| `fuse_apply.slang` | 77 |
| `fuse_blend.slang` | 107 |
| `fuse_proxy.slang` | 77 |
| `fuse_split.slang` | 56 |
| `ops/fuse_ops.slang` | 90 |
| **Total** | **1,537** |

Targeted caller/check reads: `engine/src/pipe/DevelopLocal.cpp` (operator graphs, params, both reductions), `DevelopPipeline.cpp` (apply/render), `DevelopPipeline.h` (state and dimensions), `DevelopCapture.cpp` (capture ancestry and highlight edits), `Dehaze.h`, `ExposureFusion.h`, `LocalLaplacian.h`, `apps/tests/tests_effects.cpp` (local Laplacian and dehaze GPU cases), `apps/tests/tests_pipeline.cpp` (edit/cache cycle), and `apps/bench/bench_compose.cpp`, `bench_invariants.cpp`, `bench_fusion.cpp` (existing invariants). Existing tests cover isolated shader math and some white-balance re-enablement; they do not compare an unchanged final state reached through dehaze/clarity/highlight edit orders. The benchmarks count dispatches or image sanity, not these CPU reduction identities.

Context read before conclusions: `planning/STATUS.md`, relevant `DECISIONS.md` and `ARCHITECTURE.md` entries; `research/dehaze.md`, `local-laplacian.md`, `exposure-fusion.md`; `feedback/README.md`, the core-shader, filter-support, GPU-resource, masking and memory-retention audits. The published methods remain as cited in those research files; no new algorithm or GPL-source implementation is proposed. After a repair, run the focused real-GPU order check and Orion's required nine gates when host memory permits.
