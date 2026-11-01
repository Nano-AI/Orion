/*  Stack integration — see Integrate.h for the contract and the sources. */

#include "stack/Integrate.h"

#include "util/Half.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <thread>

namespace orion::stack {
namespace {

constexpr double kGridSamples = 50000.0;   // UNSOURCED §32
constexpr float kBrightSigmas = 10.0f;     // UNSOURCED §32
constexpr std::size_t kMinBright = 200;    // UNSOURCED §32: the sky subset's minimum
constexpr std::size_t kMinCores = 50;      // UNSOURCED §32: star cores needed before k is believed
constexpr float kClip = 0.98f;             // UNSOURCED §32: a box touching this is clipped, no flux to measure
constexpr float kNoiseFloor = 1e-6f;       // a noiseless synthetic frame keeps a finite weight
constexpr float kMinScale = 0.1f, kMaxScale = 10.0f;   // UNSOURCED §32: outside, the scale is not believed
constexpr float kMaxWeightRatio = 10.0f;   // UNSOURCED §32: no frame outweighs the median frame by more
constexpr int kAperture = 3;               // UNSOURCED §32: 7x7 flux box, half-width 3
constexpr float kBoxArea = float((2 * kAperture + 1) * (2 * kAperture + 1));

/// Bilinear tap of `f` at reference texel (x, y) through `h` (dest->source),
/// merge/Merge.cpp's `tap` on half input. false = outside: "nothing to say", never black.
// ponytail: 12 halfToFloat per tap; a 64K-entry LUT or hardware fp16 if Task 6's 24 MP bench says so.
bool tap(const Frame& f, const persp::Matrix3& h, float x, float y, float out[3]) noexcept {
    persp::apply(h, x, y);
    if (!(x >= 0.0f) || !(y >= 0.0f) || x > float(f.width - 1) || y > float(f.height - 1)) return false;
    const auto x0 = std::uint32_t(x), y0 = std::uint32_t(y);
    const std::uint32_t x1 = std::min(x0 + 1, f.width - 1), y1 = std::min(y0 + 1, f.height - 1);
    const float fx = x - float(x0), fy = y - float(y0);
    // A band holds rows from firstRow on, and rowsRead promised these two.
    const std::size_t r0 = y0 - f.firstRow, r1 = y1 - f.firstRow;
    const std::uint16_t* p00 = f.rgb + (r0 * f.width + x0) * 3;
    const std::uint16_t* p10 = f.rgb + (r0 * f.width + x1) * 3;
    const std::uint16_t* p01 = f.rgb + (r1 * f.width + x0) * 3;
    const std::uint16_t* p11 = f.rgb + (r1 * f.width + x1) * 3;
    for (int c = 0; c < 3; ++c) {
        const float a = util::halfToFloat(p00[c]), b = util::halfToFloat(p10[c]);
        const float d = util::halfToFloat(p01[c]), e = util::halfToFloat(p11[c]);
        const float top = a + (b - a) * fx, bot = d + (e - d) * fx;
        out[c] = top + (bot - top) * fy;
    }
    return true;
}

/// Σ luma over the 7x7 box about reference texel (x, y), each texel through `h`;
/// false when any is outside or clipped (>= kClip in any channel). A star's flux survives a sub-pixel bilinear tap
/// where its peak does not (box half-width 3.5 px: a σ 1.2 px star loses < 1%
/// to the blur of a half-pixel tap; a 3x3 box loses ~3%, see UNSOURCED §32).
bool boxLuma(const Frame& f, const persp::Matrix3& h, std::uint32_t x, std::uint32_t y, float& sum) noexcept {
    sum = 0.0f;
    for (int dy = -kAperture; dy <= kAperture; ++dy)
        for (int dx = -kAperture; dx <= kAperture; ++dx) {
            float t[3];
            if (!tap(f, h, float(x) + float(dx), float(y) + float(dy), t)) return false;
            if (t[0] >= kClip || t[1] >= kClip || t[2] >= kClip) return false;
            sum += (t[0] + t[1] + t[2]) * (1.0f / 3.0f);
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

}  // namespace

Reference measureReference(const Frame& ref, const float* skyMatte) {
    const std::uint32_t W = ref.width, H = ref.height;
    if (W < 2 || H < 1 || !ref.rgb) throw std::runtime_error("stack: empty reference");
    Reference out;
    out.width = W;
    out.height = H;

    // The statistics grid: ~50k reference texels; with a matte, its sky when that is big enough
    // (the ground is static and would otherwise dominate the sky's location and scale).
    const auto step = std::uint32_t(std::max(1.0, std::floor(std::sqrt(double(W) * H / kGridSamples))));
    out.step = step;
    std::vector<std::size_t>& grid = out.grid;
    std::vector<std::size_t> sky;
    for (std::uint32_t y = 0; y < H; y += step)
        for (std::uint32_t x = 0; x < W; x += step) {
            const std::size_t p = std::size_t(y) * W + x;
            grid.push_back(p);
            if (skyMatte && skyMatte[p] >= 0.5f) sky.push_back(p);
        }
    const bool skyOnly = sky.size() >= kMinBright;
    if (skyOnly) grid.swap(sky);

    // The reference's location from its own pixels, its noise for the bright floor.
    float* Lref = out.L;
    std::vector<float> a;
    for (int c = 0; c < 3; ++c) {
        a.clear();
        for (std::size_t p : grid) a.push_back(util::halfToFloat(ref.rgb[p * 3 + std::size_t(c)]));
        Lref[c] = median(a, 0.0f);
    }
    const float lumaLref = (Lref[0] + Lref[1] + Lref[2]) / 3.0f;
    const float sigmaRef = noiseSigma(ref, step);
    const float brightFloor = lumaLref + kBrightSigmas * sigmaRef;

    // The scale's sample: every reference pixel >= 10σ over its background that is a local
    // maximum (a star's core, so the box holds its flux), found at full resolution (stars are
    // ~0.1% of a sky; the grid would miss them), strided to ~50k, each with its box sum.
    // In the sky only, when the statistics are.
    constexpr std::uint32_t a3 = kAperture;
    std::vector<std::size_t> bright;
    for (std::uint32_t y = a3; y + a3 < H; ++y)
        for (std::uint32_t x = a3; x + a3 < W; ++x) {
            const std::size_t p = std::size_t(y) * W + x;
            if (skyOnly && !(skyMatte[p] >= 0.5f)) continue;
            const float v = luma(ref, p);
            if (v < brightFloor) continue;
            // Strictly above the neighbours before it in raster order, at least the ones after:
            // a plateau (a flat-topped core) yields one maximum, not one per texel.
            bool peak = true;
            for (int dy = -1; dy <= 1 && peak; ++dy)
                for (int dx = -1; dx <= 1 && peak; ++dx) {
                    if (dx == 0 && dy == 0) continue;
                    const float u = luma(ref, std::size_t(std::ptrdiff_t(p) + dy * std::ptrdiff_t(W) + dx));
                    peak = (dy < 0 || (dy == 0 && dx < 0)) ? u < v : u <= v;
                }
            if (peak) bright.push_back(p);
        }
    const std::size_t stride = bright.size() / std::size_t(kGridSamples) + 1;
    for (std::size_t j = 0; j < bright.size(); j += stride) {
        float s;
        if (!boxLuma(ref, persp::identity(), std::uint32_t(bright[j] % W), std::uint32_t(bright[j] / W), s)) continue;
        // The ratio's denominator: a core whose box holds no real excess (a lone noise peak)
        // would hand the median noise/noise.
        if (!(s - kBoxArea * lumaLref > kBrightSigmas * sigmaRef)) continue;
        out.pick.push_back(bright[j]);
        out.box.push_back(s - kBoxArea * lumaLref);
    }
    return out;
}

Norm measureFrame(const Frame& f, const Reference& ref, bool weightByNoise) {
    // Location on the grid and scale on the bright boxes, both on registered pairs.
    const std::uint32_t W = ref.width;
    std::vector<float> ch[3], ratios;
    for (std::size_t p : ref.grid) {
        float t[3];
        if (!tap(f, f.refToSource, float(p % W), float(p / W), t)) continue;
        for (int c = 0; c < 3; ++c) ch[c].push_back(t[c]);
    }
    Norm nm;
    for (int c = 0; c < 3; ++c) nm.L[c] = median(ch[c], ref.L[c]);
    const float lumaL = (nm.L[0] + nm.L[1] + nm.L[2]) / 3.0f;
    for (std::size_t j = 0; j < ref.pick.size(); ++j) {
        float s;
        if (!boxLuma(f, f.refToSource, std::uint32_t(ref.pick[j] % W), std::uint32_t(ref.pick[j] / W), s)) continue;
        const float r = (s - kBoxArea * lumaL) / ref.box[j];
        if (std::isfinite(r)) ratios.push_back(r);   // a NaN breaks nth_element's ordering
    }
    if (ratios.size() >= kMinCores) nm.k = median(ratios, 1.0f);
    if (!(nm.k >= kMinScale && nm.k <= kMaxScale)) nm.k = 1.0f;   // NaN included
    if (weightByNoise) {
        const float s = noiseSigma(f, ref.step);
        nm.weight = (nm.k / s) * (nm.k / s);
    }
    return nm;
}

void capWeights(std::vector<Norm>& norms) {
    // A frame whose MAD collapsed must not take the stack over.
    std::vector<float> a;
    for (const Norm& nm : norms) a.push_back(nm.weight);
    const float cap = kMaxWeightRatio * median(a, 1.0f);
    for (Norm& nm : norms) nm.weight = std::min(nm.weight, cap);
}

Rows rowsRead(const persp::Matrix3& h, std::uint32_t width, std::uint32_t height, std::uint32_t y0,
              std::uint32_t y1) noexcept {
    if (y1 <= y0 || width == 0 || height == 0) return {0, 0};
    // w is affine in (x, y), so positive at the band's four corners means positive over the band, and
    // the band then maps onto the convex hull of its corners' images: their y range bounds every tap.
    float lo = std::numeric_limits<float>::infinity(), hi = -lo;
    for (const float x : {0.0f, float(width - 1)})
        for (const float y : {float(y0), float(y1 - 1)}) {
            const float w = h.m[6] * x + h.m[7] * y + h.m[8];
            float sx = x, sy = y;
            persp::apply(h, sx, sy);
            if (!(w > 0.0f) || !std::isfinite(sy)) return {0, height};
            lo = std::min(lo, sy);
            hi = std::max(hi, sy);
        }
    // A tap reads floor(y) and the row below; one more row each side for rounding.
    const auto clampRow = [&](float r) { return std::uint32_t(std::clamp(r, 0.0f, float(height))); };
    const std::uint32_t first = clampRow(std::floor(lo) - 1.0f), last = clampRow(std::floor(hi) + 3.0f);
    return {std::min(first, last), last};
}

void integrateRows(const std::vector<Frame>& frames, int reference, const float* skyMatte, const Reference& refStats,
                   const std::vector<Norm>& norm, Rejection used, std::uint32_t y0, std::uint32_t y1, float* out) {
    const Frame& ref = frames[std::size_t(reference)];
    const std::uint32_t W = refStats.width;
    const float* Lref = refStats.L;
    const std::size_t sn = frames.size();
    if (y1 <= y0) return;
    const std::uint32_t rows = y1 - y0;

    // The rows split into one contiguous run per hardware thread.
    const unsigned threads = std::max(1u, std::min(std::thread::hardware_concurrency(), rows));
    std::vector<std::vector<float>> scratch(threads, std::vector<float>(sn * 6));

    auto run = [&](unsigned t) {
        float* v = scratch[t].data();
        float* w = v + 3 * sn;
        // One part (sky or ground) of one pixel: tap every frame through `warp`, normalize, combine.
        auto part = [&](persp::Matrix3 Frame::*warp, std::uint32_t x, std::uint32_t y, float o[3]) {
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
            const std::uint16_t* own = ref.rgb + ((std::size_t(y) - ref.firstRow) * W + x) * 3;
            for (int c = 0; c < 3; ++c)
                o[c] = k ? combine(v + std::size_t(c) * sn, w + std::size_t(c) * sn, int(k), used)
                         : util::halfToFloat(own[c]);
        };
        const std::uint32_t r0 = y0 + std::uint32_t(std::uint64_t(rows) * t / threads);
        const std::uint32_t r1 = y0 + std::uint32_t(std::uint64_t(rows) * (t + 1) / threads);
        for (std::uint32_t y = r0; y < r1; ++y)
            for (std::uint32_t x = 0; x < W; ++x) {
                const std::size_t p = std::size_t(y) * W + x;
                // A NaN matte value fails both tests below and the pixel comes out 0 (the matte is our own output).
                const float m = skyMatte ? std::clamp(skyMatte[p], 0.0f, 1.0f) : 1.0f;
                float s[3] = {0, 0, 0}, g[3] = {0, 0, 0};
                if (m > 0.0f) part(&Frame::refToSource, x, y, s);
                if (m < 1.0f) part(&Frame::groundToSource, x, y, g);
                float* o = out + ((std::size_t(y) - y0) * W + x) * 3;
                for (int c = 0; c < 3; ++c) o[c] = std::max(0.0f, m * s[c] + (1.0f - m) * g[c]);
            }
    };
    std::vector<std::thread> pool;
    try {
        for (unsigned t = 1; t < threads; ++t) pool.emplace_back(run, t);
    } catch (...) {   // a joinable thread's destructor would terminate the process
        for (auto& th : pool) th.join();
        throw;
    }
    run(0);
    for (auto& th : pool) th.join();
}

Result integrate(const std::vector<Frame>& frames, int reference, const float* skyMatte, const Options& options) {
    if (frames.empty()) throw std::runtime_error("stack: no frames");
    if (reference < 0 || std::size_t(reference) >= frames.size()) throw std::runtime_error("stack: bad reference");
    const Frame& ref = frames[std::size_t(reference)];
    const std::uint32_t W = ref.width, H = ref.height;
    if (W < 2 || H < 1) throw std::runtime_error("stack: empty reference");
    for (const Frame& f : frames)
        if (f.width != W || f.height != H || !f.rgb || f.firstRow) throw std::runtime_error("stack: frame size mismatch");

    Result result;
    result.used = options.rejection == Rejection::Auto ? autoRejection(int(frames.size())) : options.rejection;
    const Reference stats = measureReference(ref, skyMatte);
    std::vector<Norm> norm;
    for (const Frame& f : frames) norm.push_back(measureFrame(f, stats, options.weightByNoise));
    if (options.weightByNoise) capWeights(norm);
    for (const Norm& nm : norm) result.scale.push_back(nm.k);

    result.rgb.resize(std::size_t(W) * H * 3);
    integrateRows(frames, reference, skyMatte, stats, norm, result.used, 0, H, result.rgb.data());
    return result;
}

}  // namespace orion::stack
