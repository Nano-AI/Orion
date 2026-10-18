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

namespace {

/// Luma of a wide (half-float RGBA) develop output at a pixel.
double outLuma(const std::vector<std::uint8_t>& p, std::uint32_t W, std::uint32_t x,
               std::uint32_t y) {
    __fp16 v[3];
    std::memcpy(v, p.data() + (std::size_t(y) * W + x) * 8, 6);
    return luma2020(float(v[0]), float(v[1]), float(v[2]));
}

/// Luma's RMS deviation from its 3x3 box mean over a rectangle: the grain.
double grainRms(const std::vector<std::uint8_t>& p, std::uint32_t W, std::uint32_t x0,
                std::uint32_t x1, std::uint32_t y0, std::uint32_t y1) {
    double s = 0.0; int n = 0;
    for (std::uint32_t y = y0; y < y1; ++y)
        for (std::uint32_t x = x0; x < x1; ++x) {
            double box = 0.0;
            for (int dy = -1; dy <= 1; ++dy)
                for (int dx = -1; dx <= 1; ++dx)
                    box += outLuma(p, W, x + std::uint32_t(dx), y + std::uint32_t(dy));
            const double e = outLuma(p, W, x, y) - box / 9.0;
            s += e * e; ++n;
        }
    return std::sqrt(s / n);
}

}  // namespace

