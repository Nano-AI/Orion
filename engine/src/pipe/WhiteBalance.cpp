#include "pipe/WhiteBalance.h"

#include <algorithm>
#include <cmath>

namespace orion::pipe {
namespace {

/// Kim et al.'s cubic approximation of the Planckian locus in CIE 1931 xy,
/// accurate from 1667 K to 25000 K — comfortably wider than any photographic
/// white balance a user will reach for.
void locusXy(float cct, float& x, float& y) {
    const float t = std::clamp(cct, 1667.0f, 25000.0f);
    const float t2 = t * t, t3 = t2 * t;

    if (t <= 4000.0f) {
        x = -0.2661239e9f / t3 - 0.2343589e6f / t2 + 0.8776956e3f / t + 0.179910f;
    } else {
        x = -3.0258469e9f / t3 + 2.1070379e6f / t2 + 0.2226347e3f / t + 0.240390f;
    }

    const float x2 = x * x, x3 = x2 * x;
    if (t <= 2222.0f) {
        y = -1.1063814f * x3 - 1.34811020f * x2 + 2.18555832f * x - 0.20219683f;
    } else if (t <= 4000.0f) {
        y = -0.9549476f * x3 - 1.37418593f * x2 + 2.09137015f * x - 0.16748867f;
    } else {
        y =  3.0817580f * x3 - 5.87338670f * x2 + 3.75112997f * x - 0.37001483f;
    }
}

std::array<float, 3> applyMatrix(const float m[9], const std::array<float, 3>& v) {
    return {m[0] * v[0] + m[1] * v[1] + m[2] * v[2],
            m[3] * v[0] + m[4] * v[1] + m[5] * v[2],
            m[6] * v[0] + m[7] * v[1] + m[8] * v[2]};
}

}  // namespace

std::array<float, 3> multipliersFor(const WhiteBalance& wb, const float xyzToCam[9]) {
    float x = 0.0f, y = 0.0f;
    locusXy(wb.temperatureK, x, y);

    // Tint moves perpendicular to the locus — the green/magenta axis. A small
    // offset in y is close enough to perpendicular over the range that matters,
    // and it is what users actually feel as "more green" or "more magenta".
    y += wb.tint * 0.05f;
    y = std::max(y, 1e-4f);

    // xy at unit luminance -> XYZ.
    const std::array<float, 3> xyz{x / y, 1.0f, (1.0f - x - y) / y};

    // The illuminant as the camera sees it. Neutralizing means dividing by it.
    const auto cam = applyMatrix(xyzToCam, xyz);

    std::array<float, 3> mul{};
    for (int i = 0; i < 3; ++i) {
        mul[i] = (std::abs(cam[i]) > 1e-6f) ? 1.0f / cam[i] : 1.0f;
    }

    // Normalize to green, matching what the linearize shader expects.
    const float g = (mul[1] != 0.0f) ? mul[1] : 1.0f;
    for (auto& m : mul) m /= g;
    return mul;
}

WhiteBalance estimateFrom(const std::array<float, 3>& camMul, const float xyzToCam[9]) {
    // The camera's multipliers came from its own estimate of the scene
    // illuminant; recover the temperature by finding the locus point whose
    // multipliers match. A coarse sweep then a refinement is plenty — this
    // runs once per file, and the answer only needs to be right to a few Kelvin.
    const float g = (camMul[1] != 0.0f) ? camMul[1] : 1.0f;
    const std::array<float, 3> target{camMul[0] / g, 1.0f, camMul[2] / g};

    auto errorAt = [&](float cct) {
        const auto m = multipliersFor({cct, 0.0f}, xyzToCam);
        const float dr = std::log(std::max(m[0], 1e-6f)) - std::log(std::max(target[0], 1e-6f));
        const float db = std::log(std::max(m[2], 1e-6f)) - std::log(std::max(target[2], 1e-6f));
        return dr * dr + db * db;
    };

    float best = 5500.0f, bestErr = errorAt(best);
    for (float cct = 2000.0f; cct <= 15000.0f; cct += 100.0f) {
        const float e = errorAt(cct);
        if (e < bestErr) { bestErr = e; best = cct; }
    }
    for (float cct = best - 100.0f; cct <= best + 100.0f; cct += 5.0f) {
        const float e = errorAt(cct);
        if (e < bestErr) { bestErr = e; best = cct; }
    }

    // Tint is whatever green offset is still needed once temperature is fixed.
    const auto atBest = multipliersFor({best, 0.0f}, xyzToCam);
    float tint = 0.0f, tintErr = 1e30f;
    for (float t = -1.0f; t <= 1.0f; t += 0.01f) {
        const auto m = multipliersFor({best, t}, xyzToCam);
        const float dr = m[0] - target[0], db = m[2] - target[2];
        const float e = dr * dr + db * db;
        if (e < tintErr) { tintErr = e; tint = t; }
    }
    (void)atBest;

    return {best, tint};
}

}  // namespace orion::pipe
