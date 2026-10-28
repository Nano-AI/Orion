// White balance, the Planckian locus, tone curves, CFA indexing and orientation.
//
// Split out of main.cpp 2026-07-31; see harness.h.

#include "harness.h"

constexpr float kSonyXyzToCam[9] = {
     0.7374f, -0.2389f, -0.0551f,
    -0.5435f,  1.3162f,  0.2519f,
    -0.1006f,  0.1795f,  0.6552f,
};

/// The Planckian locus and the tint axis, against Adobe's own numbers.
///
/// Orion used to shift CIE 1931 y by `tint * 0.05`, which was wrong three ways:
/// wrong space (the offset belongs in CIE 1960 UCS), wrong direction (it runs
/// along the isotemperature line, whose slope depends on temperature), and
/// wrong scale. These vectors come from a line-by-line port of
/// `dng_temperature::Get_xy_coord`, so passing them means agreeing with
/// Lightroom rather than merely being self-consistent.
void testPlanckianLocus() {
    section("Planckian locus and tint");

    // (temperature, Adobe tint) -> xy. Orion's tint is -1..1 against Adobe's
    // +/-150, so the third column is divided by 150 on the way in.
    struct Vector { float kelvin; float adobeTint; double x, y; };
    static const Vector kVectors[] = {
        { 2000.0f,    0.0f, 0.52669291, 0.41330847 },
        { 2856.0f,    0.0f, 0.44754761, 0.40743728 },
        { 3000.0f,    0.0f, 0.43610027, 0.40418917 },
        { 4000.0f,    0.0f, 0.38044617, 0.37675624 },
        { 5000.0f,    0.0f, 0.34510414, 0.35162252 },
        { 6500.0f,    0.0f, 0.31352792, 0.32353408 },
        { 7500.0f,    0.0f, 0.30036317, 0.31013622 },
        {10000.0f,    0.0f, 0.28063070, 0.28827855 },
        {25000.0f,    0.0f, 0.25251461, 0.25221552 },
        { 5000.0f,  100.0f, 0.35241776, 0.43338023 },
        { 5000.0f, -100.0f, 0.33946110, 0.28853988 },
        { 6500.0f,   50.0f, 0.30941427, 0.35311940 },
        { 6500.0f,  -50.0f, 0.31721352, 0.29702728 },
    };

    double worst = 0.0;
    for (const auto& v : kVectors) {
        orion::pipe::WhiteBalance wb{};
        wb.temperatureK = v.kelvin;
        wb.tint = v.adobeTint / 150.0f;

        float x = 0.0f, y = 0.0f;
        orion::pipe::whitePointXy(wb, x, y);
        worst = std::max(worst, std::max(std::abs(double(x) - v.x),
                                         std::abs(double(y) - v.y)));
    }
    report(worst < 2e-5, "the white point matches Adobe's Get_xy_coord",
           "worst " + std::to_string(worst));

    // The as-shot estimate sweeps this same locus backwards — it searches for
    // the temperature whose multipliers match what the camera reported. Changing
    // the locus therefore changes what every file opens at, and nothing was
    // checking that the two directions still agree.
    {
        // The identity: a hypothetical camera whose responses are XYZ. Chosen
        // deliberately over a real matrix — XYZ-to-sRGB carries large negative
        // entries, so a tinted white point drives a "camera" response negative,
        // the log-error clamps it, and the error surface goes flat. That
        // measures the matrix's conditioning, not the estimator's search, which
        // is what this test is about.
        const float xyzToCam[9] = {1.0f, 0.0f, 0.0f,
                                   0.0f, 1.0f, 0.0f,
                                   0.0f, 0.0f, 1.0f};

        double worstK = 0.0, worstTint = 0.0, worstMul = 0.0;
        for (const float kelvin : {2800.0f, 4000.0f, 5500.0f, 6500.0f, 9000.0f}) {
            for (const float tint : {-0.4f, 0.0f, 0.3f}) {
                const auto mul = orion::pipe::multipliersFor({kelvin, tint}, xyzToCam);
                const auto back = orion::pipe::estimateFrom(mul, xyzToCam);
                const auto again = orion::pipe::multipliersFor(back, xyzToCam);

                worstK = std::max(worstK,
                                  std::abs(double(back.temperatureK) - double(kelvin)));
                worstTint = std::max(worstTint, std::abs(double(back.tint) - double(tint)));
                for (int c = 0; c < 3; ++c) {
                    worstMul = std::max(worstMul, std::abs(double(again[c]) - double(mul[c])));
                }
            }
        }
        // Which pair is actually failing, and is it the search or the map?
        for (const float kelvin : {2800.0f, 4000.0f, 5500.0f, 6500.0f, 9000.0f}) {
            for (const float tint : {-0.4f, 0.0f, 0.3f}) {
                const auto mul  = orion::pipe::multipliersFor({kelvin, tint}, xyzToCam);
                const auto back = orion::pipe::estimateFrom(mul, xyzToCam);
                const auto again = orion::pipe::multipliersFor(back, xyzToCam);
                double e = 0.0;
                for (int c = 0; c < 3; ++c) {
                    const double d = double(again[c]) - double(mul[c]);
                    e += d * d;
                }
                // Prints only what fails, so a regression names the pair.
                if (e > 1e-8) {
                    std::printf("    %5.0f K tint %+.2f -> %7.1f K tint %+.3f   err %.2e\n",
                                kelvin, tint, back.temperatureK, back.tint, e);
                }
            }
        }

        std::printf("  as-shot round trip: %.3f in the multipliers, "
                    "%.0f K / %.3f tint in the parameters\n",
                    worstMul, worstK, worstTint);

        // **The multipliers are what the assertion is on, not the parameters.**
        //
        // The error surface along the locus is a shallow valley: a white point
        // reached at 6500 K with one tint is very nearly the white point
        // reached a hundred Kelvin away with a slightly different tint, and
        // which one a search lands on is decided by float noise. The parameters
        // are therefore not recoverable to better than about a hundred Kelvin,
        // and demanding that they are would be demanding something untrue of
        // the problem rather than of the code.
        //
        // What has to hold is that the estimate *renders the same* — that
        // opening a file and re-deriving its white balance does not change a
        // pixel. That is what a photographer would notice, and it is tight.
        // Exact, and the tightness is the point: this started at 845 K out,
        // and every loosening along the way would have been a threshold chosen
        // to make a broken search look acceptable. The estimate recovers
        // exactly what produced it, so a file reopens at the white balance it
        // was saved with.
        report(worstMul < 1e-4,
               "the as-shot estimate reproduces the multipliers it came from",
               "worst " + std::to_string(worstMul));
        report(worstK < 10.0 && worstTint < 0.005,
               "and recovers the temperature and tint themselves",
               std::to_string(worstK) + " K, " + std::to_string(worstTint) + " tint");
    }

    // Tint runs along the isotemperature line, so its direction turns with
    // temperature. If it were a fixed axis — which the old code assumed — the
    // displacement would point the same way at every Kelvin.
    const auto displacement = [](float kelvin) {
        orion::pipe::WhiteBalance a{}, b{};
        a.temperatureK = b.temperatureK = kelvin;
        a.tint = 0.0f;
        b.tint = 100.0f / 150.0f;
        float ax = 0, ay = 0, bx = 0, by = 0;
        orion::pipe::whitePointXy(a, ax, ay);
        orion::pipe::whitePointXy(b, bx, by);
        return std::atan2(double(by - ay), double(bx - ax));
    };
    const double warm = displacement(2800.0f);
    const double cool = displacement(9000.0f);
    report(std::abs(warm - cool) > 0.15,
           "and its direction turns with temperature, as an isotherm does",
           "2800 K " + std::to_string(warm) + " rad, 9000 K " + std::to_string(cool));
}

