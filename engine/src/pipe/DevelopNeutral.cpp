// The white balance eyedropper and Auto's grey world: what the last render's
// reference image says the white balance should have been. research/white-balance.md.

#include "pipe/DevelopPipeline.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace orion::pipe {

std::optional<WhiteBalance> DevelopPipeline::neutralWhiteBalance(float fx, float fy, int radius) const {
    if (width_ == 0 || height_ == 0) return std::nullopt;
    std::uint32_t x0 = 0, y0 = 0, w = width_, h = height_;
    if (radius >= 0) {
        const auto at = [](float f, std::uint32_t n) {
            return int(std::lround(std::clamp(f, 0.0f, 1.0f) * float(n - 1)));
        };
        const int cx = at(fx, width_), cy = at(fy, height_);
        x0 = std::uint32_t(std::max(cx - radius, 0));
        y0 = std::uint32_t(std::max(cy - radius, 0));
        w = std::min(std::uint32_t(cx + radius + 1), width_) - x0;
        h = std::min(std::uint32_t(cy + radius + 1), height_) - y0;
    }
    std::vector<__fp16> px(std::size_t(w) * h * 4);
    referenceImage().downloadRect(px.data(), std::size_t(w) * 4 * sizeof(__fp16), x0, y0, w, h);

    // The reference is working RGB after the profile's hue/saturation table,
    // which leaves a neutral neutral; the camera matrix's rows sum to one, so
    // its inverse takes the patch back to the white-balanced camera RGB the
    // gains were applied to. The table moves a near-neutral a little, which is
    // why Engine::whiteBalanceAt solves again on its own result.
    const auto toCamera = huesat::invert(camToWorking_);
    const float clip = lastWhiteClip_ > 0.0f ? 0.95f * lastWhiteClip_ : INFINITY;
    double sum[3] = {0, 0, 0};
    std::size_t kept = 0;
    for (std::size_t i = 0; i < std::size_t(w) * h; ++i) {
        float cam[3];
        for (int r = 0; r < 3; ++r)
            cam[r] = toCamera[r * 3] * float(px[i * 4]) + toCamera[r * 3 + 1] * float(px[i * 4 + 1]) +
                     toCamera[r * 3 + 2] * float(px[i * 4 + 2]);
        if (std::max({cam[0], cam[1], cam[2]}) >= clip || std::min({cam[0], cam[1], cam[2]}) <= 1e-4f) continue;
        for (int c = 0; c < 3; ++c) sum[c] += cam[c];
        ++kept;
    }
    // A patch must be mostly usable; the whole frame needs only some of it to be.
    if (kept == 0 || kept * (radius >= 0 ? 2 : 100) < std::size_t(w) * h) return std::nullopt;
    const std::array<float, 3> mean{float(sum[0] / double(kept)), float(sum[1] / double(kept)),
                                    float(sum[2] / double(kept))};
    return neutralFor(mean, denoiseMultipliers(lastAdj_), asShotMul_, asShotRef_, xyzToCam_);
}

}  // namespace orion::pipe
