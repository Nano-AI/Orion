// Noise, denoise, highlight recovery, linearize, grading offsets and lenses.
//
// Split out of main.cpp 2026-07-31; see harness.h.

#include "harness.h"

void testNoiseEstimator() {
    section("Noise profile");

    constexpr std::uint32_t kW = 512, kH = 512;
    constexpr float kWhite = 16383.0f;
    constexpr double kTrueA = 4.0e-5, kTrueB = 9.0e-6;

    // A fixed generator: a flaky test that depends on the weather is worse
    // than no test.
    std::mt19937 rng(12345);
    std::normal_distribution<double> gauss(0.0, 1.0);

    orion::raw::BayerImage image;
    image.width = kW;
    image.height = kH;
    image.white = static_cast<std::uint16_t>(kWhite);
    image.black = {0, 0, 0, 0};
    image.samples.resize(static_cast<std::size_t>(kW) * kH);

    for (std::uint32_t y = 0; y < kH; ++y) {
        for (std::uint32_t x = 0; x < kW; ++x) {
            // A horizontal ramp, so every brightness bin is populated. Locally
            // smooth, which is what the differencing estimator assumes.
            const double signal = 0.02 + 0.9 * (double(x) / (kW - 1));
            const double sigma = std::sqrt(kTrueA * signal + kTrueB);
            const double v = std::clamp(signal + sigma * gauss(rng), 0.0, 1.0);
            image.samples[std::size_t(y) * kW + x] =
                static_cast<std::uint16_t>(v * kWhite);
        }
    }

    const auto profile = orion::raw::estimateNoise(image);
    report(profile.measured, "the estimator produced a fit");

    // Twenty percent: this is a robust median-based fit over twelve bins from
    // one frame, not a laboratory measurement, and the denoise threshold moves
    // with the square root of it.
    checkNear(profile.a, kTrueA, kTrueA * 0.2, "the shot-noise term is recovered");
    checkNear(profile.b, kTrueB, kTrueB * 0.35, "the read-noise term is recovered");

    // A clean frame must not be reported as noisy, or denoise would smooth an
    // image that has nothing to remove.
    orion::raw::BayerImage clean = image;
    for (std::uint32_t y = 0; y < kH; ++y) {
        for (std::uint32_t x = 0; x < kW; ++x) {
            const double signal = 0.02 + 0.9 * (double(x) / (kW - 1));
            clean.samples[std::size_t(y) * kW + x] =
                static_cast<std::uint16_t>(signal * kWhite);
        }
    }
    const auto quiet = orion::raw::estimateNoise(clean);
    report(quiet.a < kTrueA * 0.05 && quiet.b < kTrueB * 0.2,
           "a noiseless frame measures as noiseless",
           "a = " + std::to_string(quiet.a) + ", b = " + std::to_string(quiet.b));

    // A frame with almost no brightness range — a night sky, which is exactly
    // the kind of frame that needs denoising. Equal-width bins put every pixel
    // in the bottom twelfth, eleven bins came back empty, and the fit was
    // abandoned: the denoiser silently did nothing on the frames that needed it
    // most. It must now come back measured, with the constant term carrying the
    // model and no slope invented from a lever arm that is not there.
    orion::raw::BayerImage dark;
    dark.width = kW; dark.height = kH; dark.white = static_cast<std::uint16_t>(kWhite);
    dark.black = {0, 0, 0, 0};
    dark.samples.resize(static_cast<std::size_t>(kW) * kH);
    {
        std::mt19937 darkRng(4242);
        std::normal_distribution<double> g(0.0, 1.0);
        const double signal = 0.03;
        const double sigma = std::sqrt(kTrueA * signal + kTrueB);
        for (auto& sample : dark.samples) {
            const double v = std::clamp(signal + sigma * g(darkRng), 0.0, 1.0);
            sample = static_cast<std::uint16_t>(v * kWhite);
        }
    }
    const auto night = orion::raw::estimateNoise(dark);
    report(night.measured, "a frame with no brightness range is still measured");
    checkNear(night.b, kTrueA * 0.03 + kTrueB, (kTrueA * 0.03 + kTrueB) * 0.35,
              "the constant term carries a flat frame's whole noise level");

    // Too small to fit is reported as unmeasured rather than guessed at.
    orion::raw::BayerImage tiny;
    tiny.width = 8; tiny.height = 8; tiny.white = 4095;
    tiny.samples.assign(64, 1000);
    report(!orion::raw::estimateNoise(tiny).measured,
           "a frame too small to fit is not fitted");
}

