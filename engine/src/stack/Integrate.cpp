/*  Stack integration — see Integrate.h for the contract and the sources. */

#include "stack/Integrate.h"

#include "util/Half.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <thread>

namespace orion::stack {
namespace {

constexpr double kGridSamples = 50000.0;   // UNSOURCED §22
constexpr float kBrightSigmas = 10.0f;     // UNSOURCED §22
constexpr std::size_t kMinBright = 200;    // UNSOURCED §22, also the sky subset's minimum
constexpr float kNoiseFloor = 1e-6f;       // a noiseless synthetic frame keeps a finite weight

/// Bilinear tap of `f` at reference texel (x, y) through `h` (dest->source),
/// merge/Merge.cpp's `tap` on half input. false = outside: "nothing to say", never black.
// ponytail: 12 halfToFloat per tap; a 64K-entry LUT or hardware fp16 if Task 6's 24 MP bench says so.
bool tap(const Frame& f, const persp::Matrix3& h, float x, float y, float out[3]) noexcept {
    persp::apply(h, x, y);
    if (!(x >= 0.0f) || !(y >= 0.0f) || x > float(f.width - 1) || y > float(f.height - 1)) return false;
    const auto x0 = std::uint32_t(x), y0 = std::uint32_t(y);
    const std::uint32_t x1 = std::min(x0 + 1, f.width - 1), y1 = std::min(y0 + 1, f.height - 1);
    const float fx = x - float(x0), fy = y - float(y0);
    const std::uint16_t* p00 = f.rgb + (std::size_t(y0) * f.width + x0) * 3;
    const std::uint16_t* p10 = f.rgb + (std::size_t(y0) * f.width + x1) * 3;
    const std::uint16_t* p01 = f.rgb + (std::size_t(y1) * f.width + x0) * 3;
    const std::uint16_t* p11 = f.rgb + (std::size_t(y1) * f.width + x1) * 3;
    for (int c = 0; c < 3; ++c) {
        const float a = util::halfToFloat(p00[c]), b = util::halfToFloat(p10[c]);
        const float d = util::halfToFloat(p01[c]), e = util::halfToFloat(p11[c]);
        const float top = a + (b - a) * fx, bot = d + (e - d) * fx;
        out[c] = top + (bot - top) * fy;
    }
    return true;
}

float luma(const Frame& f, std::size_t pixel) noexcept {
    const std::uint16_t* p = f.rgb + pixel * 3;
    return (util::halfToFloat(p[0]) + util::halfToFloat(p[1]) + util::halfToFloat(p[2])) * (1.0f / 3.0f);
}

/// Median by partial sort; reorders v. Empty -> `fallback`.
float median(std::vector<float>& v, float fallback) {
    if (v.empty()) return fallback;
    const auto mid = v.begin() + std::ptrdiff_t(v.size() / 2);
    std::nth_element(v.begin(), mid, v.end());
    return *mid;
}

/// σ = 1.4826·MAD of horizontal neighbour luma differences / √2, on the frame's
/// own pixels (a tap would interpolate noise away), sampled every `step`.
float noiseSigma(const Frame& f, std::uint32_t step) {
    std::vector<float> d;
    for (std::uint32_t y = 0; y < f.height; y += step)
        for (std::uint32_t x = 0; x + 1 < f.width; x += step) {
            const std::size_t p = std::size_t(y) * f.width + x;
            d.push_back(luma(f, p + 1) - luma(f, p));
        }
    const float m = median(d, 0.0f);
    for (float& v : d) v = std::fabs(v - m);
    return std::max(1.4826f * median(d, 0.0f) / std::sqrt(2.0f), kNoiseFloor);
}

/// v' = (v − L)/k + L_ref, weighted w.
struct Norm { float L[3] = {0, 0, 0}; float k = 1.0f, weight = 1.0f; };

}  // namespace

Result integrate(const std::vector<Frame>& frames, int reference, const float* skyMatte, const Options& options) {
    if (frames.empty()) throw std::runtime_error("stack: no frames");
    if (reference < 0 || std::size_t(reference) >= frames.size()) throw std::runtime_error("stack: bad reference");
    const Frame& ref = frames[std::size_t(reference)];
    const std::uint32_t W = ref.width, H = ref.height;
    if (W < 2 || H < 1) throw std::runtime_error("stack: empty reference");
    for (const Frame& f : frames)
        if (f.width != W || f.height != H || !f.rgb) throw std::runtime_error("stack: frame size mismatch");
    const int n = int(frames.size());

    Result result;
    result.used = options.rejection == Rejection::Auto ? autoRejection(n) : options.rejection;

    // The statistics grid: ~50k reference texels; with a matte, its sky when that is big enough
    // (the ground is static and would otherwise dominate the sky's location and scale).
    const auto step = std::uint32_t(std::max(1.0, std::floor(std::sqrt(double(W) * H / kGridSamples))));
    std::vector<std::size_t> grid, sky;
    for (std::uint32_t y = 0; y < H; y += step)
        for (std::uint32_t x = 0; x < W; x += step) {
            const std::size_t p = std::size_t(y) * W + x;
            grid.push_back(p);
            if (skyMatte && skyMatte[p] >= 0.5f) sky.push_back(p);
        }
    if (sky.size() >= kMinBright) grid.swap(sky);

    // The reference's location from its own pixels, its noise for the bright floor.
    float Lref[3];
    std::vector<float> a, b, ratios;
    for (int c = 0; c < 3; ++c) {
        a.clear();
        for (std::size_t p : grid) a.push_back(util::halfToFloat(ref.rgb[p * 3 + std::size_t(c)]));
        Lref[c] = median(a, 0.0f);
    }
    const float lumaLref = (Lref[0] + Lref[1] + Lref[2]) / 3.0f;
    const float brightFloor = lumaLref + kBrightSigmas * noiseSigma(ref, step);

    // Per frame: location and scale on registered pairs, then the weight.
    std::vector<Norm> norm(frames.size());
    std::vector<float> ch[3];
    for (std::size_t i = 0; i < frames.size(); ++i) {
        const Frame& f = frames[i];
        for (auto& v : ch) v.clear();
        a.clear();   // reference luma of each pair
        b.clear();   // frame luma of each pair
        for (std::size_t p : grid) {
            float t[3];
            if (!tap(f, f.refToSource, float(p % W), float(p / W), t)) continue;
            for (int c = 0; c < 3; ++c) ch[c].push_back(t[c]);
            a.push_back(luma(ref, p));
            b.push_back((t[0] + t[1] + t[2]) / 3.0f);
        }
        Norm& nm = norm[i];
        for (int c = 0; c < 3; ++c) nm.L[c] = median(ch[c], Lref[c]);
        const float lumaL = (nm.L[0] + nm.L[1] + nm.L[2]) / 3.0f;
        ratios.clear();
        for (std::size_t j = 0; j < a.size(); ++j)
            if (a[j] >= brightFloor) ratios.push_back((b[j] - lumaL) / (a[j] - lumaLref));
        if (ratios.size() >= kMinBright) nm.k = median(ratios, 1.0f);
        if (!(nm.k > 0.0f) || !std::isfinite(nm.k)) nm.k = 1.0f;
        if (options.weightByNoise) {
            const float s = noiseSigma(f, step);
            nm.weight = (nm.k / s) * (nm.k / s);
        }
    }

    // The pixel loop, rows split into one contiguous band per hardware thread.
    result.rgb.resize(std::size_t(W) * H * 3);
    const unsigned threads = std::max(1u, std::min(std::thread::hardware_concurrency(), H));
    std::vector<std::vector<float>> scratch(threads, std::vector<float>(std::size_t(n) * 6));
    const std::size_t sn = std::size_t(n);

    auto band = [&](unsigned t) {
        float* v = scratch[t].data();
        float* w = v + 3 * sn;
        // One part (sky or ground) of one pixel: tap every frame through `warp`, normalize, combine.
        auto part = [&](persp::Matrix3 Frame::*warp, std::uint32_t x, std::uint32_t y, float out[3]) {
            std::size_t k = 0;
            for (std::size_t i = 0; i < sn; ++i) {
                float s[3];
                if (!tap(frames[i], frames[i].*warp, float(x), float(y), s)) continue;
                for (int c = 0; c < 3; ++c) {
                    v[std::size_t(c) * sn + k] = (s[c] - norm[i].L[c]) / norm[i].k + Lref[c];
                    w[std::size_t(c) * sn + k] = norm[i].weight;
                }
                ++k;
            }
            const std::size_t p = std::size_t(y) * W + x;
            for (int c = 0; c < 3; ++c)
                out[c] = k ? combine(v + std::size_t(c) * sn, w + std::size_t(c) * sn, int(k), result.used)
                           : util::halfToFloat(ref.rgb[p * 3 + std::size_t(c)]);
        };
        const std::uint32_t y0 = std::uint32_t(std::uint64_t(H) * t / threads);
        const std::uint32_t y1 = std::uint32_t(std::uint64_t(H) * (t + 1) / threads);
        for (std::uint32_t y = y0; y < y1; ++y)
            for (std::uint32_t x = 0; x < W; ++x) {
                const std::size_t p = std::size_t(y) * W + x;
                const float m = skyMatte ? std::clamp(skyMatte[p], 0.0f, 1.0f) : 1.0f;
                float s[3] = {0, 0, 0}, g[3] = {0, 0, 0};
                if (m > 0.0f) part(&Frame::refToSource, x, y, s);
                if (m < 1.0f) part(&Frame::groundToSource, x, y, g);
                for (int c = 0; c < 3; ++c)
                    result.rgb[p * 3 + std::size_t(c)] = std::max(0.0f, m * s[c] + (1.0f - m) * g[c]);
            }
    };
    std::vector<std::thread> pool;
    try {
        for (unsigned t = 1; t < threads; ++t) pool.emplace_back(band, t);
    } catch (...) {   // a joinable thread's destructor would terminate the process
        for (auto& th : pool) th.join();
        throw;
    }
    band(0);
    for (auto& th : pool) th.join();
    return result;
}

}  // namespace orion::stack