void testWhiteBalance() {
    section("White balance");

    // Warmer light needs more blue gain to neutralize, cooler needs more red.
    // If this inverts, every image comes out with the cast backwards.
    const auto warm = orion::pipe::multipliersFor({2800.0f, 0.0f}, kSonyXyzToCam);
    const auto cool = orion::pipe::multipliersFor({9000.0f, 0.0f}, kSonyXyzToCam);
    report(warm[2] > cool[2], "warmer light gets more blue gain");
    report(cool[0] > warm[0], "cooler light gets more red gain");

    // Green is the reference channel throughout the pipeline.
    for (float k : {2500.0f, 5500.0f, 12000.0f}) {
        const auto m = orion::pipe::multipliersFor({k, 0.0f}, kSonyXyzToCam);
        checkNear(m[1], 1.0, 1e-5, "green normalized at " + std::to_string(int(k)) + "K");
    }

    // Round trip: estimating a temperature from its own multipliers must give
    // that temperature back. When this drifts, "as shot" is not as shot.
    for (float k : {3200.0f, 5200.0f, 6500.0f, 8000.0f}) {
        const auto m = orion::pipe::multipliersFor({k, 0.0f}, kSonyXyzToCam);
        const auto back = orion::pipe::estimateFrom(m, kSonyXyzToCam);
        checkNear(back.temperatureK, k, 60.0,
                  "round trip at " + std::to_string(int(k)) + "K");
    }

    // Tint moves green against magenta, and zero tint is neutral.
    const auto plus = orion::pipe::multipliersFor({5500.0f, 0.6f}, kSonyXyzToCam);
    const auto zero = orion::pipe::multipliersFor({5500.0f, 0.0f}, kSonyXyzToCam);
    report(std::abs(plus[0] - zero[0]) > 1e-4 || std::abs(plus[2] - zero[2]) > 1e-4,
           "tint changes the multipliers");
}