/// The wavelet denoise, on a real GPU.
///
/// Two things have to be true at once, and each is easy to get alone: noise in
/// a flat area has to fall, and an edge has to survive. A denoiser that only
/// does the first is a blur.
void testDenoiseGpu() {
    section("Wavelet denoise (GPU)");

    constexpr std::uint32_t kW = 256, kH = 256;

    std::unique_ptr<orion::gpu::Device> device;
    try {
        device = orion::gpu::Device::create();
    } catch (const std::exception& e) {
        report(false, "Metal device available", e.what());
        return;
    }

    auto blurLib = orion::gpu::Library::createFromFile(
        *device, std::string(ORION_SHADER_DIR) + "/atrousBlur.metallib");
    auto blurKernel = orion::gpu::Kernel::create(*device, *blurLib, "atrousBlur");
    auto shrinkLib = orion::gpu::Library::createFromFile(
        *device, std::string(ORION_SHADER_DIR) + "/atrousShrink.metallib");
    auto shrinkKernel = orion::gpu::Kernel::create(*device, *shrinkLib, "atrousShrink");

    // Left half dark, right half bright, plus noise. The step down the middle
    // is the edge that must survive.
    constexpr float kDark = 0.20f, kBright = 0.60f, kSigma = 0.03f;
    std::mt19937 rng(999);
    std::normal_distribution<float> gauss(0.0f, kSigma);

    std::vector<__fp16> input(std::size_t(kW) * kH * 4);
    std::vector<float> cleanSignal(std::size_t(kW) * kH);
    for (std::uint32_t y = 0; y < kH; ++y) {
        for (std::uint32_t x = 0; x < kW; ++x) {
            const std::size_t i = std::size_t(y) * kW + x;
            const float base = (x < kW / 2) ? kDark : kBright;
            cleanSignal[i] = base;
            const float v = base + gauss(rng);
            input[i * 4 + 0] = static_cast<__fp16>(v);
            input[i * 4 + 1] = static_cast<__fp16>(v);
            input[i * 4 + 2] = static_cast<__fp16>(v);
            input[i * 4 + 3] = 1;
        }
    }

    const auto make = [&] {
        return orion::gpu::Texture::create(*device, kW, kH,
                                           orion::gpu::PixelFormat::RGBA16Float);
    };
    auto c0 = make();
    c0->upload(input.data(), std::size_t(kW) * 4 * sizeof(__fp16));

    constexpr int kScales = 4;
    constexpr float kScaleNorm[kScales] = {0.8907f, 0.2007f, 0.0855f, 0.0412f};

    std::vector<std::unique_ptr<orion::gpu::Texture>> blurs;
    for (int j = 0; j < kScales; ++j) blurs.push_back(make());

    // Blur chain: c_1..c_4.
    for (int j = 0; j < kScales; ++j) {
        orion::pipe::params::AtrousBlur bp{};
        bp.size[0] = kW; bp.size[1] = kH;
        bp.step = 1 << j;
        orion::gpu::CommandBuffer cb(*device);
        cb.dispatch(*blurKernel,
                    {j == 0 ? c0.get() : blurs[std::size_t(j - 1)].get(),
                     blurs[std::size_t(j)].get()},
                    &bp, sizeof bp, kW, kH);
        cb.commitAndWait();
    }

    // Shrink chain, coarse to fine, starting from the residual.
    std::vector<std::unique_ptr<orion::gpu::Texture>> shrinks;
    for (int j = 0; j < kScales; ++j) shrinks.push_back(make());

    for (int j = kScales - 1; j >= 0; --j) {
        orion::pipe::params::AtrousShrink sp{};
        sp.size[0] = kW; sp.size[1] = kH;
        // The synthetic noise is constant, so it is all in the b term.
        sp.noiseA = 0.0f;
        sp.noiseB = kSigma * kSigma;
        sp.scaleNorm = kScaleNorm[j];
        sp.strength = 2.0f;
        sp.chromaBoost = 1.0f;

        const orion::gpu::Texture* fine =
            (j == 0) ? c0.get() : blurs[std::size_t(j - 1)].get();
        const orion::gpu::Texture* coarse = blurs[std::size_t(j)].get();
        const orion::gpu::Texture* accum =
            (j == kScales - 1) ? blurs[std::size_t(j)].get()
                               : shrinks[std::size_t(j + 1)].get();

        orion::gpu::CommandBuffer cb(*device);
        cb.dispatch(*shrinkKernel,
                    {fine, coarse, accum, shrinks[std::size_t(j)].get()},
                    &sp, sizeof sp, kW, kH);
        cb.commitAndWait();
    }

    std::vector<__fp16> out(std::size_t(kW) * kH * 4);
    shrinks[0]->download(out.data(), std::size_t(kW) * 4 * sizeof(__fp16), kW, kH);

    // Residual against the clean signal, well away from the edge and the
    // border, where the transform has nothing either side to work with.
    const auto residual = [&](const std::vector<__fp16>& img) {
        double sum = 0.0; int n = 0;
        for (std::uint32_t y = 24; y < kH - 24; ++y) {
            for (std::uint32_t x = 24; x < kW / 2 - 24; ++x) {
                const std::size_t i = std::size_t(y) * kW + x;
                const double d = double(img[i * 4]) - double(cleanSignal[i]);
                sum += d * d; ++n;
            }
        }
        return std::sqrt(sum / std::max(n, 1));
    };

    const double before = residual(input);
    const double after = residual(out);

    report(after < before * 0.5,
           "denoise halves the error in a flat area",
           "before " + std::to_string(before) + ", after " + std::to_string(after));

    // The edge must still be an edge. Compare the mean either side, two pixels
    // clear of the boundary so the transform's own support is not the subject.
    const auto meanNear = [&](const std::vector<__fp16>& img, std::uint32_t x0,
                              std::uint32_t x1) {
        double sum = 0.0; int n = 0;
        for (std::uint32_t y = 40; y < kH - 40; ++y) {
            for (std::uint32_t x = x0; x < x1; ++x) {
                sum += double(img[(std::size_t(y) * kW + x) * 4]); ++n;
            }
        }
        return sum / std::max(n, 1);
    };

    const double left = meanNear(out, kW / 2 - 12, kW / 2 - 4);
    const double right = meanNear(out, kW / 2 + 4, kW / 2 + 12);
    checkNear(left, kDark, 0.02, "the dark side of the edge keeps its level");
    checkNear(right, kBright, 0.02, "the bright side of the edge keeps its level");
    report(right - left > (kBright - kDark) * 0.9,
           "the edge survives denoising",
           "step " + std::to_string(right - left));

    // Strength zero must reconstruct exactly: I = c_J + sum of w_j. If that is
    // not an identity, every other setting is built on sand.
    for (int j = kScales - 1; j >= 0; --j) {
        orion::pipe::params::AtrousShrink sp{};
        sp.size[0] = kW; sp.size[1] = kH;
        sp.strength = 0.0f;
        const orion::gpu::Texture* fine =
            (j == 0) ? c0.get() : blurs[std::size_t(j - 1)].get();
        const orion::gpu::Texture* accum =
            (j == kScales - 1) ? blurs[std::size_t(j)].get()
                               : shrinks[std::size_t(j + 1)].get();
        orion::gpu::CommandBuffer cb(*device);
        cb.dispatch(*shrinkKernel,
                    {fine, blurs[std::size_t(j)].get(), accum,
                     shrinks[std::size_t(j)].get()},
                    &sp, sizeof sp, kW, kH);
        cb.commitAndWait();
    }
    shrinks[0]->download(out.data(), std::size_t(kW) * 4 * sizeof(__fp16), kW, kH);

    double worst = 0.0;
    for (std::uint32_t i = 0; i < kW * kH; ++i) {
        worst = std::max(worst, std::abs(double(out[i * 4]) - double(input[i * 4])));
    }
    report(worst < 0.01, "the transform reconstructs exactly at zero strength",
           "worst " + std::to_string(worst));
}

