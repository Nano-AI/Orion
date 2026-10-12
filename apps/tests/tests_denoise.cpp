// The second denoise pass: separate luma/chroma strengths, the texture-adaptive
// threshold, and the masked chain after the profile.
// research/denoise-masked.md §0.1, §1 and §2.

#include "harness.h"

namespace {

using Frame = std::vector<__fp16>;

constexpr float kScaleNorm[4] = {0.8907f, 0.2007f, 0.0855f, 0.0412f};

float luma2020(float r, float g, float b) {
    return 0.2627f * r + 0.6780f * g + 0.0593f * b;
}

/// The capture chain's four blurs and four shrinks, dispatched by hand like
/// `testDenoiseGpu`. `shape` fills each scale's shrink block beyond size,
/// scale norm and step.
struct Starlet {
    orion::gpu::Device& device;
    std::unique_ptr<orion::gpu::Library> blurLib, shrinkLib;
    std::unique_ptr<orion::gpu::Kernel> blur, shrink;
    std::uint32_t w, h;

    Starlet(orion::gpu::Device& d, std::uint32_t width, std::uint32_t height)
        : device(d), w(width), h(height) {
        const std::string dir = ORION_SHADER_DIR;
        blurLib = orion::gpu::Library::createFromFile(d, dir + "/atrousBlur.metallib");
        blur = orion::gpu::Kernel::create(d, *blurLib, "atrousBlur");
        shrinkLib = orion::gpu::Library::createFromFile(d, dir + "/atrousShrink.metallib");
        shrink = orion::gpu::Kernel::create(d, *shrinkLib, "atrousShrink");
    }

    std::unique_ptr<orion::gpu::Texture> make() const {
        return orion::gpu::Texture::create(device, w, h,
                                           orion::gpu::PixelFormat::RGBA16Float);
    }

    template <class Shape>
    Frame run(const Frame& input, Shape shape) {
        auto c0 = make();
        c0->upload(input.data(), std::size_t(w) * 4 * sizeof(__fp16));
        std::vector<std::unique_ptr<orion::gpu::Texture>> blurs, shrinks;
        for (int j = 0; j < 4; ++j) { blurs.push_back(make()); shrinks.push_back(make()); }
        for (int j = 0; j < 4; ++j) {
            orion::pipe::params::AtrousBlur bp{};
            bp.size[0] = w; bp.size[1] = h; bp.step = 1 << j;
            orion::gpu::CommandBuffer cb(device);
            cb.dispatch(*blur, {j == 0 ? c0.get() : blurs[std::size_t(j - 1)].get(),
                                blurs[std::size_t(j)].get()},
                        &bp, sizeof bp, w, h);
            cb.commitAndWait();
        }
        for (int j = 3; j >= 0; --j) {
            orion::pipe::params::AtrousShrink sp{};
            sp.size[0] = w; sp.size[1] = h;
            sp.scaleNorm = kScaleNorm[j];
            sp.step = 1 << j;
            shape(sp);
            const auto* fine = (j == 0) ? c0.get() : blurs[std::size_t(j - 1)].get();
            const auto* coarse = blurs[std::size_t(j)].get();
            const auto* accum = (j == 3) ? coarse : shrinks[std::size_t(j + 1)].get();
            orion::gpu::CommandBuffer cb(device);
            cb.dispatch(*shrink, {fine, coarse, accum, coarse, shrinks[std::size_t(j)].get()},
                        &sp, sizeof sp, w, h);
            cb.commitAndWait();
        }
        Frame out(std::size_t(w) * h * 4);
        shrinks[0]->download(out.data(), std::size_t(w) * 4 * sizeof(__fp16), w, h);
        return out;
    }
};

std::unique_ptr<orion::gpu::Device> deviceOrReport() {
    try {
        return orion::gpu::Device::create();
    } catch (const std::exception& e) {
        report(false, "Metal device available", e.what());
        return nullptr;
    }
}

}  // namespace