// ── Tone curve ─────────────────────────────────────────────────────────────

void testToneCurve() {
    section("Tone curve");

    orion::pipe::CurveChannel identity{{0.0f, 0.0f}, {1.0f, 1.0f}};
    for (float x : {0.0f, 0.25f, 0.5f, 0.75f, 1.0f}) {
        checkNear(orion::pipe::evaluateCurve(identity, x), x, 1e-4,
                  "identity passes through " + std::to_string(x));
    }

    // A film S must stay monotone. A plain Catmull-Rom would overshoot between
    // control points, which shows up in an image as banding or a tonal reversal.
    orion::pipe::CurveChannel filmS{
        {0.0f, 0.0f}, {0.25f, 0.14f}, {0.75f, 0.86f}, {1.0f, 1.0f}};
    float previous = -1.0f;
    bool monotone = true, inRange = true;
    for (int i = 0; i <= 512; ++i) {
        const float x = static_cast<float>(i) / 512.0f;
        const float y = orion::pipe::evaluateCurve(filmS, x);
        if (y < previous - 1e-5f) monotone = false;
        if (y < -1e-4f || y > 1.0f + 1e-4f) inRange = false;
        previous = y;
    }
    report(monotone, "film S curve is monotone");
    report(inRange, "film S curve stays within 0..1");

    // Control points are honoured exactly.
    checkNear(orion::pipe::evaluateCurve(filmS, 0.25f), 0.14, 1e-4, "passes through (0.25, 0.14)");
    checkNear(orion::pipe::evaluateCurve(filmS, 0.75f), 0.86, 1e-4, "passes through (0.75, 0.86)");

    // Unsorted input must not change the result — the UI can hand us points
    // in any order after a drag.
    orion::pipe::CurveChannel shuffled{
        {0.75f, 0.86f}, {0.0f, 0.0f}, {1.0f, 1.0f}, {0.25f, 0.14f}};
    checkNear(orion::pipe::evaluateCurve(shuffled, 0.5f),
              orion::pipe::evaluateCurve(filmS, 0.5f), 1e-5, "unsorted points give same curve");

    // The LUT is what the shader actually samples.
    orion::pipe::ToneCurveSpec spec;
    CHECK(spec.isIdentity());
    spec.master = filmS;
    CHECK(!spec.isIdentity());

    const auto lut = orion::pipe::buildCurveLut(spec);
    report(lut.size() == std::size_t(orion::pipe::kCurveResolution) * orion::pipe::kCurveRows,
           "LUT has the expected size");
    checkNear(lut[0], 0.0, 1e-4, "LUT starts at 0");
    checkNear(lut[orion::pipe::kCurveResolution - 1], 1.0, 1e-4, "LUT ends at 1");

    // Rows 1..3 are the untouched per-channel curves, so they stay identity.
    const auto red0 = lut[orion::pipe::kCurveResolution];
    const auto redMid = lut[orion::pipe::kCurveResolution + orion::pipe::kCurveResolution / 2];
    checkNear(red0, 0.0, 1e-4, "red row starts at 0");
    checkNear(redMid, 0.5, 0.01, "red row is identity at midpoint");
}