/// Highlight reconstruction, on a real GPU.
///
/// The defect: a sensor clips per channel. For a *neutral* subject that happens
/// in every channel at once and costs only brightness — the interesting case is
/// a colored one. A warm cloud drives red hardest, so red reaches its stop
/// while green and blue are still reading, the ratio between the channels
/// changes, and the cloud turns cyan. This builds exactly that.
void testHighlightRecoveryGpu() {
    section("Highlight recovery (GPU)");

    constexpr std::uint32_t kW = 192, kH = 64;

    std::unique_ptr<orion::gpu::Device> device;
    try {
        device = orion::gpu::Device::create();
    } catch (const std::exception& e) {
        report(false, "Metal device available", e.what());
        return;
    }

    auto library = orion::gpu::Library::createFromFile(
        *device, std::string(ORION_SHADER_DIR) + "/highlightRecover.metallib");
    auto kernel = orion::gpu::Kernel::create(*device, *library, "highlightRecover");

    // Clipping levels after white balance, of the sort a daylight frame gives:
    // red and blue scaled up relative to green, so they stop higher.
    constexpr float kClipR = 2.0f, kClipG = 1.0f, kClipB = 1.5f;
    constexpr float kGamma = 0.97f;

    // A warm subject: post-white-balance the channels sit in the ratio
    // 2.8 : 1.0 : 1.05, so red reaches its stop while green is barely past
    // two-thirds of the way to its own.
    constexpr float kRatioR = 2.8f, kRatioG = 1.0f, kRatioB = 1.05f;

    // Left half a ramp that never clips anything, right half a highlight where
    // red alone is blown. The ramp matters: a fit needs the reference to *vary*
    // across the window, and a flat valid region would make the least squares
    // singular — which the shader detects and declines, correctly but untestably.
    constexpr float kHighlight = 0.85f;
    const std::uint32_t split = kW / 2;

    std::vector<__fp16> input(std::size_t(kW) * kH * 4);
    std::vector<float> scene(std::size_t(kW) * kH);
    for (std::uint32_t y = 0; y < kH; ++y) {
        for (std::uint32_t x = 0; x < kW; ++x) {
            const std::size_t i = std::size_t(y) * kW + x;
            const float v = (x < split)
                ? 0.25f + 0.44f * (float(x) / float(split - 1))
                : kHighlight;
            scene[i] = v;
            input[i * 4 + 0] = static_cast<__fp16>(std::min(kRatioR * v, kClipR));
            input[i * 4 + 1] = static_cast<__fp16>(std::min(kRatioG * v, kClipG));
            input[i * 4 + 2] = static_cast<__fp16>(std::min(kRatioB * v, kClipB));
            input[i * 4 + 3] = 1;
        }
    }

    auto src = orion::gpu::Texture::create(*device, kW, kH,
                                           orion::gpu::PixelFormat::RGBA16Float);
    auto dst = orion::gpu::Texture::create(*device, kW, kH,
                                           orion::gpu::PixelFormat::RGBA16Float);
    src->upload(input.data(), std::size_t(kW) * 4 * sizeof(__fp16));

    orion::pipe::params::Highlights hl{};
    hl.size[0] = kW; hl.size[1] = kH;
    hl.clipR = kClipR; hl.clipG = kClipG; hl.clipB = kClipB;
    hl.gamma = kGamma;
    hl.strength = 1.0f;

    const auto run = [&](std::vector<__fp16>& out) {
        orion::gpu::CommandBuffer cb(*device);
        cb.dispatch(*kernel, {src.get(), dst.get()}, &hl, sizeof hl, kW, kH);
        cb.commitAndWait();
        dst->download(out.data(), std::size_t(kW) * 4 * sizeof(__fp16), kW, kH);
    };

    std::vector<__fp16> out(std::size_t(kW) * kH * 4);
    run(out);

    // Four pixels into the highlight, so the window still reaches the valid
    // ramp behind it. Further in there is nothing to fit against and the shader
    // declines — the window's reach is a real limit of the method, not a bug.
    const std::uint32_t probe = split + 4;
    report(probe < kW, "the probe column is inside the frame");
    report(kRatioR * kHighlight > kClipR,
           "the probe is genuinely clipped, not merely near the threshold");
    report(kRatioG * kHighlight < kClipG * kGamma &&
           kRatioB * kHighlight < kClipB * kGamma,
           "green and blue are still reading at the probe");

    const std::size_t i = (std::size_t(kH / 2) * kW + probe) * 4;
    const double before = double(input[i]);
    const double after = double(out[i]);
    const double want = kRatioR * double(kHighlight);

    report(after > before + 0.05, "a clipped red channel is raised",
           "was " + std::to_string(before) + ", now " + std::to_string(after));
    checkNear(after, want, 0.10, "the rebuilt red is close to what red would have read");

    // Green and blue were never clipped there, and must come back untouched.
    checkNear(double(out[i + 1]), double(input[i + 1]), 1e-3,
              "an unclipped green is left alone");
    checkNear(double(out[i + 2]), double(input[i + 2]), 1e-3,
              "an unclipped blue is left alone");

    // Nothing with all three channels below their own levels may move at all.
    // A highlight tool that shifts the midtones is worse than one that does
    // nothing.
    double worstValid = 0.0;
    for (std::uint32_t y = 0; y < kH; ++y) {
        for (std::uint32_t x = 0; x < kW; ++x) {
            const std::size_t k = std::size_t(y) * kW + x;
            const float v = scene[k];
            if (kRatioR * v >= kClipR * kGamma) continue;
            if (kRatioG * v >= kClipG * kGamma) continue;
            if (kRatioB * v >= kClipB * kGamma) continue;
            for (int ch = 0; ch < 3; ++ch) {
                worstValid = std::max(worstValid,
                    std::abs(double(out[k * 4 + ch]) - double(input[k * 4 + ch])));
            }
        }
    }
    report(worstValid < 1e-3, "a wholly valid pixel is never touched",
           "worst " + std::to_string(worstValid));

    // Strength zero must be a pass-through, or the control does not turn off.
    hl.strength = 0.0f;
    run(out);
    double worstOff = 0.0;
    for (std::size_t k = 0; k < std::size_t(kW) * kH * 4; ++k) {
        worstOff = std::max(worstOff, std::abs(double(out[k]) - double(input[k])));
    }
    report(worstOff < 1e-6, "strength zero is a pass-through",
           "worst " + std::to_string(worstOff));

    // All three clipped has nothing left to correlate against, and must go
    // neutral rather than keep whatever hue three different stops implied.
    std::vector<__fp16> blown(std::size_t(kW) * kH * 4);
    for (std::uint32_t k = 0; k < kW * kH; ++k) {
        blown[k * 4 + 0] = static_cast<__fp16>(kClipR);
        blown[k * 4 + 1] = static_cast<__fp16>(kClipG);
        blown[k * 4 + 2] = static_cast<__fp16>(kClipB);
        blown[k * 4 + 3] = 1;
    }
    src->upload(blown.data(), std::size_t(kW) * 4 * sizeof(__fp16));
    hl.strength = 1.0f;
    run(out);

    const std::size_t mid = (std::size_t(kH / 2) * kW + kW / 2) * 4;
    const double r = double(out[mid]), g = double(out[mid + 1]), b = double(out[mid + 2]);
    report(std::abs(r - g) < 1e-3 && std::abs(g - b) < 1e-3,
           "a wholly blown highlight comes back neutral",
           "rgb " + std::to_string(r) + ", " + std::to_string(g) + ", "
                  + std::to_string(b));
}

