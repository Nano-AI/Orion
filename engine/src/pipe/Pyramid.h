/*  Gaussian and Laplacian pyramids on the CPU, in the conventions the shaders
 *  use.
 *
 *  Burt & Adelson (1983), 5x5 kernels — the pyramid Paris et al. name in
 *  section 5.1 of the local Laplacian paper, and the one Mertens et al. blend
 *  over in exposure fusion. Two features now build on it, which is why it lives
 *  here rather than inside either of them.
 *
 *  These exist to be the *reference* the GPU is measured against, so they must
 *  stay in step with llf_down.slang and llfExpand1D in ops/llf_ops.slang. Any
 *  expand operator reconstructs exactly so long as analysis and synthesis share
 *  it; these are the ones those shaders imply, which is what makes a comparison
 *  coefficient by coefficient meaningful.
 */

#pragma once

#include <algorithm>
#include <cstddef>
#include <vector>

namespace orion::pipe::pyr {

struct Plane {
    int width = 0, height = 0;
    std::vector<float> v;

    [[nodiscard]] float at(int x, int y) const noexcept {
        return v[static_cast<std::size_t>(std::clamp(y, 0, height - 1)) * width +
                 std::clamp(x, 0, width - 1)];
    }
};

[[nodiscard]] inline int nextSize(int n) noexcept { return std::max(1, (n + 1) / 2); }

[[nodiscard]] inline Plane downsample(const Plane& in) {
    static constexpr float k[5] = {0.0625f, 0.25f, 0.375f, 0.25f, 0.0625f};
    Plane out{nextSize(in.width), nextSize(in.height), {}};
    out.v.resize(static_cast<std::size_t>(out.width) * out.height);
    for (int y = 0; y < out.height; ++y) {
        for (int x = 0; x < out.width; ++x) {
            float sum = 0.0f;
            for (int dy = -2; dy <= 2; ++dy)
                for (int dx = -2; dx <= 2; ++dx)
                    sum += k[dy + 2] * k[dx + 2] * in.at(2 * x + dx, 2 * y + dy);
            out.v[static_cast<std::size_t>(y) * out.width + x] = sum;
        }
    }
    return out;
}

/// One axis of Burt & Adelson's expand, split by parity. Mirrors llfExpand1D.
inline void expandTaps(int x, int c[3], float w[3]) noexcept {
    const int h = x >> 1;
    if ((x & 1) == 0) {
        c[0] = h - 1; c[1] = h; c[2] = h + 1;
        w[0] = 0.125f; w[1] = 0.75f; w[2] = 0.125f;
    } else {
        c[0] = h; c[1] = h + 1; c[2] = h + 1;
        w[0] = 0.5f; w[1] = 0.5f; w[2] = 0.0f;
    }
}

[[nodiscard]] inline Plane expand(const Plane& in, int width, int height) {
    Plane out{width, height, {}};
    out.v.resize(static_cast<std::size_t>(width) * height);
    for (int y = 0; y < height; ++y) {
        int cy[3]; float wy[3];
        expandTaps(y, cy, wy);
        for (int x = 0; x < width; ++x) {
            int cx[3]; float wx[3];
            expandTaps(x, cx, wx);
            float sum = 0.0f;
            for (int b = 0; b < 3; ++b)
                for (int a = 0; a < 3; ++a)
                    sum += wx[a] * wy[b] * in.at(cx[a], cy[b]);
            out.v[static_cast<std::size_t>(y) * width + x] = sum;
        }
    }
    return out;
}

[[nodiscard]] inline std::vector<Plane> gaussianPyramid(const Plane& img, int levels) {
    std::vector<Plane> p;
    p.reserve(static_cast<std::size_t>(levels));
    p.push_back(img);
    for (int l = 1; l < levels; ++l) p.push_back(downsample(p.back()));
    return p;
}

}  // namespace orion::pipe::pyr
