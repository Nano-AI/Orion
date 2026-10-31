/*  Stack rejection — the per-pixel outlier rejection every stack mode calls.
 *
 *  For one pixel of one channel, N normalized values arrive (one per frame)
 *  with a weight each; `combine` throws out the ones that disagree with the
 *  rest (a satellite trail, a hot or dead pixel, a passing bird) and returns
 *  the weighted mean of the survivors. Pure maths, no I/O: the stack driver
 *  that feeds it frames is another file.
 *
 *  Sources (full table in research/stacking.md):
 *    Percentile  — [SIRIL] documentation, one-step clipping for small sets.
 *    Sigma       — [SIRIL] documentation, iterated clipping about the median.
 *    Winsorized  — [HR09] Huber & Ronchetti, Robust Statistics 2e: clamp to
 *                  c·sigma before estimating sigma, divide by the consistency
 *                  factor 1/sqrt(E[min(|z|,c)^2]) = 1.134 at c = 1.5 (recomputed
 *                  in research/stacking.md §2); [PI] for its use in stacking.
 *                  That sigma is estimated once, on the full sample; the
 *                  rounds that follow only re-centre the median.
 *    LinearFit   — [PI] linear-fit clipping; Orion's fit is a plain
 *                  least-squares line of value on rank.
 *  Implemented from those descriptions; no GPL source was read.
 *
 *  Sigma is manual only: autoRejection never picks it (see research/stacking.md §2).
 *
 *  ⚠ The frame-count thresholds in autoRejection and the percentile bounds
 *  (0.2 low, 0.1 high) are ours, not published — research/UNSOURCED.md §32.
 */

#pragma once

namespace orion::stack {

/// The integers are the C facade's; do not renumber.
enum class Rejection { Auto = -1, None = 0, Percentile = 1, Sigma = 2, Winsorized = 3, LinearFit = 4 };

/// The method research/stacking.md §2 recommends for `frames` frames.
[[nodiscard]] Rejection autoRejection(int frames) noexcept;

/// Weighted mean of the values that survive `r` (Auto picks by n). Reorders v
/// and w together in place. n >= 1 (n < 1 returns NaN); w all > 0. Rejection stops before fewer
/// than 3 values would survive: a pass that would leave fewer is not applied.
/// With n < 3 every method is the plain weighted mean. Allocation-free.
[[nodiscard]] float combine(float* v, float* w, int n, Rejection r) noexcept;

}  // namespace orion::stack