/// Texture: Starck et al. 2003's noise-aware gain on the two finest starlet
/// scales. research/high-iso.md §2.
///
/// One mosaic, three regions: flat noise top left, a period-4 luma ripple at
/// six deviations top right, and a step from 0.25 to 0.5 across the bottom.
/// ⚠ The ripple is sin(πx/2), which is zero on every fourth column, so the
/// estimator's same-colour second differences (columns 0, 2, 4 mod 4) never
/// see it and the profile is the noise's alone; the step gives the fit its
/// lever arm.
void testTextureGpu() {
    section("Denoise: Texture, the noise-aware fine-scale gain (GPU)");
    auto device = deviceOrReport();
    if (!device) return;

    constexpr std::uint32_t kN = 256;
    constexpr double kA = 4.0e-4, kB = 4.0e-5, kSignal = 0.25;
    const double sigma = std::sqrt(kA * kSignal + kB);
    auto img = noisyGrey(kN, kN, kSignal, kA, kB, 4242);
    {
        std::mt19937 rng(77);
        std::normal_distribution<double> g(0.0, 1.0);
        for (std::uint32_t y = 0; y < kN; ++y)
            for (std::uint32_t x = 0; x < kN; ++x) {
                double v = -1.0;
                if (y < kN / 2 && x >= kN / 2)
                    v = kSignal + 6.0 * sigma * std::sin(M_PI * 0.5 * double(x));
                else if (y >= kN / 2 && x >= kN / 2)
                    v = 2.0 * kSignal;
                if (v < 0.0) continue;
                const double s = v + std::sqrt(kA * v + kB) * g(rng);
                img.samples[std::size_t(y) * kN + x] =
                    std::uint16_t(std::clamp(s, 0.0, 1.0) * 16383.0);
            }
    }
    const auto prof = orion::raw::estimateNoise(img);
    std::printf("  profile a %.3g b %.3g (true %.3g, %.3g)\n", prof.a, prof.b, kA, kB);
    report(prof.measured, "the texture fixture's noise is measured");

    orion::pipe::DevelopPipeline d(*device, ORION_SHADER_DIR, img);
    d.setWideOutput(true);
    orion::pipe::Adjustments off;
    off.wb = d.asShotWhiteBalance();
    const std::uint32_t W = d.outputWidth();
    const auto render = [&](float texture, float luma = 0.0f) {
        auto a = off;
        a.texture = texture;
        a.denoiseLuma = luma;
        d.apply(a); d.render();
        return pixels(d);
    };
    const auto flat = [&](const std::vector<std::uint8_t>& p) {
        return grainRms(p, W, 16, kN / 2 - 16, 16, kN / 2 - 16);
    };
    // Projection onto the ripple's own sine, over the ripple's quadrant.
    const auto ripple = [&](const std::vector<std::uint8_t>& p) {
        double s = 0.0; int n = 0;
        for (std::uint32_t y = 16; y < kN / 2 - 16; ++y)
            for (std::uint32_t x = kN / 2 + 16; x < kN - 16; ++x) {
                s += outLuma(p, W, x, y) * std::sin(M_PI * 0.5 * double(x)); ++n;
            }
        return 2.0 * s / n;
    };
    const auto mean = [&](const std::vector<std::uint8_t>& p, std::uint32_t x0,
                          std::uint32_t x1, std::uint32_t y0, std::uint32_t y1) {
        double s = 0.0; int n = 0;
        for (std::uint32_t y = y0; y < y1; ++y)
            for (std::uint32_t x = x0; x < x1; ++x) { s += outLuma(p, W, x, y); ++n; }
        return s / n;
    };

    const auto executed = [&] {
        std::set<std::string> ran;
        for (const auto& n : d.graph().lastRun())
            if (n.executed) ran.insert(n.name);
        return ran;
    };
    const auto p0 = render(0.0f);
    report(!executed().count("denoise:shrink 0"), "with every strength at zero the chain is off");
    // ⚠ Read on the tick that switches it on: later ticks reuse the blurs,
    // which read only the frame.
    const auto pHalf = render(0.5f);
    const auto ran = executed();
    report(ran.count("denoise:shrink 0") && ran.count("denoise:blur 3"),
           "Texture 0.5 alone, at Luminance 0 and Color 0, runs the chain");
    const auto pUp = render(1.0f);
    const auto pDown = render(-1.0f);

    const double r0 = ripple(p0), rUp = ripple(pUp), rDown = ripple(pDown);
    const double f0 = flat(p0), fUp = flat(pUp), fHalf = flat(pHalf);
    const double meanShift = std::abs(mean(pUp, 16, kN / 2 - 16, 16, kN / 2 - 16) -
                                      mean(p0, 16, kN / 2 - 16, 16, kN / 2 - 16));
    double stepMove = 0.0;
    for (const auto& span : {std::pair{16u, kN / 2 - 16}, std::pair{kN / 2 + 16, kN - 16}})
        stepMove = std::max(stepMove,
                            std::abs(mean(pUp, span.first, span.second, kN / 2 + 16, kN - 16) -
                                     mean(p0, span.first, span.second, kN / 2 + 16, kN - 16)));
    std::printf("  ripple %.5f -> x%.3f at +1, x%.3f at -1; flat grain %.5f -> "
                "%+.2f%% at +1, %+.2f%% at +0.5; mean shift %.2g; step %.2g\n",
                r0, rUp / r0, rDown / r0, f0, 100.0 * (fUp / f0 - 1.0),
                100.0 * (fHalf / f0 - 1.0), meanShift, stepMove);

    report(rUp / r0 >= 1.3 && rUp / r0 <= 2.5, "Texture +1 lifts the ripple by 1.3-2.5x",
           "x" + std::to_string(rUp / r0));
    report(fUp <= f0 * 1.05, "and the flat grain by no more than 5%",
           std::to_string(f0) + " -> " + std::to_string(fUp));
    report(fHalf <= f0 * 1.05, "Texture 0.5 alone keeps the grain within 5% as well",
           std::to_string(f0) + " -> " + std::to_string(fHalf));
    report(meanShift < 1e-3, "the gain is odd in w, so the flat mean holds",
           std::to_string(meanShift));
    report(stepMove < 1e-3, "and a step far above m passes at gain one away from its edge",
           std::to_string(stepMove));
    report(rDown / r0 < 0.8, "Texture -1 attenuates the ripple below 0.8x",
           "x" + std::to_string(rDown / r0));

    // 0 is today's chain: a moved-then-restored Texture under Luminance 2
    // must not move a byte.
    const auto base = render(0.0f, 2.0f);
    (void)render(0.5f, 2.0f);
    report(render(0.0f, 2.0f) == base, "Texture 0 pushed explicitly renders byte-identical");

    // ── The same control on a clean frame ───────────────────────────────
    //
    // With `m` at 30σ alone the gain was a high-ISO-only control: on a clean
    // frame σ is so small that every real coefficient sat above 30σ and
    // passed at gain one (measured 0.0001-0.0002 on the bench's low-ISO
    // frames, below its "moved at all" line). `m` is now the larger of 30σ
    // and a quarter of the local brightness, so a ripple an eighth of the
    // level deep is texture here too.
    {
        constexpr double kA2 = 1.0e-6, kB2 = 1.0e-7, kRipple = 0.02;
        auto clean = noisyGrey(kN, kN, kSignal, kA2, kB2, 9191);
        std::mt19937 rng(78);
        std::normal_distribution<double> g(0.0, 1.0);
        for (std::uint32_t y = 0; y < kN; ++y)
            for (std::uint32_t x = 0; x < kN; ++x) {
                double v = -1.0;
                if (y < kN / 2 && x >= kN / 2)
                    v = kSignal + kRipple * std::sin(M_PI * 0.5 * double(x));
                else if (y >= kN / 2 && x >= kN / 2)
                    v = 2.0 * kSignal;
                if (v < 0.0) continue;
                const double s = v + std::sqrt(kA2 * v + kB2) * g(rng);
                clean.samples[std::size_t(y) * kN + x] =
                    std::uint16_t(std::clamp(s, 0.0, 1.0) * 16383.0);
            }
        const auto prof2 = orion::raw::estimateNoise(clean);
        report(prof2.measured, "the clean fixture's noise is measured too",
               "a " + std::to_string(prof2.a) + " b " + std::to_string(prof2.b));

        orion::pipe::DevelopPipeline d2(*device, ORION_SHADER_DIR, clean);
        d2.setWideOutput(true);
        orion::pipe::Adjustments off2;
        off2.wb = d2.asShotWhiteBalance();
        const std::uint32_t W2 = d2.outputWidth();
        const auto render2 = [&](float texture) {
            auto a = off2; a.texture = texture;
            d2.apply(a); d2.render();
            return pixels(d2);
        };
        const auto ripple2 = [&](const std::vector<std::uint8_t>& p) {
            double s = 0.0; int n = 0;
            for (std::uint32_t y = 16; y < kN / 2 - 16; ++y)
                for (std::uint32_t x = kN / 2 + 16; x < kN - 16; ++x) {
                    s += outLuma(p, W2, x, y) * std::sin(M_PI * 0.5 * double(x)); ++n;
                }
            return 2.0 * s / n;
        };
        const auto c0 = render2(0.0f), cUp = render2(1.0f), cDown = render2(-1.0f);
        const double cr0 = ripple2(c0), crUp = ripple2(cUp), crDown = ripple2(cDown);
        std::printf("  clean card: ripple x%.3f at +1, x%.3f at -1\n", crUp / cr0, crDown / cr0);
        report(crUp / cr0 >= 1.3, "on a clean frame Texture +1 still lifts an eighth-deep ripple by 1.3x or more");
        report(crDown / cr0 < 0.8, "and Texture -1 attenuates it below 0.8x");
        const double meanShift2 = std::fabs(mean(cUp, 16, kN / 2 - 16, 16, kN / 2 - 16) -
                                            mean(c0, 16, kN / 2 - 16, 16, kN / 2 - 16));
        report(meanShift2 < 1e-3, "with the flat mean held", std::to_string(meanShift2));
    }
}

