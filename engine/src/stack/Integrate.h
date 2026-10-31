/*  Stack integration — N registered frames into one linear image.
 *
 *  Per frame, once: an additive location and a multiplicative scale against
 *  the reference, measured on registered pixel pairs (a frame taken through
 *  thin cloud or brightening sky glow lines up with the rest), and a noise
 *  estimate for the optional inverse-variance weight. Then per pixel and
 *  channel: tap every frame through its warp, normalize, and hand the values
 *  to `combine` (Rejection.h). With a sky matte, the sky and the ground are
 *  integrated through their own warps and blended by the matte, so the stars
 *  stay points and the horizon stays sharp.
 *
 *  Sources (research/stacking.md §1, §3):
 *    Pipeline order, additive + scale normalization with median/MAD
 *    estimators, noise weighting — [SIRIL] documentation.
 *    One scalar scale between two linear frames from pixel pairs — [GN03],
 *    specialized to a linear response.
 *    Weight (k/σ)² is inverse variance after scaling.
 *  Implemented from those descriptions; no GPL source was read.
 *
 *  The scale is measured on 7x7 box sums of luma about the reference's bright
 *  local maxima (star cores), found at full resolution: a sub-pixel bilinear
 *  tap flattens a star's peak but nearly conserves its flux.
 *
 *  ⚠ Ours, not published (research/UNSOURCED.md §32): the 10σ bright-pixel
 *  floor (and 10σ minimum box excess), the 50-core minimum for the scale, the
 *  0.98 clip that drops a box, the ~50k-sample grid and
 *  pair cap, the 7x7 box at local maxima, the [0.1, 10] range a scale must fall in, the 10x
 *  median cap on a noise weight, the matte >= 0.5 sky subset for the
 *  statistics, the 1e-6 noise floor.
 *
 *  Pure CPU, threaded over row bands; frames stay half floats throughout.
 */

#pragma once

#include "pipe/Perspective.h"
#include "stack/Rejection.h"

#include <cstdint>
#include <vector>

namespace orion::stack {

namespace persp = orion::pipe::persp;

struct Frame {
    std::uint32_t width = 0, height = 0;
    const std::uint16_t* rgb = nullptr;   ///< half-float RGB, packed, own-normalized (1.0 = clip)
    persp::Matrix3 refToSource;           ///< the sky's (or the whole frame's) warp; identity for the reference
    persp::Matrix3 groundToSource;        ///< used only where a sky matte is given and < 1
};

struct Options {
    Rejection rejection = Rejection::Auto;
    bool weightByNoise = false;           ///< false: every weight is exactly 1
};

struct Result {
    std::vector<float> rgb;               ///< reference width*height*3, linear, >= 0
    Rejection used = Rejection::None;     ///< Auto resolved by frame count
    std::vector<float> scale;             ///< k_i per frame (v' = (v − L_i)/k_i + L_ref); 1 for the reference
};

/// skyMatte: nullptr, or reference width*height in 0..1 (1 = sky, frame coordinates).
/// Every frame must be the reference's size. A tap outside a frame contributes
/// nothing; a pixel no frame covers takes the reference's own value.
/// Throws std::runtime_error on an empty stack, a bad reference, or a size mismatch.
[[nodiscard]] Result integrate(const std::vector<Frame>& frames, int reference,
                               const float* skyMatte, const Options& options);

}  // namespace orion::stack
