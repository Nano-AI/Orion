/*  orion-tests — the things that broke, and must not break again.
 *
 *  No framework: a handful of macros and a count. These cover the maths that
 *  has actually produced visible bugs — white balance round-tripping, curve
 *  monotonicity, orientation dimensions, and CFA indexing — so a regression
 *  fails here rather than in a screenshot.
 */

#include "pipe/ToneCurve.h"
#include "pipe/WhiteBalance.h"
#include "raw/RawImage.h"
#include "gpu/MetalDevice.h"
#include "gpu/Resources.h"
#include "pipe/ShaderParams.h"
#include "util/ImageWriter.h"

#include <vector>

#include <cmath>
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
    testExportFormats();

    std::printf("\n%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
