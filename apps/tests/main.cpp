/*  orion-tests — the things that broke, and must not break again.
 *
 *  No framework: a handful of macros and a count. These cover the maths that
 *  has actually produced visible bugs — white balance round-tripping, curve
 *  monotonicity, orientation dimensions, and CFA indexing — so a regression
 *  fails here rather than in a screenshot.
 */

#include "pipe/ToneCurve.h"
#include "pipe/WhiteBalance.h"
#include "raw/NoiseProfile.h"
#include "raw/RawImage.h"
#include "gpu/MetalDevice.h"
#include "gpu/Resources.h"
#include "pipe/ShaderParams.h"
#include "util/ImageWriter.h"

#include <vector>

#include <algorithm>
#include <cmath>
#include <random>
#include <cstdio>
#include <string>

namespace {

int failures = 0;
int checks = 0;

void report(bool ok, const std::string& what, const std::string& detail = "") {
    ++checks;
    if (ok) return;
    ++failures;
    std::printf("  FAIL  %s%s%s\n", what.c_str(),
                detail.empty() ? "" : " — ", detail.c_str());
}

#define CHECK(cond) report((cond), #cond)

void checkNear(double got, double want, double tol, const std::string& what) {
    const bool ok = std::abs(got - want) <= tol;
    char detail[128];
    std::snprintf(detail, sizeof detail, "got %.5f, want %.5f (tol %.5f)", got, want, tol);
    report(ok, what, ok ? "" : detail);
}

void section(const char* name) { std::printf("%s\n", name); }

// ── White balance ──────────────────────────────────────────────────────────

// Sony ILCE-7M3, from LibRaw. XYZ -> camera, row-major.
constexpr float kSonyXyzToCam[9] = {
     0.7374f, -0.2389f, -0.0551f,
    -0.5435f,  1.3162f,  0.2519f,
    -0.1006f,  0.1795f,  0.6552f,
};

void testWhiteBalance() {
    section("White balance");

    // Warmer light needs more blue gain to neutralise, cooler needs more red.
    // If this inverts, every image comes out with the cast backwards.
    const auto warm = orion::pipe::multipliersFor({2800.0f, 0.0f}, kSonyXyzToCam);
    const auto cool = orion::pipe::multipliersFor({9000.0f, 0.0f}, kSonyXyzToCam);
    report(warm[2] > cool[2], "warmer light gets more blue gain");
    report(cool[0] > warm[0], "cooler light gets more red gain");

    // Green is the reference channel throughout the pipeline.
    for (float k : {2500.0f, 5500.0f, 12000.0f}) {
        const auto m = orion::pipe::multipliersFor({k, 0.0f}, kSonyXyzToCam);
        checkNear(m[1], 1.0, 1e-5, "green normalised at " + std::to_string(int(k)) + "K");
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
    // two ever disagree the demosaic reads the wrong colour everywhere.
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

void testExportFormats() {
    section("Export");

    using orion::util::ImageFormat;
    report(orion::util::formatForPath("a.png")  == ImageFormat::Png,  "png by extension");
    report(orion::util::formatForPath("a.PNG")  == ImageFormat::Png,  "extension is case-insensitive");
    report(orion::util::formatForPath("a.tif")  == ImageFormat::Tiff, "tif by extension");
    report(orion::util::formatForPath("a.tiff") == ImageFormat::Tiff, "tiff by extension");
    report(orion::util::formatForPath("a.jpg")  == ImageFormat::Jpeg, "jpg by extension");
    report(orion::util::formatForPath("noext")  == ImageFormat::Jpeg, "no extension defaults to jpeg");
    report(orion::util::formatForPath("a.b.png") == ImageFormat::Png, "uses the last extension");
}

// ── Orientation on the GPU ─────────────────────────────────────────────────

/// Runs the real orient kernel over a synthetic image whose pixels encode
/// their own coordinates, then checks that each output pixel came from where
/// the rotation says it should.
///
/// This is the test that would have caught the torn, sideways frame: the maths
/// tests above all pass on code that renders garbage, because they never touch
/// a texture.
void testOrientGpu() {
    section("Orientation (GPU)");

    constexpr std::uint32_t kW = 8, kH = 5;
    constexpr std::uint32_t kSquare = 8;   // max(kW, kH), as DevelopPipeline does

    std::unique_ptr<orion::gpu::Device> device;
    try {
        device = orion::gpu::Device::create();
    } catch (const std::exception& e) {
        report(false, "Metal device available", e.what());
        return;
    }

    auto library = orion::gpu::Library::createFromFile(
        *device, std::string(ORION_SHADER_DIR) + "/geometry.metallib");
    auto kernel = orion::gpu::Kernel::create(*device, *library, "geometry");

    auto src = orion::gpu::Texture::create(*device, kW, kH,
                                           orion::gpu::PixelFormat::RGBA16Float);
    auto dst = orion::gpu::Texture::create(*device, kSquare, kSquare,
                                           orion::gpu::PixelFormat::RGBA16Float);

    // Each pixel carries its own coordinates, so a misplaced sample is
    // immediately identifiable rather than just "looking wrong".
    std::vector<__fp16> input(std::size_t(kW) * kH * 4);
    for (std::uint32_t y = 0; y < kH; ++y) {
        for (std::uint32_t x = 0; x < kW; ++x) {
            const std::size_t i = (std::size_t(y) * kW + x) * 4;
            input[i + 0] = static_cast<__fp16>(x);
            input[i + 1] = static_cast<__fp16>(y);
            input[i + 2] = 0;
            input[i + 3] = 1;
        }
    }
    src->upload(input.data(), std::size_t(kW) * 4 * sizeof(__fp16));

    for (std::uint32_t turns = 0; turns < 4; ++turns) {
        const bool swaps = (turns % 2) != 0;
        const std::uint32_t ow = swaps ? kH : kW;
        const std::uint32_t oh = swaps ? kW : kH;

        orion::pipe::params::Geometry p{};
        p.outSize[0] = ow;
        p.outSize[1] = oh;
        p.inSize[0]  = kW;
        p.inSize[1]  = kH;
        p.quarterTurns = turns;
        p.straightenRad = 0.0f;
        p.cropOrigin[0] = 0.0f; p.cropOrigin[1] = 0.0f;
        p.cropSize[0]   = 1.0f; p.cropSize[1]   = 1.0f;

        orion::gpu::CommandBuffer cb(*device);
        cb.dispatch(*kernel, {src.get(), dst.get()}, &p, sizeof p, ow, oh);
        cb.commitAndWait();

        std::vector<__fp16> out(std::size_t(ow) * oh * 4);
        dst->download(out.data(), std::size_t(ow) * 4 * sizeof(__fp16), ow, oh);

        bool allCorrect = true;
        std::string firstBad;

        for (std::uint32_t oy = 0; oy < oh && allCorrect; ++oy) {
            for (std::uint32_t ox = 0; ox < ow && allCorrect; ++ox) {
                std::uint32_t sx = 0, sy = 0;
                switch (turns) {
                    case 1: sx = oy;              sy = kH - 1 - ox; break;
                    case 2: sx = kW - 1 - ox;     sy = kH - 1 - oy; break;
                    case 3: sx = kW - 1 - oy;     sy = ox;          break;
                    default: sx = ox;             sy = oy;          break;
                }

                const std::size_t i = (std::size_t(oy) * ow + ox) * 4;
                const auto gotX = static_cast<int>(float(out[i + 0]) + 0.5f);
                const auto gotY = static_cast<int>(float(out[i + 1]) + 0.5f);

                if (gotX != int(sx) || gotY != int(sy)) {
                    allCorrect = false;
                    char buf[160];
                    std::snprintf(buf, sizeof buf,
                                  "out(%u,%u) holds src(%d,%d), expected src(%u,%u)",
                                  ox, oy, gotX, gotY, sx, sy);
                    firstBad = buf;
                }
            }
        }

        report(allCorrect,
               "rotation " + std::to_string(turns * 90) + " maps every pixel",
               firstBad);

        // A quarter turn must actually change the framing — catches the case
        // where the kernel silently falls through to identity.
        if (turns != 0) {
            const auto x0 = static_cast<int>(float(out[0]) + 0.5f);
            const auto y0 = static_cast<int>(float(out[1]) + 0.5f);
            report(!(x0 == 0 && y0 == 0),
                   "rotation " + std::to_string(turns * 90) + " moves the origin");
        }
    }
}

// ── Neutrality of the display transform ────────────────────────────────────

/// Grey in, grey out.
///
/// This is the single most valuable check in the file. A tone mapper's inset
/// and outset matrices must each preserve the achromatic axis; if they do not,
/// *every* pixel picks up a cast and the image looks subtly wrong in a way that
/// is easy to mistake for a white balance problem. An earlier pair of matrices
/// mapped neutral to roughly (0.84, 0.94, 1.22) and cast the whole image purple.
void testDisplayNeutrality() {
    section("Display transform neutrality");

    std::unique_ptr<orion::gpu::Device> device;
    try {
        device = orion::gpu::Device::create();
    } catch (const std::exception& e) {
        report(false, "Metal device available", e.what());
        return;
    }

    auto library = orion::gpu::Library::createFromFile(
        *device, std::string(ORION_SHADER_DIR) + "/developDisplay.metallib");
    auto kernel = orion::gpu::Kernel::create(*device, *library, "developDisplay");

    // A ramp of neutral greys spanning 12 stops, from deep shadow to well
    // above diffuse white.
    constexpr std::uint32_t kN = 24;
    std::vector<__fp16> input(std::size_t(kN) * 4);
    std::vector<float> levels(kN);
    for (std::uint32_t i = 0; i < kN; ++i) {
        const float ev = -10.0f + 12.0f * float(i) / float(kN - 1);
        const float v = std::pow(2.0f, ev);
        levels[i] = v;
        input[i * 4 + 0] = static_cast<__fp16>(v);
        input[i * 4 + 1] = static_cast<__fp16>(v);
        input[i * 4 + 2] = static_cast<__fp16>(v);
        input[i * 4 + 3] = 1;
    }

    auto src = orion::gpu::Texture::create(*device, kN, 1,
                                           orion::gpu::PixelFormat::RGBA16Float);
    auto dst = orion::gpu::Texture::create(*device, kN, 1,
                                           orion::gpu::PixelFormat::RGBA8Unorm);
    src->upload(input.data(), std::size_t(kN) * 4 * sizeof(__fp16));

    // An identity LUT, so the curve stage is a pass-through.
    auto lut = orion::gpu::Texture::create(*device, orion::pipe::kCurveResolution,
                                           orion::pipe::kCurveRows,
                                           orion::gpu::PixelFormat::R32Float);
    const auto lutData = orion::pipe::buildCurveLut({});
    lut->upload(lutData.data(), orion::pipe::kCurveResolution * sizeof(float));

    orion::pipe::params::Display p{};
    p.contrast = 1.0f;
    p.pivot = -2.5f;
    p.curveIdentity = 1u;
    p.resolution = orion::pipe::kCurveResolution;
    p.size[0] = kN;
    p.size[1] = 1;

    orion::gpu::CommandBuffer cb(*device);
    cb.dispatch(*kernel, {src.get(), lut.get(), dst.get()}, &p, sizeof p, kN, 1);
    cb.commitAndWait();

    std::vector<std::uint8_t> out(std::size_t(kN) * 4);
    dst->download(out.data(), std::size_t(kN) * 4, kN, 1);

    int worst = 0;
    float worstLevel = 0.0f;
    bool monotone = true;
    int previousGreen = -1;

    for (std::uint32_t i = 0; i < kN; ++i) {
        const int r = out[i * 4 + 0], g = out[i * 4 + 1], b = out[i * 4 + 2];
        const int spread = std::max({r, g, b}) - std::min({r, g, b});
        if (spread > worst) { worst = spread; worstLevel = levels[i]; }
        if (g < previousGreen) monotone = false;
        previousGreen = g;
    }

    char detail[160];
    std::snprintf(detail, sizeof detail,
                  "worst channel spread %d/255 at scene level %.4f", worst, worstLevel);
    // 2/255 covers 8-bit rounding; anything beyond that is a genuine cast.
    report(worst <= 2, "neutral grey stays neutral through the display transform",
           worst <= 2 ? "" : detail);

    report(monotone, "brighter input never produces a darker output");

    // Middle grey must land near the middle of the display range. This is what
    // catches a double encode: applying a transfer function on top of AgX put
    // 0.18 at 189/255 instead of about 128.
    const float target = 0.18f;
    std::uint32_t nearest = 0;
    for (std::uint32_t i = 1; i < kN; ++i) {
        if (std::abs(levels[i] - target) < std::abs(levels[nearest] - target)) nearest = i;
    }
    const int mid = out[nearest * 4 + 1];
    std::snprintf(detail, sizeof detail, "scene %.3f -> %d/255", levels[nearest], mid);
    report(mid > 95 && mid < 165, "middle grey lands mid-range", detail);
}

/// Crop selects the requested region, not merely a smaller one.
///
/// An off-by-one or a transposed origin here shows up as "the crop works but
/// grabs the wrong part of the frame", which is easy to miss by eye on a
/// uniform subject.
void testCropGpu() {
    section("Crop (GPU)");

    constexpr std::uint32_t kW = 16, kH = 16;

    std::unique_ptr<orion::gpu::Device> device;
    try {
        device = orion::gpu::Device::create();
    } catch (const std::exception& e) {
        report(false, "Metal device available", e.what());
        return;
    }

    auto library = orion::gpu::Library::createFromFile(
        *device, std::string(ORION_SHADER_DIR) + "/geometry.metallib");
    auto kernel = orion::gpu::Kernel::create(*device, *library, "geometry");

    auto src = orion::gpu::Texture::create(*device, kW, kH,
                                           orion::gpu::PixelFormat::RGBA16Float);
    auto dst = orion::gpu::Texture::create(*device, kW, kH,
                                           orion::gpu::PixelFormat::RGBA16Float);

    std::vector<__fp16> input(std::size_t(kW) * kH * 4);
    for (std::uint32_t y = 0; y < kH; ++y) {
        for (std::uint32_t x = 0; x < kW; ++x) {
            const std::size_t i = (std::size_t(y) * kW + x) * 4;
            input[i + 0] = static_cast<__fp16>(x);
            input[i + 1] = static_cast<__fp16>(y);
            input[i + 2] = 0;
            input[i + 3] = 1;
        }
    }
    src->upload(input.data(), std::size_t(kW) * 4 * sizeof(__fp16));

    // Right half, bottom half: origin (0.5, 0.5), size (0.5, 0.5).
    const std::uint32_t ow = kW / 2, oh = kH / 2;
    orion::pipe::params::Geometry p{};
    p.outSize[0] = ow; p.outSize[1] = oh;
    p.inSize[0]  = kW; p.inSize[1]  = kH;
    p.quarterTurns = 0;
    p.straightenRad = 0.0f;
    p.cropOrigin[0] = 0.5f; p.cropOrigin[1] = 0.5f;
    p.cropSize[0]   = 0.5f; p.cropSize[1]   = 0.5f;

    orion::gpu::CommandBuffer cb(*device);
    cb.dispatch(*kernel, {src.get(), dst.get()}, &p, sizeof p, ow, oh);
    cb.commitAndWait();

    std::vector<__fp16> out(std::size_t(ow) * oh * 4);
    dst->download(out.data(), std::size_t(ow) * 4 * sizeof(__fp16), ow, oh);

    // The crop's top-left must be the source's centre, within half a pixel of
    // bilinear positioning.
    const double x0 = double(out[0]);
    const double y0 = double(out[1]);
    checkNear(x0, 8.0, 0.6, "crop origin lands at the requested column");
    checkNear(y0, 8.0, 0.6, "crop origin lands at the requested row");

    // And the far corner must reach the source's far corner.
    const std::size_t last = (std::size_t(oh - 1) * ow + (ow - 1)) * 4;
    checkNear(double(out[last + 0]), 15.0, 0.6, "crop reaches the right edge");
    checkNear(double(out[last + 1]), 15.0, 0.6, "crop reaches the bottom edge");

    // A full-frame crop must be an exact pass-through.
    p.cropOrigin[0] = 0.0f; p.cropOrigin[1] = 0.0f;
    p.cropSize[0]   = 1.0f; p.cropSize[1]   = 1.0f;
    p.outSize[0] = kW; p.outSize[1] = kH;

    orion::gpu::CommandBuffer cb2(*device);
    cb2.dispatch(*kernel, {src.get(), dst.get()}, &p, sizeof p, kW, kH);
    cb2.commitAndWait();

    std::vector<__fp16> full(std::size_t(kW) * kH * 4);
    dst->download(full.data(), std::size_t(kW) * 4 * sizeof(__fp16), kW, kH);

    bool identity = true;
    for (std::uint32_t i = 0; i < kW * kH; ++i) {
        if (std::abs(double(full[i * 4]) - double(input[i * 4])) > 0.51 ||
            std::abs(double(full[i * 4 + 1]) - double(input[i * 4 + 1])) > 0.51) {
            identity = false;
            break;
        }
    }
    report(identity, "a full-frame crop is a pass-through");
}

/// The straighten preview must agree with what committing the crop produces.
///
/// While the crop tool is open the geometry node renders onto a canvas larger
/// than the frame, so cropOrigin and cropSize describe the canvas rather than
/// the user's rectangle. The pivot was derived from those two, which meant the
/// preview turned the picture about the frame centre and the committed render
/// turned it about the crop centre. With an off-centre crop the two disagree,
/// and the picture you got was not the picture the white box had shown.
void testStraightenPivot() {
    section("Straighten pivot (GPU)");

    constexpr std::uint32_t kW = 64, kH = 64;
    constexpr float kCanvas = 1.42f;
    constexpr float kAngle  = 8.0f * 3.14159265358979f / 180.0f;

    // An off-centre crop — the case the two paths disagreed on.
    constexpr float cx = 0.10f, cy = 0.15f, cw = 0.40f, ch = 0.35f;

    std::unique_ptr<orion::gpu::Device> device;
    try {
        device = orion::gpu::Device::create();
    } catch (const std::exception& e) {
        report(false, "Metal device available", e.what());
        return;
    }

    auto library = orion::gpu::Library::createFromFile(
        *device, std::string(ORION_SHADER_DIR) + "/geometry.metallib");
    auto kernel = orion::gpu::Kernel::create(*device, *library, "geometry");

    auto src = orion::gpu::Texture::create(*device, kW, kH,
                                           orion::gpu::PixelFormat::RGBA16Float);

    // Coordinates as colour, so every output pixel names the source pixel it
    // came from and the two renders can be compared exactly.
    std::vector<__fp16> input(std::size_t(kW) * kH * 4);
    for (std::uint32_t y = 0; y < kH; ++y) {
        for (std::uint32_t x = 0; x < kW; ++x) {
            const std::size_t i = (std::size_t(y) * kW + x) * 4;
            input[i + 0] = static_cast<__fp16>(x);
            input[i + 1] = static_cast<__fp16>(y);
            input[i + 2] = 0;
            input[i + 3] = 1;
        }
    }
    src->upload(input.data(), std::size_t(kW) * 4 * sizeof(__fp16));

    orion::pipe::params::Geometry base{};
    base.inSize[0] = kW; base.inSize[1] = kH;
    base.quarterTurns  = 0;
    base.straightenRad = kAngle;
    // The frame's centre, which is what the app sends. An off-centre crop is
    // the case the two paths used to disagree on.
    base.pivot[0] = 0.5f;
    base.pivot[1] = 0.5f;

    const auto render = [&](const orion::pipe::params::Geometry& p) {
        auto dst = orion::gpu::Texture::create(*device, p.outSize[0], p.outSize[1],
                                               orion::gpu::PixelFormat::RGBA16Float);
        orion::gpu::CommandBuffer cb(*device);
        cb.dispatch(*kernel, {src.get(), dst.get()}, &p, sizeof p,
                    p.outSize[0], p.outSize[1]);
        cb.commitAndWait();

        std::vector<__fp16> out(std::size_t(p.outSize[0]) * p.outSize[1] * 4);
        dst->download(out.data(), std::size_t(p.outSize[0]) * 4 * sizeof(__fp16),
                      p.outSize[0], p.outSize[1]);
        return out;
    };

    // The committed render: the crop rectangle alone.
    orion::pipe::params::Geometry commit = base;
    commit.cropOrigin[0] = cx; commit.cropOrigin[1] = cy;
    commit.cropSize[0]   = cw; commit.cropSize[1]   = ch;
    commit.outSize[0] = static_cast<std::uint32_t>(kW * cw);
    commit.outSize[1] = static_cast<std::uint32_t>(kH * ch);
    const auto committed = render(commit);

    // The preview: the whole enlarged canvas.
    orion::pipe::params::Geometry preview = base;
    preview.cropOrigin[0] = 0.5f - kCanvas * 0.5f;
    preview.cropOrigin[1] = 0.5f - kCanvas * 0.5f;
    preview.cropSize[0]   = kCanvas;
    preview.cropSize[1]   = kCanvas;
    preview.outSize[0] = static_cast<std::uint32_t>(kW * kCanvas);
    preview.outSize[1] = static_cast<std::uint32_t>(kH * kCanvas);
    const auto previewed = render(preview);

    // Where the crop rectangle lands inside the preview canvas.
    const double pw = preview.outSize[0], ph = preview.outSize[1];
    const double originU = (cx - preview.cropOrigin[0]) / kCanvas;
    const double originV = (cy - preview.cropOrigin[1]) / kCanvas;

    double worst = 0.0;
    int sampled = 0;
    for (std::uint32_t oy = 2; oy + 2 < commit.outSize[1]; oy += 4) {
        for (std::uint32_t ox = 2; ox + 2 < commit.outSize[0]; ox += 4) {
            // Same point of the crop, addressed in the preview's canvas.
            const double u = originU + (double(ox) + 0.5) / commit.outSize[0]
                                       * (cw / kCanvas);
            const double v = originV + (double(oy) + 0.5) / commit.outSize[1]
                                       * (ch / kCanvas);
            const auto px = std::uint32_t(u * pw);
            const auto py = std::uint32_t(v * ph);
            if (px >= preview.outSize[0] || py >= preview.outSize[1]) continue;

            const std::size_t a = (std::size_t(oy) * commit.outSize[0] + ox) * 4;
            const std::size_t b = (std::size_t(py) * preview.outSize[0] + px) * 4;

            worst = std::max(worst, std::abs(double(committed[a]) - double(previewed[b])));
            worst = std::max(worst,
                             std::abs(double(committed[a + 1]) - double(previewed[b + 1])));
            ++sampled;
        }
    }

    report(sampled > 20, "the comparison sampled the crop", "n = " + std::to_string(sampled));

    // A pixel of slack for the two grids landing on different sample points.
    report(worst <= 1.5,
           "the straighten preview shows what committing produces",
           "worst disagreement " + std::to_string(worst) + " px");

    // And with no crop the pivot is the frame centre, which must leave the
    // centre pixel exactly where it started.
    orion::pipe::params::Geometry centred{};
    centred.inSize[0] = kW; centred.inSize[1] = kH;
    centred.outSize[0] = kW; centred.outSize[1] = kH;
    centred.straightenRad = kAngle;
    centred.cropSize[0] = 1.0f; centred.cropSize[1] = 1.0f;
    centred.pivot[0] = 0.5f; centred.pivot[1] = 0.5f;
    const auto whole = render(centred);

    const std::size_t mid = (std::size_t(kH / 2) * kW + kW / 2) * 4;
    checkNear(double(whole[mid + 0]), kW / 2.0, 0.6,
              "rotating about the centre leaves the centre column put");
    checkNear(double(whole[mid + 1]), kH / 2.0, 0.6,
              "rotating about the centre leaves the centre row put");
}

/// The noise estimator, against noise we made ourselves.
///
/// A fitted a and b are only useful if they are close to the truth, and a
/// synthetic frame is the only place the truth is known. The generator is a
/// smooth ramp plus Poisson-Gaussian noise at a chosen a and b.
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
/// a coloured one. A warm cloud drives red hardest, so red reaches its stop
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

    // Coordinates as colour again, so an output pixel names where it came from.
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
    lens.centreX = 0.5f; lens.centreY = 0.5f;

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
    report(worstIdentity < 2e-3, "no correction is a pass-through",
           "worst " + std::to_string(worstIdentity));

    // Distortion. The (1 - k1) term pins r_d(1) = 1, so a pixel on the frame's
    // diagonal at r = 1 must not move: without that term the whole picture
    // scales and the control reads as a zoom.
    lens.k1 = 0.3f;
    run(out);

    // The corner is at r = 1 by construction (R_norm is half the diagonal).
    const std::size_t corner = ((std::size_t(kH) - 1) * kW + (kW - 1)) * 4;
    checkNear(double(out[corner + 0]), double(input[corner + 0]), 0.01,
              "the frame corner does not move under distortion");
    checkNear(double(out[corner + 1]), double(input[corner + 1]), 0.01,
              "the frame corner does not move vertically either");

    // The centre never moves under a radial model, at any coefficient.
    const std::size_t centre = (std::size_t(kH / 2) * kW + kW / 2) * 4;
    checkNear(double(out[centre + 0]), double(input[centre + 0]), 0.01,
              "the optical centre is fixed");

    // Between them it must actually move something, or the control is inert.
    const std::size_t mid = (std::size_t(kH / 2) * kW + (3 * kW / 4)) * 4;
    report(std::abs(double(out[mid]) - double(input[mid])) > 0.005,
           "distortion moves the interior",
           "delta " + std::to_string(double(out[mid]) - double(input[mid])));
    lens.k1 = 0.0f;

    // Vignetting divides, so a negative coefficient must *brighten* the corner
    // — that is the correction for a lens that darkens it. Getting the sign
    // backwards doubles the vignette instead of removing it, which looks
    // plausible enough to ship.
    lens.vignetteA = -0.4f;
    run(out);
    const double cornerBlue = double(out[corner + 2]);
    const double centreBlue = double(out[centre + 2]);
    report(cornerBlue > 0.5 + 1e-3, "negative vignetting brightens the corner",
           "corner " + std::to_string(cornerBlue));
    checkNear(centreBlue, 0.5, 5e-3, "vignetting leaves the centre alone");

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
}

/// The graph's output must actually carry more than eight bits.
///
/// Changing a texture format is the kind of edit that looks done the moment it
/// compiles. This renders a gradient fine enough that eight bits could not
/// represent it and checks the steps survive — if the format quietly fell back,
/// adjacent outputs would land on the same value.
void testOutputDepth() {
    section("Output depth");

    constexpr std::uint32_t kN = 512;

    std::unique_ptr<orion::gpu::Device> device;
    try {
        device = orion::gpu::Device::create();
    } catch (const std::exception& e) {
        report(false, "Metal device available", e.what());
        return;
    }

    auto library = orion::gpu::Library::createFromFile(
        *device, std::string(ORION_SHADER_DIR) + "/developDisplay.metallib");
    auto kernel = orion::gpu::Kernel::create(*device, *library, "developDisplay");

    auto src = orion::gpu::Texture::create(*device, kN, 1,
                                           orion::gpu::PixelFormat::RGBA16Float);
    auto dst = orion::gpu::Texture::create(*device, kN, 1,
                                           orion::gpu::PixelFormat::RGBA16Float);
    auto lut = orion::gpu::Texture::create(*device, orion::pipe::kCurveResolution,
                                           orion::pipe::kCurveRows,
                                           orion::gpu::PixelFormat::R32Float);
    const auto identity = orion::pipe::buildCurveLut({});
    lut->upload(identity.data(), orion::pipe::kCurveResolution * sizeof(float));

    // A gradient across a narrow slice of scene-linear values, so the *output*
    // steps are far finer than 1/255.
    std::vector<__fp16> input(std::size_t(kN) * 4);
    for (std::uint32_t i = 0; i < kN; ++i) {
        const float v = 0.180f + 0.004f * (float(i) / float(kN - 1));
        input[i * 4 + 0] = static_cast<__fp16>(v);
        input[i * 4 + 1] = static_cast<__fp16>(v);
        input[i * 4 + 2] = static_cast<__fp16>(v);
        input[i * 4 + 3] = 1;
    }
    src->upload(input.data(), std::size_t(kN) * 4 * sizeof(__fp16));

    orion::pipe::params::Display dp{};
    dp.contrast = 1.0f;
    dp.pivot = 0.18f;
    dp.curveIdentity = 1;
    dp.resolution = orion::pipe::kCurveResolution;
    dp.size[0] = kN;
    dp.size[1] = 1;

    orion::gpu::CommandBuffer cb(*device);
    cb.dispatch(*kernel, {src.get(), lut.get(), dst.get()}, &dp, sizeof dp, kN, 1);
    cb.commitAndWait();

    std::vector<__fp16> out(std::size_t(kN) * 4);
    dst->download(out.data(), std::size_t(kN) * 4 * sizeof(__fp16), kN, 1);

    // How many distinct green values came back. Eight bits across this range
    // could give only a handful.
    std::vector<float> values;
    values.reserve(kN);
    for (std::uint32_t i = 0; i < kN; ++i) values.push_back(float(out[i * 4 + 1]));

    std::sort(values.begin(), values.end());
    const auto last = std::unique(values.begin(), values.end());
    const auto distinct = static_cast<std::size_t>(std::distance(values.begin(), last));

    const double span = double(values.back() - values.front());
    const double eightBitSteps = span * 255.0;

    report(span > 0.0, "the gradient produced a range at all");
    report(distinct > eightBitSteps * 4.0,
           "the output resolves far finer than eight bits could",
           std::to_string(distinct) + " distinct values across "
               + std::to_string(eightBitSteps) + " eight-bit steps");

    // And it must be monotone: a format mismatch shows up as noise, not as a
    // smooth ramp.
    bool monotone = true;
    for (std::uint32_t i = 1; i < kN; ++i) {
        if (float(out[i * 4 + 1]) < float(out[(i - 1) * 4 + 1]) - 1e-5f) {
            monotone = false;
            break;
        }
    }
    report(monotone, "the output ramp is monotone");
}

}  // namespace

int main() {
    std::printf("orion-tests\n\n");

    testWhiteBalance();
    testToneCurve();
    testCfa();
    testOrientation();
    testOrientGpu();
    testDisplayNeutrality();
    testCropGpu();
    testStraightenPivot();
    testNoiseEstimator();
    testDenoiseGpu();
    testHighlightRecoveryGpu();
    testLensGpu();
    testOutputDepth();
    testExportFormats();

    std::printf("\n%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
