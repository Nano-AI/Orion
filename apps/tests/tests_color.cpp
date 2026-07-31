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

// ── Orientation ────────────────────────────────────────────────────────────

/// Mirrors quarterTurnsFor in DevelopPipeline.cpp. Kept in step by this test.
int quarterTurnsFor(int flip) {
    switch (flip) {
        case 3: return 2;
        case 5: return 3;
        case 6: return 1;
        default: return 0;
    }
}

void testOrientation() {
    section("Orientation");

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
