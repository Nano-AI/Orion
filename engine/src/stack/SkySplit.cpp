/*  SkySplit — see the header. */

#include "stack/SkySplit.h"

#include "raw/RawImage.h"

#include <algorithm>
#include <cmath>

namespace orion::stack {
namespace {

/// One separable box blur pass of radius r, edges clamped, O(w*h) by running sums.
void boxPass(std::vector<float>& a, std::vector<float>& tmp, int w, int h, int r) {
    const float inv = 1.0f / float(2 * r + 1);
    // Horizontal: a -> tmp.
    for (int y = 0; y < h; ++y) {
        const float* in = a.data() + std::size_t(y) * w;
        float* out = tmp.data() + std::size_t(y) * w;
        double sum = 0;
        for (int k = -r; k <= r; ++k) sum += in[std::clamp(k, 0, w - 1)];
        for (int x = 0; x < w; ++x) {
            out[x] = float(sum) * inv;
            sum += in[std::min(x + r + 1, w - 1)] - in[std::max(x - r, 0)];
        }
    }
    // Vertical: tmp -> a.
    for (int x = 0; x < w; ++x) {
        double sum = 0;
        for (int k = -r; k <= r; ++k) sum += tmp[std::size_t(std::clamp(k, 0, h - 1)) * w + x];
        for (int y = 0; y < h; ++y) {
            a[std::size_t(y) * w + x] = float(sum) * inv;
            sum += tmp[std::size_t(std::min(y + r + 1, h - 1)) * w + x]
                 - tmp[std::size_t(std::max(y - r, 0)) * w + x];
        }
    }
}

}  // namespace

std::vector<float> skyMatteToFrame(const float* upright, int mw, int mh,
                                   std::uint32_t width, std::uint32_t height, int flip) {
    const int w = int(width), h = int(height);
    std::vector<float> out(std::size_t(w) * h, 0.0f);
    if (!upright || mw <= 0 || mh <= 0 || w <= 0 || h <= 0) return out;

    // The matte is stretched edge to edge over the sensor frame. ⚠ It was painted on the embedded
    // preview, whose aspect and crop may differ from the raw's active area; a few percent of seam
    // shift at the borders is the ceiling, which the 1% feather partly hides.
    // Sensor (u, v) in 0..1 -> upright (x', y') in 0..1, by k clockwise
    // quarters. One turn sends (x, y) to (h-1-y, x): in normalized
    // coordinates (u, v) -> (1-v, u).
    const int k = raw::quarterTurnsFor(flip);
    for (int y = 0; y < h; ++y) {
        const float v = (float(y) + 0.5f) / float(h);
        for (int x = 0; x < w; ++x) {
            const float u = (float(x) + 0.5f) / float(w);
            float ux = u, uy = v;
            switch (k) {
                case 1: ux = 1.0f - v; uy = u;        break;
                case 2: ux = 1.0f - u; uy = 1.0f - v; break;
                case 3: ux = v;        uy = 1.0f - u; break;
                default: break;
            }
            const float fx = std::clamp(ux * float(mw) - 0.5f, 0.0f, float(mw - 1));
            const float fy = std::clamp(uy * float(mh) - 0.5f, 0.0f, float(mh - 1));
            const int x0 = int(fx), y0 = int(fy);
            const int x1 = std::min(x0 + 1, mw - 1), y1 = std::min(y0 + 1, mh - 1);
            const float ax = fx - float(x0), ay = fy - float(y0);
            const float top = upright[std::size_t(y0) * mw + x0] * (1 - ax) + upright[std::size_t(y0) * mw + x1] * ax;
            const float bot = upright[std::size_t(y1) * mw + x0] * (1 - ax) + upright[std::size_t(y1) * mw + x1] * ax;
            out[std::size_t(y) * w + x] = top * (1 - ay) + bot * ay;
        }
    }

    // Feather: box blur, radius 1% of the long edge, two passes.
    const int r = std::max(1, int(std::lround(0.01 * std::max(w, h))));
    std::vector<float> tmp(out.size());
    boxPass(out, tmp, w, h, r);
    boxPass(out, tmp, w, h, r);
    for (float& f : out) f = std::clamp(f, 0.0f, 1.0f);
    return out;
}

}  // namespace orion::stack