/// Lens corrections, on a real GPU.
///
/// Three properties that are easy to get wrong and invisible by eye: the frame
/// corners must not move when distortion is applied, vignetting must divide
/// rather than multiply, and the chromatic-aberration controls must move red
/// and blue *relative to green* rather than all three together.
/// A blown highlight must come out neutral.
///
/// This is the bug that painted every light in a night frame magenta. A sensor
/// saturates at one count for all three channels, so a blown pixel arrives as
/// (S, S, S); white balance then multiplies each channel by its own gain and
/// what was a white light is, in ratio, the gains themselves. Every stage after
/// this one preserves ratios, so nothing downstream can undo it.
///
/// The test is worth its length because the failure is invisible to inspection:
/// the shader was three correct lines and one missing clamp, and the output was
/// a perfectly plausible image with colored lights in it.
void testLinearizeClipsToWhite() {
    section("Linearize clips a blown highlight to white");

    constexpr std::uint32_t kW = 64, kH = 64;
    constexpr float kWhite = 16383.0f, kBlack = 512.0f;

    // Gains of the shape a warm scene gives: red and blue lifted against green.
    // These are what a blown pixel would wear as a color if nothing clipped.
    constexpr float kGainR = 2.2f, kGainG = 1.0f, kGainB = 1.6f;

    std::unique_ptr<orion::gpu::Device> device;
    try {
        device = orion::gpu::Device::create();
    } catch (const std::exception& e) {
        report(false, "Metal device available", e.what());
        return;
    }

    auto library = orion::gpu::Library::createFromFile(
        *device, std::string(ORION_SHADER_DIR) + "/linearize.metallib");
    auto kernel = orion::gpu::Kernel::create(*device, *library, "linearize");

    // Top half blown, bottom half a neutral midtone. The midtone is the control:
    // a clamp that fixed the highlight by flattening everything would pass a
    // test that only looked at the highlight.
    constexpr float kMidRaw = kBlack + 0.25f * (kWhite - kBlack);
    std::vector<std::uint16_t> input(std::size_t(kW) * kH);
    for (std::uint32_t y = 0; y < kH; ++y) {
        for (std::uint32_t x = 0; x < kW; ++x) {
            input[std::size_t(y) * kW + x] = static_cast<std::uint16_t>(
                (y < kH / 2) ? kWhite : kMidRaw);
        }
    }

    auto src = orion::gpu::Texture::create(*device, kW, kH,
                                           orion::gpu::PixelFormat::R16Uint);
    auto dst = orion::gpu::Texture::create(*device, kW, kH,
                                           orion::gpu::PixelFormat::R32Float);
    src->upload(input.data(), std::size_t(kW) * sizeof(std::uint16_t));

    orion::pipe::params::Linearize lin{};
    for (int c = 0; c < 4; ++c) lin.black[c] = kBlack;
    lin.whiteBalance[0] = kGainR;
    lin.whiteBalance[1] = kGainG;
    lin.whiteBalance[2] = kGainB;
    lin.whiteBalance[3] = kGainG;   // second green
    lin.invRange = 1.0f / (kWhite - kBlack);
    lin.filters  = 0x94949494u;     // RGGB
    lin.size[0] = kW; lin.size[1] = kH;
    lin.whiteClip = std::min({kGainR, kGainG, kGainB});

    std::vector<float> out(std::size_t(kW) * kH);
    {
        orion::gpu::CommandBuffer cb(*device);
        cb.dispatch(*kernel, {src.get(), dst.get()}, &lin, sizeof lin, kW, kH);
        cb.commitAndWait();
        dst->download(out.data(), std::size_t(kW) * sizeof(float), kW, kH);
    }

    report(lin.whiteClip < kGainR,
           "the test's gains would actually tint an unclipped highlight");

    // Every channel of the blown half must land on one value. Reading the
    // mosaic per CFA channel is the point: the cast is a *difference between*
    // channels, and an average over all of them hides it completely.
    double blown[3] = {0, 0, 0};
    int blownCount[3] = {0, 0, 0};
    for (std::uint32_t y = 0; y < kH / 2; ++y) {
        for (std::uint32_t x = 0; x < kW; ++x) {
            const std::uint32_t shift = (((y << 1) & 14u) | (x & 1u)) << 1;
            int c = int((lin.filters >> shift) & 3u);
            if (c == 3) c = 1;
            blown[c] += double(out[std::size_t(y) * kW + x]);
            blownCount[c] += 1;
        }
    }
    for (int c = 0; c < 3; ++c) blown[c] /= std::max(blownCount[c], 1);

    checkNear(blown[0], blown[1], 1e-4, "a blown red matches a blown green");
    checkNear(blown[2], blown[1], 1e-4, "a blown blue matches a blown green");
    checkNear(blown[1], double(lin.whiteClip), 1e-4,
              "a blown pixel lands on the white level");

    // The midtone keeps its gains. Below the clip, white balance is still white
    // balance — clipping is a ceiling, not a desaturation.
    double mid[3] = {0, 0, 0};
    int midCount[3] = {0, 0, 0};
    for (std::uint32_t y = kH / 2; y < kH; ++y) {
        for (std::uint32_t x = 0; x < kW; ++x) {
            const std::uint32_t shift = (((y << 1) & 14u) | (x & 1u)) << 1;
            int c = int((lin.filters >> shift) & 3u);
            if (c == 3) c = 1;
            mid[c] += double(out[std::size_t(y) * kW + x]);
            midCount[c] += 1;
        }
    }
    for (int c = 0; c < 3; ++c) mid[c] /= std::max(midCount[c], 1);

    // Against the fraction the sensor counts actually carry, not the nominal
    // quarter — kMidRaw truncates to an integer on the way into the texture.
    const double midFraction =
        (double(static_cast<std::uint16_t>(kMidRaw)) - kBlack) / (kWhite - kBlack);
    checkNear(mid[0], midFraction * kGainR, 1e-4, "a midtone red keeps its gain");
    checkNear(mid[1], midFraction * kGainG, 1e-4, "a midtone green keeps its gain");
    checkNear(mid[2], midFraction * kGainB, 1e-4, "a midtone blue keeps its gain");
    report(mid[0] > mid[1], "the midtone is still white-balanced, not flattened");
}

