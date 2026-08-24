/*  Alignment — the homography from the reference frame to another frame of a
 *  handheld bracket.
 *
 *  Estimation lives behind this header so OpenCV lives behind it too:
 *  Align.cpp is the ONE translation unit in the tree that includes
 *  <opencv2/...>, and this header speaks only Orion types. The dependency is
 *  the mature-library rule applied to a solved problem — feature matching
 *  and robust estimation are not code a solo project should own — and the
 *  result comes back in the same persp::Matrix3 dest->source convention the
 *  geometry shader and the merge already share, so the two can never
 *  disagree about which way a warp points.
 *
 *  Method (sources in research/hdr-merge.md): ORB features [RRKB11] matched
 *  by Hamming distance on exposure-normalized log-luma proxies at quarter
 *  resolution, homography by RANSAC [FB81], refined by ECC [EP08] when it
 *  converges. ORB is patent-free by design, which is why it is the choice.
 *
 *  Failure is a first-class result: too few features (sky, fog), a wild
 *  estimate (a warp no handheld bracket produces) or non-convergence all
 *  come back ok = false with an identity matrix — the merge then proceeds
 *  reference-only rather than warping garbage into the picture.
 */

#pragma once

#include "merge/Merge.h"

namespace orion::merge {

struct AlignResult {
    /// Reference texel -> frame texel, full resolution. Identity when !ok.
    persp::Matrix3 refToSource;
    int  inliers = 0;
    bool ok = false;
};

/// Estimates frame's position against the reference. Both frames are the
/// merge's own currency — scene-linear camera RGB with exposure ratios —
/// and the exposure difference is normalized away before any feature is
/// looked for. `refToSource` on the inputs is ignored.
[[nodiscard]] AlignResult align(const Frame& frame, const Frame& reference);

}  // namespace orion::merge