// ── CFA indexing ───────────────────────────────────────────────────────────

void testCfa() {
    section("CFA pattern");

    // RGGB, LibRaw's encoding. The shader mirrors channelAt exactly; if the
    // two ever disagree the demosaic reads the wrong color everywhere.
    orion::raw::BayerImage img;
    img.filters = 0x94949494u;
    img.width = 4;
    img.height = 4;

    using orion::raw::Channel;
    report(img.channelAt(0, 0) == Channel::R,  "(0,0) is red");
    report(img.channelAt(1, 0) == Channel::G,  "(1,0) is green");
    // LibRaw's 0x94949494 encodes both greens as 1; the G2 index only appears
    // in patterns that distinguish them. cfaChannelRGB folds 3 onto 1 so the
    // shader does not have to care either way.
    report(img.channelAt(0, 1) == Channel::G,  "(0,1) is green");
    report(img.channelAt(1, 1) == Channel::B,  "(1,1) is blue");
    report(img.patternString() == "RGGB", "pattern string reads RGGB");

    // The pattern repeats on a 2x2 grid.
    report(img.channelAt(2, 2) == img.channelAt(0, 0), "pattern repeats horizontally");
    report(img.channelAt(0, 2) == img.channelAt(0, 0), "pattern repeats vertically");
}

// ── CFA decimation ─────────────────────────────────────────────────────────
//
// The same mosaic `testCfa` builds, decimated. Moved here from
// tests_brush.cpp (decision #129): what it asserts is that `channelAt` still
// answers for the *output*, so its fixture is the CFA pattern above and not
// the brush's dab list.

