# Detail shader source audit — 2026-09-28

Source-only review of the **23 assigned product shaders / 1,961 lines**, fully read at the current worktree. The findings below are arithmetic and caller traces, **not observed renders**. Kernel memory pressure warning 2 barred builds, compilers, apps, GPU checks, benchmarks and memory stress. No product source or planning file changed; no fresh latency, RSS, GPU result or nine-gate claim follows.

## Ranked findings

| Rank | Reachable trigger and source | Consequence | Smallest repair and missing check |
|---|---|---|---|
| **P2 — RCD swaps the missing color axis on a Bayer border** | For an even-width RGGB frame, the last pixel of an even row is green. `engine/shaders/rcd_rb.slang:65-66` asks the **clamped east pixel** whether horizontal neighbors are red. At the right edge `clampToImage` returns the current green pixel, so `redIsHorizontal` becomes false, although the west neighbor is red. `:58-73` then assigns the vertical blue difference to red and the horizontal red difference to blue. The same clamped east tap also contaminates the horizontal average. Product path: `engine/src/pipe/DevelopCapture.cpp:103-111` dispatches this after green reconstruction for Bayer RAW. | The rightmost green column can have wrong red/blue values, yielding a one-pixel color fringe; the exact rendered magnitude depends on the frame. For constant Bayer samples R=0.8, G=0.4, B=0.2, this branch uses the blue axis for red and averages one red difference with the green center for blue. `apps/tests/tests_color.cpp:247-265` pins CFA indexing, not reconstructed border colors; `tests_merge_render.cpp` explicitly samples an interior pixel. | Determine the axis from CFA parity at the **logical** neighbor (or the valid west neighbor), independent of fetch clamping. At an absent neighbor, reuse the valid opposite same-color tap instead of the center's different CFA color. Add one small GPU check of an even-width constant-color RGGB frame's last green column, plus the mirrored pattern/edge case. Verify through the real RCD graph before closing. |
| **P2 — grain changes the mean of saturated colors** | With Amount above zero, `engine/shaders/grain.slang:118-130` derives nonzero `sigma` from **luma** and adds one signed scalar to all RGB channels, then `saturate`s each channel. A pure red patch `(1,0,0)` has luma 0.2126 and hence nonzero sigma: positive grain clips at R=1 while lifting G/B; negative grain clips at G/B=0 while lowering R. `engine/src/pipe/DevelopOutput.cpp:381-393,621-649` enables this kernel for the product Amount control. | Even a zero-mean plate cannot stay zero-mean after these asymmetric per-channel clips. A saturated red/blue patch can lose saturation or shift hue as Amount rises. The claim in `research/film-grain.md` §The weighting and decision #81 that sigma vanishes wherever clipping can bite holds for neutral pixels at luma endpoints, not for saturated RGB at interior luma. Existing `apps/tests/tests_grain.cpp` checks mid-gray and a neutral ramp; its mean test cannot see this case. No actual shift magnitude was measured here. | First add a GPU mean/saturation check on flat saturated red and blue, alongside the neutral control. Then bound the shared monochrome offset by the available headroom of **all three** channels before adding it, or choose a measured color-preserving rule and document its deviation in `research/` / `UNSOURCED.md`. Keep the existing neutral grain amplitude and preview agreement checks. |

No separate performance or RAM defect is established by this pass. The four full-resolution denoise blur passes (`denoise_blur.slang:29-50`) read 25 taps each when enabled, and `spot_apply.slang:40-82` scans every active spot per output pixel, but this is the documented graph design, not a fresh measured regression. Grain Amount 0 disables its node through `retargetOutputChain`; treating it as always active would overstate cost. The existing mask report owns mask-composition and refinement-chain behavior; none of its eight findings is repeated here.

## Exact full-read inventory

Counts are `wc -l` source lines. Every path has prefix `engine/shaders/`.

| File | Lines | File | Lines | File | Lines |
|---|---:|---|---:|---|---:|
| `cfa.slang` | 45 | `denoise_accum.slang` | 87 | `denoise_blur.slang` | 50 |
| `geometry.slang` | 134 | `grain.slang` | 131 | `hl_apply.slang` | 149 |
| `hl_mask.slang` | 120 | `hl_pull.slang` | 92 | `hl_push.slang` | 81 |
| `lens.slang` | 119 | `mask_base.slang` | 34 | `mask_guide_ab.slang` | 57 |
| `mask_guide_apply.slang` | 78 | `mask_guide_prep.slang` | 69 | `ops/grain_ops.slang` | 96 |
| `ops/rcd_stat.slang` | 60 | `rcd_dirs.slang` | 45 | `rcd_green.slang` | 96 |
| `rcd_lpf.slang` | 47 | `rcd_rb.slang` | 115 | `sharpen.slang` | 85 |
| `spot_apply.slang` | 85 | `spot_measure.slang` | 86 | **Total: 23** | **1,961** |

**Targeted only, not full-read coverage:** `engine/src/pipe/DevelopCapture.cpp` (graph construction, spot/sharpen parameter pushes); `DevelopOutput.cpp` (grain graph and enable/format/parameter path); `apps/tests/tests_color.cpp` (CFA oracle), `tests_merge_render.cpp` (interior demosaic oracle), `tests_grain.cpp` (neutral/ramp/preview checks), `tests_spot.cpp` (clone/heal check), and `tests_mask_matte.cpp` (guided-mask check locations). Caller search also checked `engine/shaders` uses of `ops/grain_ops.slang`; it has no current include or product call, so its older comments are not used as the live grain contract.

Research and settled context read: `research/{README,demosaic,detail,highlight-reconstruction,lens-corrections,film-grain,masking,spot-removal,UNSOURCED}.md`, relevant `planning/{STATUS,VISION,DECISIONS,ARCHITECTURE,ROADMAP,FEATURES,RESEARCH,UI-DECISION}.md`, `feedback/README.md`, and the current mask, core-shader, filter-support and coverage reports. The RCD implementation is the MIT-licensed Rodríguez reference port per `research/demosaic.md`; this report proposes only a border handling fix, with no GPL source or unlicensed algorithm replacement. The grain proposal is a bounded correction to the existing cited model, pending a measured render and documentation of any changed behavior.