/// Smoothness: chroma at j = 4, 5, quarter resolution. research/high-iso.md §1.
///
/// A grey card with fine Poisson-Gaussian noise and a smooth chroma field at
/// a 24 px block scale, zero luminance after white balance, which is the
/// shape demosaicing gives chroma noise, at about the fine noise's own coarse
/// chroma amplitude. A garrote removes what sits under its threshold and keeps
/// what stands above it, so a field ten times this is mostly kept (measured:
/// 36% removed at 1e-2), which is the "small saturated objects" line in §1.
///
/// ⚠ With the plan's luma model for σ, Smoothness 1 at Color 3 removed 37% of
/// the coarse energy here against a 67% plateau; `coarseChromaNoise` is the
/// deviation that fixed it (66%).
void testDenoiseSmoothnessGpu() {
    section("Denoise: Smoothness, coarse chroma at quarter resolution (GPU)");

    // ‖W_j‖₂ from the B3 taps, against the header's constants.
    {
        constexpr int kG = 512;
        std::vector<double> c(std::size_t(kG) * kG, 0.0), n(c.size()), t(c.size());
        c[std::size_t(kG / 2) * kG + kG / 2] = 1.0;
        const double h[5] = {1.0 / 16, 4.0 / 16, 6.0 / 16, 4.0 / 16, 1.0 / 16};
        const auto norms = orion::pipe::DevelopPipeline::starletScaleNorms();
        for (int j = 0; j < 6; ++j) {
            const int s = 1 << j;
            for (int y = 0; y < kG; ++y)
                for (int x = 0; x < kG; ++x) {
                    double v = 0.0;
                    for (int k = 0; k < 5; ++k)
                        v += h[k] * c[std::size_t(y) * kG + std::size_t(std::clamp(x + (k - 2) * s, 0, kG - 1))];
                    t[std::size_t(y) * kG + x] = v;
                }
            double e = 0.0;
            for (int y = 0; y < kG; ++y)
                for (int x = 0; x < kG; ++x) {
                    double v = 0.0;
                    for (int k = 0; k < 5; ++k)
                        v += h[k] * t[std::size_t(std::clamp(y + (k - 2) * s, 0, kG - 1)) * kG + x];
                    const std::size_t i = std::size_t(y) * kG + x;
                    n[i] = v;
                    e += (c[i] - v) * (c[i] - v);
                }
            c.swap(n);
            const double got = std::sqrt(e);
            const double rel = std::abs(double(norms[std::size_t(j)]) - got) / got;
            std::printf("  ||W_%d|| %.5f, header %.4f\n", j, got, double(norms[std::size_t(j)]));
            report(rel < 5e-3, "the header's ||W_" + std::to_string(j) +
                                   "|| matches the B3 taps to three figures",
                   std::to_string(got) + " against " + std::to_string(norms[std::size_t(j)]));
        }
    }

    auto device = deviceOrReport();
    if (!device) return;

    constexpr std::uint32_t kN = 384, kBlock = 24;
    constexpr double kA = 4.0e-4, kB = 4.0e-5, kSignal = 0.25;
    auto img = noisyGrey(kN, kN, kSignal, kA, kB, 9001);
    // Block offsets on the zero-luminance plane after white balance, bilinear
    // between block centres, then divided by each channel's multiplier so the
    // chain sees them after linearize. CFA RGGB: (x & 1, y & 1) = (0,0) R,
    // (1,1) B, else G.
    const double wb[3] = {2.0, 1.0, 1.5};
    // About the fine noise's own coarse chroma: the field doubles the energy
    // the 16 px box reads with no field at all (4.3e-7 to 8.6e-7).
    constexpr double kField = 3.0e-3;   // per-channel deviation, after white balance
    constexpr std::uint32_t kBlocks = kN / kBlock + 2;
    std::vector<std::array<double, 3>> block(std::size_t(kBlocks) * kBlocks);
    {
        std::mt19937 rng(5);
        std::normal_distribution<double> g(0.0, kField);
        for (auto& b : block) {
            const double u = g(rng), v = g(rng);
            b = {u, -(0.2627 * u + 0.0593 * v) / 0.6780, v};
        }
    }
    for (std::uint32_t y = 0; y < kN; ++y)
        for (std::uint32_t x = 0; x < kN; ++x) {
            const double fx = double(x) / kBlock, fy = double(y) / kBlock;
            const auto bx = std::uint32_t(fx), by = std::uint32_t(fy);
            const double ax = fx - bx, ay = fy - by;
            const int ch = ((x & 1) == 0 && (y & 1) == 0) ? 0 : ((x & 1) && (y & 1)) ? 2 : 1;
            const auto at = [&](std::uint32_t i, std::uint32_t j) {
                return block[std::size_t(j) * kBlocks + i][std::size_t(ch)];
            };
            const double f = (1 - ay) * ((1 - ax) * at(bx, by) + ax * at(bx + 1, by)) +
                             ay * ((1 - ax) * at(bx, by + 1) + ax * at(bx + 1, by + 1));
            auto& s = img.samples[std::size_t(y) * kN + x];
            s = std::uint16_t(std::clamp(double(s) + f / wb[ch] * 16383.0, 0.0, 16383.0));
        }
    report(orion::raw::estimateNoise(img).measured, "the smoothness fixture's noise is measured");

    orion::pipe::DevelopPipeline d(*device, ORION_SHADER_DIR, img);
    d.setWideOutput(true);
    orion::pipe::Adjustments base;
    base.wb = d.asShotWhiteBalance();
    base.denoiseColor = 3.0f;
    const std::uint32_t W = d.outputWidth();
    const auto render = [&](float smooth, float color = 3.0f) {
        auto a = base;
        a.denoiseSmoothness = smooth;
        a.denoiseColor = color;
        d.apply(a); d.render();
        return pixels(d);
    };
    const auto upRan = [&] {
        for (const auto& n : d.graph().lastRun())
            if (n.name == "denoise:chroma up") return n.executed;
        return false;
    };
    // Chroma after a 16 px box: what is left of the blotches once the fine
    // grain has averaged away. Energy about its own mean, over the interior.
    const auto coarseChroma = [&](const std::vector<std::uint8_t>& p) {
        std::vector<std::array<double, 3>> chroma(std::size_t(W) * kN);
        for (std::uint32_t y = 0; y < kN; ++y)
            for (std::uint32_t x = 0; x < W; ++x) {
                __fp16 v[3];
                std::memcpy(v, p.data() + (std::size_t(y) * W + x) * 8, 6);
                const double l = luma2020(float(v[0]), float(v[1]), float(v[2]));
                chroma[std::size_t(y) * W + x] = {v[0] - l, v[1] - l, v[2] - l};
            }
        std::vector<std::array<double, 3>> box;
        for (std::uint32_t y = 64; y + 80 < kN; y += 4)
            for (std::uint32_t x = 64; x + 80 < W; x += 4) {
                std::array<double, 3> s{};
                for (std::uint32_t j = 0; j < 16; ++j)
                    for (std::uint32_t i = 0; i < 16; ++i)
                        for (int c = 0; c < 3; ++c)
                            s[std::size_t(c)] += chroma[std::size_t(y + j) * W + x + i][std::size_t(c)];
                for (auto& v : s) v /= 256.0;
                box.push_back(s);
            }
        std::array<double, 3> m{};
        for (const auto& b : box) for (int c = 0; c < 3; ++c) m[std::size_t(c)] += b[std::size_t(c)];
        for (auto& v : m) v /= double(box.size());
        double e = 0.0;
        for (const auto& b : box)
            for (int c = 0; c < 3; ++c)
                e += (b[std::size_t(c)] - m[std::size_t(c)]) * (b[std::size_t(c)] - m[std::size_t(c)]);
        return e / double(box.size());
    };

    const auto p0 = render(0.0f);
    report(!upRan(), "Smoothness 0 leaves the six nodes off");
    const auto p1 = render(1.0f);
    report(upRan(), "Smoothness 1 at Color 3 runs them");
    const double e0 = coarseChroma(p0), e1 = coarseChroma(p1);
    // ⚠ Luma is judged against Color's own leak, not against zero. The chain
    // splits luma with Rec.2020 weights in *camera* RGB, before the matrix, so
    // a chroma move there is not luma-free in the output: Color 3's own
    // full-resolution shrink moves output luma by 0.035 on this card.
    const auto pColor0 = render(0.0f, 0.0f);
    double worstLuma = 0.0, worstColor = 0.0, meanLuma = 0.0, meanColor = 0.0;
    int interior = 0;
    for (std::uint32_t y = 0; y < kN; ++y)
        for (std::uint32_t x = 0; x < W; ++x) {
            const double dy = outLuma(p1, W, x, y) - outLuma(p0, W, x, y);
            const double dc = outLuma(p0, W, x, y) - outLuma(pColor0, W, x, y);
            worstLuma = std::max(worstLuma, std::abs(dy));
            worstColor = std::max(worstColor, std::abs(dc));
            // The mean away from the borders, where the demosaic's own edge
            // colour is what the coarse scales remove.
            if (x >= 64 && x + 64 < W && y >= 64 && y + 64 < kN) {
                meanLuma += dy; meanColor += dc; ++interior;
            }
        }
    meanLuma /= interior;
    meanColor /= interior;
    std::printf("  coarse chroma energy %.3g -> %.3g (%.0f%% removed); luma: worst |dY| "
                "%.2g (Color 3's own %.2g), interior mean dY %.2g (Color's %.2g)\n",
                e0, e1, 100.0 * (1.0 - e1 / e0), worstLuma, worstColor, meanLuma, meanColor);
    report(e1 <= 0.4 * e0, "Smoothness 1 removes at least 60% of the coarse chroma energy",
           std::to_string(e0) + " -> " + std::to_string(e1));
    report(worstLuma <= 0.1 * worstColor && std::abs(meanLuma) < 1e-3,
           "and moves luma by under a tenth of Color's own leak, mean under 1e-3",
           std::to_string(worstLuma) + " against " + std::to_string(worstColor) +
               ", mean " + std::to_string(meanLuma));

    (void)render(0.5f);
    report(render(0.0f) == p0, "Smoothness 0 pushed explicitly renders byte-identical");
    (void)render(1.0f, 0.0f);
    report(!upRan(), "Color 0 runs nothing, whatever Smoothness says");
}