void testBayerDecimation() {
    section("Bayer decimation");

    // A mosaic whose value *is* its channel, so a phase error shows up as a
    // wrong number rather than as a subtle color cast.
    orion::raw::BayerImage src;
    src.width = 32; src.height = 16;
    src.filters = 0x94949494u;   // RGGB, LibRaw's usual
    src.samples.resize(std::size_t(src.width) * src.height);
    for (std::uint32_t y = 0; y < src.height; ++y) {
        for (std::uint32_t x = 0; x < src.width; ++x) {
            src.samples[std::size_t(y) * src.width + x] =
                std::uint16_t(static_cast<int>(src.channelAt(x, y)) * 1000 + 100);
        }
    }

    for (int scale : {2, 4}) {
        const auto out = orion::raw::decimate(src, scale);

        report(out.width == src.width / std::uint32_t(scale)
               && out.height == src.height / std::uint32_t(scale),
               "scale " + std::to_string(scale) + ": the mosaic shrinks by it",
               std::to_string(out.width) + "x" + std::to_string(out.height));

        report(out.filters == src.filters,
               "scale " + std::to_string(scale) + ": the pattern is carried across");

        // ⚠ The check that matters. Every output sample must carry the value of
        // the channel the *output's own* `channelAt` reports — which is only
        // true if the phase survived.
        bool phaseHeld = true;
        std::string firstBad;
        for (std::uint32_t y = 0; y < out.height && phaseHeld; ++y) {
            for (std::uint32_t x = 0; x < out.width; ++x) {
                const auto want =
                    std::uint16_t(static_cast<int>(out.channelAt(x, y)) * 1000 + 100);
                const auto got = out.samples[std::size_t(y) * out.width + x];
                if (got != want) {
                    phaseHeld = false;
                    firstBad = "at " + std::to_string(x) + "," + std::to_string(y)
                             + " got " + std::to_string(got)
                             + " want " + std::to_string(want);
                    break;
                }
            }
        }
        report(phaseHeld,
               "scale " + std::to_string(scale)
               + ": every sample keeps its CFA channel", firstBad);
    }

    // Point sampling, not averaging (`RawImage.h` says why): an output sample is
    // one real sensor sample, not a mean that has lost three quarters of the
    // noise the release will render.
    {
        orion::raw::BayerImage ramp;
        ramp.width = 8; ramp.height = 8;
        ramp.filters = 0x94949494u;
        ramp.samples.resize(64);
        for (auto& v : ramp.samples) v = 100;
        ramp.samples[0]  = 900;    // (0,0), a corner of output cell (0,0)'s block
        ramp.samples[36] = 500;    // (4,4), the middle of it at scale 4
        const auto out = orion::raw::decimate(ramp, 4);
        report(out.width == 2 && out.height == 2, "a 8x8 mosaic decimates to 2x2",
               std::to_string(out.width) + "x" + std::to_string(out.height));
        report(out.samples[0] == 500,
               "and an output sample is the middle same-phase input, not a mean",
               std::to_string(out.samples[0]));
    }

    // Every output sample against the definition, on a mosaic whose values all
    // differ and whose size is not a multiple of the stride. The checks above
    // cannot see a sample taken from the wrong block — every same-phase input
    // in them is equal — and a rewrite for speed (#319) could do exactly that
    // while keeping every phase.
    {
        orion::raw::BayerImage mixed;
        mixed.width = 38; mixed.height = 22;
        mixed.filters = 0x94949494u;
        mixed.samples.resize(std::size_t(mixed.width) * mixed.height);
        for (std::size_t i = 0; i < mixed.samples.size(); ++i)
            mixed.samples[i] = std::uint16_t(((i * 2654435761u) >> 7) & 0x3fff);

        for (int scale : {2, 4}) {
            const auto out = orion::raw::decimate(mixed, scale);
            const std::uint32_t s = std::uint32_t(scale);
            bool exact = out.width == mixed.width / (2 * s) * 2 &&
                         out.height == mixed.height / (2 * s) * 2;
            std::string firstBad;
            for (std::uint32_t y = 0; y < out.height && exact; ++y) {
                for (std::uint32_t x = 0; x < out.width && exact; ++x) {
                    const auto got = out.samples[std::size_t(y) * out.width + x];
                    const auto want =
                        mixed.samples[std::size_t(((y / 2) * s + s / 2) * 2 + y % 2) * mixed.width +
                                      ((x / 2) * s + s / 2) * 2 + x % 2];
                    if (got != want) {
                        exact = false;
                        firstBad = "at " + std::to_string(x) + "," + std::to_string(y) +
                                   " got " + std::to_string(got) + " want " +
                                   std::to_string(want);
                    }
                }
            }
            report(exact, "scale " + std::to_string(scale) +
                          ": every sample is its own block's middle", firstBad);
        }
    }

    // Odd or too-small scales are refused rather than guessed at.
    {
        report(orion::raw::decimate(src, 3).width == src.width,
               "an odd scale returns the mosaic unchanged");
        report(orion::raw::decimate(src, 1).width == src.width,
               "and so does a scale of one");
        report(orion::raw::decimate(src, 64).width == src.width,
               "a scale larger than the mosaic returns it unchanged too");
    }
}