/// A grading wheel changes colour and not brightness.
///
/// That property is the whole reason the wheels are usable: without it, pushing
/// toward yellow also lifts the zone, so every wheel fights the exposure slider
/// and a neutral grey stops keeping its luminance. It is one subtraction in
/// gradeOffsets and nothing in the picture would announce its absence — the
/// image would just drift brighter as you graded.
void testGradeOffsets() {
    section("Grading wheel offsets");

    using orion::pipe::DevelopPipeline;

    float o[3];
    DevelopPipeline::gradeOffsets(0.0f, 0.0f, o);
    checkNear(o[0], 0.0, 1e-6, "the centre is exactly no correction (r)");
    checkNear(o[1], 0.0, 1e-6, "the centre is exactly no correction (g)");
    checkNear(o[2], 0.0, 1e-6, "the centre is exactly no correction (b)");

    // Every angle around the rim, and a few radii on each.
    for (int deg = 0; deg < 360; deg += 15) {
        const float t = float(deg) * 3.14159265358979f / 180.0f;
        for (float r : {0.25f, 0.5f, 1.0f}) {
            DevelopPipeline::gradeOffsets(r * std::cos(t), r * std::sin(t), o);
            const double sum = double(o[0]) + double(o[1]) + double(o[2]);
            checkNear(sum, 0.0, 1e-5,
                      "the offset is zero-sum at " + std::to_string(deg) +
                      " degrees, radius " + std::to_string(r));
        }
    }

    // Strength scales with the radius, so the wheel feels linear under the
    // hand rather than accelerating toward the rim.
    float half[3], full[3];
    DevelopPipeline::gradeOffsets(0.5f, 0.0f, half);
    DevelopPipeline::gradeOffsets(1.0f, 0.0f, full);
    checkNear(double(full[0]), 2.0 * double(half[0]), 1e-5,
              "twice the radius is twice the offset");
    // 0.02 in scene-linear terms, against a middle grey of 0.18. Small
    // sounding and not small: an additive offset is measured against the
    // *scene*, not against a display value, and the calibration in
    // research/color-grading.md is what settled the constant.
    report(std::abs(full[0]) > 0.02f, "a wheel at the rim actually does something",
           "got " + std::to_string(full[0]));

    // Past the rim the radius saturates rather than growing without limit.
    float beyond[3];
    DevelopPipeline::gradeOffsets(3.0f, 0.0f, beyond);
    checkNear(double(beyond[0]), double(full[0]), 1e-5,
              "the offset is clamped at the rim");

    // Where each primary actually lives on the wheel.
    //
    // This is the half of the wheel that can be tested. The other half is the
    // gradient the panel paints, and the two disagreed: the maths winds
    // counter-clockwise with y upward, SwiftUI's AngularGradient winds
    // clockwise with y downward, so the ordinary R-Y-G-C-B-M ring put green
    // where the engine puts blue. Dragging toward visible green made the
    // picture blue. Pinning the angles here is what makes the panel's ordering
    // checkable against something.
    struct Primary { float degrees; int channel; const char* name; };
    const Primary primaries[] = {{0.0f, 0, "red"}, {120.0f, 1, "green"},
                                 {240.0f, 2, "blue"}};
    for (const auto& prim : primaries) {
        const float t = prim.degrees * 3.14159265358979f / 180.0f;
        float o[3];
        DevelopPipeline::gradeOffsets(std::cos(t), std::sin(t), o);
        const int c = prim.channel;
        report(o[c] > o[(c + 1) % 3] && o[c] > o[(c + 2) % 3],
               std::string("the wheel's ") + prim.name + " direction raises " +
                   prim.name + " above the other two");
    }

    // And the complements land opposite their primaries, which is what makes
    // the ring continuous rather than three spikes.
    float yellow[3];
    DevelopPipeline::gradeOffsets(std::cos(1.0471975512f), std::sin(1.0471975512f),
                                  yellow);   // 60 degrees
    report(yellow[0] > yellow[2] && yellow[1] > yellow[2],
           "sixty degrees is yellow: red and green above blue");
}

