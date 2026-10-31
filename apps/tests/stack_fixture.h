/*  stack_fixture — a synthetic star field with known truth, shared by the
 *  stack tests.
 *
 *  `n` stars are placed at seeded random reference positions; the frame shows
 *  each one at `apply(refToSource, p)` — the same dest->source convention the
 *  aligners answer in, so a test compares the estimate against the matrix it
 *  built the frame with. Each star is a point-sampled Gaussian PSF (sigma
 *  1.2 px) whose *peak* is 0.05-0.8 above `background`, plus Gaussian noise
 *  `noiseSigma`. Packed float RGB, R = G = B. Pixel (x, y)'s centre is the
 *  coordinate (x, y), as in tests_align.cpp.
 *
 *  Stars sit at least 10 px from the frame edge and from each other, so a
 *  detection count measures the detector, not the seed's blends.
 *
 *  `extra` stars are drawn as given, in frame coordinates, untransformed.
 *  `addSpikes` plants single-pixel hot pixels at fixed sensor positions.
 */

#pragma once

#include "pipe/Perspective.h"
#include "stack/StarAlign.h"

#include <cmath>
#include <cstdint>
#include <random>
#include <vector>

namespace stackfix {

namespace persp = orion::pipe::persp;
using orion::stack::Star;

constexpr float kPsfSigma = 1.2f;

struct StarField {
    std::vector<float> rgb;          ///< w*h*3
    std::vector<Star> reference;     ///< every star, reference coordinates (flux = peak)
    std::vector<Star> truth;         ///< the stars inside the frame, frame coordinates
};

/// A rotation of `degrees` about (cx, cy), then a translation (tx, ty).
inline persp::Matrix3 rotation(float degrees, float cx, float cy, float tx, float ty) {
    const float t = degrees * 3.14159265f / 180.0f, c = std::cos(t), s = std::sin(t);
    persp::Matrix3 m;
    m.m[0] = c; m.m[1] = -s; m.m[2] = cx - c * cx + s * cy + tx;
    m.m[3] = s; m.m[4] =  c; m.m[5] = cy - s * cx - c * cy + ty;
    return m;
}

inline StarField starField(unsigned seed, int n, std::uint32_t w, std::uint32_t h,
                           const persp::Matrix3& refToSource, float noiseSigma,
                           float background, unsigned noiseSeed = 0,
                           const std::vector<Star>& extra = {}) {
    StarField f;
    std::mt19937 rng(seed);
    std::uniform_real_distribution<float> ux(10.0f, float(w) - 11.0f), uy(10.0f, float(h) - 11.0f),
        peak(0.05f, 0.8f);
    for (int tries = 0; int(f.reference.size()) < n && tries < 100 * n; ++tries) {
        const float x = ux(rng), y = uy(rng);   // sequenced: same seed, same field
        const float p = peak(rng);
        bool clear = true;
        for (const Star& s : f.reference) clear &= std::hypot(s.x - x, s.y - y) >= 10.0f;
        if (clear) f.reference.push_back({x, y, p});
    }

    std::vector<Star> drawn;
    for (Star s : f.reference) {
        persp::apply(refToSource, s.x, s.y);
        drawn.push_back(s);
    }
    drawn.insert(drawn.end(), extra.begin(), extra.end());

    std::vector<float> luma(std::size_t(w) * h, background);
    const int r = int(std::ceil(5.0f * kPsfSigma));
    for (const Star& s : drawn) {
        if (s.x >= 0 && s.y >= 0 && s.x <= float(w - 1) && s.y <= float(h - 1))
            f.truth.push_back(s);
        const int x0 = int(std::lround(s.x)), y0 = int(std::lround(s.y));
        for (int y = std::max(0, y0 - r); y <= std::min(int(h) - 1, y0 + r); ++y)
            for (int x = std::max(0, x0 - r); x <= std::min(int(w) - 1, x0 + r); ++x) {
                const float dx = float(x) - s.x, dy = float(y) - s.y;
                luma[std::size_t(y) * w + std::size_t(x)] +=
                    s.flux * std::exp(-(dx * dx + dy * dy) / (2 * kPsfSigma * kPsfSigma));
            }
    }

    std::mt19937 noise(noiseSeed ? noiseSeed : seed + 7919);
    std::normal_distribution<float> g(0.0f, noiseSigma > 0 ? noiseSigma : 1.0f);
    f.rgb.resize(luma.size() * 3);
    for (std::size_t i = 0; i < luma.size(); ++i) {
        const float v = luma[i] + (noiseSigma > 0 ? g(noise) : 0.0f);
        f.rgb[i * 3] = f.rgb[i * 3 + 1] = f.rgb[i * 3 + 2] = v;
    }
    return f;
}

/// `count` single-pixel spikes of `amplitude` added to all three channels at
/// seeded positions — the same positions for the same seed, as a sensor's hot
/// pixels are.
inline void addSpikes(std::vector<float>& rgb, std::uint32_t w, std::uint32_t h,
                      unsigned seed, int count, float amplitude) {
    std::mt19937 rng(seed);
    std::uniform_int_distribution<std::uint32_t> ux(0, w - 1), uy(0, h - 1);
    for (int i = 0; i < count; ++i) {
        const std::uint32_t x = ux(rng), y = uy(rng);
        for (int c = 0; c < 3; ++c) rgb[(std::size_t(y) * w + x) * 3 + std::size_t(c)] += amplitude;
    }
}

}  // namespace stackfix