/// §0.1: Color alone must not touch luminance, and Luminance alone must not
/// touch color. The shipped chain shrank luma at the Color value.
void testDenoiseColorOnlyGpu() {
    section("Denoise: color only leaves luminance (GPU)");
    auto device = deviceOrReport();
    if (!device) return;

    constexpr std::uint32_t kW = 256, kH = 256;
    constexpr float kClean[3] = {0.5f, 0.3f, 0.2f};
    constexpr float kSigma = 0.03f;
    std::mt19937 rng(2026);
    std::normal_distribution<float> gauss(0.0f, kSigma);
    Frame input(std::size_t(kW) * kH * 4);
    for (std::size_t i = 0; i < std::size_t(kW) * kH; ++i) {
        for (int c = 0; c < 3; ++c) input[i * 4 + c] = __fp16(kClean[c] + gauss(rng));
        input[i * 4 + 3] = 1;
    }

    // Independent channels: the luma variance is the weight-squared sum.
    const float lumVar = kSigma * kSigma *
        (0.2627f * 0.2627f + 0.6780f * 0.6780f + 0.0593f * 0.0593f);
    Starlet chain(*device, kW, kH);
    const auto render = [&](float luma, float chroma) {
        return chain.run(input, [&](orion::pipe::params::AtrousShrink& sp) {
            sp.noiseB = lumVar;
            sp.strengthLuma = luma;
            sp.strengthChroma = chroma;
        });
    };

    const float cleanY = luma2020(kClean[0], kClean[1], kClean[2]);
    // Luma residual and chroma residual (each channel minus its pixel's luma,
    // against the clean frame's), over the interior.
    const auto measure = [&](const Frame& f) {
        double lum = 0.0, chr = 0.0; int n = 0;
        for (std::uint32_t y = 24; y < kH - 24; ++y)
            for (std::uint32_t x = 24; x < kW - 24; ++x) {
                const std::size_t i = std::size_t(y) * kW + x;
                const float r = f[i * 4], g = f[i * 4 + 1], b = f[i * 4 + 2];
                const float Y = luma2020(r, g, b);
                lum += double(Y - cleanY) * (Y - cleanY);
                const float px[3] = {r, g, b};
                for (int c = 0; c < 3; ++c) {
                    const double d = (px[c] - Y) - (kClean[c] - cleanY);
                    chr += d * d;
                }
                ++n;
            }
        return std::pair{std::sqrt(lum / n), std::sqrt(chr / (3.0 * n))};
    };

    const auto [lumIn, chrIn] = measure(input);
    const auto [lumC, chrC] = measure(render(0.0f, 2.0f));
    const auto [lumL, chrL] = measure(render(2.0f, 0.0f));
    const auto detail = [](double a, double b) {
        return "input " + std::to_string(a) + ", output " + std::to_string(b);
    };

    std::printf("  luma residual %.5f -> %.5f (Color 2), %.5f (Luminance 2); "
                "chroma %.5f -> %.5f, %.5f\n", lumIn, lumC, lumL, chrIn, chrC, chrL);
    report(std::abs(lumC - lumIn) <= lumIn * 0.02,
           "Color 2, Luminance 0 leaves the luminance residual where it was",
           detail(lumIn, lumC));
    report(chrC < chrIn * 0.6, "and does remove chroma noise", detail(chrIn, chrC));
    report(std::abs(chrL - chrIn) <= chrIn * 0.02,
           "Luminance 2, Color 0 leaves the chroma spread where it was",
           detail(chrIn, chrL));
    report(lumL < lumIn * 0.5, "and halves the luminance residual", detail(lumIn, lumL));
}

