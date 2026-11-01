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
 *  Pure CPU, threaded over rows; frames stay half floats throughout.
 *
 *  In pieces, so a stack need not hold every frame at once (#410): the
 *  reference's statistics (measureReference) and each frame's normalization
 *  (measureFrame) are taken while that frame is in memory; integrateRows then
 *  combines any band of rows from frames that hold only the rows rowsRead
 *  names. integrate() is the pieces in order over whole frames.
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
    std::uint32_t firstRow = 0;           ///< rgb starts at this row (a band); every row a tap reads must be held
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

/// v' = (v − L)/k + L_ref, weighted w.
struct Norm { float L[3] = {0, 0, 0}; float k = 1.0f, weight = 1.0f; };

/// What every frame is measured against: the reference's location, its statistics grid (sky only
/// when a matte gives enough sky) and its star cores with their 7x7 box excess.
struct Reference {
    std::uint32_t width = 0, height = 0, step = 1;
    float L[3] = {0, 0, 0};
    std::vector<std::size_t> grid, pick;
    std::vector<float> box;
};

/// The reference, whole (firstRow 0). Throws std::runtime_error on an empty one.
[[nodiscard]] Reference measureReference(const Frame& reference, const float* skyMatte);
/// One frame, whole, tapped through its refToSource at the reference's grid and cores.
[[nodiscard]] Norm measureFrame(const Frame& frame, const Reference& reference, bool weightByNoise);
/// No noise weight above 10x the median frame's (weightByNoise only).
void capWeights(std::vector<Norm>& norms);

/// Result rows [y0, y1) into `out` (row y0 first, 3 floats a pixel), every frame holding the rows
/// rowsRead names for its warps (frames[reference] those rows themselves: its own value fills a pixel
/// no frame covers).
void integrateRows(const std::vector<Frame>& frames, int reference, const float* skyMatte,
                   const Reference& ref, const std::vector<Norm>& norms, Rejection used,
                   std::uint32_t y0, std::uint32_t y1, float* out);

/// The source rows [first, last) that taps through `warp` read for result rows [y0, y1); the whole
/// frame when the warp's w is not positive over the band.
struct Rows { std::uint32_t first = 0, last = 0; };
[[nodiscard]] Rows rowsRead(const persp::Matrix3& warp, std::uint32_t width, std::uint32_t height,
                            std::uint32_t y0, std::uint32_t y1) noexcept;

/// skyMatte: nullptr, or reference width*height in 0..1 (1 = sky, frame coordinates).
/// Every frame must be the reference's size. A tap outside a frame contributes
/// nothing; a pixel no frame covers takes the reference's own value.
/// Throws std::runtime_error on an empty stack, a bad reference, or a size mismatch.
[[nodiscard]] Result integrate(const std::vector<Frame>& frames, int reference,
                               const float* skyMatte, const Options& options);

}  // namespace orion::stack