void testLensGpu() {
    section("Lens corrections (GPU)");

    constexpr std::uint32_t kW = 256, kH = 256;

    std::unique_ptr<orion::gpu::Device> device;
    try {
        device = orion::gpu::Device::create();
    } catch (const std::exception& e) {
        report(false, "Metal device available", e.what());
        return;
    }

    auto library = orion::gpu::Library::createFromFile(
        *device, std::string(ORION_SHADER_DIR) + "/lensCorrect.metallib");
    auto kernel = orion::gpu::Kernel::create(*device, *library, "lensCorrect");

    auto src = orion::gpu::Texture::create(*device, kW, kH,
                                           orion::gpu::PixelFormat::RGBA16Float);
    auto dst = orion::gpu::Texture::create(*device, kW, kH,
                                           orion::gpu::PixelFormat::RGBA16Float);

    // Coordinates as color again, so an output pixel names where it came from.
    // Blue carries a constant, which is what the vignetting check reads.
    std::vector<__fp16> input(std::size_t(kW) * kH * 4);
    for (std::uint32_t y = 0; y < kH; ++y) {
        for (std::uint32_t x = 0; x < kW; ++x) {
            const std::size_t i = (std::size_t(y) * kW + x) * 4;
            input[i + 0] = static_cast<__fp16>(x / double(kW));
            input[i + 1] = static_cast<__fp16>(y / double(kH));
            input[i + 2] = static_cast<__fp16>(0.5);
            input[i + 3] = 1;
        }
    }
    src->upload(input.data(), std::size_t(kW) * 4 * sizeof(__fp16));

    orion::pipe::params::Lens lens{};
    lens.size[0] = kW; lens.size[1] = kH;
    lens.centerX = 0.5f; lens.centerY = 0.5f;

    const auto run = [&](std::vector<__fp16>& out) {
        orion::gpu::CommandBuffer cb(*device);
        cb.dispatch(*kernel, {src.get(), dst.get()}, &lens, sizeof lens, kW, kH);
        cb.commitAndWait();
        dst->download(out.data(), std::size_t(kW) * 4 * sizeof(__fp16), kW, kH);
    };
    std::vector<__fp16> out(std::size_t(kW) * kH * 4);

    // Nothing set must be an exact pass-through, or the node cannot be left on.
    run(out);
    double worstIdentity = 0.0;
    for (std::uint32_t i = 0; i < kW * kH; ++i) {
        for (int c = 0; c < 3; ++c) {
            worstIdentity = std::max(worstIdentity,
                std::abs(double(out[i * 4 + c]) - double(input[i * 4 + c])));
        }
    }
    // Exact, not 2e-3. The old tolerance was one half of a texel step on the
    // ramp this test reads, which is precisely the error a half-texel sampling
    // offset produces — so the check was sized to pass over the bug it existed
    // to catch. Every fetch landed midway between two texels, blurring and
    // shifting the whole frame as soon as any lens slider left zero.
    report(worstIdentity < 1e-4, "no correction is a pass-through, to the texel",
           "worst " + std::to_string(worstIdentity));

    // Distortion. The (1 - k1) term pins r_d(1) = 1, so a pixel on the frame's
    // diagonal at r = 1 must not move: without that term the whole picture
    // scales and the control reads as a zoom.
    lens.distB = 0.3f;
    run(out);

    // The corner is at r = 1 by construction (R_norm is half the diagonal).
    const std::size_t corner = ((std::size_t(kH) - 1) * kW + (kW - 1)) * 4;
    checkNear(double(out[corner + 0]), double(input[corner + 0]), 0.01,
              "the frame corner does not move under distortion");
    checkNear(double(out[corner + 1]), double(input[corner + 1]), 0.01,
              "the frame corner does not move vertically either");

    // The center never moves under a radial model, at any coefficient.
    const std::size_t center = (std::size_t(kH / 2) * kW + kW / 2) * 4;
    checkNear(double(out[center + 0]), double(input[center + 0]), 0.01,
              "the optical center is fixed");

    // Between them it must actually move something, or the control is inert.
    const std::size_t mid = (std::size_t(kH / 2) * kW + (3 * kW / 4)) * 4;
    report(std::abs(double(out[mid]) - double(input[mid])) > 0.005,
           "distortion moves the interior",
           "delta " + std::to_string(double(out[mid]) - double(input[mid])));
    lens.distB = 0.0f;

    // ptlens carries three coefficients, and a profile can put weight on any of
    // them. `b` alone is poly3 and is what the manual slider drives, so `a` and
    // `c` would be the two that ship untested — and a profile that sets them
    // would then correct nothing while the interface said it had.
    for (int which = 0; which < 2; ++which) {
        lens.distA = (which == 0) ? 0.25f : 0.0f;
        lens.distC = (which == 0) ? 0.0f  : 0.25f;
        run(out);
        checkNear(double(out[corner + 0]), double(input[corner + 0]), 0.01,
                  which == 0 ? "ptlens a still pins the corner"
                             : "ptlens c still pins the corner");
        report(std::abs(double(out[mid]) - double(input[mid])) > 0.005,
               which == 0 ? "ptlens a moves the interior" : "ptlens c moves the interior",
               "delta " + std::to_string(double(out[mid]) - double(input[mid])));
    }
    lens.distA = 0.0f;
    lens.distC = 0.0f;

    // The higher vignetting terms, for the same reason: a manual control only
    // ever sets p_a, so p_b and p_c arrive with the first real profile.
    lens.vignetteB = -0.3f;
    run(out);
    report(std::abs(double(out[corner + 2]) - 0.5) > 1e-3,
           "the r^4 vignetting term reaches the corner",
           "corner " + std::to_string(double(out[corner + 2])));
    lens.vignetteB = 0.0f;
    lens.vignetteC = -0.3f;
    run(out);
    report(std::abs(double(out[corner + 2]) - 0.5) > 1e-3,
           "the r^6 vignetting term reaches the corner",
           "corner " + std::to_string(double(out[corner + 2])));
    lens.vignetteC = 0.0f;

    // Vignetting divides, so a negative coefficient must *brighten* the corner
    // — that is the correction for a lens that darkens it. Getting the sign
    // backwards doubles the vignette instead of removing it, which looks
    // plausible enough to ship.
    lens.vignetteA = -0.4f;
    run(out);
    const double cornerBlue = double(out[corner + 2]);
    const double centerBlue = double(out[center + 2]);
    report(cornerBlue > 0.5 + 1e-3, "negative vignetting brightens the corner",
           "corner " + std::to_string(cornerBlue));
    checkNear(centerBlue, 0.5, 5e-3, "vignetting leaves the center alone");

    // 1 + p_a·r² at r = 1 is 0.6, and 0.5 / 0.6 = 0.8333.
    checkNear(cornerBlue, 0.5 / 0.6, 0.02, "the vignette follows 1 + p_a·r²");
    lens.vignetteA = 0.0f;

    // Chromatic aberration moves red and blue against green. Green is the
    // reference and must not move at all.
    //
    // A coefficient well past the production range on purpose: a real fringe
    // correction is a sub-pixel shift, and fp16 cannot resolve one at these
    // values. Exaggerating it is the only way to test the sign and the
    // channel routing, which is what actually goes wrong.
    lens.caRed = 0.2f;
    run(out);
    const std::size_t probe = (std::size_t(kH / 2) * kW + (7 * kW / 8)) * 4;
    checkNear(double(out[probe + 1]), double(input[probe + 1]), 2e-3,
              "green is the reference and does not move");
    report(std::abs(double(out[probe + 0]) - double(input[probe + 0])) > 1e-3,
           "the red fringe control moves red",
           "delta " + std::to_string(double(out[probe]) - double(input[probe])));
    checkNear(double(out[probe + 2]), double(input[probe + 2]), 2e-3,
              "the red control leaves blue alone");
    lens.caRed = 0.0f;

    // ── The case nobody measured: negative k₁ ────────────────────────────
    //
    // Everything above runs k₁ > 0, which pulls samples *inward* and can never
    // leave the frame. Half the slider's travel does the opposite. poly3 pins
    // the corners at r = 1, but the edge midpoints sit at r ≈ 0.70 here (0.83
    // on a 3:2 frame), where the multiplier exceeds 1 — so the fetch runs off
    // the border, the clamp returns the edge pixel for all of it, and the
    // output gets a band of one column smeared sideways. It shipped, and it is
    // what the developer saw on a real frame.
    //
    // R still carries the source x, so a smear is directly readable: two output
    // columns that came from the same place report the same number.
    lens.distB = -0.3f;
    lens.scale = 0.0f;   // the shader reads 0 as 1 — i.e. the old behavior
    run(out);

    const std::uint32_t row = kH / 2;
    const auto redAt = [&](std::uint32_t x) {
        return double(out[(std::size_t(row) * kW + x) * 4 + 0]);
    };
    report(std::abs(redAt(kW - 1) - redAt(kW - 2)) < 1e-4,
           "without autoscale the edge columns are the same pixel twice",
           "delta " + std::to_string(redAt(kW - 1) - redAt(kW - 2)));

    // With it, every column has to come from somewhere different.
    lens.scale = orion::pipe::lens::autoScale(kW, kH, 0.5f, 0.5f, 0.0f, -0.3f, 0.0f, 0.0f, 0.0f);
    report(lens.scale < 1.0f, "barrel correction needs a zoom",
           "scale " + std::to_string(lens.scale));
    run(out);

    double closest = 1.0;
    for (std::uint32_t x = 1; x < kW; ++x) {
        closest = std::min(closest, std::abs(redAt(x) - redAt(x - 1)));
    }
    report(closest > 1e-4, "with autoscale no two columns are the same pixel",
           "closest " + std::to_string(closest));

    // And the same along y, where G carries the source row.
    const std::uint32_t col = kW / 2;
    const auto greenAt = [&](std::uint32_t y) {
        return double(out[(std::size_t(y) * kW + col) * 4 + 1]);
    };
    double closestRow = 1.0;
    for (std::uint32_t y = 1; y < kH; ++y) {
        closestRow = std::min(closestRow, std::abs(greenAt(y) - greenAt(y - 1)));
    }
    report(closestRow > 1e-4, "and no two rows are the same pixel",
           "closest " + std::to_string(closestRow));

    // The zoom must be the smallest one that works, or the correction quietly
    // throws away frame. One percent looser and the edge smears again.
    lens.scale *= 1.01f;
    run(out);
    report(std::abs(redAt(kW - 1) - redAt(kW - 2)) < 1e-4,
           "a one percent looser zoom smears again, so the scale is not slack",
           "delta " + std::to_string(redAt(kW - 1) - redAt(kW - 2)));
}

/// Three-way colour grading, on a real GPU.
///
/// The newest node in the graph and the least checked one: `color-grading.md`
/// admitted its effect "is a check somebody ran once", there was no GPU test,
/// and no bench probe. Both exist now.
/// The camera profile's hue/saturation stage.
///
/// Three questions, and the first two are the ones that would let a wrong node
/// ship looking right: does an identity table leave the image alone (the space
/// conversion is a full round trip through linear ProPhoto and HSV, and any
/// error in it tints *everything*), and does a neutral stay neutral (the DNG
/// spec requires it, and a purple cast on greys is exactly the bug this whole
/// node exists to undo). Only then: does blue actually move.