/// §1: Detail keeps texture above the noise and still clears a flat area.
void testDenoiseDetailGpu() {
    section("Denoise: Detail keeps texture (GPU)");
    auto device = deviceOrReport();
    if (!device) return;

    // Left half flat, right half a vertical square stripe, period 4 px, ±0.08.
    constexpr std::uint32_t kW = 256, kH = 256;
    constexpr float kBase = 0.4f, kAmp = 0.08f, kSigma = 0.03f;
    std::mt19937 rng(77);
    std::normal_distribution<float> gauss(0.0f, kSigma);
    const auto clean = [&](std::uint32_t x) {
        if (x < kW / 2) return kBase;
        return kBase + ((x % 4) < 2 ? kAmp : -kAmp);
    };
    Frame input(std::size_t(kW) * kH * 4);
    for (std::uint32_t y = 0; y < kH; ++y)
        for (std::uint32_t x = 0; x < kW; ++x) {
            const std::size_t i = std::size_t(y) * kW + x;
            const float v = clean(x) + gauss(rng);
            input[i * 4] = input[i * 4 + 1] = input[i * 4 + 2] = __fp16(v);
            input[i * 4 + 3] = 1;
        }

    Starlet chain(*device, kW, kH);
    const auto render = [&](float detail) {
        return chain.run(input, [&](orion::pipe::params::AtrousShrink& sp) {
            sp.noiseB = kSigma * kSigma;   // gray noise: luma sigma is kSigma
            sp.strengthLuma = 2.0f;
            sp.strengthChroma = 2.0f;
            sp.detail = detail;
        });
    };

    // Mean of the high phase minus mean of the low phase, well inside the
    // stripe half; the flat half's residual, well clear of the boundary.
    const auto stripe = [&](const Frame& f) {
        double hi = 0.0, lo = 0.0; int nh = 0, nl = 0;
        for (std::uint32_t y = 24; y < kH - 24; ++y)
            for (std::uint32_t x = kW / 2 + 16; x < kW - 24; ++x) {
                const double v = f[(std::size_t(y) * kW + x) * 4];
                if ((x % 4) < 2) { hi += v; ++nh; } else { lo += v; ++nl; }
            }
        return hi / nh - lo / nl;
    };
    const auto flat = [&](const Frame& f) {
        double s = 0.0; int n = 0;
        for (std::uint32_t y = 24; y < kH - 24; ++y)
            for (std::uint32_t x = 24; x < kW / 2 - 16; ++x) {
                const double d = double(f[(std::size_t(y) * kW + x) * 4]) - kBase;
                s += d * d; ++n;
            }
        return std::sqrt(s / n);
    };

    const auto d0 = render(0.0f), d1 = render(1.0f);
    const double a0 = stripe(d0), a1 = stripe(d1);
    std::printf("  stripe %.4f in, %.4f at Detail 0, %.4f at Detail 1; flat %.4f -> %.4f, %.4f\n",
                stripe(input), a0, a1, flat(input), flat(d0), flat(d1));
    report(a1 >= a0 * 1.5, "Detail 1 keeps at least 1.5x the stripe Detail 0 does",
           "input " + std::to_string(stripe(input)) + ", detail 0 " +
           std::to_string(a0) + ", detail 1 " + std::to_string(a1));
    const double fIn = flat(input), f1 = flat(d1);
    report(f1 < fIn * 0.6, "and the flat half still loses most of its noise",
           "input " + std::to_string(fIn) + ", detail 1 " + std::to_string(f1) +
           ", detail 0 " + std::to_string(flat(d0)));
}