/// A noisy shadow: the release renders it without a lift, and the preview
/// renders it as the release does.
///
/// ⚠ Two claims, because they fail to different mutations. Read noise is
/// zero-mean, so a night frame's deep shadows put a third of their samples
/// below black, and a clamp at zero anywhere on the mosaic's way to the camera
/// profile throws that half of the noise away and lifts the shadow — most in
/// the channel with the least light, which made `_PIC8148`'s sky grey-purple
/// (research/black-level.md). That is read against a *clean* frame of the same
/// level, in camera RGB after the demosaic, where nothing has a reason to move
/// the mean. And the display transform is not linear, so how a noisy shadow
/// looks depends on how much noise it carries as well as on its mean: the
/// preview has to carry the release's own noise to look like it, which is why
/// `decimate` point-samples. That is read at the 8-bit output. The first cannot
/// see a preview that averages; the second cannot see a clamp both graphs share.
///
/// The fixture is that sky: 20 counts above a black point per channel, a sigma
/// of 80, at +1 EV. ⚠ Its camera is linear sRGB (`camToXyz` is LibRaw's
/// XYZ->camera, so this is sRGB's inverse), not the forward matrix the other
/// fixtures carry: that one makes their grey an imaginary color, whose noise
/// renders at 70 levels a pixel, which is enough to drown a one-level claim.
void testPreviewShadowsMatchGpu() {
    section("A noisy shadow: no lift, and the preview renders it as the release does");

    std::unique_ptr<orion::gpu::Device> device;
    try {
        device = orion::gpu::Device::create();
    } catch (const std::exception& e) {
        report(false, "Metal device available", e.what());
        return;
    }

    constexpr std::uint32_t kN = 1024;
    const auto field = [&](double sigma) {
        orion::raw::BayerImage img;
        img.width = kN; img.height = kN;
        img.filters = 0x94949494u;                 // RGGB
        img.black = {500, 512, 530, 512};          // a wrong channel's black shows
        img.white = 16383;
        img.camMul = {2.0f, 1.0f, 1.5f, 1.0f};
        img.camToXyz = {3.2406f, -1.5372f, -0.4986f,
                        -0.9689f, 1.8758f, 0.0415f,
                        0.0557f, -0.2040f, 1.0570f};
        img.samples.resize(std::size_t(kN) * kN);
        std::mt19937 rng(8148);
        std::normal_distribution<double> noise(0.0, 1.0);
        for (std::uint32_t y = 0; y < kN; ++y)
            for (std::uint32_t x = 0; x < kN; ++x)
                img.samples[std::size_t(y) * kN + x] = std::uint16_t(std::lround(
                    img.black[std::size_t(img.channelAt(x, y))] + 20.0 + sigma * noise(rng)));
        return img;
    };

    // Per-channel means away from the border.
    struct Means { std::array<double, 3> linear{}, display{}; };
    const auto render = [&](const orion::raw::BayerImage& mosaic) {
        orion::pipe::DevelopPipeline dev(*device, std::string(ORION_SHADER_DIR), mosaic);
        orion::pipe::Adjustments adj;
        adj.wb = dev.asShotWhiteBalance();
        adj.exposureEv = 1.0f;
        dev.apply(adj);
        dev.render();
        Means m;
        const std::uint32_t w = dev.width(), h = dev.height(), e = w / 32;
        std::vector<__fp16> lin(std::size_t(w) * h * 4);
        dev.highlightStages().input->download(lin.data(), std::size_t(w) * 4 * sizeof(__fp16), w, h);
        const std::uint32_t ow = dev.outputWidth(), oh = dev.outputHeight();
        std::vector<std::uint8_t> out(std::size_t(ow) * oh * 4);
        dev.output().download(out.data(), std::size_t(ow) * 4, ow, oh);
        double n = 0, on = 0;
        for (std::uint32_t y = e; y < h - e; ++y)
            for (std::uint32_t x = e; x < w - e; ++x, ++n)
                for (std::size_t c = 0; c < 3; ++c)
                    m.linear[c] += double(lin[(std::size_t(y) * w + x) * 4 + c]);
        for (std::uint32_t y = e; y < oh - e; ++y)
            for (std::uint32_t x = e; x < ow - e; ++x, ++on)
                for (std::size_t c = 0; c < 3; ++c)
                    m.display[c] += double(out[(std::size_t(y) * ow + x) * 4 + c]);
        for (std::size_t c = 0; c < 3; ++c) { m.linear[c] /= n; m.display[c] /= on; }
        return m;
    };

    Means clean, full, preview;
    try {
        const auto noisy = field(80.0);
        clean = render(field(0.0));
        full = render(noisy);
        preview = render(orion::raw::decimate(noisy, 4));   // Engine::kPreviewScale
    } catch (const std::exception& e) {
        report(false, "all three graphs render", e.what());
        return;
    }
    std::printf("  camera RGB clean (%.5f %.5f %.5f) noisy (%.5f %.5f %.5f)\n"
                "  8-bit full (%.2f %.2f %.2f) preview (%.2f %.2f %.2f) clean (%.2f %.2f %.2f)\n",
                clean.linear[0], clean.linear[1], clean.linear[2],
                full.linear[0], full.linear[1], full.linear[2],
                full.display[0], full.display[1], full.display[2],
                preview.display[0], preview.display[1], preview.display[2],
                clean.display[0], clean.display[1], clean.display[2]);

    // A clamp at zero roughly doubles this level — the mean of max(X, 0) for a
    // mean of 20 and a sigma of 80 is 43 — and 262k samples a channel put the
    // standard error near 1%.
    const char* names[3] = {"red", "green", "blue"};
    for (std::size_t c = 0; c < 3; ++c) {
        const double ratio = full.linear[c] / clean.linear[c];
        report(std::abs(ratio - 1.0) < 0.10,
               std::string("the noisy shadow's ") + names[c] + " is the clean one's, within 10%",
               "noisy " + std::to_string(full.linear[c]) + " clean " +
                   std::to_string(clean.linear[c]) + " ratio " + std::to_string(ratio));
    }
    // The preview's mean stands on 57k pixels of a 27-level spread: a tenth of
    // a level of standard error. Averaging was five levels off on the real sky.
    for (std::size_t c = 0; c < 3; ++c) {
        const double gap = preview.display[c] - full.display[c];
        report(std::abs(gap) < 1.0,
               std::string("the preview's ") + names[c] + " shadow is the release's, within a level",
               "preview " + std::to_string(preview.display[c]) + " full " +
                   std::to_string(full.display[c]));
    }
}

