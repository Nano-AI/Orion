/*  Star registration — find the stars in a frame, and the homography that
 *  carries the reference's stars onto them. The core of the Stars and
 *  Landscape stack modes: stars have no texture, so the ORB matcher in
 *  merge/Align.* has nothing to bite on, but the *shapes* the stars make
 *  survive any rotation, shift and scale.
 *
 *  Sources (research/stacking.md §4-5):
 *    Detection — [BA96] Bertin & Arnouts, SExtractor (background, threshold,
 *                connected pixels, centroid); [S87] Stetson, DAOPHOT (smooth
 *                before thresholding).
 *    Matching  — [G86] Groth and [V95] Valdes et al. (triangle side-ratio
 *                invariants); [BCS20] Beroiz et al., Astroalign (each star's
 *                4 nearest neighbours, C(5,3) triangles, match radius 0.1).
 *    Fit       — [FB81] Fischler & Bolles, RANSAC, through OpenCV, of a
 *                homography: the sky turning over a tripod is a pure
 *                rotation, H = K·R·K⁻¹ ([HZ04] Hartley & Zisserman §8.4.2).
 *  Implemented from those descriptions; no GPL source was read.
 *
 *  As with merge/Align.h, OpenCV lives behind this header: only StarAlign.cpp
 *  includes it.
 */

#pragma once

#include "merge/Align.h"

#include <cstdint>
#include <vector>

namespace orion::stack {

struct Star { float x = 0, y = 0, flux = 0; };

/// Packed float RGB (luma = mean of the three). mask: optional w*h, a star counts only where mask >= 0.5.
/// Brightest first, at most maxStars.
[[nodiscard]] std::vector<Star> detectStars(const float* rgb, std::uint32_t width, std::uint32_t height,
                                            const float* mask = nullptr, int maxStars = 60);

/// Homography mapping reference star coordinates to frame coordinates (refToSource). ok=false, identity, below 8 inliers.
[[nodiscard]] merge::AlignResult alignStars(const std::vector<Star>& frame, const std::vector<Star>& reference);

}  // namespace orion::stack