namespace {

/// A flat mid-grey mosaic `estimateNoise` measures (the construction of
/// `testNoiseEstimator`'s night frame, at a brighter level and more noise so
/// it survives to eight bits).
orion::raw::BayerImage noisyGrey(std::uint32_t w, std::uint32_t h) {
    constexpr float kWhite = 16383.0f;
    constexpr double kA = 4.0e-4, kB = 4.0e-5, kSignal = 0.25;
    orion::raw::BayerImage img;
    img.width = w; img.height = h;
    img.filters = 0x94949494u;             // RGGB
    img.white = std::uint16_t(kWhite);
    img.black = {0, 0, 0, 0};
    img.camMul = {2.0f, 1.0f, 1.5f, 1.0f};
    img.camToXyz = {0.4124f, 0.3576f, 0.1805f,
                    0.2126f, 0.7152f, 0.0722f,
                    0.0193f, 0.1192f, 0.9505f};
    img.samples.resize(std::size_t(w) * h);
    std::mt19937 rng(31337);
    std::normal_distribution<double> g(0.0, 1.0);
    const double sigma = std::sqrt(kA * kSignal + kB);
    for (auto& s : img.samples)
        s = std::uint16_t(std::clamp(kSignal + sigma * g(rng), 0.0, 1.0) * kWhite);
    return img;
}

std::vector<std::uint8_t> pixels(const orion::pipe::DevelopPipeline& p) {
    const auto row = p.outputWidth() * orion::gpu::bytesPerPixel(p.output().format());
    std::vector<std::uint8_t> bytes(row * p.outputHeight());
    p.output().download(bytes.data(), row, p.outputWidth(), p.outputHeight());
    return bytes;
}

}  // namespace

/// §2: a layer's denoise acts through its coverage and nowhere else, and a
/// layer at zero is a disabled chain, not one running at zero.
void testDenoiseMaskedGpu() {
    section("Denoise: the masked chain (GPU)");
    auto device = deviceOrReport();
    if (!device) return;

    constexpr std::uint32_t kN = 512;
    const auto img = noisyGrey(kN, kN);
    report(orion::raw::estimateNoise(img).measured, "the fixture's noise is measured");

    orion::pipe::DevelopPipeline d(*device, ORION_SHADER_DIR, img);
    // The export's half-float tail: no dither, so the grain measured below is
    // the picture's rather than the eight-bit pattern laid over it.
    d.setWideOutput(true);
    orion::pipe::Adjustments off;
    off.wb = d.asShotWhiteBalance();
    auto on = off;
    on.maskCount = 1;
    auto& c = on.maskComponents[0];
    c.kind = 2;
    c.center[0] = 0.3f; c.center[1] = 0.5f;
    c.radius[0] = c.radius[1] = 0.15f;
    on.layers[0].denoiseLuma = 3.0f;
    auto zero = on;
    zero.layers[0].denoiseLuma = 0.0f;

    const auto render = [&](const orion::pipe::Adjustments& a) {
        d.apply(a); d.render();
        return pixels(d);
    };
    const auto pOff = render(off);
    const auto pOn = render(on);
    // Disabled names a different graph state from `off` (a mask is live), so
    // this is the claim the chain's predicate makes, not a cache replay.
    const auto pZero = render(zero);

    report(d.output().format() == orion::gpu::PixelFormat::RGBA16Float,
           "the wide tail is half float, which the reads below assume");
    const std::uint32_t W = d.outputWidth();
    const auto at = [&](const std::vector<std::uint8_t>& p, std::uint32_t x,
                        std::uint32_t y, int ch) {
        __fp16 v;
        std::memcpy(&v, p.data() + ((std::size_t(y) * W + x) * 4 + std::size_t(ch)) * 2, 2);
        return double(v);
    };
    const auto Y = [&](const std::vector<std::uint8_t>& p, std::uint32_t x, std::uint32_t y) {
        return luma2020(float(at(p, x, y, 0)), float(at(p, x, y, 1)), float(at(p, x, y, 2)));
    };
    // Mean squared difference from the 3x3 box mean, over luma.
    const auto hf = [&](const std::vector<std::uint8_t>& p, float cx, float cy, float r) {
        double s = 0.0; int n = 0;
        const int x0 = int((cx - r) * kN), x1 = int((cx + r) * kN);
        const int y0 = int((cy - r) * kN), y1 = int((cy + r) * kN);
        for (int y = y0; y <= y1; ++y)
            for (int x = x0; x <= x1; ++x) {
                double box = 0.0;
                for (int dy = -1; dy <= 1; ++dy)
                    for (int dx = -1; dx <= 1; ++dx)
                        box += Y(p, std::uint32_t(x + dx), std::uint32_t(y + dy));
                const double e = Y(p, std::uint32_t(x), std::uint32_t(y)) - box / 9.0;
                s += e * e; ++n;
            }
        return s / n;
    };

    const double hOff = hf(pOff, 0.3f, 0.5f, 0.07f), hOn = hf(pOn, 0.3f, 0.5f, 0.07f);
    report(hOn < hOff * 0.5, "inside the radial the fine grain more than halves",
           "off " + std::to_string(hOff) + ", on " + std::to_string(hOn));

    // ⚠ Not zero: outside, the field is zero and the shrink reconstructs, but
    // through four half-float sums rather than none.
    double worst = 0.0, peak = 0.0;
    for (int ch = 0; ch < 3; ++ch) {
        double s = 0.0; int n = 0;
        for (std::uint32_t y = std::uint32_t(0.43f * kN); y < std::uint32_t(0.57f * kN); ++y)
            for (std::uint32_t x = std::uint32_t(0.73f * kN); x < std::uint32_t(0.87f * kN); ++x) {
                const double e = std::abs(at(pOn, x, y, ch) - at(pOff, x, y, ch));
                s += e; peak = std::max(peak, e); ++n;
            }
        worst = std::max(worst, s / n);
    }
    std::printf("  grain inside %.3g -> %.3g; outside |on - off| mean %.2g, max %.2g\n",
                hOff, hOn, worst, peak);
    report(worst <= 2e-3, "outside the radial the picture is the unmasked one",
           "mean |on - off| " + std::to_string(worst) + " (worst channel)");

    report(pZero == pOff, "a layer at zero denoise renders byte-identical to no mask chain");
    report(pOn != pOff, "and the chain does change the frame when it runs");
}