// ── Orientation ────────────────────────────────────────────────────────────

void testOrientation() {
    section("Orientation");

    // The shared mapping itself — one function in raw/RawImage.h, used by the
    // pipeline's geometry node and the thumbnail path alike. This used to
    // check a local mirror "kept in step by this test"; now that both callers
    // share the declared symbol, the real one is checked directly.
    using orion::raw::quarterTurnsFor;
    report(quarterTurnsFor(0) == 0, "flip 0 is no rotation");
    report(quarterTurnsFor(3) == 2, "flip 3 is 180 degrees");
    report(quarterTurnsFor(6) == 1, "flip 6 is 90 clockwise");
    report(quarterTurnsFor(5) == 3, "flip 5 is 90 anticlockwise");

    // A quarter turn swaps the output dimensions; a half turn does not.
    // Getting this wrong is why portraits displayed as landscape.
    const std::uint32_t w = 6024, h = 4024;
    for (int turns = 0; turns < 4; ++turns) {
        const bool swaps = (turns % 2) != 0;
        const std::uint32_t ow = swaps ? h : w;
        const std::uint32_t oh = swaps ? w : h;
        report(ow * oh == w * h, "rotation preserves pixel count at " +
                                 std::to_string(turns * 90) + " degrees");
        if (swaps) {
            report(ow == h && oh == w, "quarter turn swaps dimensions");
        } else {
            report(ow == w && oh == h, "half turn keeps dimensions");
        }
    }

    // Rotation wraps rather than growing without bound.
    int r = 0;
    for (int i = 0; i < 5; ++i) r = ((r + 1) % 4 + 4) % 4;
    report(r == 1, "five quarter turns wrap to one");
    report(((0 - 1) % 4 + 4) % 4 == 3, "rotating left from zero wraps to three");
}

// ── Export ─────────────────────────────────────────────────────────────────

/// Bit depth and the top-left pixel of a written file, straight from ImageIO.
///
/// The pixel is read in the file's *own* space — the bitmap context is built
/// from the image's color space, so nothing is converted on the way in and the
/// numbers are the ones actually stored. Reading it in sRGB would undo the very
/// conversion the test exists to prove happened.
///
/// The C API only, so this stays in a .cpp alongside the rest of the suite.