/// §1's default is the shipped chain: `denoiseDetail = 0` is a value the
/// predicate compares, and pushing it must not move a byte.
void testDenoiseDetailZeroIsShippedGpu() {
    section("Denoise: Detail 0 is the shipped chain (GPU)");
    auto device = deviceOrReport();
    if (!device) return;

    const auto img = noisyGrey(256, 256);
    orion::pipe::DevelopPipeline d(*device, ORION_SHADER_DIR, img);
    orion::pipe::Adjustments base;
    base.wb = d.asShotWhiteBalance();
    base.denoiseLuma = 2.0f;
    base.denoiseColor = 1.0f;
    d.apply(base); d.render();
    const auto never = pixels(d);

    auto explicitZero = base;
    explicitZero.denoiseDetail = 0.0f;
    explicitZero.denoiseLuma = 2.0f;
    // A moved-then-restored Detail forces the blocks to be pushed again.
    auto moved = base;
    moved.denoiseDetail = 0.5f;
    d.apply(moved); d.render();
    const auto half = pixels(d);
    // The blurs read only the frame's size; a Detail drag that reran them
    // would be four full-resolution passes for nothing. ⚠ The second tick, not
    // the first: the pool frees a blur after its last read once, and the
    // first tick after that rebuilds it (decision #219, `Pipeline::maybeFree`).
    auto tick = moved;
    tick.denoiseDetail = 0.75f;
    d.apply(tick); d.render();
    std::set<std::string> ran;
    for (const auto& n : d.graph().lastRun())
        if (n.executed) ran.insert(n.name);
    report(!ran.count("denoise:blur 0") && ran.count("denoise:shrink 0"),
           "a Detail move reruns the shrinks and not the blurs");
    d.apply(explicitZero); d.render();
    report(pixels(d) == never, "Detail 0 pushed explicitly renders byte-identical");
    report(half != never, "and Detail 0.5 is a different frame, so the push was live");
}
