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
#include "pipe/DevelopPipeline.h"
#include "pipe/HueSatMap.h"
#include "pipe/LensDatabase.h"
#include "pipe/LensGeometry.h"
#include "pipe/AutoEnhance.h"
#include "pipe/CubeLut.h"
#include "pipe/ExposureFusion.h"
#include "pipe/Dehaze.h"
#include "pipe/LocalLaplacian.h"
#include <set>

#include "pipe/MaskGeometry.h"
#include "util/ImageWriter.h"

#include <CoreGraphics/CoreGraphics.h>
#include <ImageIO/ImageIO.h>

#include <vector>

#include <algorithm>
#include <utility>
#include <array>
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
bool readBack(const std::string& path, int& bitsPerComponent, double rgb[3]) {
    CFStringRef p = CFStringCreateWithCString(nullptr, path.c_str(), kCFStringEncodingUTF8);
    if (p == nullptr) return false;
    CFURLRef url = CFURLCreateWithFileSystemPath(nullptr, p, kCFURLPOSIXPathStyle, false);
    CFRelease(p);
    if (url == nullptr) return false;

    CGImageSourceRef src = CGImageSourceCreateWithURL(url, nullptr);
    CFRelease(url);
    if (src == nullptr) return false;

    CGImageRef image = CGImageSourceCreateImageAtIndex(src, 0, nullptr);
    CFRelease(src);
    if (image == nullptr) return false;

    bitsPerComponent = static_cast<int>(CGImageGetBitsPerComponent(image));

    CGColorSpaceRef space = CGImageGetColorSpace(image);
    if (space == nullptr) { CGImageRelease(image); return false; }

    std::uint16_t pixel[4] = {0, 0, 0, 0};
    CGContextRef ctx = CGBitmapContextCreate(
        pixel, 1, 1, 16, sizeof pixel, space,
        static_cast<CGBitmapInfo>(kCGImageAlphaNoneSkipLast) | kCGBitmapByteOrder16Little);
    if (ctx == nullptr) { CGImageRelease(image); return false; }

    // Draw the whole image scaled down to the single pixel we sample. The test
    // image is a horizontal ramp with a constant red channel, so the average is
    // exactly as diagnostic as any one pixel and needs no coordinate care.
    CGContextDrawImage(ctx, CGRectMake(0, 0, 1, 1), image);
    CGContextRelease(ctx);
    CGImageRelease(image);

    for (int i = 0; i < 3; ++i) rgb[i] = double(pixel[i]) / 65535.0;
    return true;
}

/// Which metadata a written file carries: a GPS block, and the one EXIF field
/// the stand-in source below puts in.
///
/// The *field* rather than the EXIF dictionary, because ImageIO writes a small
/// EXIF block of its own — colour space, pixel dimensions — into every JPEG
/// whatever it is handed. What matters is whether the camera's data came
/// across, not whether the container has an EXIF section at all.
bool metadataBlocks(const std::string& path, bool& gps, bool& exif) {
    gps = exif = false;
    CFStringRef p = CFStringCreateWithCString(nullptr, path.c_str(), kCFStringEncodingUTF8);
    if (p == nullptr) return false;
    CFURLRef url = CFURLCreateWithFileSystemPath(nullptr, p, kCFURLPOSIXPathStyle, false);
    CFRelease(p);
    if (url == nullptr) return false;

    CGImageSourceRef src = CGImageSourceCreateWithURL(url, nullptr);
    CFRelease(url);
    if (src == nullptr) return false;

    CFDictionaryRef all = CGImageSourceCopyPropertiesAtIndex(src, 0, nullptr);
    CFRelease(src);
    if (all == nullptr) return false;

    gps = CFDictionaryContainsKey(all, kCGImagePropertyGPSDictionary);
    if (auto block = static_cast<CFDictionaryRef>(
            CFDictionaryGetValue(all, kCGImagePropertyExifDictionary))) {
        exif = CFDictionaryContainsKey(block, kCGImagePropertyExifFNumber);
    }
    CFRelease(all);
    return true;
}

/// A JPEG carrying a GPS block and one EXIF field, to stand in for a camera
/// file. Written here rather than taken from `samples/` because that folder is
/// local-only, and because the bodies in it have no GPS receiver.
bool writeJpegWithGps(const std::string& path, const std::uint16_t* rgba,
                      std::uint32_t width, std::uint32_t height, std::size_t bytesPerRow) {
    CFStringRef p = CFStringCreateWithCString(nullptr, path.c_str(), kCFStringEncodingUTF8);
    if (p == nullptr) return false;
    CFURLRef url = CFURLCreateWithFileSystemPath(nullptr, p, kCFURLPOSIXPathStyle, false);
    CFRelease(p);
    if (url == nullptr) return false;

    CGColorSpaceRef space = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
    CGContextRef ctx = CGBitmapContextCreate(
        const_cast<std::uint16_t*>(rgba), width, height, 16, bytesPerRow, space,
        static_cast<CGBitmapInfo>(kCGImageAlphaNoneSkipLast) | kCGBitmapByteOrder16Little);
    CGColorSpaceRelease(space);
    if (ctx == nullptr) { CFRelease(url); return false; }

    CGImageRef image = CGBitmapContextCreateImage(ctx);
    CGContextRelease(ctx);
    if (image == nullptr) { CFRelease(url); return false; }

    CGImageDestinationRef dst =
        CGImageDestinationCreateWithURL(url, CFSTR("public.jpeg"), 1, nullptr);
    CFRelease(url);
    if (dst == nullptr) { CGImageRelease(image); return false; }

    const double lat = 47.6205, lon = 122.3493, fnumber = 2.8;
    CFNumberRef latRef = CFNumberCreate(nullptr, kCFNumberDoubleType, &lat);
    CFNumberRef lonRef = CFNumberCreate(nullptr, kCFNumberDoubleType, &lon);
    CFNumberRef fRef   = CFNumberCreate(nullptr, kCFNumberDoubleType, &fnumber);

    const void* gpsKeys[] = {kCGImagePropertyGPSLatitude, kCGImagePropertyGPSLatitudeRef,
                             kCGImagePropertyGPSLongitude, kCGImagePropertyGPSLongitudeRef};
    const void* gpsVals[] = {latRef, CFSTR("N"), lonRef, CFSTR("W")};
    CFDictionaryRef gps = CFDictionaryCreate(nullptr, gpsKeys, gpsVals, 4,
                                             &kCFTypeDictionaryKeyCallBacks,
                                             &kCFTypeDictionaryValueCallBacks);

    const void* exifKeys[] = {kCGImagePropertyExifFNumber};
    const void* exifVals[] = {fRef};
    CFDictionaryRef exif = CFDictionaryCreate(nullptr, exifKeys, exifVals, 1,
                                              &kCFTypeDictionaryKeyCallBacks,
                                              &kCFTypeDictionaryValueCallBacks);

    const void* keys[] = {kCGImagePropertyGPSDictionary, kCGImagePropertyExifDictionary};
    const void* vals[] = {gps, exif};
    CFDictionaryRef props = CFDictionaryCreate(nullptr, keys, vals, 2,
                                               &kCFTypeDictionaryKeyCallBacks,
                                               &kCFTypeDictionaryValueCallBacks);

    CGImageDestinationAddImage(dst, image, props);
    const bool ok = CGImageDestinationFinalize(dst);

    CFRelease(props); CFRelease(exif); CFRelease(gps);
    CFRelease(fRef); CFRelease(lonRef); CFRelease(latRef);
    CFRelease(dst);
    CGImageRelease(image);
    return ok;
}

/// Black levels from LibRaw's three sources: a global offset, a per-channel
/// trim, and a 2D pattern.
///
/// The 2x2 pattern used to be averaged into one number and added to all four
/// channels. That is wrong whenever the pattern has real spread — it leaves the
/// spread in the shadows as a colour cast, on every frame, with no control that
/// can remove it. The pattern lines up with the CFA cell for cell, so each
/// channel can have its own.
/// The vendored lensfun database, and the name matching that has to survive
/// EXIF spelling.
///
/// The matcher is the risky part, not the parser: a *confident wrong* profile
/// distorts the frame and labels the result "measured". So the assertions are
/// as much about what must not match as about what must.
void testLensDatabase() {
    section("Lens database");

    orion::pipe::LensDatabase db(std::string(ORION_DATA_DIR) + "/lensfun");
    report(db.loaded(), "the vendored database parses",
           std::to_string(db.lensCount()) + " lenses");
    if (!db.loaded()) return;

    report(db.lensCount() > 1000, "and it is the whole database, not one file",
           std::to_string(db.lensCount()) + " lenses");

    // A lens that is in it, spelled the way a Sony body writes it: no maker,
    // capital F, no slash.
    {
        const auto p = db.lookup("FE 24-70mm F2.8 GM", 35.0f, 4.0f);
        report(p.found, "a Sony zoom is found from the EXIF spelling", p.lens);
        if (p.found) {
            report(p.lens.find("24-70") != std::string::npos,
                   "and it is the 24-70, not another lens", p.lens);
            const bool moves = p.poly[0] != 0.0f || p.poly[1] != 0.0f || p.poly[2] != 0.0f;
            report(moves, "with distortion coefficients that are not all zero");
        }
    }

    // Vignetting interpolates across aperture rather than snapping to the
    // nearest calibrated stop, and does it in the reciprocal — which is what
    // lensfun does, because the f-number is a ratio and vignetting tracks the
    // entrance pupil.
    //
    // Nearest-stop is invisible by inspection and obvious here: a lens
    // calibrated at two stops renders every aperture between them identically,
    // then jumps. This asks whether an intermediate aperture lands strictly
    // between its neighbours.
    {
        // A prime with vignetting calibrated at many stops. The Sony zoom
        // above has distortion data but no vignetting, which is common — the
        // database is not uniformly populated, and a test that assumed it was
        // measured nothing.
        const char* lens = "smc Pentax-FA 50mm f/1.4";
        const float focal = 50.0f;
        const auto wide  = db.lookup(lens, focal, 2.0f);
        const auto mid   = db.lookup(lens, focal, 2.4f);   // between 2 and 2.8
        const auto tight = db.lookup(lens, focal, 2.8f);

        if (wide.found && mid.found && tight.found) {
            const float a = wide.vignette[0], b = mid.vignette[0], c = tight.vignette[0];
            std::printf("  vignette p_a: f/2 %.4f, f/2.4 %.4f (uncalibrated), f/2.8 %.4f\n", a, b, c);

            // Vignetting weakens as the lens stops down, so the coefficients
            // should be ordered and the middle one should be neither endpoint.
            const bool between = (b != a) && (b != c);
            report(between, "an uncalibrated aperture is interpolated, not snapped",
                   std::to_string(a) + " / " + std::to_string(b) + " / " + std::to_string(c));

            const bool ordered = (a < b && b < c) || (a > b && b > c);
            report(ordered, "and lands between its neighbours rather than outside them",
                   std::to_string(a) + " / " + std::to_string(b) + " / " + std::to_string(c));
        } else {
            report(false, "the calibrated prime is found at three apertures",
                   "lookup failed");
        }
    }

    // Interpolation across the zoom range: a 24-70 does not distort the same
    // way at both ends, so two focal lengths must not return one answer.
    {
        const auto wide = db.lookup("FE 24-70mm F2.8 GM", 24.0f, 4.0f);
        const auto tele = db.lookup("FE 24-70mm F2.8 GM", 70.0f, 4.0f);
        if (wide.found && tele.found) {
            const bool differ = wide.poly[0] != tele.poly[0] ||
                                wide.poly[1] != tele.poly[1] ||
                                wide.poly[2] != tele.poly[2];
            report(differ, "the correction changes with focal length");
        } else {
            report(false, "both ends of the zoom are found");
        }
    }

    // What must NOT match.
    report(!db.lookup("", 24.0f, 4.0f).found, "an empty lens name finds nothing");
    report(!db.lookup("50mm", 50.0f, 2.0f).found,
           "a name too short to identify a lens finds nothing");
    report(!db.lookup("Wobbleflex Hyperprime 404mm", 404.0f, 4.0f).found,
           "a lens that does not exist finds nothing");

    // The developer's own lens is a mirrorless Sigma that is not in the
    // database. The DSLR "24mm f/1.4 DG HSM" is a different optical design, and
    // matching it would apply one lens's distortion to another's picture.
    {
        const auto p = db.lookup("24mm F1.4 DG DN | Art 022", 24.0f, 4.0f);
        if (p.found) {
            report(p.lens.find("DG DN") != std::string::npos,
                   "a DG DN lens never matches a DG HSM entry", p.lens);
        } else {
            report(true, "an uncalibrated lens reports no profile rather than a guess");
        }
    }
}

void testBlackLevels() {
    section("Black levels");

    using orion::raw::BayerImage;

    // RGGB: red at (0,0), green at (1,0) and (0,1), blue at (1,1).
    constexpr std::uint32_t kRggb = 0x94949494;

    // No pattern at all: global plus per-channel trim, nothing else.
    {
        unsigned cblack[10] = {1, 2, 3, 4, 0, 0, 0, 0, 0, 0};
        const auto b = BayerImage::blackLevels(100, cblack, kRggb);
        report(b[0] == 101 && b[1] == 102 && b[2] == 103 && b[3] == 104,
               "with no pattern each channel is the global black plus its own trim");
    }

    // A 2x2 pattern, row-major over the CFA cell.
    {
        unsigned cblack[10] = {0, 0, 0, 0, 2, 2, 512, 520, 530, 540};
        const auto b = BayerImage::blackLevels(0, cblack, kRggb);
        report(b[0] == 512, "the red cell takes the red pattern value",
               "got " + std::to_string(b[0]));
        report(b[2] == 540, "the blue cell takes the blue pattern value",
               "got " + std::to_string(b[2]));
        report(b[1] == 520, "green takes a green pattern value",
               "got " + std::to_string(b[1]));

        // The bug this replaces: every channel got (512+520+530+540)/4 = 525,
        // so red sat 13 counts high and blue 15 counts low.
        report(b[0] != b[2], "red and blue no longer collapse to the same number");
    }

    // A pattern larger than the CFA cell has no cell-by-cell answer, so it is
    // averaged — stated rather than silent.
    {
        unsigned cblack[42] = {0, 0, 0, 0, 6, 6};
        for (int i = 0; i < 36; ++i) cblack[6 + i] = 100u + unsigned(i);
        const auto b = BayerImage::blackLevels(0, cblack, kRggb);
        report(b[0] == 117 && b[1] == 117 && b[2] == 117,
               "a pattern bigger than the mosaic cell falls back to its mean",
               "got " + std::to_string(b[0]));
    }

    // BGGR, to prove the mapping follows the mosaic rather than the order the
    // values happen to be stored in.
    {
        constexpr std::uint32_t kBggr = 0x16161616;
        unsigned cblack[10] = {0, 0, 0, 0, 2, 2, 512, 520, 530, 540};
        const auto b = BayerImage::blackLevels(0, cblack, kBggr);
        report(b[2] == 512, "on a BGGR sensor the blue cell takes the first value",
               "got " + std::to_string(b[2]));
        report(b[0] == 540, "and red takes the last",
               "got " + std::to_string(b[0]));
    }
}

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

    // ── Color space and depth on the way out ───────────────────────────────
    //
    // Both of these fail silently. A file tagged with the wrong profile opens
    // shifted in one application and correct in another, and a resize that
    // quietly drops to eight bits undoes the 16-bit path for exactly the
    // exports — the smaller ones — where nobody thinks to check.
    using orion::util::ColorSpace;

    constexpr std::uint32_t kW = 64, kH = 48;
    std::vector<std::uint16_t> pixels(std::size_t(kW) * kH * 4);
    for (std::uint32_t y = 0; y < kH; ++y) {
        for (std::uint32_t x = 0; x < kW; ++x) {
            const std::size_t i = (std::size_t(y) * kW + x) * 4;
            // A saturated red, which is where two profiles disagree most.
            pixels[i + 0] = 65535;
            pixels[i + 1] = static_cast<std::uint16_t>(x * 400);
            pixels[i + 2] = 0;
            pixels[i + 3] = 65535;
        }
    }
    const std::size_t stride = std::size_t(kW) * 4 * sizeof(std::uint16_t);

    const std::string dir = "/tmp/";
    struct Case { ColorSpace space; const char* name; };
    const Case cases[] = {
        {ColorSpace::Srgb,      "sRGB"},
        {ColorSpace::DisplayP3, "Display P3"},
        {ColorSpace::AdobeRgb,  "Adobe RGB"},
    };

    // Read the written file back rather than inferring from its size. A PNG of
    // a smooth ramp compresses to almost nothing at either depth, so byte
    // counts cannot tell eight bits from sixteen — which is exactly how the
    // resize path lost its depth without any test noticing.
    double red[3] = {0, 0, 0};
    for (int i = 0; i < 3; ++i) {
        orion::util::ExportOptions o{};
        o.format = ImageFormat::Png;
        o.space = cases[i].space;

        const std::string path = dir + "orion-space-" + std::to_string(i) + ".png";
        try {
            orion::util::writeImage(path, pixels.data(), kW, kH, stride, o);
        } catch (const std::exception& e) {
            report(false, std::string("writes ") + cases[i].name, e.what());
            continue;
        }

        int bits = 0;
        double rgb[3] = {0, 0, 0};
        if (!readBack(path, bits, rgb)) {
            report(false, std::string("reads back ") + cases[i].name);
            continue;
        }

        report(bits == 16, std::string("a ") + cases[i].name + " export is 16-bit",
               "got " + std::to_string(bits));
        report(orion::util::encodedSize(pixels.data(), kW, kH, stride, o) > 0,
               std::string("encodes to ") + cases[i].name);

        red[i] = rgb[0];
    }

    // sRGB's full red is inside the gamut of both wider spaces, so expressing
    // it there needs *less* than full red — around 0.92 in P3. A number that
    // stayed at 1.0 would mean the file had been relabelled rather than
    // converted, which is the failure worth catching: it opens oversaturated
    // in every application that honours the profile.
    report(red[0] > 0.99, "sRGB keeps a saturated red at full",
           "got " + std::to_string(red[0]));
    report(red[1] < 0.97 && red[1] > 0.80,
           "the same red is inside Display P3, so it sits below full",
           "got " + std::to_string(red[1]));
    report(red[2] < 0.97 && red[2] > 0.70,
           "the same red is inside Adobe RGB, so it sits below full",
           "got " + std::to_string(red[2]));

    // ── Metadata policy ────────────────────────────────────────────────────
    //
    // Location is the one metadata field with consequences: a photo taken at
    // home carries the home coordinates, and no viewer says so. The sample
    // frames have no GPS — the body has no receiver — so the source is written
    // here with a GPS block, which also makes the test run anywhere.
    {
        using orion::util::Metadata;

        const std::string source = dir + "orion-meta-source.jpg";
        report(writeJpegWithGps(source, pixels.data(), kW, kH, stride),
               "a stand-in camera file with GPS is written");

        bool gps = false, exif = false;
        report(metadataBlocks(source, gps, exif) && gps,
               "the stand-in source carries GPS");

        struct MetaCase { Metadata policy; bool wantGps; bool wantExif; const char* name; };
        const MetaCase metaCases[] = {
            {Metadata::All,        true,  true,  "keep all"},
            {Metadata::NoLocation, false, true,  "strip location"},
            {Metadata::None,       false, false, "strip everything"},
        };
        for (const auto& m : metaCases) {
            orion::util::ExportOptions o{};
            o.format = ImageFormat::Jpeg;
            o.metadataFrom = source;
            o.metadata = m.policy;
            const std::string path = dir + "orion-meta-" + std::to_string(int(m.policy)) + ".jpg";
            try {
                orion::util::writeImage(path, pixels.data(), kW, kH, stride, o);
            } catch (const std::exception& e) {
                report(false, std::string("writes with ") + m.name, e.what());
                continue;
            }
            if (!metadataBlocks(path, gps, exif)) {
                report(false, std::string("reads back ") + m.name);
                continue;
            }
            report(gps == m.wantGps,
                   std::string(m.name) + (m.wantGps ? " keeps GPS" : " removes GPS"));
            report(exif == m.wantExif,
                   std::string(m.name) +
                       (m.wantExif ? " keeps the camera EXIF" : " removes the camera EXIF"));
        }

        // The default is the safe one. This is the assertion that matters: the
        // failure mode is silent, and it publishes someone's address.
        orion::util::ExportOptions fresh{};
        report(fresh.metadata == Metadata::NoLocation,
               "an export that says nothing about metadata strips location");
    }

    // A resize must not cost the depth. It used to: the resize context was
    // eight bits, so every export with a size limit quietly halved its
    // precision — and the smaller exports are the ones nobody re-checks.
    orion::util::ExportOptions resized{};
    resized.format = ImageFormat::Png;
    resized.maxDimension = 32;
    const std::string smallPath = dir + "orion-space-small.png";
    try {
        orion::util::writeImage(smallPath, pixels.data(), kW, kH, stride, resized);
        int bits = 0;
        double rgb[3] = {0, 0, 0};
        if (readBack(smallPath, bits, rgb)) {
            report(bits == 16, "a resized export keeps sixteen bits",
                   "got " + std::to_string(bits));
        } else {
            report(false, "a resized export reads back");
        }
    } catch (const std::exception& e) {
        report(false, "a resized export writes", e.what());
    }
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

/// Gray in, gray out.
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

    // A ramp of neutral grays spanning 12 stops, from deep shadow to well
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
    // The display kernel binds the creative LUT after the curve LUT. It is
    // never sampled here — lutSize stays zero — but the binding has to exist or
    // every texture after it shifts by one, which is silent and total.
    auto cubeStub = orion::gpu::Texture::create(*device, 2, 4,
                                                orion::gpu::PixelFormat::RGBA32Float);
    cb.dispatch(*kernel, {src.get(), lut.get(), cubeStub.get(), dst.get()},
                &p, sizeof p, kN, 1);
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
    report(worst <= 2, "neutral gray stays neutral through the display transform",
           worst <= 2 ? "" : detail);

    report(monotone, "brighter input never produces a darker output");

    // Middle gray must land near the middle of the display range. This is what
    // catches a double encode: applying a transfer function on top of AgX put
    // 0.18 at 189/255 instead of about 128.
    const float target = 0.18f;
    std::uint32_t nearest = 0;
    for (std::uint32_t i = 1; i < kN; ++i) {
        if (std::abs(levels[i] - target) < std::abs(levels[nearest] - target)) nearest = i;
    }
    const int mid = out[nearest * 4 + 1];
    std::snprintf(detail, sizeof detail, "scene %.3f -> %d/255", levels[nearest], mid);
    report(mid > 95 && mid < 165, "middle gray lands mid-range", detail);
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

    // The crop's top-left must be the source's center, within half a pixel of
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
/// preview turned the picture about the frame center and the committed render
/// turned it about the crop center. With an off-center crop the two disagree,
/// and the picture you got was not the picture the white box had shown.
void testStraightenPivot() {
    section("Straighten pivot (GPU)");

    constexpr std::uint32_t kW = 64, kH = 64;
    constexpr float kCanvas = 1.42f;
    constexpr float kAngle  = 8.0f * 3.14159265358979f / 180.0f;

    // An off-center crop — the case the two paths disagreed on.
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

    // Coordinates as color, so every output pixel names the source pixel it
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
    // The frame's center, which is what the app sends. An off-center crop is
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

    // And with no crop the pivot is the frame center, which must leave the
    // center pixel exactly where it started.
    orion::pipe::params::Geometry centered{};
    centered.inSize[0] = kW; centered.inSize[1] = kH;
    centered.outSize[0] = kW; centered.outSize[1] = kH;
    centered.straightenRad = kAngle;
    centered.cropSize[0] = 1.0f; centered.cropSize[1] = 1.0f;
    centered.pivot[0] = 0.5f; centered.pivot[1] = 0.5f;
    const auto whole = render(centered);

    const std::size_t mid = (std::size_t(kH / 2) * kW + kW / 2) * 4;
    checkNear(double(whole[mid + 0]), kW / 2.0, 0.6,
              "rotating about the center leaves the center column put");
    checkNear(double(whole[mid + 1]), kH / 2.0, 0.6,
              "rotating about the center leaves the center row put");
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
void testHueSatMapGpu() {
    section("Camera profile: HueSatMap (GPU)");

    using namespace orion::pipe;

    // The matrices, before any pixel touches them. A composed round trip that
    // is not the identity would show up as a global tint no amount of table
    // fitting could remove.
    {
        const auto m = huesat::multiply(huesat::rec2020ToProPhoto(),
                                        huesat::proPhotoToRec2020());
        double worst = 0.0;
        for (int r = 0; r < 3; ++r)
            for (int c = 0; c < 3; ++c)
                worst = std::max(worst, std::abs(double(m[r * 3 + c]) - (r == c ? 1.0 : 0.0)));
        checkNear(worst, 0.0, 1e-5, "Rec.2020 -> ProPhoto -> Rec.2020 is the identity");

        // Published matrices are rounded, so white lands on white to about a
        // part in ten thousand. Anything looser would be a transcription error.
        const auto toPro = huesat::rec2020ToProPhoto();
        for (int r = 0; r < 3; ++r) {
            const double sum = double(toPro[r * 3 + 0]) + toPro[r * 3 + 1] + toPro[r * 3 + 2];
            checkNear(sum, 1.0, 1e-3, "the working white maps to ProPhoto white");
        }
    }

    // The table's own structure, which the spec constrains.
    {
        const auto table = huesat::buildTable(huesat::blueSky());
        bool neutralClean = true, valueScaleOne = true;
        for (int h = 0; h < huesat::kHueDivisions; ++h) {
            const std::size_t i = std::size_t(h) * huesat::kSatDivisions * 4;
            if (table[i + 0] != 0.0f || table[i + 1] != 1.0f || table[i + 2] != 1.0f)
                neutralClean = false;
            for (int s = 0; s < huesat::kSatDivisions; ++s)
                if (table[i + std::size_t(s) * 4 + 2] != 1.0f) valueScaleOne = false;
        }
        report(neutralClean, "every zero-saturation entry is (0, 1, 1)");
        report(valueScaleOne, "no entry scales value — this stage moves chroma only");

        // Red is outside the window and must be untouched; blue at full
        // saturation must carry the whole fit.
        const auto entry = [&](int hueDeg, int sat) {
            const int h = (hueDeg * huesat::kHueDivisions / 360) % huesat::kHueDivisions;
            const std::size_t i = (std::size_t(h) * huesat::kSatDivisions + sat) * 4;
            return std::array<float, 3>{table[i], table[i + 1], table[i + 2]};
        };
        const auto red = entry(0, huesat::kSatDivisions - 1);
        checkNear(red[0], 0.0, 1e-6, "red is outside the corrected window");
        checkNear(red[1], 1.0, 1e-6, "red's saturation is untouched");
        const auto blue = entry(252, huesat::kSatDivisions - 1);
        checkNear(blue[0], huesat::blueSky().hueShiftDeg, 0.35,
                  "the blue centre carries the fitted rotation");
    }

    constexpr std::uint32_t kW = 64, kH = 4;

    std::unique_ptr<orion::gpu::Device> device;
    try {
        device = orion::gpu::Device::create();
    } catch (const std::exception& e) {
        report(false, "Metal device available", e.what());
        return;
    }

    auto library = orion::gpu::Library::createFromFile(
        *device, std::string(ORION_SHADER_DIR) + "/hueSatMap.metallib");
    auto kernel = orion::gpu::Kernel::create(*device, *library, "hueSatMap");

    auto src = orion::gpu::Texture::create(*device, kW, kH,
                                           orion::gpu::PixelFormat::RGBA16Float);
    auto dst = orion::gpu::Texture::create(*device, kW, kH,
                                           orion::gpu::PixelFormat::RGBA16Float);
    auto tex = orion::gpu::Texture::create(*device, huesat::kSatDivisions,
                                           huesat::kHueDivisions,
                                           orion::gpu::PixelFormat::RGBA32Float);

    params::HueSat hs{};
    {
        const auto toPro   = huesat::rec2020ToProPhoto();
        const auto fromPro = huesat::proPhotoToRec2020();
        for (int r = 0; r < 3; ++r)
            for (int c = 0; c < 3; ++c) {
                hs.toProPhoto[r][c]   = toPro[r * 3 + c];
                hs.fromProPhoto[r][c] = fromPro[r * 3 + c];
            }
        hs.size[0] = kW; hs.size[1] = kH;
        hs.hueDivisions = huesat::kHueDivisions;
        hs.satDivisions = huesat::kSatDivisions;
    }

    // Row 0 a neutral ramp, row 1 sky blue, row 2 foliage green, row 3 skin.
    // The three colours are ordinary linear Rec.2020 values; what matters is
    // that only one of them is in the corrected hue region.
    const double colours[4][3] = {
        {0.0, 0.0, 0.0},                 // filled per column below
        {0.09, 0.13, 0.30},              // saturated blue, the sky's shape
        {0.06, 0.11, 0.03},              // foliage
        {0.34, 0.22, 0.17},              // skin
    };

    std::vector<__fp16> input(std::size_t(kW) * kH * 4);
    for (std::uint32_t x = 0; x < kW; ++x) {
        const double level = 0.004 * std::pow(250.0, x / double(kW - 1));   // 0.004 .. 1
        for (std::uint32_t y = 0; y < kH; ++y) {
            const std::size_t i = (std::size_t(y) * kW + x) * 4;
            for (int c = 0; c < 3; ++c) {
                const double v = (y == 0) ? level : colours[y][c] * level * 3.0;
                input[i + c] = static_cast<__fp16>(v);
            }
            input[i + 3] = 1;
        }
    }
    src->upload(input.data(), std::size_t(kW) * 4 * sizeof(float) / 2);

    std::vector<__fp16> out(std::size_t(kW) * kH * 4);
    const auto run = [&](const std::vector<float>& table) {
        tex->upload(table.data(), huesat::kSatDivisions * 4 * sizeof(float));
        orion::gpu::CommandBuffer cb(*device);
        cb.dispatch(*kernel, {src.get(), tex.get(), dst.get()}, &hs, sizeof hs, kW, kH);
        cb.commitAndWait();
        dst->download(out.data(), std::size_t(kW) * 4 * sizeof(float) / 2, kW, kH);
    };
    const auto pixel = [&](const std::vector<__fp16>& o, std::uint32_t x, std::uint32_t y) {
        const std::size_t i = (std::size_t(y) * kW + x) * 4;
        return std::array<double, 3>{double(o[i]), double(o[i + 1]), double(o[i + 2])};
    };

    // 1. An identity table is a no-op — through both matrices and HSV.
    {
        huesat::Correction none{};
        none.satScale = 1.0f;
        run(huesat::buildTable(none));

        double worst = 0.0;
        for (std::uint32_t y = 0; y < kH; ++y)
            for (std::uint32_t x = 0; x < kW; ++x) {
                const auto a = pixel(input, x, y), b = pixel(out, x, y);
                for (int c = 0; c < 3; ++c)
                    worst = std::max(worst, std::abs(a[c] - b[c]) / std::max(1e-4, a[c]));
            }
        // Half float carries about three decimal digits, and the round trip is
        // four matrix multiplies plus HSV and back.
        checkNear(worst, 0.0, 6e-3, "an identity table leaves every pixel where it was");
    }

    // 2. The fitted table, on a neutral: still neutral.
    {
        run(huesat::buildTable(huesat::blueSky()));

        double worstTint = 0.0;
        for (std::uint32_t x = 1; x < kW; ++x) {
            const auto p = pixel(out, x, 0);
            const double mean = (p[0] + p[1] + p[2]) / 3.0;
            for (int c = 0; c < 3; ++c)
                worstTint = std::max(worstTint, std::abs(p[c] - mean) / std::max(1e-4, mean));
        }
        checkNear(worstTint, 0.0, 6e-3, "a grey ramp comes through grey at every level");

        // 3. Blue moves, and the other hues do not.
        const std::uint32_t mid = kW / 2;
        const auto blueIn  = pixel(input, mid, 1), blueOut  = pixel(out, mid, 1);
        const auto leafIn  = pixel(input, mid, 2), leafOut  = pixel(out, mid, 2);
        const auto skinIn  = pixel(input, mid, 3), skinOut  = pixel(out, mid, 3);

        const auto rb = [](const std::array<double, 3>& p) { return p[0] / p[2]; };
        report(rb(blueOut) < rb(blueIn) * 0.9,
               "the correction takes red out of a saturated blue",
               std::to_string(rb(blueIn)) + " -> " + std::to_string(rb(blueOut)));

        const auto same = [](const std::array<double, 3>& a, const std::array<double, 3>& b) {
            double worst = 0.0;
            for (int c = 0; c < 3; ++c)
                worst = std::max(worst, std::abs(a[c] - b[c]) / std::max(1e-4, a[c]));
            return worst;
        };
        checkNear(same(leafIn, leafOut), 0.0, 6e-3, "foliage is not in the corrected window");
        checkNear(same(skinIn, skinOut), 0.0, 6e-3, "skin is not in the corrected window");
    }
}

void testColorGradeGpu() {
    section("Colour grading (GPU)");

    constexpr std::uint32_t kW = 64, kH = 8;

    std::unique_ptr<orion::gpu::Device> device;
    try {
        device = orion::gpu::Device::create();
    } catch (const std::exception& e) {
        report(false, "Metal device available", e.what());
        return;
    }

    auto library = orion::gpu::Library::createFromFile(
        *device, std::string(ORION_SHADER_DIR) + "/colorGrade.metallib");
    auto kernel = orion::gpu::Kernel::create(*device, *library, "colorGrade");

    auto src = orion::gpu::Texture::create(*device, kW, kH,
                                           orion::gpu::PixelFormat::RGBA16Float);
    auto dst = orion::gpu::Texture::create(*device, kW, kH,
                                           orion::gpu::PixelFormat::RGBA16Float);

    // A neutral wedge across the zones the shader partitions on: scene-linear
    // luminance from deep shadow to well past the highlight knee at 1.0.
    std::vector<__fp16> input(std::size_t(kW) * kH * 4);
    std::vector<double> level(kW);
    for (std::uint32_t x = 0; x < kW; ++x) {
        level[x] = 0.002 * std::pow(2000.0, x / double(kW - 1));   // 0.002 .. 4
        for (std::uint32_t y = 0; y < kH; ++y) {
            const std::size_t i = (std::size_t(y) * kW + x) * 4;
            for (int c = 0; c < 3; ++c) input[i + c] = static_cast<__fp16>(level[x]);
            input[i + 3] = 1;
        }
    }
    src->upload(input.data(), std::size_t(kW) * 4 * sizeof(float) / 2);

    orion::pipe::params::Grade g{};
    g.size[0] = kW; g.size[1] = kH;

    std::vector<__fp16> out(std::size_t(kW) * kH * 4);
    const auto run = [&]() {
        orion::gpu::CommandBuffer cb(*device);
        cb.dispatch(*kernel, {src.get(), dst.get()}, &g, sizeof g, kW, kH);
        cb.commitAndWait();
        dst->download(out.data(), std::size_t(kW) * 4 * sizeof(float) / 2, kW, kH);
    };

    // Chroma *relative* to the pixel, because that is what the grade now
    // produces: the offset is scaled by luminance, so a wheel is a constant
    // chromaticity shift at every exposure rather than a constant additive one
    // whose authority fell as one over the level.
    const auto chromaAt = [&](const std::vector<__fp16>& o, std::uint32_t x) {
        const std::size_t i = (std::size_t(kH / 2) * kW + x) * 4;
        const double r = o[i + 0], gg = o[i + 1], b = o[i + 2];
        const double mean = (r + gg + b) / 3.0;
        if (mean < 1e-6) return 0.0;
        return (std::abs(r - mean) + std::abs(gg - mean) + std::abs(b - mean)) / 3.0 / mean;
    };

    // Every wheel centred is the identity, exactly. The node is disabled in
    // that state, but a disabled node passes its input through — so if this
    // were not an identity, enabling grading would visibly jump.
    run();
    double worstIdentity = 0.0;
    for (std::size_t i = 0; i < std::size_t(kW) * kH * 4; ++i) {
        worstIdentity = std::max(worstIdentity,
                                 std::abs(double(out[i]) - double(input[i])));
    }
    report(worstIdentity < 1e-3, "every wheel centred is the identity",
           "worst " + std::to_string(worstIdentity));

    // Columns at the zone centres, in EV relative to middle gray. The zones
    // are Gaussian bands on log2(Y/0.18) centred at -2.5 / 0 / +2.5, the same
    // partition-of-unity construction the tone controls use.
    const auto columnAtEv = [&](double ev) {
        const double want = 0.18 * std::pow(2.0, ev);
        std::uint32_t best = 0;
        double bestErr = 1e30;
        for (std::uint32_t x = 0; x < kW; ++x) {
            const double err = std::abs(std::log2(level[x] / want));
            if (err < bestErr) { bestErr = err; best = x; }
        }
        return best;
    };
    const std::uint32_t atShadow    = columnAtEv(-2.5);
    const std::uint32_t atMidtone   = columnAtEv(0.0);
    const std::uint32_t atHighlight = columnAtEv(2.5);

    // A shadow push has to tint the shadows and leave the highlights alone.
    // That is the whole claim a three-way grade makes, and the claim the linear
    // partition could not keep: at Y = 0.0/0.5/1.0 middle gray weighed 0.70
    // shadows, so the shadow wheel graded most of a normal photograph.
    orion::pipe::DevelopPipeline::gradeOffsets(-0.6f, -0.6f, g.shadow);
    run();

    report(chromaAt(out, atShadow) > 0.05, "the shadow wheel tints its own zone",
           "relative chroma " + std::to_string(chromaAt(out, atShadow)));
    report(chromaAt(out, atHighlight) < 0.1 * chromaAt(out, atShadow),
           "and leaves the highlight zone alone",
           "shadow " + std::to_string(chromaAt(out, atShadow)) + " highlight " +
           std::to_string(chromaAt(out, atHighlight)));

    // ── Scale invariance: the property the whole rewrite is about ─────────
    //
    // A wheel must be worth the same thing at every exposure. The offset used
    // to be an additive constant in unbounded scene-linear, so what it was
    // worth relative to the pixel fell as one over the level — which is why the
    // highlight wheel measured -0.0000 chroma on a lifted frame while the
    // shadow wheel was strong enough to clip channels to black.
    //
    // Same wheel, same zone weight, two levels three stops apart: the relative
    // tint has to match.
    for (float* zone : {g.shadow, g.midtone, g.highlight}) {
        zone[0] = zone[1] = zone[2] = 0.0f;
    }
    orion::pipe::DevelopPipeline::gradeOffsets(0.6f, 0.4f, g.shadow);
    orion::pipe::DevelopPipeline::gradeOffsets(0.6f, 0.4f, g.midtone);
    orion::pipe::DevelopPipeline::gradeOffsets(0.6f, 0.4f, g.highlight);
    run();
    // All three zones carry the same push, so the weights sum to one and every
    // level gets the identical correction. Any level dependence left is the
    // bug this replaced.
    const double lo = chromaAt(out, columnAtEv(-3.0));
    const double hi = chromaAt(out, columnAtEv(3.0));
    report(lo > 0.05 && std::abs(lo - hi) < 0.02 * lo + 1e-4,
           "a wheel is worth the same at every level",
           "-3 EV " + std::to_string(lo) + "  +3 EV " + std::to_string(hi));
    std::printf("  same wheel at -3 EV: %.4f relative chroma, at +3 EV: %.4f\n", lo, hi);

    // ── Zero-sum survives, including in deep shadow ──────────────────────
    //
    // A wheel is a colour control, not a brightness one, so the mean of the
    // three channels must not move. It used to, below about a fiftieth of
    // middle gray: the offset was a constant larger than the pixel, the
    // negative channels stuck at the shader's zero clamp, the sum stopped
    // cancelling and the wheel *brightened* what it was tinting — measured at
    // +29% on a 0.0096-linear patch. Scaling the offset by luminance makes that
    // unrepresentable rather than merely unlikely, so this is checked across
    // the whole wedge rather than at one convenient level.
    double worstDrift = 0.0;
    std::uint32_t worstAt = 0;
    for (std::uint32_t x = 0; x < kW; ++x) {
        const std::size_t i = (std::size_t(kH / 2) * kW + x) * 4;
        const double mean = (double(out[i]) + double(out[i + 1]) +
                             double(out[i + 2])) / 3.0;
        const double drift = std::abs(mean / level[x] - 1.0);
        if (drift > worstDrift) { worstDrift = drift; worstAt = x; }
    }
    report(worstDrift < 0.02,
           "and does not change brightness anywhere on the wedge",
           "worst " + std::to_string(100.0 * worstDrift) + "% at " +
           std::to_string(level[worstAt]) + " linear");

    // The slope is the component that is *meant* to change brightness, and it
    // has to be read where its own zone has the weight.
    for (float* zone : {g.shadow, g.midtone, g.highlight}) {
        zone[0] = zone[1] = zone[2] = 0.0f;
    }
    g.shadow[3] = 0.5f;
    run();
    const std::size_t si = (std::size_t(kH / 2) * kW + atShadow) * 4;
    const double lifted = (double(out[si]) + double(out[si + 1]) +
                           double(out[si + 2])) / 3.0;
    report(lifted > level[atShadow] * 1.2,
           "the zone's luminance control does lift it",
           std::to_string(level[atShadow]) + " -> " + std::to_string(lifted));

    // And leaves the zone at the other end of the wedge alone.
    const std::size_t hi_i = (std::size_t(kH / 2) * kW + atHighlight) * 4;
    const double untouched = (double(out[hi_i]) + double(out[hi_i + 1]) +
                              double(out[hi_i + 2])) / 3.0;
    report(untouched < level[atHighlight] * 1.03,
           "and leaves the far zone's brightness alone",
           std::to_string(level[atHighlight]) + " -> " + std::to_string(untouched));
    g.shadow[3] = 0.0f;
}

/// The tone bands, with the guided-filter chain switched off.
///
/// The guide chain is seven nodes feeding only the highlight and shadow bands,
/// so it is disabled when both sliders are zero — and a disabled node resolves
/// to the last live producer, which is the color matrix. `developLinear`
/// sampled the two guide bindings regardless, so it read linear RGB as log2
/// luminance and as filter coefficients.
///
/// The offsets were zero so nothing moved directly, and every visual check
/// passed. But the four band weights are normalized into a partition of unity,
/// and two of them came from that garbage — so they sat in the denominator and
/// diluted whites and blacks per pixel, by an amount that varied with the
/// pixel's own color. The bench printed `ok` throughout, because it asserted
/// that a blacks edit changed *something* rather than that it changed by the
/// right amount.
///
/// The invariant: a blacks-only edit must land identically whether the guide
/// chain is running or not.
void testToneBandsWithoutGuide() {
    section("Tone bands with the guide chain off (GPU)");

    constexpr std::uint32_t kW = 64, kH = 64;

    std::unique_ptr<orion::gpu::Device> device;
    try {
        device = orion::gpu::Device::create();
    } catch (const std::exception& e) {
        report(false, "Metal device available", e.what());
        return;
    }

    auto library = orion::gpu::Library::createFromFile(
        *device, std::string(ORION_SHADER_DIR) + "/developLinear.metallib");
    auto kernel = orion::gpu::Kernel::create(*device, *library, "developLinear");

    auto src = orion::gpu::Texture::create(*device, kW, kH,
                                           orion::gpu::PixelFormat::RGBA16Float);
    auto dst = orion::gpu::Texture::create(*device, kW, kH,
                                           orion::gpu::PixelFormat::RGBA16Float);
    // The real guide textures are RG32Float; the *resolved* ones, when the
    // chain is off, are the color matrix's RGBA16Float. Both are built here so
    // the test exercises the binding production actually makes.
    auto guideAb = orion::gpu::Texture::create(*device, kW, kH,
                                               orion::gpu::PixelFormat::RG32Float);
    auto maskStub = orion::gpu::Texture::create(*device, kW, kH,
                                                orion::gpu::PixelFormat::R16Float);
    auto guideRaw = orion::gpu::Texture::create(*device, kW, kH,
                                                orion::gpu::PixelFormat::RG32Float);

    // A luminance ramp spanning every band center, from below blacks (-5.5 EV)
    // to above whites (+5.5 EV). Slightly tinted, because the garbage `ab` is
    // the pixel's own red and green — a neutral frame would understate it.
    std::vector<__fp16> input(std::size_t(kW) * kH * 4);
    std::vector<float> lumaEv(std::size_t(kW) * kH);
    for (std::uint32_t y = 0; y < kH; ++y) {
        for (std::uint32_t x = 0; x < kW; ++x) {
            const std::size_t i = std::size_t(y) * kW + x;
            const double ev = -7.0 + 14.0 * (x / double(kW - 1));
            const double base = 0.18 * std::pow(2.0, ev);
            const double tint = 0.7 + 0.6 * (y / double(kH - 1));
            input[i * 4 + 0] = static_cast<__fp16>(base * tint);
            input[i * 4 + 1] = static_cast<__fp16>(base);
            input[i * 4 + 2] = static_cast<__fp16>(base / tint);
            input[i * 4 + 3] = 1;
            // Rec.2020 luma of what was written, which is what the shader's own
            // fallback computes.
            lumaEv[i] = static_cast<float>(
                0.2627 * double(input[i * 4 + 0]) +
                0.6780 * double(input[i * 4 + 1]) +
                0.0593 * double(input[i * 4 + 2]));
        }
    }
    src->upload(input.data(), std::size_t(kW) * 4 * sizeof(float) / 2);

    // A guide that is deliberately the identity: a = 1, b = 0, and raw = the
    // pixel's own log2 luminance. Then the guided estimate *is* the pixel, so a
    // correct disabled path has to agree with it exactly. Any other guide would
    // only prove the two paths differ, which is not the question.
    std::vector<float> ab(std::size_t(kW) * kH * 2);
    std::vector<float> raw(std::size_t(kW) * kH * 2);
    for (std::size_t i = 0; i < std::size_t(kW) * kH; ++i) {
        ab[i * 2 + 0] = 1.0f;
        ab[i * 2 + 1] = 0.0f;
        raw[i * 2 + 0] = std::log2(std::max(lumaEv[i], 1e-6f));
        raw[i * 2 + 1] = 0.0f;
    }
    guideAb->upload(ab.data(), std::size_t(kW) * 2 * sizeof(float));
    guideRaw->upload(raw.data(), std::size_t(kW) * 2 * sizeof(float));

    orion::pipe::params::LinearAdjust la{};
    la.size[0] = kW; la.size[1] = kH;
    la.guideSize[0] = kW; la.guideSize[1] = kH;

    std::vector<__fp16> out(std::size_t(kW) * kH * 4);
    // `live` picks which textures land in the guide slots: the real ones, or
    // the color matrix output that `Pipeline::resolve` substitutes when the
    // chain is disabled.
    const auto run = [&](bool live, std::vector<__fp16>& o) {
        orion::gpu::CommandBuffer cb(*device);
        // The kernel gained a mask input. It is never sampled here — maskActive
        // stays zero — but the binding has to exist or every texture after it
        // shifts by one, which is silent and total.
        cb.dispatch(*kernel,
                    {src.get(),
                     live ? guideAb.get() : src.get(),
                     live ? guideRaw.get() : src.get(),
                     maskStub.get(),
                     dst.get()},
                    &la, sizeof la, kW, kH);
        cb.commitAndWait();
        dst->download(o.data(), std::size_t(kW) * 4 * sizeof(float) / 2, kW, kH);
    };

    // Green carries the untinted luminance, so it is what the deltas are read
    // from. In EV, because that is the unit the bands are defined in.
    const auto greenEvDelta = [&](const std::vector<__fp16>& o, std::size_t i) {
        const double after = std::max(double(o[i * 4 + 1]), 1e-9);
        const double before = std::max(double(input[i * 4 + 1]), 1e-9);
        return std::log2(after / before);
    };

    // Nothing set is a pass-through down both paths, or the fallback is not a
    // fallback. This kernel had no GPU test at all before now.
    std::vector<__fp16> idle(std::size_t(kW) * kH * 4);
    la.guideEnabled = 0.0f;
    run(false, idle);
    double worstIdle = 0.0;
    for (std::size_t i = 0; i < std::size_t(kW) * kH; ++i) {
        worstIdle = std::max(worstIdle, std::abs(greenEvDelta(idle, i)));
    }
    report(worstIdle < 0.01, "no adjustment is a pass-through with the guide off",
           "worst " + std::to_string(worstIdle) + " EV");

    // ── The invariant ───────────────────────────────────────────────────
    la.blacks = -1.0f;

    std::vector<__fp16> off(std::size_t(kW) * kH * 4);
    la.guideEnabled = 0.0f;
    run(false, off);                 // guide disabled, bindings resolved past it

    std::vector<__fp16> on(std::size_t(kW) * kH * 4);
    la.guideEnabled = 1.0f;
    run(true, on);                   // guide live, and equal to the pixel

    double worstGap = 0.0, worstAt = 0.0;
    for (std::size_t i = 0; i < std::size_t(kW) * kH; ++i) {
        const double gap = std::abs(greenEvDelta(off, i) - greenEvDelta(on, i));
        if (gap > worstGap) { worstGap = gap; worstAt = greenEvDelta(on, i); }
    }
    report(worstGap < 0.02,
           "a blacks edit lands the same whether the guide chain runs or not",
           "worst " + std::to_string(worstGap) + " EV, against a delta of " +
           std::to_string(worstAt));

    // ── And the test has to be able to fail ─────────────────────────────
    //
    // The shipped behavior: the flag set, but the bindings still resolved back
    // to the color matrix. If this passed, the check above would be measuring
    // nothing.
    std::vector<__fp16> shipped(std::size_t(kW) * kH * 4);
    la.guideEnabled = 1.0f;
    run(false, shipped);

    // The darkest patches are where blacks does its work, so that is where the
    // dilution shows. Compare the strongest correct delta against what the
    // garbage weights allowed.
    double bestCorrect = 0.0, atSameSpot = 0.0;
    for (std::size_t i = 0; i < std::size_t(kW) * kH; ++i) {
        const double d = std::abs(greenEvDelta(on, i));
        if (d > bestCorrect) { bestCorrect = d; atSameSpot = std::abs(greenEvDelta(shipped, i)); }
    }
    report(bestCorrect > 1.0, "blacks at -1 is worth more than a stop somewhere",
           std::to_string(bestCorrect) + " EV");
    report(atSameSpot < 0.75 * bestCorrect,
           "the old binding measurably diluted it, so this check can fail",
           "correct " + std::to_string(bestCorrect) + " EV, shipped " +
           std::to_string(atSameSpot) + " EV");
    std::printf("  blacks -1 at its strongest: %.3f EV correct, %.3f EV as shipped\n",
                bestCorrect, atSameSpot);

    // Whites, the same way. It reads the pixel too, so it was diluted too.
    la.blacks = 0.0f;
    la.whites = 1.0f;
    la.guideEnabled = 0.0f;
    run(false, off);
    la.guideEnabled = 1.0f;
    run(true, on);
    double worstWhites = 0.0;
    for (std::size_t i = 0; i < std::size_t(kW) * kH; ++i) {
        worstWhites = std::max(worstWhites,
                               std::abs(greenEvDelta(off, i) - greenEvDelta(on, i)));
    }
    report(worstWhites < 0.02, "and so does a whites edit",
           "worst " + std::to_string(worstWhites) + " EV");
}

/// The zoom that keeps a lens correction inside the frame.
///
/// Host maths, so it is checked exhaustively rather than at the four points a
/// GPU test can afford to read.
void testLensAutoScale() {
    section("Lens autoscale");

    using orion::pipe::lens::autoScale;

    // A real frame, and the shipped coefficient range: the sliders run -1..1
    // into k₁ = ±0.35 and ca = ±0.003.
    constexpr std::uint32_t kW = 6024, kH = 4024;

    checkNear(autoScale(kW, kH, 0.5f, 0.5f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f), 1.0, 0.0,
              "no correction is exactly no zoom");
    checkNear(autoScale(kW, kH, 0.5f, 0.5f, 0.0f, 0.35f, 0.0f, 0.0f, 0.0f), 1.0, 0.0,
              "pincushion pulls inward, so it needs no zoom");
    checkNear(autoScale(kW, kH, 0.5f, 0.5f, 0.0f, 0.0f, 0.0f, 0.003f, 0.003f), 1.0, 0.0,
              "a fringe correction that shrinks red and blue needs no zoom");

    const float barrel = autoScale(kW, kH, 0.5f, 0.5f, 0.0f, -0.35f, 0.0f, 0.0f, 0.0f);
    report(barrel < 1.0f, "full barrel correction needs a zoom",
           "scale " + std::to_string(barrel));

    // The invariant, recomputed here rather than trusted: at the returned
    // scale, no pixel on the frame's perimeter fetches outside it. Written out
    // independently of the implementation, because a bug shared between the
    // function and its test is not caught by either.
    const auto worstOverreach = [&](float k1, float caR, float caB, float s) {
        const double w = kW, h = kH;
        const double cx = 0.5 * w, cy = 0.5 * h;
        const double rNorm = 0.5 * std::sqrt(w * w + h * h);
        double worst = 0.0;
        const auto probeAt = [&](double dx, double dy) {
            const double px = dx * s, py = dy * s;
            const double ru = std::sqrt(px * px + py * py) / rNorm;
            const double r2 = ru * ru;
            const double m = ((1.0 - k1) + k1 * r2) *
                std::max({1.0, 1.0 - caR * r2, 1.0 - caB * r2});
            const double fx = cx + px * m - 0.5, fy = cy + py * m - 0.5;
            worst = std::max({worst, -fx, fx - (w - 1.0), -fy, fy - (h - 1.0)});
        };
        // Denser than the implementation walks, so a coarse perimeter would
        // show up as an overreach here.
        for (int i = 0; i <= 977; ++i) {
            const double t = double(i) / 977.0;
            const double x = 0.5 + t * (w - 1.0);
            const double y = 0.5 + t * (h - 1.0);
            probeAt(x - cx, 0.5 - cy);
            probeAt(x - cx, h - 0.5 - cy);
            probeAt(0.5 - cx, y - cy);
            probeAt(w - 0.5 - cx, y - cy);
        }
        return worst;
    };

    report(worstOverreach(-0.35f, 0.0f, 0.0f, barrel) < 0.01,
           "at the returned scale nothing fetches outside the frame",
           "overreach " + std::to_string(worstOverreach(-0.35f, 0.0f, 0.0f, barrel)));

    // Uncorrected must land exactly on the last texel, not a hair past it —
    // that half texel is the difference between "no zoom" and 0.9999.
    report(worstOverreach(0.0f, 0.0f, 0.0f, 1.0f) < 1e-6,
           "an uncorrected frame reaches exactly its own edge");

    // Every step of the slider, both signs, with fringe at its extremes.
    // A sign this function got wrong anywhere would leave a band on screen.
    int violations = 0, needless = 0;
    for (int i = -100; i <= 100; ++i) {
        const float k1 = i * 0.0035f;
        for (const float ca : {-0.003f, 0.0f, 0.003f}) {
            const float s = autoScale(kW, kH, 0.5f, 0.5f, 0.0f, k1, 0.0f, ca, -ca);
            if (worstOverreach(k1, ca, -ca, s) > 0.01) ++violations;
            // And not slack: a scale that could be larger is frame thrown away.
            if (s < 0.9999f && worstOverreach(k1, ca, -ca, s * 1.002f) < 0.01) ++needless;
        }
    }
    report(violations == 0, "no coefficient in range leaves the frame",
           std::to_string(violations) + " of 603");
    report(needless == 0, "and none zooms further than it has to",
           std::to_string(needless) + " of 603");

    // An off-center optical axis reaches further on one side. Real lens
    // profiles carry one, so the maths must not assume the middle.
    const float offset = autoScale(kW, kH, 0.44f, 0.53f, 0.0f, -0.35f, 0.0f, 0.0f, 0.0f);
    report(offset < barrel, "an off-center axis needs more zoom, not the same",
           "centered " + std::to_string(barrel) + " off-center " +
           std::to_string(offset));

    // A portrait frame is the same maths with the axes swapped; it has been
    // the thing that was wrong before.
    const float portrait = autoScale(kH, kW, 0.5f, 0.5f, 0.0f, -0.35f, 0.0f, 0.0f, 0.0f);
    checkNear(portrait, barrel, 1e-4, "portrait and landscape zoom the same");
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
    // The display kernel binds the creative LUT after the curve LUT. It is
    // never sampled here — lutSize stays zero — but the binding has to exist or
    // every texture after it shifts by one, which is silent and total.
    auto cubeStub = orion::gpu::Texture::create(*device, 2, 4,
                                                orion::gpu::PixelFormat::RGBA32Float);
    cb.dispatch(*kernel, {src.get(), lut.get(), cubeStub.get(), dst.get()},
                &dp, sizeof dp, kN, 1);
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

/// A bright highlight on a dark, color-cast background.
///
/// The case that produced a purple halo around every light in a night shot.
/// The fit that recovers a clipped channel has to be anchored on pixels near
/// the saturation boundary — Masood et al.'s Ω. Anchored on the general
/// neighbourhood instead, which around a small light at night is nearly all
/// dark background, it learns the background's channel balance and then
/// extrapolates it out to highlight brightness, which is a long way past any
/// data it saw.
void testHighlightHaloGpu() {
    section("Highlight halo (GPU)");

    constexpr std::uint32_t kW = 192, kH = 192;
    constexpr float kClipR = 2.0f, kClipG = 1.0f, kClipB = 1.5f;
    constexpr float kGamma = 0.97f;

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

    auto src = orion::gpu::Texture::create(*device, kW, kH,
                                           orion::gpu::PixelFormat::RGBA16Float);
    auto dst = orion::gpu::Texture::create(*device, kW, kH,
                                           orion::gpu::PixelFormat::RGBA16Float);

    // A neutral lamp with a smooth falloff, on a dark background that is
    // strongly warm — a sodium-lit street, which is what teaches the fit a
    // channel balance that has nothing to do with the lamp.
    std::vector<__fp16> input(std::size_t(kW) * kH * 4);
    const double cx = kW / 2.0, cy = kH / 2.0;
    for (std::uint32_t y = 0; y < kH; ++y) {
        for (std::uint32_t x = 0; x < kW; ++x) {
            const std::size_t i = (std::size_t(y) * kW + x) * 4;
            const double d = std::hypot(double(x) - cx, double(y) - cy);

            // A warm lamp, which is what a real one is: red reaches its stop
            // before green, and green before blue, so there is a ring around
            // the white core where only some channels have clipped. That ring
            // is where the halo appeared.
            const double lamp = std::exp(-(d * d) / (2.0 * 16.0 * 16.0));
            const double back = 0.04;

            const double r = 3.2 * lamp * kClipR + back * kClipR * 1.9;
            const double g = 2.4 * lamp * kClipG + back * kClipG * 1.0;
            const double b = 1.7 * lamp * kClipB + back * kClipB * 0.30;

            input[i + 0] = static_cast<__fp16>(std::min(r, double(kClipR)));
            input[i + 1] = static_cast<__fp16>(std::min(g, double(kClipG)));
            input[i + 2] = static_cast<__fp16>(std::min(b, double(kClipB)));
            input[i + 3] = 1;
        }
    }
    src->upload(input.data(), std::size_t(kW) * 4 * sizeof(__fp16));

    orion::pipe::params::Highlights hl{};
    hl.size[0] = kW; hl.size[1] = kH;
    hl.clipR = kClipR; hl.clipG = kClipG; hl.clipB = kClipB;
    hl.gamma = kGamma;
    hl.strength = 1.0f;

    orion::gpu::CommandBuffer cb(*device);
    cb.dispatch(*kernel, {src.get(), dst.get()}, &hl, sizeof hl, kW, kH);
    cb.commitAndWait();

    std::vector<__fp16> out(std::size_t(kW) * kH * 4);
    dst->download(out.data(), std::size_t(kW) * 4 * sizeof(__fp16), kW, kH);

    // Magenta is red and blue running ahead of green, measured against each
    // channel's own clipping level so the white balance is divided back out.
    const auto magentaShift = [&](const std::vector<__fp16>& img, std::size_t i) {
        const double r = double(img[i * 4 + 0]) / kClipR;
        const double g = double(img[i * 4 + 1]) / kClipG;
        const double b = double(img[i * 4 + 2]) / kClipB;
        return (r + b) * 0.5 - g;
    };

    double worstBefore = 0.0, worstAfter = 0.0;
    for (std::uint32_t y = 0; y < kH; ++y) {
        for (std::uint32_t x = 0; x < kW; ++x) {
            const std::size_t i = std::size_t(y) * kW + x;
            worstBefore = std::max(worstBefore, magentaShift(input, i));
            worstAfter = std::max(worstAfter, magentaShift(out, i));
        }
    }

    // The lamp is neutral, so nothing anywhere should come back appreciably
    // more magenta than it went in.
    std::printf("  magenta shift: before %.4f, after %.4f\n",
                worstBefore, worstAfter);
    report(worstAfter <= worstBefore + 0.01,
           "recovery does not tint a highlight magenta",
           "before " + std::to_string(worstBefore) + ", after "
               + std::to_string(worstAfter));
}

/// Local Laplacian clarity, against Paris et al.'s own algorithm.
///
/// The GPU runs Aubry et al.'s approximation: eight remapped pyramids and a
/// linear interpolation between the two that bracket each coefficient. The
/// reference in pipe/LocalLaplacian.h runs Algorithm 1 literally — one full
/// pyramid per output coefficient, no discretization at all. The difference
/// between them *is* the approximation error, and the paper states its own
/// accuracy as "above 30 dB", so that is the gate.
///
/// This is the check that says the shader implements the published filter
/// rather than a different filter that happens to look sharper. Nothing else
/// here can tell those apart.
void testLocalLaplacianGpu() {
    section("Local Laplacian clarity (GPU)");

    namespace llf = orion::pipe::llf;
    using orion::gpu::PixelFormat;

    constexpr int kW = 48, kH = 32;
    constexpr int kLevels = llf::kPyramidLevels;
    constexpr int kStacks = (llf::kGammaLevels + 3) / 4;

    std::unique_ptr<orion::gpu::Device> device;
    try {
        device = orion::gpu::Device::create();
    } catch (const std::exception& e) {
        report(false, "Metal device available", e.what());
        return;
    }

    const auto kernelNamed = [&](const char* entry) {
        auto lib = orion::gpu::Library::createFromFile(
            *device, std::string(ORION_SHADER_DIR) + "/" + entry + ".metallib");
        auto k = orion::gpu::Kernel::create(*device, *lib, entry);
        // The library has to outlive the kernel; hand both back.
        return std::pair{std::move(lib), std::move(k)};
    };

    auto kDown     = kernelNamed("llfDown");
    auto kRemap    = kernelNamed("llfRemapDown");
    auto kDown4    = kernelNamed("llfDownPacked");
    auto kColl     = kernelNamed("llfCollapse");
    auto kColl0    = kernelNamed("llfCollapse0");

    // Level sizes, exactly as DevelopPipeline computes them.
    int lw[kLevels], lh[kLevels];
    for (int l = 0; l < kLevels; ++l) {
        lw[l] = (l == 0) ? kW : std::max(1, (lw[l - 1] + 1) / 2);
        lh[l] = (l == 0) ? kH : std::max(1, (lh[l - 1] + 1) / 2);
    }

    // A frame with all three things the filter has to tell apart: a step edge
    // far larger than sigmaR, a fine ripple far smaller than it, and a smooth
    // ramp that is neither.
    llf::Plane input{kW, kH, std::vector<float>(std::size_t(kW) * kH)};
    for (int y = 0; y < kH; ++y) {
        for (int x = 0; x < kW; ++x) {
            const double ramp   = 0.25 + 0.30 * (double(x) / (kW - 1));
            const double step   = (x >= kW / 2) ? 0.30 : 0.0;
            const double ripple = 0.012 * std::sin(x * 1.7) * std::cos(y * 2.3);
            input.v[std::size_t(y) * kW + x] =
                float(std::clamp(ramp + step + ripple, 0.0, 1.0));
        }
    }

    auto luma = orion::gpu::Texture::create(*device, kW, kH, PixelFormat::R16Float);
    std::vector<__fp16> half(std::size_t(kW) * kH);
    for (std::size_t i = 0; i < half.size(); ++i) half[i] = __fp16(input.v[i]);
    luma->upload(half.data(), std::size_t(kW) * sizeof(__fp16));

    // Every intermediate the chain needs.
    std::vector<std::unique_ptr<orion::gpu::Texture>> gauss(kLevels), out(kLevels);
    std::vector<std::array<std::unique_ptr<orion::gpu::Texture>, kStacks>> pack(kLevels);
    for (int l = 1; l < kLevels; ++l) {
        gauss[l] = orion::gpu::Texture::create(*device, lw[l], lh[l], PixelFormat::R16Float);
        for (int st = 0; st < kStacks; ++st) {
            pack[l][st] = orion::gpu::Texture::create(*device, lw[l], lh[l],
                                                      PixelFormat::RGBA16Float);
        }
    }
    for (int l = 0; l < kLevels - 1; ++l) {
        out[l] = orion::gpu::Texture::create(*device, lw[l], lh[l], PixelFormat::R16Float);
    }

    std::vector<float> gpu(std::size_t(kW) * kH);

    const auto runChain = [&](float alpha) {
        orion::gpu::CommandBuffer cb(*device);

        for (int l = 1; l < kLevels; ++l) {
            orion::pipe::params::LlfDown d{{std::uint32_t(lw[l]), std::uint32_t(lh[l])},
                                           {std::uint32_t(lw[l - 1]), std::uint32_t(lh[l - 1])}};
            cb.dispatch(*kDown.second,
                        {l == 1 ? luma.get() : gauss[l - 1].get(), gauss[l].get()},
                        &d, sizeof d, lw[l], lh[l]);
        }

        for (int l = 1; l < kLevels; ++l) {
            for (int st = 0; st < kStacks; ++st) {
                if (l == 1) {
                    orion::pipe::params::LlfRemap r{};
                    r.outSize[0] = std::uint32_t(lw[1]); r.outSize[1] = std::uint32_t(lh[1]);
                    r.inSize[0]  = kW;                   r.inSize[1]  = kH;
                    r.gamma0    = float(st * 4) * llf::kGammaStep;
                    r.gammaStep = llf::kGammaStep;
                    r.sigmaR    = llf::kSigmaR;
                    r.alpha     = alpha;
                    r.noiseLo   = llf::kNoiseLo;
                    r.noiseHi   = llf::kNoiseHi;
                    cb.dispatch(*kRemap.second, {luma.get(), pack[1][st].get()},
                                &r, sizeof r, lw[1], lh[1]);
                } else {
                    orion::pipe::params::LlfDown d{
                        {std::uint32_t(lw[l]), std::uint32_t(lh[l])},
                        {std::uint32_t(lw[l - 1]), std::uint32_t(lh[l - 1])}};
                    cb.dispatch(*kDown4.second,
                                {pack[l - 1][st].get(), pack[l][st].get()},
                                &d, sizeof d, lw[l], lh[l]);
                }
            }
        }

        for (int l = kLevels - 2; l >= 1; --l) {
            orion::pipe::params::LlfCollapse c{};
            c.size[0] = std::uint32_t(lw[l]);           c.size[1] = std::uint32_t(lh[l]);
            c.coarseSize[0] = std::uint32_t(lw[l + 1]); c.coarseSize[1] = std::uint32_t(lh[l + 1]);
            c.gammaStep  = llf::kGammaStep;
            c.gammaCount = llf::kGammaLevels;
            // The coarsest level is the residual: the input's own pyramid.
            const orion::gpu::Texture* coarseOut =
                (l + 1 == kLevels - 1) ? gauss[l + 1].get() : out[l + 1].get();
            cb.dispatch(*kColl.second,
                        {gauss[l].get(),
                         pack[l][0].get(), pack[l][1].get(),
                         pack[l][2].get(), pack[l][3].get(),
                         pack[l + 1][0].get(), pack[l + 1][1].get(),
                         pack[l + 1][2].get(), pack[l + 1][3].get(),
                         coarseOut, out[l].get()},
                        &c, sizeof c, lw[l], lh[l]);
        }

        orion::pipe::params::LlfCollapse0 c0{};
        c0.size[0] = kW;                       c0.size[1] = kH;
        c0.coarseSize[0] = std::uint32_t(lw[1]); c0.coarseSize[1] = std::uint32_t(lh[1]);
        c0.gammaStep  = llf::kGammaStep;
        c0.gammaCount = llf::kGammaLevels;
        c0.sigmaR     = llf::kSigmaR;
        c0.alpha      = alpha;
        c0.noiseLo    = llf::kNoiseLo;
        c0.noiseHi    = llf::kNoiseHi;
        cb.dispatch(*kColl0.second,
                    {luma.get(),
                     pack[1][0].get(), pack[1][1].get(),
                     pack[1][2].get(), pack[1][3].get(),
                     (kLevels - 1 == 1) ? gauss[1].get() : out[1].get(),
                     out[0].get()},
                    &c0, sizeof c0, kW, kH);

        cb.commitAndWait();

        std::vector<__fp16> got(std::size_t(kW) * kH);
        out[0]->download(got.data(), std::size_t(kW) * sizeof(__fp16), kW, kH);
        for (std::size_t i = 0; i < got.size(); ++i) gpu[i] = float(got[i]);
    };

    const auto psnrAgainst = [&](const std::vector<float>& a,
                                 const std::vector<float>& b) {
        double mse = 0.0;
        for (std::size_t i = 0; i < a.size(); ++i) {
            const double d = double(a[i]) - double(b[i]);
            mse += d * d;
        }
        mse /= double(a.size());
        // Signal peak is 1: the plane is normalized log luminance.
        return (mse <= 0.0) ? 99.0 : 10.0 * std::log10(1.0 / mse);
    };

    // ── alpha = 1 is the identity, and proves the pyramid round-trips ──────
    //
    // With fd(D) = D the remapping is exactly r(i) = i, so the whole chain
    // reduces to "analyse into a Laplacian pyramid, collapse it again". That
    // has to return the input to the precision of the storage — and if the
    // downsample and the expand disagree even slightly, this is where it shows.
    // Every other check in this function would still pass with a subtly wrong
    // expand operator, because both branches would share the mistake.
    runChain(1.0f);
    double worst = 0.0;
    for (std::size_t i = 0; i < gpu.size(); ++i) {
        worst = std::max(worst, std::abs(double(gpu[i]) - double(input.v[i])));
    }
    report(worst < 2e-3, "alpha = 1 collapses back to the input",
           "worst " + std::to_string(worst));

    // ── The approximation, against the published algorithm ────────────────
    for (const float clarity : {0.5f, 1.0f, -1.0f}) {
        const float alpha = llf::alphaForClarity(clarity);
        runChain(alpha);
        const llf::Plane want = llf::reference(input, kLevels, llf::kSigmaR, alpha,
                                               llf::kNoiseLo, llf::kNoiseHi);

        // First: is the shader running the algorithm it claims to? Same
        // discretization, same everything, on the CPU. A gap here is a bug in
        // a kernel and nothing to do with the approximation.
        const llf::Plane same = llf::referenceFast(input, kLevels, llf::kGammaLevels,
                                                   llf::kSigmaR, alpha,
                                                   llf::kNoiseLo, llf::kNoiseHi);
        double worstShader = 0.0;
        for (std::size_t i = 0; i < gpu.size(); ++i) {
            worstShader = std::max(worstShader,
                                   std::abs(double(gpu[i]) - double(same.v[i])));
        }
        report(worstShader < 5e-3,
               "clarity " + std::to_string(clarity) +
                   ": the shaders run Aubry et al.'s algorithm",
               "worst " + std::to_string(worstShader));

        // Second: how much does the discretization itself cost, and does it
        // converge? If it does not, the sampling argument is not what is
        // limiting the accuracy and more gamma levels would be wasted money.
        for (const int n : {8, 16, 32}) {
            const llf::Plane approx = llf::referenceFast(input, kLevels, n, llf::kSigmaR,
                                                         alpha, llf::kNoiseLo, llf::kNoiseHi);
            double sum = 0.0, worstN = 0.0;
            for (std::size_t i = 0; i < approx.v.size(); ++i) {
                const double d = std::abs(double(approx.v[i]) - double(want.v[i]));
                sum += d; worstN = std::max(worstN, d);
            }
            std::printf("      N = %2d  mean %.4f EV  max %.4f EV\n", n,
                        sum / double(approx.v.size()) * llf::kWindowEv,
                        worstN * llf::kWindowEv);
        }
        const double db = psnrAgainst(gpu, want.v);
        // dB alone is misleading here: the plane is normalized over a twelve
        // stop window, so a respectable PSNR can still be a visible error in
        // EV. Print the distribution as well — if the error is a handful of
        // pixels at the step edge that is a different fact about the filter
        // than a drift across the whole frame.
        std::vector<double> err(gpu.size());
        for (std::size_t i = 0; i < gpu.size(); ++i) {
            err[i] = std::abs(double(gpu[i]) - double(want.v[i])) * llf::kWindowEv;
        }
        std::sort(err.begin(), err.end());
        std::printf("  clarity %+.2f  alpha %.3f  %.2f dB   median %.4f EV  "
                    "p99 %.4f EV  max %.4f EV\n",
                    clarity, alpha, db, err[err.size() / 2],
                    err[err.size() * 99 / 100], err.back());
        report(db > 30.0,
               "clarity " + std::to_string(clarity) + ": matches Paris et al. "
               "Algorithm 1 above 30 dB",
               std::to_string(db) + " dB");
    }

    // ── A step edge is an edge, and no halo of our own making ─────────────
    //
    // The step here is 0.30 and sigmaR is 0.143, so Eq. 2 with fe the identity
    // leaves it alone by construction. A clarity slider that moves it is a
    // tone control wearing the wrong name.
    //
    // Overshoot is measured against the *exact* algorithm rather than against
    // the input's range, and the difference matters. Increasing local contrast
    // legitimately pushes an edge profile past the values that were there —
    // that is what it is for, and the paper's own filter does it. What the
    // paper claims is that it does not ring, so the question worth asking is
    // whether the approximation overshoots more than the algorithm it
    // approximates. That is a question about this code; the other is a
    // question about the 2011 paper.
    const float strong = llf::alphaForClarity(1.0f);
    runChain(strong);
    const llf::Plane exact = llf::reference(input, kLevels, llf::kSigmaR, strong,
                                            llf::kNoiseLo, llf::kNoiseHi);

    double worstEdge = 0.0;
    for (int y = 0; y < kH; ++y) {
        for (int x = kW / 2 - 4; x < kW / 2 + 4; ++x) {
            const std::size_t i = std::size_t(y) * kW + x;
            worstEdge = std::max(worstEdge, std::abs(double(gpu[i]) - double(input.v[i])));
        }
    }
    report(worstEdge < 0.06, "a step far larger than sigmaR survives clarity",
           "worst " + std::to_string(worstEdge));

    const auto range = [](const std::vector<float>& v) {
        return std::pair{double(*std::min_element(v.begin(), v.end())),
                         double(*std::max_element(v.begin(), v.end()))};
    };
    const auto [inLo, inHi]       = range(input.v);
    const auto [exactLo, exactHi] = range(exact.v);
    const auto [gpuLo, gpuHi]     = range(gpu);

    std::printf("  range: input [%.3f %.3f]  exact [%.3f %.3f]  gpu [%.3f %.3f]\n",
                inLo, inHi, exactLo, exactHi, gpuLo, gpuHi);

    report(gpuHi <= exactHi + 0.02 && gpuLo >= exactLo - 0.02,
           "the approximation adds no overshoot the exact filter does not",
           "gpu [" + std::to_string(gpuLo) + " " + std::to_string(gpuHi) +
               "] against exact [" + std::to_string(exactLo) + " " +
               std::to_string(exactHi) + "]");

    // ── The noise term is doing something ─────────────────────────────────
    //
    // Paris et al. section 5.2 blends fd back to the identity below 1% of the
    // range. Without it the lowest-amplitude signal in the frame gets the
    // largest gain of anything in the picture, which on a photograph is the
    // noise. Compare what the ripple — amplitude 0.012, right in the band the
    // term protects — is worth with the term and with it disabled.
    const float alpha = llf::alphaForClarity(1.0f);
    const float g = 0.5f;
    const float withTerm    = llf::remap(g + 0.004f, g, llf::kSigmaR, alpha,
                                         llf::kNoiseLo, llf::kNoiseHi) - g;
    const float withoutTerm = llf::remap(g + 0.004f, g, llf::kSigmaR, alpha, 0.0f, 0.0f) - g;
    report(withTerm < withoutTerm * 0.35f,
           "the noise term declines to amplify a 0.4% ripple",
           std::to_string(withTerm) + " against " + std::to_string(withoutTerm));

    // Above the upper threshold it must get out of the way entirely.
    const float big = llf::remap(g + 0.10f, g, llf::kSigmaR, alpha,
                                 llf::kNoiseLo, llf::kNoiseHi) - g;
    const float bigRaw = llf::remap(g + 0.10f, g, llf::kSigmaR, alpha, 0.0f, 0.0f) - g;
    report(std::abs(big - bigRaw) < 1e-6f,
           "and leaves real detail to the published power curve",
           std::to_string(big) + " against " + std::to_string(bigRaw));

    // alpha = 1 is the identity point-wise, which is what makes clarity = 0
    // safe to leave switched on if it ever is.
    double worstRemap = 0.0;
    for (int i = 0; i <= 100; ++i) {
        const float v = float(i) / 100.0f;
        worstRemap = std::max(worstRemap,
                              std::abs(double(llf::remap(v, 0.5f, llf::kSigmaR, 1.0f,
                                                         llf::kNoiseLo, llf::kNoiseHi)) - v));
    }
    report(worstRemap < 1e-6, "alpha = 1 is the identity remapping",
           "worst " + std::to_string(worstRemap));
}

/// Dehaze — the three claims that are not obvious by reading.
void testDehazeGpu() {
    section("Dehaze, dark channel prior");

    namespace dh = orion::pipe::dehaze;
    using orion::gpu::PixelFormat;

    // ── The atmospheric light picks haze, not a specular ──────────────────
    //
    // CVPR section 4.4 is two stages: the most haze-opaque pixels by dark
    // channel *first*, then the brightest among those. The paper is explicit
    // that the answer "may not be brightest ones in the whole input image", and
    // this is the case that separates a correct implementation from the obvious
    // wrong one — a specular highlight is the brightest pixel in most frames,
    // and picking it hands the whole recovery a wrong constant.
    {
        std::vector<dh::Candidate> c;
        for (int i = 0; i < 1000; ++i) {
            c.push_back({0.02f, 0.2f, 0.2f, 0.2f});          // ordinary scene
        }
        c.push_back({0.60f, 0.75f, 0.74f, 0.72f});           // haze-opaque sky
        c.push_back({0.61f, 0.70f, 0.69f, 0.68f});           // slightly dimmer haze
        c.push_back({0.01f, 3.00f, 3.00f, 3.00f});           // a specular

        const auto a = dh::airlightFrom(c);
        report(a[0] > 0.6f && a[0] < 0.8f,
               "the atmospheric light is the haze, not the brightest pixel",
               "A.r " + std::to_string(a[0]));
        report(a[0] < 1.0f, "a specular four times brighter is rejected",
               "A.r " + std::to_string(a[0]));
    }

    std::unique_ptr<orion::gpu::Device> device;
    try {
        device = orion::gpu::Device::create();
    } catch (const std::exception& e) {
        report(false, "Metal device available", e.what());
        return;
    }
    const auto load = [&](const char* entry) {
        auto lib = orion::gpu::Library::createFromFile(
            *device, std::string(ORION_SHADER_DIR) + "/" + entry + ".metallib");
        auto k = orion::gpu::Kernel::create(*device, *lib, entry);
        return std::pair{std::move(lib), std::move(k)};
    };

    // ── A separable rank filter really is the square patch ────────────────
    //
    // The claim in dehaze_rank.slang is that a 15-tap minimum along each axis
    // *is* the 15 x 15 minimum, not an approximation of it — that is what buys
    // 30 taps instead of 225. Checked against the square patch computed
    // directly, which is the only way to know.
    {
        constexpr int kW = 61, kH = 41;
        auto kRank = load("dehazeRank");

        std::vector<float> src(std::size_t(kW) * kH);
        std::mt19937 rng(20260728);
        std::uniform_real_distribution<float> uni(0.0f, 1.0f);
        for (auto& v : src) v = uni(rng);
        // A few hard zeros and ones, so the extremes are exercised and not just
        // the interior of the distribution.
        src[std::size_t(7) * kW + 9] = 0.0f;
        src[std::size_t(30) * kW + 40] = 1.0f;

        auto a = orion::gpu::Texture::create(*device, kW, kH, PixelFormat::R16Float);
        auto b = orion::gpu::Texture::create(*device, kW, kH, PixelFormat::R16Float);
        auto c = orion::gpu::Texture::create(*device, kW, kH, PixelFormat::R16Float);

        std::vector<__fp16> up(src.size());
        for (std::size_t i = 0; i < src.size(); ++i) up[i] = __fp16(src[i]);
        a->upload(up.data(), std::size_t(kW) * sizeof(__fp16));

        const auto runRank = [&](bool maximum) {
            orion::gpu::CommandBuffer cb(*device);
            orion::pipe::params::DehazeRank h{};
            h.size[0] = kW; h.size[1] = kH;
            h.radius = dh::kPatchRadius; h.horizontal = 1; h.maximum = maximum ? 1 : 0;
            cb.dispatch(*kRank.second, {a.get(), b.get()}, &h, sizeof h, kW, kH);
            orion::pipe::params::DehazeRank v = h;
            v.horizontal = 0;
            cb.dispatch(*kRank.second, {b.get(), c.get()}, &v, sizeof v, kW, kH);
            cb.commitAndWait();
            std::vector<__fp16> got(src.size());
            c->download(got.data(), std::size_t(kW) * sizeof(__fp16), kW, kH);
            return got;
        };

        for (const bool maximum : {false, true}) {
            const auto got = runRank(maximum);
            double worst = 0.0;
            for (int y = 0; y < kH; ++y) {
                for (int x = 0; x < kW; ++x) {
                    float want = src[std::size_t(y) * kW + x];
                    for (int dy = -dh::kPatchRadius; dy <= dh::kPatchRadius; ++dy) {
                        for (int dx = -dh::kPatchRadius; dx <= dh::kPatchRadius; ++dx) {
                            const int sx = std::clamp(x + dx, 0, kW - 1);
                            const int sy = std::clamp(y + dy, 0, kH - 1);
                            const float v = src[std::size_t(sy) * kW + sx];
                            want = maximum ? std::max(want, v) : std::min(want, v);
                        }
                    }
                    worst = std::max(worst,
                        std::abs(double(got[std::size_t(y) * kW + x]) - double(want)));
                }
            }
            report(worst < 1e-3,
                   maximum ? "separable 15-tap max is the 15x15 maximum"
                           : "separable 15-tap min is the 15x15 minimum",
                   "worst " + std::to_string(worst));
        }
    }

    // ── Eq. (16), and the identity at omega = 0 ───────────────────────────
    {
        constexpr int kW = 32, kH = 16;
        auto kRec = load("dehazeRecover");

        auto src    = orion::gpu::Texture::create(*device, kW, kH, PixelFormat::RGBA16Float);
        auto coeffs = orion::gpu::Texture::create(*device, kW, kH, PixelFormat::RG32Float);
        auto dst    = orion::gpu::Texture::create(*device, kW, kH, PixelFormat::RGBA16Float);

        std::vector<__fp16> in(std::size_t(kW) * kH * 4);
        for (int y = 0; y < kH; ++y) {
            for (int x = 0; x < kW; ++x) {
                const std::size_t i = (std::size_t(y) * kW + x) * 4;
                in[i + 0] = __fp16(0.05 + 0.9 * x / double(kW - 1));
                in[i + 1] = __fp16(0.04 + 0.7 * x / double(kW - 1));
                in[i + 2] = __fp16(0.06 + 0.5 * x / double(kW - 1));
                in[i + 3] = __fp16(1.0f);
            }
        }
        src->upload(in.data(), std::size_t(kW) * 4 * sizeof(__fp16));

        const float airlight[3] = {0.80f, 0.78f, 0.75f};

        // a = 0 and b = t makes the transmission a constant, which is what lets
        // Eq. (16) be checked as arithmetic rather than against another
        // implementation of the guided filter.
        const auto runWithT = [&](float t) {
            std::vector<float> ab(std::size_t(kW) * kH * 2);
            for (std::size_t i = 0; i < std::size_t(kW) * kH; ++i) {
                ab[i * 2 + 0] = 0.0f;
                ab[i * 2 + 1] = t;
            }
            coeffs->upload(ab.data(), std::size_t(kW) * 2 * sizeof(float));

            orion::pipe::params::DehazeRecover p{};
            p.size[0] = kW; p.size[1] = kH;
            p.coeffSize[0] = kW; p.coeffSize[1] = kH;
            p.t0 = dh::kT0;
            p.lo = orion::pipe::llf::kWindowLoEv;
            p.invRange = 1.0f / orion::pipe::llf::kWindowEv;
            for (int c = 0; c < 3; ++c) p.airlight[c] = airlight[c];

            orion::gpu::CommandBuffer cb(*device);
            cb.dispatch(*kRec.second, {src.get(), coeffs.get(), dst.get()},
                        &p, sizeof p, kW, kH);
            cb.commitAndWait();
            std::vector<__fp16> got(in.size());
            dst->download(got.data(), std::size_t(kW) * 4 * sizeof(__fp16), kW, kH);
            return got;
        };

        // t = 1 is what omega = 0 produces, and Eq. (16) collapses to J = I.
        // The slider's zero is exact because of this, not because the result is
        // blended back afterwards.
        const auto identity = runWithT(1.0f);
        double worstId = 0.0;
        for (std::size_t i = 0; i < in.size(); ++i) {
            if ((i & 3) == 3) continue;
            worstId = std::max(worstId, std::abs(double(identity[i]) - double(in[i])));
        }
        report(worstId < 2e-3, "transmission of one is exactly the identity",
               "worst " + std::to_string(worstId));

        const float t = 0.55f;
        const auto got = runWithT(t);
        double worstEq = 0.0;
        for (int x = 0; x < kW; ++x) {
            const std::size_t i = (std::size_t(kH / 2) * kW + x) * 4;
            for (int c = 0; c < 3; ++c) {
                const double want =
                    (double(in[i + c]) - airlight[c]) / t + airlight[c];
                worstEq = std::max(worstEq,
                                   std::abs(double(got[i + c]) - std::max(want, 0.0)));
            }
        }
        report(worstEq < 3e-3, "Eq. (16) recovers the radiance it was given",
               "worst " + std::to_string(worstEq));

        // The floor on t. Below it the division would run away, and the paper
        // puts t0 at 0.1 for exactly that reason.
        const auto floored = runWithT(0.001f);
        const auto atFloor = runWithT(dh::kT0);
        double worstFloor = 0.0;
        for (std::size_t i = 0; i < in.size(); ++i) {
            worstFloor = std::max(worstFloor,
                                  std::abs(double(floored[i]) - double(atFloor[i])));
        }
        report(worstFloor < 2e-3, "transmission is floored at t0, not divided by",
               "worst " + std::to_string(worstFloor));
    }
}

/// Reading .cube files, and applying them tetrahedrally.
void testCreativeLut() {
    section("Creative LUTs");

    using orion::pipe::parseCube;

    // ── The parser ────────────────────────────────────────────────────────
    {
        // Deliberately awkward: CRLF endings, a comment mid-file, blank lines,
        // a quoted title, mixed-case keywords, and leading whitespace. Every
        // one of these appears in LUTs people actually download.
        const std::string text =
            "# a comment\r\n"
            "TITLE \"Test Look\"\r\n"
            "\r\n"
            "  lut_3d_size 2\r\n"
            "DOMAIN_MIN 0 0 0\r\n"
            "DOMAIN_MAX 1 1 1\r\n"
            "0 0 0\r\n"          // r=0 g=0 b=0
            "1 0 0\r\n"          // r=1 g=0 b=0   <- red varies fastest
            "0 1 0\r\n"
            "1 1 0\r\n"
            "0 0 1\r\n"
            "1 0 1\r\n"
            "0 1 1\r\n"
            "1 1 1\r\n"
            "# and a trailing comment line\r\n";

        const auto r = parseCube(text);
        report(r.ok, "a well-formed 3D cube parses", r.error);
        report(r.lut.size == 2, "LUT_3D_SIZE is read",
               std::to_string(r.lut.size));
        report(r.lut.title == "Test Look", "TITLE loses its quotes", r.lut.title);

        // The ordering claim. Entry 1 is (r=1, g=0, b=0), which is only true if
        // red varies fastest. Getting this backwards swaps red and blue in
        // every LUT the product ever loads, and it would look plausible.
        report(r.ok && r.lut.data.size() == 24 &&
               r.lut.data[3] == 1.0f && r.lut.data[4] == 0.0f && r.lut.data[5] == 0.0f,
               "red varies fastest in the data block",
               r.ok ? std::to_string(r.lut.data[3]) + "," +
                      std::to_string(r.lut.data[4]) + "," +
                      std::to_string(r.lut.data[5]) : r.error);
    }
    {
        // Comments in this format are whole lines, not trailing text
        // (specification section 5.8) — and a look really can be called
        // "Look #3". A mid-line rule truncates that to "Look" and never says
        // so. This is the case that was written wrong first.
        std::string text = "TITLE \"Look #3\"\nLUT_3D_SIZE 2\n";
        for (int i = 0; i < 8; ++i) text += "0 0 0\n";
        const auto r = parseCube(text);
        report(r.ok && r.lut.title == "Look #3",
               "a hash inside a title is part of the title",
               r.ok ? r.lut.title : r.error);
    }
    {
        const auto r = parseCube("LUT_3D_SIZE 2\n0 0 0\n1 1 1\n");
        report(!r.ok, "a short data block is refused", r.error);
        report(r.error.find("needs 8 rows") != std::string::npos,
               "and the message says how many rows were expected", r.error);
    }
    {
        const auto r = parseCube("0 0 0\n1 1 1\n");
        report(!r.ok, "a file with no size keyword is refused", r.error);
    }
    {
        const auto r = parseCube("LUT_3D_SIZE 2\nLUT_1D_SIZE 4\n0 0 0\n");
        report(!r.ok, "declaring both 1D and 3D is refused", r.error);
    }
    {
        const auto r = parseCube("LUT_3D_SIZE 2\n0 0 0\n1 1 nonsense\n");
        report(!r.ok && r.error.find("line 3") != std::string::npos,
               "a bad number is refused, with its line number", r.error);
    }
    {
        // A 1D LUT is a separable 3D LUT; it is lifted onto the grid so there
        // is one code path downstream. Halving red, leaving green and blue.
        const auto r = parseCube("LUT_1D_SIZE 2\n0 0 0\n0.5 1 1\n");
        report(r.ok && r.lut.wasOneDimensional, "a 1D cube is lifted onto the grid",
               r.error);
        if (r.ok) {
            // The grid corner at r=1, g=1, b=1 must read (0.5, 1, 1).
            const int n = r.lut.size;
            const std::size_t last =
                ((static_cast<std::size_t>(n - 1) * n + (n - 1)) * n + (n - 1)) * 3;
            report(std::abs(r.lut.data[last] - 0.5f) < 1e-5f &&
                   std::abs(r.lut.data[last + 1] - 1.0f) < 1e-5f,
                   "and the lift preserves the curve at the grid corners",
                   std::to_string(r.lut.data[last]));
        }
    }

    // ── Tetrahedral, and not trilinear ────────────────────────────────────
    //
    // These two agree on any linear function, so a LUT that does something
    // gentle cannot tell them apart — which is exactly why "it looks right" is
    // not evidence here. This table is zero at every corner except (1,1,1),
    // where the two interpolations disagree enormously: at a sample inside the
    // cell, tetrahedral returns the smallest fractional coordinate and
    // trilinear returns the product of all three.
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

    constexpr std::uint32_t kN = 64;
    auto src  = orion::gpu::Texture::create(*device, kN, 1, orion::gpu::PixelFormat::RGBA16Float);
    auto curve = orion::gpu::Texture::create(*device, 256, 4, orion::gpu::PixelFormat::R32Float);
    auto cube = orion::gpu::Texture::create(*device, orion::pipe::kMaxCubeSize,
                                            orion::pipe::kMaxCubeSize * orion::pipe::kMaxCubeSize,
                                            orion::gpu::PixelFormat::RGBA32Float);
    auto dst  = orion::gpu::Texture::create(*device, kN, 1, orion::gpu::PixelFormat::RGBA8Unorm);

    // A scene-linear sweep, so the display values the LUT sees cover the range.
    std::vector<__fp16> in(std::size_t(kN) * 4);
    for (std::uint32_t x = 0; x < kN; ++x) {
        const double v = 0.002 * std::pow(1500.0, x / double(kN - 1));
        in[x * 4 + 0] = __fp16(v);
        in[x * 4 + 1] = __fp16(v * 0.82);
        in[x * 4 + 2] = __fp16(v * 0.65);
        in[x * 4 + 3] = __fp16(1.0f);
    }
    src->upload(in.data(), std::size_t(kN) * 4 * sizeof(float) / 2);

    std::vector<float> curveData(256 * 4, 0.0f);
    curve->upload(curveData.data(), 256 * sizeof(float));

    constexpr int kGrid = 2;
    std::vector<float> grid(std::size_t(orion::pipe::kMaxCubeSize) *
                            orion::pipe::kMaxCubeSize * orion::pipe::kMaxCubeSize * 4, 0.0f);
    // Row is b * grid + g — the LUT's own edge, which is what the shader
    // recomputes; the texture is only wide enough to hold the largest grid.
    const auto gridAt = [&](int r, int g, int b) -> std::size_t {
        const std::size_t row = std::size_t(b) * kGrid + g;
        return (row * orion::pipe::kMaxCubeSize + r) * 4;
    };
    const std::size_t top = gridAt(1, 1, 1);
    grid[top + 0] = grid[top + 1] = grid[top + 2] = 1.0f;
    cube->upload(grid.data(), std::size_t(orion::pipe::kMaxCubeSize) * 4 * sizeof(float));

    const auto run = [&](std::uint32_t lutSize, float strength) {
        orion::pipe::params::Display p{};
        p.contrast = 1.0f;
        p.pivot = -2.5f;
        p.curveIdentity = 1u;
        p.resolution = 256u;
        p.size[0] = kN; p.size[1] = 1;
        p.dither = 0u;                 // noise would swamp the comparison
        p.lutSize = lutSize;
        p.lutStrength = strength;
        p.lutMin[0] = p.lutMin[1] = p.lutMin[2] = 0.0f;
        p.lutMax[0] = p.lutMax[1] = p.lutMax[2] = 1.0f;

        orion::gpu::CommandBuffer cb(*device);
        cb.dispatch(*kernel, {src.get(), curve.get(), cube.get(), dst.get()},
                    &p, sizeof p, kN, 1);
        cb.commitAndWait();
        std::vector<std::uint8_t> out(std::size_t(kN) * 4);
        dst->download(out.data(), std::size_t(kN) * 4, kN, 1);
        return out;
    };

    // What the display transform produces without any LUT — the values the
    // lookup is indexed by. Measured rather than predicted, because AgX is not
    // something to re-derive in a test.
    const auto plain = run(0u, 1.0f);
    const auto looked = run(kGrid, 1.0f);

    double worstTetra = 0.0, worstTriBest = 1e30;
    for (std::uint32_t x = 0; x < kN; ++x) {
        const double fr = plain[x * 4 + 0] / 255.0;
        const double fg = plain[x * 4 + 1] / 255.0;
        const double fb = plain[x * 4 + 2] / 255.0;

        // Only c111 is nonzero, so tetrahedral reduces to the smallest of the
        // three fractions and trilinear to their product.
        const double tetra = std::min({fr, fg, fb});
        const double tri   = fr * fg * fb;

        const double got = looked[x * 4 + 0] / 255.0;
        worstTetra = std::max(worstTetra, std::abs(got - tetra));
        worstTriBest = std::min(worstTriBest, std::abs(got - tri));
    }

    report(worstTetra < 0.02,
           "the lookup is tetrahedral, matching the simplex the sample is in",
           "worst " + std::to_string(worstTetra));
    report(worstTriBest > 0.02 || worstTetra < worstTriBest,
           "and is measurably not trilinear, which the two agree would differ",
           "tetrahedral " + std::to_string(worstTetra));

    // Strength zero has to be exactly the untouched picture, so that loading a
    // LUT and dialling it out is not a slightly different image.
    const auto off = run(kGrid, 0.0f);
    int differing = 0;
    for (std::size_t i = 0; i < off.size(); ++i) if (off[i] != plain[i]) ++differing;
    report(differing == 0, "strength zero is byte-identical to no LUT at all",
           std::to_string(differing) + " bytes differ");

    // An identity LUT must be the identity, which is the check that catches the
    // packing being wrong: a transposed or mis-strided grid still looks like a
    // plausible look, and only an identity table makes it obvious.
    std::fill(grid.begin(), grid.end(), 0.0f);
    for (int b = 0; b < kGrid; ++b)
        for (int g = 0; g < kGrid; ++g)
            for (int r = 0; r < kGrid; ++r) {
                const std::size_t o = gridAt(r, g, b);
                grid[o + 0] = float(r); grid[o + 1] = float(g); grid[o + 2] = float(b);
            }
    cube->upload(grid.data(), std::size_t(orion::pipe::kMaxCubeSize) * 4 * sizeof(float));

    const auto identity = run(kGrid, 1.0f);
    int worstId = 0;
    for (std::size_t i = 0; i < identity.size(); ++i) {
        worstId = std::max(worstId, std::abs(int(identity[i]) - int(plain[i])));
    }
    report(worstId <= 1, "an identity LUT leaves every pixel where it was",
           "worst " + std::to_string(worstId) + "/255");
}

/// Simulated exposure fusion — the maths, before any of it reaches a shader.
void testExposureFusionMath() {
    section("Exposure fusion (maths)");

    namespace sef = orion::pipe::sef;

    const float alpha = sef::kAlphaDefault, beta = sef::kBeta;
    sef::Plan p{};
    p.images = 5; p.dark = 0; p.bright = 4;

    // ── The smooth clip ───────────────────────────────────────────────────
    //
    // g is the identity inside the restrained range and decays outside it. The
    // join is where a hand-rolled version goes wrong: a discontinuity there is
    // a visible edge in the simulated image, and a slope discontinuity is a
    // visible edge in the *weight*, which is worse because it moves.
    {
        double worstJoin = 0.0, worstSlope = 0.0;
        bool monotone = true, bounded = true;
        for (int k = 0; k <= p.bright; ++k) {
            const float c = sef::rho(k, p, beta);
            const float half = beta * 0.5f;

            // Continuity of value at both ends of the range.
            worstJoin = std::max(worstJoin,
                std::abs(double(sef::clip(c + half, c, beta, sef::kLambda)) - double(c + half)));
            worstJoin = std::max(worstJoin,
                std::abs(double(sef::clip(c - half, c, beta, sef::kLambda)) - double(c - half)));

            // Continuity of slope: one inside, and the tail's own expression
            // evaluated at the join must also be one.
            worstSlope = std::max(worstSlope,
                std::abs(double(sef::clipSlope(c + half + 1e-6f, c, beta, sef::kLambda)) - 1.0));

            float previous = -1e9f;
            for (int i = -200; i <= 400; ++i) {
                const float t = float(i) / 200.0f;      // deliberately outside [0,1]
                const float g = sef::clip(t, c, beta, sef::kLambda);
                if (g < previous - 1e-6f) monotone = false;
                previous = g;
                // The tail asymptote: |g - centre| < beta/2 + lambda.
                if (std::abs(g - c) > half + sef::kLambda + 1e-5f) bounded = false;
            }
        }
        report(worstJoin < 1e-6, "the clip is continuous where its branches meet",
               "worst " + std::to_string(worstJoin));
        report(worstSlope < 1e-3, "and its slope is one on both sides of the join",
               "worst " + std::to_string(worstSlope));
        report(monotone, "the clip is monotone, so it cannot invert tones", "");
        report(bounded, "and is bounded by beta/2 + lambda however far the input goes", "");
    }

    // beta = 1 degenerates to plain exposure fusion — the paper says so, and it
    // is the cheapest check that rho and the clip agree with each other.
    {
        sef::Plan q{}; q.images = 5; q.dark = 0; q.bright = 4;
        double worst = 0.0;
        for (int k = 0; k <= q.bright; ++k) {
            for (int i = 0; i <= 100; ++i) {
                const float t = float(i) / 100.0f;
                worst = std::max(worst, std::abs(double(sef::clip(t, sef::rho(k, q, 1.0f),
                                                                 1.0f, sef::kLambda)) - double(t)));
            }
        }
        report(worst < 1e-6, "beta = 1 makes the clip the identity, as the paper states",
               "worst " + std::to_string(worst));
    }

    // ── The contrast weight is the clip's own derivative ──────────────────
    //
    // Hessel & Morel replace Mertens' Laplacian filter with it, so if this is
    // not actually the derivative there is no contrast measure at all. Checked
    // against a finite difference rather than against itself.
    {
        double worst = 0.0;
        for (int k = 0; k <= p.bright; ++k) {
            const float c = sef::rho(k, p, beta);
            for (int i = 1; i < 200; ++i) {
                const float t = -0.2f + 1.4f * float(i) / 200.0f;
                const float h = 1e-4f;
                if (std::abs(std::abs(t - c) - beta * 0.5f) < 2e-3f) continue;  // skip the join
                const float numeric = (sef::clip(t + h, c, beta, sef::kLambda) -
                                       sef::clip(t - h, c, beta, sef::kLambda)) / (2.0f * h);
                worst = std::max(worst,
                    std::abs(double(numeric) - double(sef::clipSlope(t, c, beta, sef::kLambda))));
            }
        }
        report(worst < 2e-3, "the contrast weight is the clip's actual derivative",
               "worst " + std::to_string(worst));
    }

    // ── The simulated set is solved, not chosen ───────────────────────────
    {
        const auto dark  = sef::planFor(0.05f, alpha, beta);
        const auto light = sef::planFor(0.95f, alpha, beta);
        report(dark.valid() && light.valid(), "a plan is found at both ends of the histogram",
               std::to_string(dark.images) + " and " + std::to_string(light.images));
        // "contrast enhancement is generally needed in the dark parts only" —
        // so a dark frame gets brightened images and a bright one gets darkened.
        report(dark.bright > dark.dark,
               "a dark frame simulates more brightened images than darkened",
               std::to_string(dark.bright) + " vs " + std::to_string(dark.dark));
        report(light.dark >= light.bright,
               "and a bright frame the other way round",
               std::to_string(light.dark) + " vs " + std::to_string(light.bright));

        // The paper reports N = 4, N* = 0 — five images — for its own recommended
        // alpha = 8 and beta = 0.5. Reproducing that is the check that says
        // Algorithm 1 was implemented rather than approximated.
        const auto recommended = sef::planFor(0.0f, 8.0f, 0.5f);
        report(recommended.images == 5 && recommended.bright == 4 && recommended.dark == 0,
               "the paper's own alpha = 8, beta = 0.5 gives its own N = 4, N* = 0",
               std::to_string(recommended.images) + " images, N " +
                   std::to_string(recommended.bright) + ", N* " +
                   std::to_string(recommended.dark));

        // And the count moves with beta, which a hardcoded five could not: the
        // paper tabulates N = 3 at beta = 0.6 and N = 6 at beta = 0.4.
        const auto wide   = sef::planFor(0.0f, 8.0f, 0.6f);
        const auto narrow = sef::planFor(0.0f, 8.0f, 0.4f);
        std::printf("  images by beta: 0.4 -> %d, 0.5 -> %d, 0.6 -> %d\n",
                    narrow.images, recommended.images, wide.images);
        report(wide.images < recommended.images && narrow.images > recommended.images,
               "and the count follows beta, which a hardcoded five could not",
               std::to_string(narrow.images) + " / " + std::to_string(recommended.images) +
                   " / " + std::to_string(wide.images));
    }

    // ── The blend, exactly ────────────────────────────────────────────────
    //
    // On a constant image every Laplacian coefficient is zero and only the
    // residual survives, so the fused value must be the weighted average of the
    // remapped constants — computable here in closed form. If the pyramid,
    // the expand, the weighting or the collapse is wrong in any way that does
    // not cancel, this is where it shows.
    {
        constexpr int kW = 24, kH = 16, kLevels = 4;
        const float t = 0.3f;
        const auto plan = sef::planFor(t, alpha, beta);

        orion::pipe::pyr::Plane flat{kW, kH, std::vector<float>(std::size_t(kW) * kH, t)};
        auto fused = sef::fuseReference(flat, plan, alpha, beta, sef::kSigma, kLevels);

        double sumW = 0.0, sumWV = 0.0;
        for (int k = -plan.dark; k <= plan.bright; ++k) {
            const double w = double(sef::wellExposed(sef::remap(t, k, plan, alpha, beta),
                                                     sef::kSigma)) *
                             double(sef::contrastWeight(t, k, plan, alpha, beta));
            sumW  += w;
            sumWV += w * double(sef::remap(t, k, plan, alpha, beta));
        }
        const double want = sumWV / sumW;

        double worst = 0.0, spread = 0.0;
        for (std::size_t i = 0; i < fused.v.size(); ++i) {
            worst  = std::max(worst,  std::abs(double(fused.v[i]) - want));
            spread = std::max(spread, std::abs(double(fused.v[i]) - double(fused.v[0])));
        }
        report(worst < 2e-4, "a flat field fuses to the weighted average of its remaps",
               "want " + std::to_string(want) + ", worst error " + std::to_string(worst));
        report(spread < 1e-5, "and stays flat — no seam from the pyramid or the borders",
               "spread " + std::to_string(spread));
    }

    // A ramp must not gain a reversal: the whole point is more local contrast,
    // not different tonal ordering.
    {
        constexpr int kW = 64, kH = 8, kLevels = 4;
        orion::pipe::pyr::Plane ramp{kW, kH, std::vector<float>(std::size_t(kW) * kH)};
        for (int y = 0; y < kH; ++y)
            for (int x = 0; x < kW; ++x)
                ramp.v[std::size_t(y) * kW + x] = float(x) / float(kW - 1);

        const auto plan = sef::planFor(0.5f, alpha, beta);
        auto fused = sef::fuseReference(ramp, plan, alpha, beta, sef::kSigma, kLevels);

        // Exposure fusion does not *guarantee* monotonicity — it blends
        // Laplacian coefficients with spatially varying weights, and Mertens
        // et al. section 4.1 names a "spurious low frequency brightness change"
        // as a known artefact of exactly this. So the question is not whether
        // reversals exist but how large they are, and whether they grow with
        // the amplification — which is what says they come from the method
        // rather than from a mistake in the blend.
        const std::size_t row = std::size_t(kH / 2) * kW;
        const auto worstDropAt = [&](float a) {
            const auto q = sef::planFor(0.5f, a, beta);
            auto f = sef::fuseReference(ramp, q, a, beta, sef::kSigma, kLevels);
            double worst = 0.0;
            for (int x = 1; x < kW; ++x) {
                worst = std::max(worst, double(f.v[row + x - 1]) - double(f.v[row + x]));
            }
            return worst;
        };
        const double mild = worstDropAt(2.0f);
        const double mid  = worstDropAt(4.0f);
        const double full = worstDropAt(8.0f);
        std::printf("  ramp reversal by alpha: 2 -> %.2e, 4 -> %.2e, 8 -> %.2e\n",
                    mild, mid, full);

        report(mild < full && mid <= full,
               "reversals scale with the amplification, as the method implies",
               std::to_string(mild) + " < " + std::to_string(full));
        // A regression guard on the worst case, not an aspiration. If a change
        // to the blend pushes this up, that is the signal.
        report(full < 0.02, "and stay under two percent at full amplification",
               "worst " + std::to_string(full));
    }

    // ── Robust normalisation ──────────────────────────────────────────────
    {
        orion::pipe::pyr::Plane v{101, 1, std::vector<float>(101)};
        for (int i = 0; i <= 100; ++i) v.v[std::size_t(i)] = 0.2f + 0.006f * float(i);
        sef::robustNormalise(v, sef::kRobustClip);

        const auto mm = std::minmax_element(v.v.begin(), v.v.end());
        report(*mm.first <= 1e-5f && *mm.second >= 1.0f - 1e-5f,
               "robust normalisation stretches to the full range",
               std::to_string(*mm.first) + " … " + std::to_string(*mm.second));

        // The clip is what makes it robust: an outlier must not set the scale.
        orion::pipe::pyr::Plane spike = v;
        for (float& t : spike.v) t = 0.5f;
        spike.v[0] = 100.0f;
        sef::robustNormalise(spike, sef::kRobustClip);
        report(spike.v[1] > 0.0f && spike.v[1] <= 1.0f,
               "and a single wild outlier does not flatten everything else",
               std::to_string(spike.v[1]));
    }
}

/// Exposure fusion on the GPU — does the shader run the maths the tests pinned?
void testExposureFusionGpu() {
    section("Exposure fusion (GPU)");

    namespace sef = orion::pipe::sef;
    using orion::gpu::PixelFormat;

    std::unique_ptr<orion::gpu::Device> device;
    try {
        device = orion::gpu::Device::create();
    } catch (const std::exception& e) {
        report(false, "Metal device available", e.what());
        return;
    }
    const auto load = [&](const char* entry) {
        auto lib = orion::gpu::Library::createFromFile(
            *device, std::string(ORION_SHADER_DIR) + "/" + entry + ".metallib");
        auto k = orion::gpu::Kernel::create(*device, *lib, entry);
        return std::pair{std::move(lib), std::move(k)};
    };

    constexpr std::uint32_t kW = 64, kH = 4;
    const auto plan = sef::planFor(0.4f, sef::kAlphaDefault, sef::kBeta);

    // ── The simulated images and weights, against the C++ mirror ──────────
    //
    // ops/fuse_ops.slang and pipe/ExposureFusion.h are two implementations of
    // the same equations, and the CPU one is what every other test in this file
    // measures against. If they disagree, those tests are pinning something the
    // product does not run.
    {
        auto kSplit = load("fuseSplit");
        auto proxy = orion::gpu::Texture::create(*device, kW, kH, PixelFormat::R16Float);
        auto dst   = orion::gpu::Texture::create(*device, kW, kH, PixelFormat::RGBA16Float);

        std::vector<__fp16> in(std::size_t(kW) * kH);
        for (std::uint32_t x = 0; x < kW; ++x) {
            for (std::uint32_t y = 0; y < kH; ++y) {
                in[std::size_t(y) * kW + x] = __fp16(float(x) / float(kW - 1));
            }
        }
        proxy->upload(in.data(), std::size_t(kW) * sizeof(__fp16));

        const auto run = [&](int base, bool weights) {
            orion::pipe::params::FuseSplit sp{};
            sp.size[0] = kW; sp.size[1] = kH;
            sp.base = base;
            sp.weights = weights ? 1 : 0;
            sp.plan.images = plan.images; sp.plan.bright = plan.bright;
            sp.plan.dark = plan.dark;     sp.plan.span = plan.span();
            sp.plan.alpha = sef::kAlphaDefault; sp.plan.beta = sef::kBeta;
            sp.plan.lambda = sef::kLambda;      sp.plan.sigma = sef::kSigma;
            sp.epsilon = sef::kWeightEpsilon;

            orion::gpu::CommandBuffer cb(*device);
            cb.dispatch(*kSplit.second, {proxy.get(), dst.get()}, &sp, sizeof sp, kW, kH);
            cb.commitAndWait();
            std::vector<__fp16> out(std::size_t(kW) * kH * 4);
            dst->download(out.data(), std::size_t(kW) * 4 * sizeof(__fp16), kW, kH);
            return out;
        };

        double worstImage = 0.0, worstWeight = 0.0, worstSum = 0.0;
        std::vector<std::vector<__fp16>> images, weights;
        for (int st = 0; st * 4 < plan.images; ++st) {
            images.push_back(run(st * 4, false));
            weights.push_back(run(st * 4, true));
        }

        for (std::uint32_t x = 0; x < kW; ++x) {
            const float t = float(x) / float(kW - 1);

            double total = 0.0;
            for (int k = -plan.dark; k <= plan.bright; ++k) {
                total += double(sef::wellExposed(sef::remap(t, k, plan, sef::kAlphaDefault,
                                                            sef::kBeta), sef::kSigma)) *
                         double(sef::contrastWeight(t, k, plan, sef::kAlphaDefault, sef::kBeta));
            }

            double sum = 0.0;
            for (int index = 0; index < plan.images; ++index) {
                const int k = index - plan.dark;
                const std::size_t st = std::size_t(index / 4);
                const std::size_t at = (std::size_t(1) * kW + x) * 4 + std::size_t(index % 4);

                const double wantImage = sef::remap(t, k, plan, sef::kAlphaDefault, sef::kBeta);
                const double wantWeight =
                    double(sef::wellExposed(sef::remap(t, k, plan, sef::kAlphaDefault,
                                                       sef::kBeta), sef::kSigma)) *
                    double(sef::contrastWeight(t, k, plan, sef::kAlphaDefault, sef::kBeta)) /
                    std::max(total, double(sef::kWeightEpsilon));

                worstImage  = std::max(worstImage,  std::abs(double(images[st][at]) - wantImage));
                worstWeight = std::max(worstWeight, std::abs(double(weights[st][at]) - wantWeight));
                sum += double(weights[st][at]);
            }
            worstSum = std::max(worstSum, std::abs(sum - 1.0));
        }

        report(worstImage < 3e-3, "the shader simulates the same exposures as the reference",
               "worst " + std::to_string(worstImage));
        // Looser than the image comparison on purpose: the weights are
        // normalised to sum to one across the whole set, so the more images the
        // plan asks for the smaller each one is, and RGBA16Float's absolute
        // quantum does not shrink with them. The invariant that actually
        // matters — that they still sum to one — is checked below and is tight.
        report(worstWeight < 5e-3, "and computes the same weights",
               "worst " + std::to_string(worstWeight));
        // Mertens et al. section 3.2 normalises so the weights sum to one at
        // every pixel. If they do not, the blend is a gain as well as a blend.
        report(worstSum < 5e-3, "the weights sum to one at every pixel",
               "worst " + std::to_string(worstSum));
    }

    // ── Strength zero is the identity, bit for bit ────────────────────────
    //
    // No published parameter of this method degenerates to the identity, which
    // is why the slider is a power on the emitted gain instead. gain^0 = 1, and
    // this is the check that says so rather than assuming it.
    {
        auto kApply = load("fuseApply");
        auto src   = orion::gpu::Texture::create(*device, kW, kH, PixelFormat::RGBA16Float);
        auto fused = orion::gpu::Texture::create(*device, 8, 2, PixelFormat::R16Float);
        auto proxy = orion::gpu::Texture::create(*device, 8, 2, PixelFormat::R16Float);
        auto dst   = orion::gpu::Texture::create(*device, kW, kH, PixelFormat::RGBA16Float);

        std::vector<__fp16> in(std::size_t(kW) * kH * 4);
        for (std::size_t i = 0; i < std::size_t(kW) * kH; ++i) {
            in[i * 4 + 0] = __fp16(0.02 + 0.9 * double(i % kW) / double(kW - 1));
            in[i * 4 + 1] = __fp16(0.30);
            in[i * 4 + 2] = __fp16(0.11);
            in[i * 4 + 3] = __fp16(1.0f);
        }
        src->upload(in.data(), std::size_t(kW) * 4 * sizeof(__fp16));

        // A deliberately violent gain: the fused proxy says every pixel should
        // move a long way, so a strength that is not exactly zero will show.
        std::vector<__fp16> a(16, __fp16(0.9f)), b(16, __fp16(0.1f));
        fused->upload(a.data(), 8 * sizeof(__fp16));
        proxy->upload(b.data(), 8 * sizeof(__fp16));

        const auto run = [&](float strength) {
            orion::pipe::params::FuseApply ap{};
            ap.size[0] = kW; ap.size[1] = kH;
            ap.proxySize[0] = 8; ap.proxySize[1] = 2;
            ap.slope = sef::kProxySlopeEv;
            ap.strength = strength;
            ap.maxGain = sef::kMaxGain;

            orion::gpu::CommandBuffer cb(*device);
            cb.dispatch(*kApply.second, {src.get(), fused.get(), proxy.get(), dst.get()},
                        &ap, sizeof ap, kW, kH);
            cb.commitAndWait();
            std::vector<__fp16> out(in.size());
            dst->download(out.data(), std::size_t(kW) * 4 * sizeof(__fp16), kW, kH);
            return out;
        };

        const auto off = run(0.0f);
        int differing = 0;
        for (std::size_t i = 0; i < in.size(); ++i) {
            if (float(off[i]) != float(in[i])) ++differing;
        }
        report(differing == 0, "strength zero leaves every pixel bit-identical",
               std::to_string(differing) + " of " + std::to_string(in.size()) + " differ");

        const auto on = run(1.0f);
        double lift = 0.0;
        for (std::size_t i = 0; i < std::size_t(kW) * kH; ++i) {
            lift = std::max(lift, double(on[i * 4]) / std::max(double(in[i * 4]), 1e-6));
        }
        report(lift > 1.5, "and full strength actually lifts", "max gain " + std::to_string(lift));

        // The clamp replaces the paper's global renormalisation, so it has to
        // hold even when the proxy asks for something absurd.
        report(lift <= double(sef::kMaxGain) + 1e-3,
               "the gain is clamped where the paper would have renormalised",
               "max gain " + std::to_string(lift));
    }
}

/// Auto-enhance reads a histogram. This is that reading, without a GPU.
void testAutoEnhanceStats() {
    section("Auto-enhance (statistics)");

    namespace ae = orion::pipe::auto_enhance;
    constexpr std::uint32_t kBins = 256;

    // A flat histogram: the p-th percentile is p, to within a bin.
    {
        std::vector<std::uint32_t> h(kBins, 100u);
        double worst = 0.0;
        for (double f = 0.05; f <= 0.95; f += 0.05) {
            worst = std::max(worst, std::abs(double(ae::percentileOf(h.data(), kBins, f)) - f));
        }
        report(worst < 1.0 / double(kBins) + 1e-6,
               "percentiles of a flat histogram are the fraction itself",
               "worst " + std::to_string(worst));
    }

    // Everything in one bin: every percentile is that bin, whatever is asked.
    {
        std::vector<std::uint32_t> h(kBins, 0u);
        h[64] = 5000u;
        report(std::abs(ae::percentileOf(h.data(), kBins, 0.01f) - 64.0f / 256.0f) < 1e-6 &&
               std::abs(ae::percentileOf(h.data(), kBins, 0.99f) - 64.0f / 256.0f) < 1e-6,
               "a single-valued image reports that value at every percentile", "");
    }

    // The bin's lower edge, not its centre. With a coarse histogram the centre
    // reports a black point above zero on an image that genuinely contains
    // black, and the correction then lifts a picture that needed nothing.
    {
        std::vector<std::uint32_t> h(kBins, 0u);
        h[0] = 1000u;
        report(ae::percentileOf(h.data(), kBins, 0.5) == 0.0f,
               "an image sitting in bin zero reports exactly zero, not half a bin",
               std::to_string(ae::percentileOf(h.data(), kBins, 0.5)));
    }

    // An empty histogram must not divide by its own total.
    {
        std::vector<std::uint32_t> h(kBins, 0u);
        report(ae::percentileOf(h.data(), kBins, 0.5) == 0.0f,
               "an empty histogram is answered, not divided by", "");
    }

    // Combining is a sum over channels: the bin index is a value, and a value's
    // frequency is how often any channel took it.
    {
        std::vector<std::uint32_t> rgb(std::size_t(kBins) * 3, 0u);
        rgb[10] = 3u;                    // red
        rgb[kBins + 10] = 4u;            // green
        rgb[2 * kBins + 10] = 5u;        // blue
        std::vector<std::uint32_t> out(kBins, 0u);
        ae::combine(rgb.data(), kBins, out.data());
        report(out[10] == 12u, "the three channel histograms combine by summing",
               std::to_string(out[10]));
    }

    // ── The solver ────────────────────────────────────────────────────────
    //
    // Driven against a stand-in for the pipeline rather than the real one, so
    // the controller's behaviour is testable without a GPU or a photograph.
    // The stand-in compresses like a display transform does, which is the
    // property that makes a full correction overshoot and the damping
    // necessary.
    {
        const auto renderStub = [](float sceneMedian, float ev) {
            const float lifted = sceneMedian * std::exp2(ev);
            // A soft shoulder — not AgX, but non-linear in the same direction.
            return lifted / (1.0f + lifted);
        };

        bool allConverged = true;
        double worst = 0.0;
        for (const float scene : {0.05f, 0.08f, 0.35f, 1.2f, 4.0f}) {
            ae::Controls c{};
            for (int pass = 0; pass < ae::kMaxPasses; ++pass) {
                ae::Stats st{};
                st.median = renderStub(scene, c.exposureEv);
                st.shadow = st.median * 0.4f;
                st.high   = std::min(1.0f, st.median * 1.8f);
                c = ae::refine(c, st);
            }
            const float finalMedian = renderStub(scene, c.exposureEv);
            const double err = std::abs(double(finalMedian) - double(ae::kMidGrey));
            worst = std::max(worst, err);
            if (err > 0.05) allConverged = false;
        }
        report(allConverged,
               "the exposure solver converges on the mid-grey anchor across four stops of scene",
               "worst " + std::to_string(worst));

        // Past its clamp it must saturate, not oscillate. A frame this dark
        // wanted a different photograph, and an automatic control that will
        // move ten stops turns a mistake into a bigger one.
        ae::Controls c{};
        float previous = 0.0f;
        bool settled = true;
        for (int pass = 0; pass < 8; ++pass) {
            ae::Stats st{};
            st.median = renderStub(0.001f, c.exposureEv);
            st.shadow = st.median * 0.4f;
            st.high   = std::min(1.0f, st.median * 1.8f);
            c = ae::refine(c, st);
            if (pass > 0 && c.exposureEv < previous - 1e-6f) settled = false;
            previous = c.exposureEv;
        }
        report(settled && std::abs(c.exposureEv - ae::kMaxExposureEv) < 1e-4f,
               "a frame darker than the clamp saturates at it rather than oscillating",
               std::to_string(c.exposureEv));
    }

    // A picture already against the stops must not be pushed further out —
    // Simplest Color Balance section 1 warns that saturation "can create flat
    // white regions or flat black regions that may look unnatural".
    {
        ae::Controls c{};
        ae::Stats st{};
        st.median = ae::kMidGrey;
        st.shadow = 0.1f; st.high = 0.9f;
        st.atFloor = 0.10f; st.atCeiling = 0.10f;    // already clipping hard
        const auto next = ae::refine(c, st);
        report(next.blacks == 0.0f && next.whites == 0.0f,
               "an already-clipping picture has its endpoints left alone",
               std::to_string(next.blacks) + " / " + std::to_string(next.whites));
    }

    // And the published slope cap has to actually bind: a picture that already
    // uses a narrow range would be stretched past smax = 2 without it.
    {
        ae::Controls c{};
        ae::Stats st{};
        st.median = ae::kMidGrey;
        st.shadow = 0.45f; st.high = 0.55f;          // span 0.1 -> slope 10
        const auto next = ae::refine(c, st);
        report(next.blacks == 0.0f && next.whites == 0.0f,
               "the slope cap stops a low-contrast frame being stretched tenfold",
               std::to_string(next.blacks) + " / " + std::to_string(next.whites));
    }

    // The look controls respond to the picture, and stay inside their range.
    {
        const auto darkLook   = ae::look({0.05f, 0.4f, 0.08f, 0.0f, 0.0f});
        const auto brightLook = ae::look({0.3f, 0.98f, 0.75f, 0.0f, 0.0f});
        report(darkLook.fusion > brightLook.fusion,
               "a dark frame is given more shadow lift than a bright one",
               std::to_string(darkLook.fusion) + " vs " + std::to_string(brightLook.fusion));
        report(brightLook.fusion == 0.0f,
               "and a frame already above mid grey is given none", "");
        report(darkLook.fusion <= 1.0f && darkLook.clarity <= 1.0f,
               "the look controls stay inside the range the sliders have", "");
    }

    // Clipping is reported, because a picture already against the stops does
    // not want its endpoints pushed further out.
    {
        std::vector<std::uint32_t> h(kBins, 10u);
        h[0] = 500u;
        h[kBins - 1] = 250u;
        const auto s = ae::measure(h.data(), kBins, 0.005);
        const double total = 500.0 + 250.0 + 254.0 * 10.0;
        report(std::abs(double(s.atFloor) - 500.0 / total) < 1e-4 &&
               std::abs(double(s.atCeiling) - 250.0 / total) < 1e-4,
               "measure reports how much is already clipped at each end",
               std::to_string(s.atFloor) + " / " + std::to_string(s.atCeiling));
        report(s.shadow < s.median && s.median < s.high,
               "and the three percentiles come back in order", "");
    }
}

/// Mask primitives, and the thing about them most likely to be got wrong.
void testMaskGpu() {
    section("Masks (GPU)");

    using orion::gpu::PixelFormat;
    constexpr std::uint32_t kW = 64, kH = 64;

    std::unique_ptr<orion::gpu::Device> device;
    try {
        device = orion::gpu::Device::create();
    } catch (const std::exception& e) {
        report(false, "Metal device available", e.what());
        return;
    }
    const auto load = [&](const char* entry) {
        auto lib = orion::gpu::Library::createFromFile(
            *device, std::string(ORION_SHADER_DIR) + "/" + entry + ".metallib");
        auto k = orion::gpu::Kernel::create(*device, *lib, entry);
        return std::pair{std::move(lib), std::move(k)};
    };

    auto kMask = load("maskComponent");
    auto src = orion::gpu::Texture::create(*device, kW, kH, PixelFormat::R16Float);
    auto dst = orion::gpu::Texture::create(*device, kW, kH, PixelFormat::R16Float);
    // Bound but unread by kinds 1-3. The kernel takes a matte for kind 4
    // (research/masking.md §5) and the binding has to be present either way.
    auto matte = orion::gpu::Texture::create(*device, kW, kH, PixelFormat::R16Float);
    // Bound but unread by every kind except 5, which reads a luminance out of
    // it (research/masking.md §4b). The binding has to be present regardless.
    auto reference = orion::gpu::Texture::create(*device, kW, kH,
                                                 PixelFormat::RGBA16Float);

    // The fold's identity, which mask:base writes in the product. Every check
    // here runs a single component over it, where add-from-zero is the
    // component's own coverage — so all of the numbers pinned before the merge
    // must reproduce exactly.
    const std::vector<__fp16> zeroes(std::size_t(kW) * kH, __fp16(0.0f));

    const auto run = [&](const orion::pipe::params::MaskComponent& m) {
        src->upload(zeroes.data(), std::size_t(kW) * sizeof(__fp16));
        orion::gpu::CommandBuffer cb(*device);
        cb.dispatch(*kMask.second, {src.get(), reference.get(), matte.get(), dst.get()}, &m, sizeof m, kW, kH);
        cb.commitAndWait();
        std::vector<__fp16> out(std::size_t(kW) * kH);
        dst->download(out.data(), std::size_t(kW) * sizeof(__fp16), kW, kH);
        return out;
    };
    const auto at = [&](const std::vector<__fp16>& a, int x, int y) {
        return float(a[std::size_t(y) * kW + x]);
    };

    // ── Linear ────────────────────────────────────────────────────────────
    {
        orion::pipe::params::MaskComponent m{};
        m.size[0] = kW; m.size[1] = kH;
        m.kind = 1;
        m.zero[0] = 0.0f; m.zero[1] = 0.0f;   // left edge
        m.full[0] = 1.0f; m.full[1] = 0.0f;   // right edge
        const auto a = run(m);

        report(at(a, 0, 32) < 0.01f && at(a, kW - 1, 32) > 0.99f,
               "a linear gradient is zero at Zero and full at Full",
               std::to_string(at(a, 0, 32)) + " … " + std::to_string(at(a, kW - 1, 32)));

        bool monotone = true;
        for (std::uint32_t x = 1; x < kW; ++x) {
            if (at(a, int(x), 32) < at(a, int(x) - 1, 32) - 1e-3f) monotone = false;
        }
        report(monotone, "and rises monotonically between them", "");

        // Perpendicular to the gradient direction nothing may change — that is
        // what makes it a *linear* gradient rather than a smear.
        double worstRow = 0.0;
        for (std::uint32_t x = 0; x < kW; ++x) {
            worstRow = std::max(worstRow,
                                std::abs(double(at(a, int(x), 4)) - double(at(a, int(x), 60))));
        }
        report(worstRow < 2e-3, "and is constant along its own perpendicular",
               "worst " + std::to_string(worstRow));

        // The midpoint of a smootherstep is a half, exactly. Cheap check that
        // the falloff is the one that was intended.
        report(std::abs(at(a, kW / 2, 32) - 0.5f) < 0.02f,
               "the falloff is symmetric about its midpoint",
               std::to_string(at(a, kW / 2, 32)));

        m.invert = 1;
        const auto inv = run(m);
        double worstInv = 0.0;
        for (std::uint32_t i = 0; i < kW * kH; ++i) {
            worstInv = std::max(worstInv,
                                std::abs(1.0 - double(a[i]) - double(inv[i])));
        }
        report(worstInv < 2e-3, "and inverting is exactly one minus it",
               "worst " + std::to_string(worstInv));
    }

    // ── Radial ────────────────────────────────────────────────────────────
    {
        orion::pipe::params::MaskComponent m{};
        m.size[0] = kW; m.size[1] = kH;
        m.kind = 2;
        // On a pixel centre, not on 0.5. With 64 pixels, 0.5 falls *between*
        // 31 and 32, so "twelve pixels either side" is not equidistant — and on
        // the steepest part of the feather that half-pixel is worth a quarter
        // of the alpha range. The asymmetry was in the test, not the shader.
        m.centre[0] = 32.5f / 64.0f; m.centre[1] = 32.5f / 64.0f;
        m.semi[0] = 0.25f; m.semi[1] = 0.25f;
        m.feather = 0.4f;
        m.roundness = 2.0f;
        const auto a = run(m);

        report(at(a, 32, 32) > 0.99f, "a radial gradient is full at its centre",
               std::to_string(at(a, 32, 32)));
        report(at(a, 0, 0) < 0.01f, "and zero well outside its boundary",
               std::to_string(at(a, 0, 0)));

        // Circular: equal radii means the alpha depends only on distance, so
        // the four cardinal points at one radius must agree.
        const float right = at(a, 32 + 12, 32), left = at(a, 32 - 12, 32);
        const float down  = at(a, 32, 32 + 12), up   = at(a, 32, 32 - 12);
        const double spread = std::max({std::abs(right - left), std::abs(right - down),
                                        std::abs(right - up)});
        report(spread < 2e-3, "and with equal radii depends only on distance",
               "spread " + std::to_string(spread));
    }

    // ── The alpha scales the parameter, not the result ────────────────────
    //
    // research/masking.md section 2 calls this the part most likely to be got
    // wrong, and the two candidates differ measurably: at alpha 0.5 with a
    // one-stop local exposure, scaling the parameter gives 2^0.5 = 1.414, while
    // blending two rendered results gives (1 + 2)/2 = 1.5. Six per cent apart,
    // and only one of them is a smooth multiplicative ramp in linear light.
    {
        auto kLinear = load("developLinear");

        auto src   = orion::gpu::Texture::create(*device, kW, 1, PixelFormat::RGBA16Float);
        auto guide = orion::gpu::Texture::create(*device, kW, 1, PixelFormat::RG32Float);
        auto mask  = orion::gpu::Texture::create(*device, kW, 1, PixelFormat::R16Float);
        auto out   = orion::gpu::Texture::create(*device, kW, 1, PixelFormat::RGBA16Float);

        std::vector<__fp16> in(std::size_t(kW) * 4);
        for (std::uint32_t x = 0; x < kW; ++x) {
            for (int c = 0; c < 3; ++c) in[x * 4 + c] = __fp16(0.25f);
            in[x * 4 + 3] = __fp16(1.0f);
        }
        src->upload(in.data(), std::size_t(kW) * 4 * sizeof(__fp16));

        // A ramp of coverage across the row.
        std::vector<__fp16> alpha(kW);
        for (std::uint32_t x = 0; x < kW; ++x) alpha[x] = __fp16(float(x) / float(kW - 1));
        mask->upload(alpha.data(), std::size_t(kW) * sizeof(__fp16));

        std::vector<float> guideData(std::size_t(kW) * 2, 0.0f);
        guide->upload(guideData.data(), std::size_t(kW) * 2 * sizeof(float));

        orion::pipe::params::LinearAdjust la{};
        la.size[0] = kW; la.size[1] = 1;
        la.guideEnabled = 0.0f;
        la.localExposureEv = 1.0f;
        la.maskActive = 1.0f;

        orion::gpu::CommandBuffer cb(*device);
        cb.dispatch(*kLinear.second,
                    {src.get(), guide.get(), guide.get(), mask.get(), out.get()},
                    &la, sizeof la, kW, 1);
        cb.commitAndWait();

        std::vector<__fp16> got(in.size());
        out->download(got.data(), std::size_t(kW) * 4 * sizeof(__fp16), kW, 1);

        double worstParam = 0.0, worstAgainstBlend = 0.0;
        for (std::uint32_t x = 0; x < kW; ++x) {
            const double a = double(x) / double(kW - 1);
            const double parameter = 0.25 * std::exp2(a * 1.0);   // what it should be
            const double blended   = 0.25 * (1.0 - a) + 0.5 * a;  // what it must not be
            const double v = double(got[x * 4]);
            worstParam = std::max(worstParam, std::abs(v - parameter));
            worstAgainstBlend = std::max(worstAgainstBlend, std::abs(v - blended));
        }
        report(worstParam < 2e-3,
               "a masked exposure scales the parameter: alpha 0.5 gives 2^0.5",
               "worst " + std::to_string(worstParam));
        report(worstAgainstBlend > 0.005,
               "and is measurably not a blend of two rendered results",
               "differs by " + std::to_string(worstAgainstBlend));

        // Coverage zero has to be the untouched pixel, or every mask would put
        // a faint edit over the whole frame.
        report(std::abs(double(got[0]) - 0.25) < 1e-3,
               "and zero coverage leaves the pixel exactly alone",
               std::to_string(double(got[0])));
    }
}

/// Brush dabs — a stroke as a list of centres. research/masking.md §1.
void testMaskBrushGpu() {
    section("Brush masks (GPU)");

    using orion::gpu::PixelFormat;
    constexpr std::uint32_t kW = 128, kH = 128;

    std::unique_ptr<orion::gpu::Device> device;
    try {
        device = orion::gpu::Device::create();
    } catch (const std::exception& e) {
        report(false, "Metal device available", e.what());
        return;
    }
    auto lib = orion::gpu::Library::createFromFile(
        *device, std::string(ORION_SHADER_DIR) + "/maskComponent.metallib");
    auto kernel = orion::gpu::Kernel::create(*device, *lib, "maskComponent");

    auto src = orion::gpu::Texture::create(*device, kW, kH, PixelFormat::R16Float);
    auto dst = orion::gpu::Texture::create(*device, kW, kH, PixelFormat::R16Float);
    // Bound but unread by kinds 1-3. The kernel takes a matte for kind 4
    // (research/masking.md §5) and the binding has to be present either way.
    auto matte = orion::gpu::Texture::create(*device, kW, kH, PixelFormat::R16Float);
    // Bound but unread by every kind except 5, which reads a luminance out of
    // it (research/masking.md §4b). The binding has to be present regardless.
    auto reference = orion::gpu::Texture::create(*device, kW, kH,
                                                 PixelFormat::RGBA16Float);

    // A fresh stroke folds over the group's identity, so `src` is zeroed before
    // each run unless a check is deliberately chaining passes.
    const std::vector<__fp16> zeroes(std::size_t(kW) * kH, __fp16(0.0f));
    src->upload(zeroes.data(), std::size_t(kW) * sizeof(__fp16));

    const auto run = [&](const orion::pipe::params::MaskComponent& b) {
        orion::gpu::CommandBuffer cb(*device);
        cb.dispatch(*kernel, {src.get(), reference.get(), matte.get(), dst.get()}, &b, sizeof b, kW, kH);
        cb.commitAndWait();
        std::vector<__fp16> out(std::size_t(kW) * kH);
        dst->download(out.data(), std::size_t(kW) * sizeof(__fp16), kW, kH);
        return out;
    };
    const auto at = [&](const std::vector<__fp16>& a, int x, int y) {
        return double(a[std::size_t(y) * kW + x]);
    };

    orion::pipe::params::MaskComponent base{};
    base.size[0] = kW; base.size[1] = kH;
    base.kind = 3;
    // 0.2 of the 128-pixel test frame. The nib is in frame pixels now,
    // so the checks below that compute an expected falloff from a
    // normalized distance stay valid on this square frame.
    base.nibPx = 0.2f * float(kW);
    base.flow = 1.0f;
    base.hardness = 0.5f;

    // ── One dab, against the falloff computed here ────────────────────────
    //
    // Not "the centre is bright and the outside is dark" — that passes on any
    // blob. The dab has to be the *stated* function, so it is checked against
    // smootherstep evaluated independently, at radii either side of where the
    // ramp starts.
    {
        auto b = base;
        b.count = 1;
        b.dabs[0][0] = 0.5f; b.dabs[0][1] = 0.5f;
        const auto a = run(b);

        const auto falloff = [](double t) {
            const double x = std::clamp(t, 0.0, 1.0);
            return x * x * x * (x * (x * 6.0 - 15.0) + 10.0);
        };
        const auto want = [&](double d) {
            const double h = 0.5;
            return 1.0 - falloff((d - h) / (1.0 - h));
        };

        double worst = 0;
        for (int px = 64; px < 64 + 25; ++px) {
            // Distance from the centre in units of the radius.
            const double d = ((px + 0.5) / double(kW) - 0.5) / 0.2;
            if (d >= 1.0) break;
            worst = std::max(worst, std::abs(at(a, px, 64) - want(d)));
        }
        report(worst < 2e-3, "a dab is smootherstep in the radius, not merely round",
               "worst " + std::to_string(worst));

        report(std::abs(at(a, 64, 64) - 1.0) < 1e-3,
               "full flow reaches full coverage at the centre");
        report(at(a, 4, 4) == 0.0,
               "and lays down exactly nothing outside its radius",
               std::to_string(at(a, 4, 4)));
    }

    // ── The one most likely to be got wrong ───────────────────────────────
    //
    // Dabs compose source-over, not additively. Two dabs at full flow on the
    // same spot must still be full coverage; adding would drive alpha past 1
    // and clip, which reads as the brush getting stronger the longer a hand
    // hovers. The check that catches it is a *partial* flow, where the two
    // rules give measurably different answers: over gives 0.5 + 0.5·0.5 =
    // 0.75, addition gives 1.0.
    {
        auto b = base;
        b.flow = 0.5f;
        b.count = 2;
        b.dabs[0][0] = 0.5f; b.dabs[0][1] = 0.5f;
        b.dabs[1][0] = 0.5f; b.dabs[1][1] = 0.5f;
        const auto a = run(b);
        report(std::abs(at(a, 64, 64) - 0.75) < 2e-3,
               "two half-flow dabs compose source-over, not by addition",
               std::to_string(at(a, 64, 64)));
    }

    // ── Chaining, which is what makes a long stroke possible ──────────────
    //
    // A stroke longer than one pass accumulates into the alpha it is handed.
    // If `accumulate` were ignored, a long stroke would silently keep only its
    // last 256 dabs — and would still look like a stroke.
    {
        auto b = base;
        b.flow = 0.5f;
        b.count = 1;
        b.dabs[0][0] = 0.5f; b.dabs[0][1] = 0.5f;
        const auto first = run(b);

        // Feed the result back in and lay the same dab again.
        src->upload(first.data(), std::size_t(kW) * sizeof(__fp16));
        b.accumulate = 1;
        const auto second = run(b);

        report(std::abs(at(second, 64, 64) - 0.75) < 2e-3,
               "a second pass builds on the first rather than replacing it",
               std::to_string(at(second, 64, 64)));

        // And a pass that lays nothing must leave the stroke untouched, or
        // every chained dispatch would erode what came before.
        //
        // ⚠️ `src` has to be fed the *second* result first. The first version
        // of this check forgot to, so it compared the pass-through of `first`
        // against `second` and failed — reporting a shader bug that was a
        // missing upload in the test.
        src->upload(second.data(), std::size_t(kW) * sizeof(__fp16));
        b.count = 0;
        const auto empty = run(b);

        // Every pixel, not one. A long stroke is several components chained
        // nose to tail, each continuing the alpha it is handed — so a
        // continuation pass that lays nothing must be exactly the identity, or
        // every chained dispatch would erode the stroke before it. Checking the
        // centre pixel alone would not notice: the centre is where a brush
        // writes most confidently and an edge is where an off-by-one shows up.
        std::size_t differing = 0;
        double worst = 0;
        for (std::size_t i = 0; i < empty.size(); ++i) {
            const double d = std::abs(double(empty[i]) - double(second[i]));
            if (d != 0.0) ++differing;
            worst = std::max(worst, d);
        }
        report(differing == 0,
               "an empty pass leaves the stroke bit-identical, every pixel",
               std::to_string(differing) + " of " + std::to_string(empty.size())
                   + " differ, worst " + std::to_string(worst));
    }

    // ── Flow zero is exactly nothing ──────────────────────────────────────
    {
        // The chaining check above left its stroke in `src`; this one starts a
        // fresh component over the group's identity.
        src->upload(zeroes.data(), std::size_t(kW) * sizeof(__fp16));
        auto b = base;
        b.flow = 0.0f;
        b.count = 8;
        for (int i = 0; i < 8; ++i) {
            b.dabs[i][0] = 0.3f + 0.05f * float(i);
            b.dabs[i][1] = 0.5f;
        }
        const auto a = run(b);
        double worst = 0;
        for (std::size_t i = 0; i < a.size(); ++i) worst = std::max(worst, double(a[i]));
        report(worst == 0.0, "flow zero lays down exactly nothing",
               std::to_string(worst));
    }

    // ── Why R16F and not R8, with the flow it actually starts to matter at ─
    //
    // The format claim is load-bearing, so it gets a number rather than an
    // assertion of principle.
    //
    // ⚠️ The first version of this check used flow 0.03 and claimed banding.
    // It does not band there and cannot: source-over moves alpha by about
    // 0.02 per dab, which is five to seven whole 8-bit codes, so all forty
    // steps resolve at eight bits and the test was demonstrating nothing while
    // reading like proof. The threshold is where one dab moves alpha less than
    // one code — flow · (1 − alpha) < 1/255, so flow below about 0.0039.
    //
    // 0.002 is below it and is an ordinary airbrush flow. There, consecutive
    // dabs land on the same 8-bit code and the buildup quantises; R16F still
    // resolves every one.
    {
        auto b = base;
        b.flow = 0.002f;
        b.count = 1;
        b.dabs[0][0] = 0.5f; b.dabs[0][1] = 0.5f;

        std::vector<__fp16> zero(std::size_t(kW) * kH, __fp16(0.0f));
        src->upload(zero.data(), std::size_t(kW) * sizeof(__fp16));
        b.accumulate = 1;

        std::set<int> levels8, levels16;
        double alpha = 0;
        for (int pass = 0; pass < 40; ++pass) {
            const auto a = run(b);
            src->upload(a.data(), std::size_t(kW) * sizeof(__fp16));
            alpha = at(a, 64, 64);
            levels16.insert(int(std::lround(alpha * 65535.0)));
            levels8.insert(int(std::lround(alpha * 255.0)));
        }
        report(levels16.size() == 40 && levels8.size() * 4 < levels16.size() * 3,
               "40 airbrush dabs resolve in R16F and quantise at 8 bits",
               std::to_string(levels16.size()) + " distinct in 16-bit, "
                   + std::to_string(levels8.size()) + " at 8-bit");
        // 1 - 0.998^40, computed independently of the shader.
        report(std::abs(alpha - (1.0 - std::pow(0.998, 40.0))) < 2e-3,
               "and the buildup is the source-over series, not something near it",
               std::to_string(alpha));
    }
}

/// Mask groups — components folding into one coverage. research/masking.md §6.
void testMaskCompositeGpu() {
    section("Mask groups (GPU)");

    using orion::gpu::PixelFormat;
    constexpr std::uint32_t kW = 64, kH = 64;

    std::unique_ptr<orion::gpu::Device> device;
    try {
        device = orion::gpu::Device::create();
    } catch (const std::exception& e) {
        report(false, "Metal device available", e.what());
        return;
    }
    auto lib = orion::gpu::Library::createFromFile(
        *device, std::string(ORION_SHADER_DIR) + "/maskComponent.metallib");
    auto kernel = orion::gpu::Kernel::create(*device, *lib, "maskComponent");

    auto src = orion::gpu::Texture::create(*device, kW, kH, PixelFormat::R16Float);
    auto dst = orion::gpu::Texture::create(*device, kW, kH, PixelFormat::R16Float);
    // Bound but unread by kinds 1-3. The kernel takes a matte for kind 4
    // (research/masking.md §5) and the binding has to be present either way.
    auto matte = orion::gpu::Texture::create(*device, kW, kH, PixelFormat::R16Float);
    // Bound but unread by every kind except 5, which reads a luminance out of
    // it (research/masking.md §4b). The binding has to be present regardless.
    auto reference = orion::gpu::Texture::create(*device, kW, kH,
                                                 PixelFormat::RGBA16Float);

    // Chains a list of components exactly as DevelopPipeline does: the fold
    // starts from zero and each pass reads the one before it.
    const auto fold = [&](const std::vector<orion::pipe::params::MaskComponent>& parts) {
        std::vector<__fp16> alpha(std::size_t(kW) * kH, __fp16(0.0f));
        for (const auto& p : parts) {
            src->upload(alpha.data(), std::size_t(kW) * sizeof(__fp16));
            orion::gpu::CommandBuffer cb(*device);
            cb.dispatch(*kernel, {src.get(), reference.get(), matte.get(), dst.get()}, &p, sizeof p, kW, kH);
            cb.commitAndWait();
            dst->download(alpha.data(), std::size_t(kW) * sizeof(__fp16), kW, kH);
        }
        return alpha;
    };
    const auto at = [&](const std::vector<__fp16>& a, int x, int y) {
        return double(a[std::size_t(y) * kW + x]);
    };

    // Two radials, deliberately overlapping and deliberately *partial* in the
    // overlap — the ops differ measurably only at fractional coverage, so full
    // coverage would pass on the wrong algebra. Wide feather keeps the region
    // between the centres in both falloffs.
    orion::pipe::params::MaskComponent A{};
    A.size[0] = kW; A.size[1] = kH;
    A.kind = 2;
    A.centre[0] = 0.375f; A.centre[1] = 0.5f;
    A.semi[0] = 0.35f; A.semi[1] = 0.35f;
    A.feather = 0.9f; A.roundness = 2.0f;

    auto B = A;
    B.centre[0] = 0.625f;

    // Each alone, for reference — the algebra below is checked against what the
    // kernel actually produces for the parts, not against a reimplementation of
    // the falloff.
    const auto a = fold({A});
    const auto b = fold({B});

    // A probe row through both centres, all in partial coverage.
    const int y = 32;

    {
        auto B2 = B; B2.compose = 0;    // add
        const auto got = fold({A, B2});
        double worstMax = 0.0, vsScreen = 0.0;
        for (int x = 8; x < 56; ++x) {
            const double va = at(a, x, y), vb = at(b, x, y);
            worstMax = std::max(worstMax,
                                std::abs(at(got, x, y) - std::max(va, vb)));
            // Screen is the research's own listed alternative, so it is the
            // failure shape to rule out by name: smoother accumulation, but a
            // visible strengthening where two components overlap.
            vsScreen = std::max(vsScreen,
                                std::abs(at(got, x, y) - (va + vb - va * vb)));
        }
        report(worstMax < 2e-3, "add is max: no buildup where components overlap",
               "worst " + std::to_string(worstMax));
        report(vsScreen > 0.05, "and is measurably not screen",
               "differs by " + std::to_string(vsScreen));
    }

    {
        auto B2 = B; B2.compose = 1;    // subtract
        const auto got = fold({A, B2});
        double worst = 0.0;
        for (int x = 8; x < 56; ++x) {
            const double want = at(a, x, y) * (1.0 - at(b, x, y));
            worst = std::max(worst, std::abs(at(got, x, y) - want));
        }
        report(worst < 2e-3, "subtract is alpha1 * (1 - alpha2)",
               "worst " + std::to_string(worst));
    }

    {
        auto B2 = B; B2.compose = 2;    // intersect
        const auto got = fold({A, B2});
        double worst = 0.0;
        for (int x = 8; x < 56; ++x) {
            const double want = at(a, x, y) * at(b, x, y);
            worst = std::max(worst, std::abs(at(got, x, y) - want));
        }
        report(worst < 2e-3, "intersect is alpha1 * alpha2",
               "worst " + std::to_string(worst));
    }

    // ── Order sensitivity, which the research states rather than implies ────
    //
    // Components fold left in listed order: A then subtract-B masks A's region
    // away from B; B-subtract then add-A is A alone, because subtracting from
    // the fold's zero start is zero. If a reorder ever stops changing the
    // answer, the fold has quietly become a merge.
    {
        auto Bsub = B; Bsub.compose = 1;
        const auto ab = fold({A, Bsub});
        const auto ba = fold({Bsub, A});
        double biggest = 0.0;
        for (int x = 8; x < 56; ++x) {
            biggest = std::max(biggest,
                               std::abs(at(ab, x, y) - at(ba, x, y)));
        }
        report(biggest > 0.05, "add then subtract is order-sensitive, as stated",
               "differs by " + std::to_string(biggest));
    }

    // ── Invert reaches the brush now ─────────────────────────────────────────
    //
    // It never did before: the gradient node held the invert, the brush node
    // discarded that node's output, so inverting a brush mask did nothing —
    // the fourth dead control of the class found in session 2026-07-29n. Pin
    // the fix: an inverted dab is full coverage far away and dips at the dab.
    {
        orion::pipe::params::MaskComponent s{};
        s.size[0] = kW; s.size[1] = kH;
        s.kind = 3;
        s.invert = 1;
        s.nibPx = 0.2f * float(kW); s.flow = 1.0f; s.hardness = 0.5f;
        s.count = 1;
        s.dabs[0][0] = 0.5f; s.dabs[0][1] = 0.5f;
        const auto got = fold({s});
        report(std::abs(at(got, 4, 4) - 1.0) < 1e-3 &&
               at(got, 32, 32) < 1e-3,
               "inverting a brush component inverts the stroke",
               std::to_string(at(got, 4, 4)) + " far, "
                   + std::to_string(at(got, 32, 32)) + " under the dab");
    }

    // ── A subtracted brush erases from a gradient ───────────────────────────
    //
    // The shape §6 exists for: a sky gradient with the treeline painted back
    // out. Under the dab the gradient must fall by exactly its own coverage
    // times the dab's; away from the dab it must not move at all.
    {
        orion::pipe::params::MaskComponent g{};
        g.size[0] = kW; g.size[1] = kH;
        g.kind = 1;
        g.zero[0] = 0.0f; g.zero[1] = 0.0f;
        g.full[0] = 1.0f; g.full[1] = 0.0f;

        orion::pipe::params::MaskComponent s{};
        s.size[0] = kW; s.size[1] = kH;
        s.kind = 3;
        s.compose = 1;   // subtract
        s.nibPx = 0.15f * float(kW); s.flow = 1.0f; s.hardness = 0.5f;
        s.count = 1;
        s.dabs[0][0] = 0.75f; s.dabs[0][1] = 0.5f;

        const auto grad = fold({g});
        const auto stroke = fold({[&]{ auto t = s; t.compose = 0; return t; }()});
        const auto got = fold({g, s});

        const int dabX = 48;
        const double want = at(grad, dabX, y) * (1.0 - at(stroke, dabX, y));
        report(std::abs(at(got, dabX, y) - want) < 2e-3,
               "a subtracted stroke erases the gradient under it",
               std::to_string(at(got, dabX, y)) + " vs " + std::to_string(want));
        report(std::abs(at(got, 8, y) - at(grad, 8, y)) < 1e-3,
               "and leaves it exactly alone elsewhere",
               std::to_string(std::abs(at(got, 8, y) - at(grad, 8, y))));
    }
}

/// A mask has to stay on its subject when the picture is turned or cropped.
/// A brush stroke has to go through the same transform a gradient's centre does.
///
/// It did not. `DevelopPipeline` ran the gradient centre through
/// `mask::toFrame` and copied the dab centres straight from displayed
/// coordinates into the shader, so a stroke ignored the crop and the rotation —
/// and because a portrait file carries an EXIF quarter turn, a stroke on one
/// landed mirrored and ninety degrees off with the rotate control untouched.
///
/// The gradients being right is what hid it, so the check is written as the
/// gradients' own invariant applied to a *stroke*: transform the points, and
/// they must sit where the same transform puts a gradient centre placed at each
/// of them. If the two ever disagree again, a mask's shapes have stopped
/// agreeing about where the picture is.
void testBrushDabsFollowTheFrame() {
    section("Brush dabs under crop and rotation");

    namespace mg = orion::pipe::mask;

    // An off-centre asymmetric stroke: a symmetric one survives a flip, and a
    // centred one survives a rotation, so neither would catch a mirrored axis.
    const float stroke[][2] = {{0.20f, 0.30f}, {0.35f, 0.32f}, {0.55f, 0.41f}};

    struct Case { const char* name; mg::Crop crop; int turns; float straighten; };
    const Case cases[] = {
        {"no crop, no turn",   mg::Crop{},                      0, 0.0f},
        {"one quarter turn",   mg::Crop{},                      1, 0.0f},
        {"three quarter turns",mg::Crop{},                      3, 0.0f},
        {"a tight crop",       mg::Crop{0.1f, 0.2f, 0.5f, 0.6f},0, 0.0f},
        {"crop and a turn",    mg::Crop{0.1f, 0.2f, 0.5f, 0.6f},1, 0.0f},
        {"and a straighten",   mg::Crop{0.1f, 0.2f, 0.5f, 0.6f},1, 0.05f},
    };

    for (const auto& c : cases) {
        const bool swaps = (c.turns % 2) != 0;
        const float rotW = swaps ? 4024.0f : 6024.0f;
        const float rotH = swaps ? 6024.0f : 4024.0f;
        const float pivotX = c.crop.x + c.crop.w * 0.5f;
        const float pivotY = c.crop.y + c.crop.h * 0.5f;

        double worst = 0.0;
        for (const auto& dab : stroke) {
            const auto asDab = mg::toFrame({dab[0], dab[1], 0.0f}, c.crop, c.turns,
                                           c.straighten, pivotX, pivotY, rotW, rotH);
            // The same point, placed as a gradient centre. One transform or two.
            const auto asCentre = mg::toFrame({dab[0], dab[1], 0.7f}, c.crop, c.turns,
                                              c.straighten, pivotX, pivotY, rotW, rotH);
            worst = std::max(worst,
                             std::max(std::abs(double(asDab.centreX - asCentre.centreX)),
                                      std::abs(double(asDab.centreY - asCentre.centreY))));
        }
        report(worst < 1e-6,
               std::string("a dab lands where a gradient centre does — ") + c.name,
               "worst " + std::to_string(worst));
    }

    // And the property that says the transform is the right one rather than
    // merely consistent: put a stroke where the subject appears, carry it
    // forward through the same rotation, and it must land back where it was put.
    for (int turns = 0; turns < 4; ++turns) {
        const bool swaps = (turns % 2) != 0;
        const float rotW = swaps ? 4024.0f : 6024.0f;
        const float rotH = swaps ? 6024.0f : 4024.0f;
        double worst = 0.0;
        for (const auto& dab : stroke) {
            const auto f = mg::toFrame({dab[0], dab[1], 0.0f}, mg::Crop{}, turns,
                                       0.0f, 0.5f, 0.5f, rotW, rotH);
            // Undo it by turning the other way the same number of times.
            float x = f.centreX, y = f.centreY;
            for (int i = 0; i < ((4 - turns) % 4); ++i) {
                const float nx = y, ny = 1.0f - x;
                x = nx; y = ny;
            }
            worst = std::max(worst, std::max(std::abs(double(x - dab[0])),
                                             std::abs(double(y - dab[1]))));
        }
        report(worst < 1e-6,
               "a stroke turned forward and back lands where it was painted, "
                   + std::to_string(turns) + " turns",
               "worst " + std::to_string(worst));
    }
}

void testMaskGeometry() {
    section("Mask geometry");

    namespace mg = orion::pipe::mask;
    const mg::Crop none{};

    // Nothing applied is nothing changed.
    {
        const auto p = mg::toFrame({0.3f, 0.7f, 0.5f}, none, 0);
        report(std::abs(p.centreX - 0.3f) < 1e-6f && std::abs(p.centreY - 0.7f) < 1e-6f &&
               std::abs(p.angle - 0.5f) < 1e-6f,
               "an unturned, uncropped frame leaves the placement alone", "");
    }

    // **The invariant that matters.** A quarter turn clockwise sends a frame
    // point (x, y) to (1 - y, x) on screen. So placing a mask where the subject
    // appears, then turning that placement forward again, must land back where
    // it was put — otherwise the mask slides off the subject the moment the
    // photograph is rotated, which is the whole reason this file exists.
    {
        const auto forward = [](float& x, float& y) {
            const float nx = 1.0f - y, ny = x;
            x = nx; y = ny;
        };

        double worst = 0.0;
        for (int turns = 0; turns < 4; ++turns) {
            for (const auto& pt : {std::pair{0.25f, 0.50f}, std::pair{0.10f, 0.90f},
                                   std::pair{0.75f, 0.30f}}) {
                const auto placed = mg::toFrame({pt.first, pt.second, 0.0f}, none, turns);
                float x = placed.centreX, y = placed.centreY;
                for (int i = 0; i < turns; ++i) forward(x, y);
                worst = std::max(worst, std::max(std::abs(double(x) - pt.first),
                                                 std::abs(double(y) - pt.second)));
            }
        }
        report(worst < 1e-6, "a placement survives being turned back through the rotation",
               "worst " + std::to_string(worst));
    }

    // Four turns is no turn.
    {
        const auto p = mg::toFrame({0.2f, 0.8f, 0.0f}, none, 4);
        report(std::abs(p.centreX - 0.2f) < 1e-6f && std::abs(p.centreY - 0.8f) < 1e-6f,
               "four quarter turns is the identity", "");
    }

    // The angle turns with the picture, or a linear gradient placed across the
    // frame would run down it after a rotation.
    {
        const auto p = mg::toFrame({0.5f, 0.5f, 0.0f}, none, 1);
        constexpr float kHalfPi = 1.57079632679489662f;
        report(std::abs(p.angle + kHalfPi) < 1e-6f,
               "one quarter turn takes ninety degrees off the angle",
               std::to_string(p.angle));
    }

    // A crop magnifies. The centre of a centred crop is the centre of the
    // frame, and its corner is the crop's corner — not the frame's.
    {
        const mg::Crop c{0.25f, 0.25f, 0.5f, 0.5f};
        const auto mid = mg::toFrame({0.5f, 0.5f, 0.0f}, c, 0);
        const auto corner = mg::toFrame({0.0f, 0.0f, 0.0f}, c, 0);
        report(std::abs(mid.centreX - 0.5f) < 1e-6f && std::abs(mid.centreY - 0.5f) < 1e-6f,
               "the middle of a centred crop is the middle of the frame", "");
        report(std::abs(corner.centreX - 0.25f) < 1e-6f &&
               std::abs(corner.centreY - 0.25f) < 1e-6f,
               "and its corner is the crop's corner, not the frame's", "");
    }

    // A gradient spanning half the visible width spans half the *crop*, which
    // is a smaller slice of the whole frame. Without this the feather widens
    // every time the picture is cropped tighter.
    {
        const mg::Crop c{0.25f, 0.25f, 0.5f, 0.5f};
        report(std::abs(mg::lengthToFrame(0.5f, c) - 0.25f) < 1e-6f,
               "a length shrinks with the crop it was measured against",
               std::to_string(mg::lengthToFrame(0.5f, c)));
        report(std::abs(mg::lengthToFrame(0.5f, none) - 0.5f) < 1e-6f,
               "and is untouched without one", "");
    }

    // ── Straighten ────────────────────────────────────────────────────────
    {
        constexpr float kDeg = 0.10471975512f;   // six degrees

        // The pivot itself cannot move, whatever the angle or the aspect.
        {
            float x = 0.4f, y = 0.6f;
            mg::unstraighten(x, y, kDeg, 0.4f, 0.6f, 6024.0f, 4024.0f);
            report(std::abs(x - 0.4f) < 1e-6f && std::abs(y - 0.6f) < 1e-6f,
                   "the straighten pivot is a fixed point", "");
        }

        // Rotating by an angle and then by its negative is the identity, which
        // is what says the transform is a rotation and not a shear.
        {
            double worst = 0.0;
            for (const auto& pt : {std::pair{0.2f, 0.3f}, std::pair{0.9f, 0.1f}}) {
                float x = pt.first, y = pt.second;
                mg::unstraighten(x, y,  kDeg, 0.5f, 0.5f, 6024.0f, 4024.0f);
                mg::unstraighten(x, y, -kDeg, 0.5f, 0.5f, 6024.0f, 4024.0f);
                worst = std::max(worst, std::max(std::abs(double(x) - pt.first),
                                                 std::abs(double(y) - pt.second)));
            }
            report(worst < 1e-6, "and undoing it returns the point exactly",
                   "worst " + std::to_string(worst));
        }

        // ⚠️ The shader rotates in the rotated frame's *pixels*, so the aspect
        // is part of the transform. If this were done in normalized space the
        // two would agree, and a mask on a 3:2 frame would drift.
        {
            float sq = 0.2f, sqy = 0.3f;
            mg::unstraighten(sq, sqy, kDeg, 0.5f, 0.5f, 1.0f, 1.0f);
            float wide = 0.2f, widey = 0.3f;
            mg::unstraighten(wide, widey, kDeg, 0.5f, 0.5f, 6024.0f, 4024.0f);
            report(std::abs(sq - wide) > 1e-4f || std::abs(sqy - widey) > 1e-4f,
                   "and a non-square frame rotates differently, as the shader does",
                   std::to_string(sq - wide) + ", " + std::to_string(sqy - widey));
        }

        // A straighten is a rotation, so it enters the mask's angle directly.
        {
            const auto p = mg::toFrame({0.5f, 0.5f, 0.0f}, none, 0,
                                       kDeg, 0.5f, 0.5f, 6024.0f, 4024.0f);
            report(std::abs(p.angle - kDeg) < 1e-6f,
                   "and it turns the mask's own angle with it",
                   std::to_string(p.angle));
        }
    }

    // ⚠️ This block used to assert the opposite, and the assertion was the bug.
    //
    // "Semi-axes have an axis each, so they swap when the picture goes on its
    // side" sounds right and is wrong: `toFrame` has already turned the mask by
    // subtracting k·π/2 from its angle, and the semi-axes are measured along
    // the mask's own axes. Swapping them as well applies the turn twice, which
    // put every radial mask in the wrong place on any frame at an odd total
    // quarter turn — including a portrait file with the rotate control never
    // touched, since its EXIF turn counts.
    //
    // The test passed for a year because it checked the transform against the
    // belief that produced it, never against the render. `repro/mask-alignment.txt`
    // checks it against the render, which is why it caught this.
    {
        float x = 0.0f, y = 0.0f;
        mg::radiusToFrame(0.4f, 0.1f, none, x, y);
        report(std::abs(x - 0.4f) < 1e-6f && std::abs(y - 0.1f) < 1e-6f,
               "radial semi-axes do not swap on a quarter turn — the angle "
               "already carries it",
               std::to_string(x) + ", " + std::to_string(y));

        // The crop does scale them, and per axis, because a crop is the one
        // part of the transform that is not rigid.
        mg::Crop tight{0.25f, 0.25f, 0.5f, 0.25f};
        mg::radiusToFrame(0.4f, 0.1f, tight, x, y);
        report(std::abs(x - 0.2f) < 1e-6f && std::abs(y - 0.025f) < 1e-6f,
               "and the crop scales each along its own axis",
               std::to_string(x) + ", " + std::to_string(y));
    }
}

// Spot removal — research/spot-removal.md.
//
// Two kernels, and the interesting one is the pair together: clone pastes the
// source unchanged, heal pastes it plus the boundary difference. The synthetic
// frame is built so that difference has an exact known value, which is what
// separates "the spot did something" from "the spot did the right thing".
void testSpotRemovalGpu() {
    section("Spot removal (GPU)");

    using orion::gpu::PixelFormat;
    namespace params = orion::pipe::params;
    constexpr std::uint32_t kW = 128, kH = 128;

    std::unique_ptr<orion::gpu::Device> device;
    try {
        device = orion::gpu::Device::create();
    } catch (const std::exception& e) {
        report(false, "Metal device available", e.what());
        return;
    }
    const std::string dir = std::string(ORION_SHADER_DIR) + "/";
    auto libM = orion::gpu::Library::createFromFile(*device, dir + "spotMeasure.metallib");
    auto libA = orion::gpu::Library::createFromFile(*device, dir + "spotApply.metallib");
    auto kM = orion::gpu::Kernel::create(*device, *libM, "spotMeasure");
    auto kA = orion::gpu::Kernel::create(*device, *libA, "spotApply");

    auto src  = orion::gpu::Texture::create(*device, kW, kH, PixelFormat::RGBA16Float);
    auto corr = orion::gpu::Texture::create(*device, params::kMaxSpots, 1,
                                            PixelFormat::RGBA32Float);
    auto dst  = orion::gpu::Texture::create(*device, kW, kH, PixelFormat::RGBA16Float);

    // Two flat fields of different brightness, left and right, with a blemish
    // painted on the left one. Flat on purpose: the constant correction is the
    // *exact* answer when the boundary difference is constant, so the numbers
    // below are the algorithm's, not a tolerance around a picture.
    constexpr float kLeft = 0.30f, kRight = 0.50f, kBlemish = 0.05f;
    const auto paint = [&]() {
        std::vector<__fp16> px(std::size_t(kW) * kH * 4);
        for (std::uint32_t y = 0; y < kH; ++y) {
            for (std::uint32_t x = 0; x < kW; ++x) {
                float v = (x < kW / 2) ? kLeft : kRight;
                // The blemish, centred at (0.25, 0.5) with radius 0.06 in x.
                const double dx = (x + 0.5) / kW - 0.25;
                const double dy = ((y + 0.5) / kH - 0.5) * double(kH) / double(kW);
                if (dx * dx + dy * dy < 0.06 * 0.06) v = kBlemish;
                const std::size_t i = (std::size_t(y) * kW + x) * 4;
                px[i + 0] = __fp16(v); px[i + 1] = __fp16(v);
                px[i + 2] = __fp16(v); px[i + 3] = __fp16(1.0f);
            }
        }
        src->upload(px.data(), std::size_t(kW) * 4 * sizeof(__fp16));
    };
    paint();

    const auto run = [&](float destX, float srcX, float radius, bool heal) {
        params::SpotMeasure sm{};
        sm.size[0] = kW; sm.size[1] = kH;
        sm.count = 1; sm.samples = 32;
        params::SpotApply sa{};
        sa.size[0] = kW; sa.size[1] = kH;
        sa.count = 1;
        const float sp[4] = {destX, 0.5f, srcX, 0.5f};
        const float sh[4] = {radius, 0.25f, heal ? 1.0f : 0.0f, 0.0f};
        for (int k = 0; k < 4; ++k) {
            sm.spots[0][k] = sp[k]; sm.shape[0][k] = sh[k];
            sa.spots[0][k] = sp[k]; sa.shape[0][k] = sh[k];
        }

        orion::gpu::CommandBuffer cb(*device);
        cb.dispatch(*kM, {src.get(), corr.get()}, &sm, sizeof sm, params::kMaxSpots, 1);
        cb.dispatch(*kA, {src.get(), corr.get(), dst.get()}, &sa, sizeof sa, kW, kH);
        cb.commitAndWait();
        std::vector<__fp16> out(std::size_t(kW) * kH * 4);
        dst->download(out.data(), std::size_t(kW) * 4 * sizeof(__fp16), kW, kH);
        return out;
    };
    const auto at = [&](const std::vector<__fp16>& v, double u, double w) {
        const auto x = std::uint32_t(u * kW), y = std::uint32_t(w * kH);
        return double(v[(std::size_t(y) * kW + x) * 4]);
    };

    // ── Clone pastes the source exactly ───────────────────────────────────
    //
    // Destination on the blemish, source on the right-hand field. Clone takes
    // the right field's value verbatim, which is visibly wrong against the
    // left field around it — and being visibly wrong is the point of clone
    // existing separately from heal.
    {
        const auto got = run(0.25f, 0.75f, 0.08f, /*heal=*/false);
        report(std::abs(at(got, 0.25, 0.5) - kRight) < 0.01,
               "clone pastes the source's own value, unmodified",
               std::to_string(at(got, 0.25, 0.5)));
    }

    // ── Heal takes the destination's tone ─────────────────────────────────
    //
    // ⚠ The exact answer, not a range. The rim of the destination disc sits on
    // the left field at 0.30 and the matching rim on the source sits on the
    // right field at 0.50, so the boundary difference is exactly -0.20 and the
    // healed patch is 0.50 - 0.20 = 0.30: the left field, with the blemish
    // gone. A correction computed from the disc's *interior* instead of its rim
    // would measure the blemish and land near 0.05.
    {
        const auto got = run(0.25f, 0.75f, 0.08f, /*heal=*/true);
        report(std::abs(at(got, 0.25, 0.5) - kLeft) < 0.01,
               "heal pastes the source's detail with the destination's tone",
               std::to_string(at(got, 0.25, 0.5)));

        // And the blemish is actually gone — the check above would also pass if
        // the patch simply were not applied, since the field it lands on is the
        // same 0.30.
        report(std::abs(at(got, 0.25, 0.5) - kBlemish) > 0.2,
               "and the blemish underneath it is replaced, not left showing",
               std::to_string(at(got, 0.25, 0.5)));
    }

    // ── Nothing outside the disc is touched ───────────────────────────────
    {
        const auto got = run(0.25f, 0.75f, 0.08f, true);
        report(std::abs(at(got, 0.60, 0.5) - kRight) < 1e-3 &&
               std::abs(at(got, 0.05, 0.1) - kLeft) < 1e-3,
               "and the rest of the frame is left alone",
               std::to_string(at(got, 0.60, 0.5)) + ", " + std::to_string(at(got, 0.05, 0.1)));
    }

    // ── The rim is measured outside the disc, not inside it ───────────────
    //
    // ⚠ The check that pins where the boundary is sampled. With the source
    // placed on the *same* field as the destination, a correct rim measurement
    // gives a difference of zero and heal reduces to clone — both paste 0.30.
    // Sampling the interior instead would put the blemish into the source term,
    // give a correction of about +0.25, and blow the patch out to 0.55.
    {
        const auto got = run(0.25f, 0.10f, 0.06f, true);
        report(at(got, 0.25, 0.5) < 0.40,
               "the boundary is sampled outside the disc, so a same-field heal "
               "is not inflated by the blemish it is covering",
               std::to_string(at(got, 0.25, 0.5)));
    }

    // ── The patch carries the source's DETAIL, not just its level ─────────
    //
    // ⚠ Added because a mutation survived. Every case above uses flat fields,
    // so sampling the source once at its centre gives the same answer as
    // sampling it per pixel — and replacing the per-pixel lookup with the
    // centre's passed the whole suite. Copying detail is the entire reason
    // clone and heal exist rather than "fill with a colour".
    {
        // Fine stripes in the source half, flat in the destination half.
        std::vector<__fp16> px(std::size_t(kW) * kH * 4);
        for (std::uint32_t y = 0; y < kH; ++y) {
            for (std::uint32_t x = 0; x < kW; ++x) {
                const float v = (x < kW / 2) ? kLeft
                                             : ((y / 4) % 2 == 0 ? 0.65f : 0.35f);
                const std::size_t i = (std::size_t(y) * kW + x) * 4;
                px[i + 0] = __fp16(v); px[i + 1] = __fp16(v);
                px[i + 2] = __fp16(v); px[i + 3] = __fp16(1.0f);
            }
        }
        src->upload(px.data(), std::size_t(kW) * 4 * sizeof(__fp16));

        const auto got = run(0.25f, 0.75f, 0.10f, /*heal=*/false);

        // Walk down the middle of the patch. A per-pixel source reproduces the
        // stripes; a single centre sample gives one flat value.
        double lo = 1.0, hi = 0.0;
        for (std::uint32_t y = kH / 2 - 8; y <= kH / 2 + 8; ++y) {
            const double v = double(got[(std::size_t(y) * kW + kW / 4) * 4]);
            lo = std::min(lo, v); hi = std::max(hi, v);
        }
        report(hi - lo > 0.2,
               "the patch reproduces the source's texture rather than one "
               "averaged value",
               "range " + std::to_string(hi - lo));

        paint();   // restore the flat frame for anything after this
    }

    // ── A spot is a disc on the picture, not an ellipse ───────────────────
    //
    // The frame here is square, so this uses a deliberately non-square one: the
    // same trap the brush nib fell into, where a radius measured in normalized
    // coordinates stretched with the frame's aspect instead of staying round.
    {
        constexpr std::uint32_t wW = 192, wH = 96;
        auto wide = orion::gpu::Texture::create(*device, wW, wH, PixelFormat::RGBA16Float);
        auto wout = orion::gpu::Texture::create(*device, wW, wH, PixelFormat::RGBA16Float);
        std::vector<__fp16> px(std::size_t(wW) * wH * 4, __fp16(0.4f));
        for (std::size_t i = 3; i < px.size(); i += 4) px[i] = __fp16(1.0f);
        wide->upload(px.data(), std::size_t(wW) * 4 * sizeof(__fp16));

        params::SpotApply sa{};
        sa.size[0] = wW; sa.size[1] = wH;
        sa.count = 1;
        sa.spots[0][0] = 0.5f; sa.spots[0][1] = 0.5f;
        sa.spots[0][2] = 0.5f; sa.spots[0][3] = 0.5f;
        sa.shape[0][0] = 0.10f; sa.shape[0][1] = 0.0f; sa.shape[0][2] = 0.0f;

        orion::gpu::CommandBuffer cb(*device);
        cb.dispatch(*kA, {wide.get(), corr.get(), wout.get()}, &sa, sizeof sa, wW, wH);
        cb.commitAndWait();

        // The disc's extent is measured by how far the coverage reaches, which
        // needs the patch to differ from the frame — so the source is offset
        // and the frame given a ramp. Simpler: count the pixels the kernel
        // treats as inside, by running a clone from a distinct source.
        std::vector<__fp16> ramp(std::size_t(wW) * wH * 4);
        for (std::uint32_t y = 0; y < wH; ++y) {
            for (std::uint32_t x = 0; x < wW; ++x) {
                const std::size_t i = (std::size_t(y) * wW + x) * 4;
                const float v = (x < wW / 2) ? 0.2f : 0.8f;
                ramp[i + 0] = __fp16(v); ramp[i + 1] = __fp16(v);
                ramp[i + 2] = __fp16(v); ramp[i + 3] = __fp16(1.0f);
            }
        }
        wide->upload(ramp.data(), std::size_t(wW) * 4 * sizeof(__fp16));
        sa.spots[0][0] = 0.25f; sa.spots[0][2] = 0.75f;
        orion::gpu::CommandBuffer cb2(*device);
        cb2.dispatch(*kA, {wide.get(), corr.get(), wout.get()}, &sa, sizeof sa, wW, wH);
        cb2.commitAndWait();

        std::vector<__fp16> out(std::size_t(wW) * wH * 4);
        wout->download(out.data(), std::size_t(wW) * 4 * sizeof(__fp16), wW, wH);
        const auto val = [&](std::uint32_t x, std::uint32_t y) {
            return double(out[(std::size_t(y) * wW + x) * 4]);
        };

        // Half-width and half-height of the changed region, in pixels. A disc
        // has them equal; an ellipse stretched by the 2:1 aspect does not.
        std::uint32_t cx = wW / 4, cy = wH / 2;
        int halfW = 0, halfH = 0;
        while (cx + std::uint32_t(halfW) + 1 < wW && val(cx + halfW + 1, cy) > 0.5) ++halfW;
        while (cy + std::uint32_t(halfH) + 1 < wH && val(cx, cy + halfH + 1) > 0.5) ++halfH;
        report(halfW > 4 && std::abs(halfW - halfH) <= 2,
               "a spot is round on a 2:1 frame, not stretched by its aspect",
               std::to_string(halfW) + " x " + std::to_string(halfH) + " px");
    }
}

// A luminance range mask — research/masking.md §4b.
//
// A band on what a pixel *is*. Three decisions to pin, and the shader's own
// comments name all three: it reads the reference image, it measures in stops,
// and its two edges are independent.
void testMaskRangeGpu() {
    section("Range masks (GPU)");

    using orion::gpu::PixelFormat;
    namespace params = orion::pipe::params;
    constexpr std::uint32_t kW = 64, kH = 64;

    std::unique_ptr<orion::gpu::Device> device;
    try {
        device = orion::gpu::Device::create();
    } catch (const std::exception& e) {
        report(false, "Metal device available", e.what());
        return;
    }
    auto lib = orion::gpu::Library::createFromFile(
        *device, std::string(ORION_SHADER_DIR) + "/maskComponent.metallib");
    auto kernel = orion::gpu::Kernel::create(*device, *lib, "maskComponent");

    auto src   = orion::gpu::Texture::create(*device, kW, kH, PixelFormat::R16Float);
    auto dst   = orion::gpu::Texture::create(*device, kW, kH, PixelFormat::R16Float);
    auto matte = orion::gpu::Texture::create(*device, kW, kH, PixelFormat::R16Float);
    auto reference = orion::gpu::Texture::create(*device, kW, kH,
                                                 PixelFormat::RGBA16Float);

    const std::vector<__fp16> zeroes(std::size_t(kW) * kH, __fp16(0.0f));

    // A neutral ramp across x, one stop per column band: column x carries
    // luminance 2^(x/8 - 4), so the frame spans -4 to +4 stops.
    const auto evAt = [&](int x) { return double(x) / 8.0 - 4.0; };
    {
        std::vector<__fp16> ref(std::size_t(kW) * kH * 4, __fp16(0.0f));
        for (std::uint32_t y = 0; y < kH; ++y) {
            for (std::uint32_t x = 0; x < kW; ++x) {
                // Neutral, so Rec.2020's coefficients sum to one and the
                // luminance is exactly the channel value — the test is about
                // the band, not about the luma weights.
                const float v = float(std::exp2(evAt(int(x))));
                const std::size_t i = (std::size_t(y) * kW + x) * 4;
                ref[i + 0] = __fp16(v); ref[i + 1] = __fp16(v);
                ref[i + 2] = __fp16(v); ref[i + 3] = __fp16(1.0f);
            }
        }
        reference->upload(ref.data(), std::size_t(kW) * 4 * sizeof(__fp16));
    }

    const auto run = [&](const params::MaskComponent& m) {
        src->upload(zeroes.data(), std::size_t(kW) * sizeof(__fp16));
        orion::gpu::CommandBuffer cb(*device);
        cb.dispatch(*kernel, {src.get(), reference.get(), matte.get(), dst.get()},
                    &m, sizeof m, kW, kH);
        cb.commitAndWait();
        std::vector<__fp16> out(std::size_t(kW) * kH);
        dst->download(out.data(), std::size_t(kW) * sizeof(__fp16), kW, kH);
        return out;
    };
    const auto at = [&](const std::vector<__fp16>& a, int x) {
        return double(a[std::size_t(kH / 2) * kW + std::size_t(x)]);
    };

    params::MaskComponent base{};
    base.size[0] = kW; base.size[1] = kH;
    base.kind = 5;
    base.rangeLo = -2.0f; base.rangeHi = 1.0f; base.rangeSoft = 0.5f;

    // ── The band sits where the stops say, not where the linear values do ──
    //
    // ⚠ This is the assertion that separates a log band from a linear one, and
    // the two are not subtly different — they are different by orders of
    // magnitude. The band -2..+1 stops is luminance 0.25..2.0; its midpoint in
    // *stops* is -0.5 (luminance 0.707), while the midpoint of the same
    // interval in *linear* light is 1.125, which is +0.17 stops. A linear
    // implementation puts the plateau's centre 0.67 stops to the right of where
    // this checks it.
    {
        const auto got = run(base);

        // Full inside, well clear of both ramps.
        const int inside = int(std::lround((-0.5 + 4.0) * 8.0));
        report(at(got, inside) > 0.99, "the band is full in its middle",
               std::to_string(at(got, inside)));

        // Zero outside, well clear of both ramps.
        const int below = int(std::lround((-3.5 + 4.0) * 8.0));
        const int above = int(std::lround((2.5 + 4.0) * 8.0));
        report(at(got, below) < 0.01 && at(got, above) < 0.01,
               "and zero outside it at both ends",
               std::to_string(at(got, below)) + ", " + std::to_string(at(got, above)));

        // The edges are where the stops put them: half coverage half a ramp in.
        const int loHalf = int(std::lround((-2.0 + 0.25 + 4.0) * 8.0));
        const int hiHalf = int(std::lround((1.0 - 0.25 + 4.0) * 8.0));
        report(std::abs(at(got, loHalf) - 0.5) < 0.06 &&
               std::abs(at(got, hiHalf) - 0.5) < 0.06,
               "with each edge at half coverage half a ramp inside it",
               std::to_string(at(got, loHalf)) + ", " + std::to_string(at(got, hiHalf)));

        // ⚠ A band in linear light would have its plateau centred here instead.
        // Asserting the difference rather than trusting the comment.
        const int linearMid = int(std::lround((std::log2(1.125) + 4.0) * 8.0));
        report(std::abs(linearMid - inside) > 4,
               "and the log midpoint is nowhere near the linear one",
               "stops " + std::to_string(inside) + " vs linear "
               + std::to_string(linearMid));
    }

    // ── The two edges are independent ─────────────────────────────────────
    //
    // Pushing one past the frame's range turns the band into a one-sided
    // selection, which is how "the highlights" and "the shadows" are expressed
    // without a second control.
    {
        auto highs = base;
        highs.rangeLo = 0.0f; highs.rangeHi = 99.0f;
        const auto got = run(highs);
        report(at(got, kW - 1) > 0.99 && at(got, 0) < 0.01,
               "a high edge past the frame's range makes it a highlight selection",
               std::to_string(at(got, 0)) + " -> " + std::to_string(at(got, kW - 1)));

        auto lows = base;
        lows.rangeLo = -99.0f; lows.rangeHi = 0.0f;
        const auto shadow = run(lows);
        report(at(shadow, 0) > 0.99 && at(shadow, kW - 1) < 0.01,
               "and a low edge past it makes a shadow selection",
               std::to_string(at(shadow, 0)) + " -> " + std::to_string(at(shadow, kW - 1)));
    }

    // ── Monotone, and smooth enough to be C² ──────────────────────────────
    //
    // The falloff is shared with every other mask, so what is checked here is
    // that the range branch actually uses it: a linear ramp would be monotone
    // too, but its second difference jumps at the ends of the ramp instead of
    // going to zero.
    {
        auto lows = base;
        lows.rangeLo = -99.0f; lows.rangeHi = 0.0f; lows.rangeSoft = 2.0f;
        const auto got = run(lows);

        bool monotone = true;
        for (std::uint32_t x = 1; x < kW; ++x) {
            if (at(got, int(x)) > at(got, int(x - 1)) + 1e-3) { monotone = false; break; }
        }
        report(monotone, "a single edge is monotone across the whole frame");

        // ⚠ At the *foot* of its own ramp smootherstep is flat to second
        // order; a straight line is not. The first version of this check
        // computed the ramp's position from a constant instead of from the
        // band actually being run — with rangeLo at -99 there is no ramp within
        // sixty columns of where it sampled, so it measured a flat plateau and
        // passed for any falloff whatever. A linear-ramp mutation survived it.
        //
        // The edge here is `fall = smootherstep((rangeHi - ev) / soft)` with
        // rangeHi = 0 and soft = 2, so it is full at -2 stops and zero at 0 —
        // the ramp spans [-2, 0] and its foot is at 0, not at +2. Getting that
        // wrong is what let the linear-falloff mutation through the first time.
        const int rampFoot = int(std::lround((0.0 + 4.0) * 8.0));
        const double drop = at(got, rampFoot - 2) - at(got, rampFoot);
        report(drop < 0.02 && drop >= 0,
               "and it approaches the foot of its ramp flat, which a straight "
               "line would not",
               std::to_string(drop));

        // The same at the ramp's midpoint, where smootherstep is steepest and a
        // straight line is not — the pair together is what pins the shape
        // rather than merely the endpoints.
        const int rampMid = int(std::lround((-1.0 + 4.0) * 8.0));
        const double steep = at(got, rampMid - 2) - at(got, rampMid + 2);
        report(steep > 0.25, "and steepest in the middle of it",
               std::to_string(steep));
    }

    // ── A band narrow enough for its two ramps to overlap ─────────────────
    //
    // ⚠ Added because a mutation survived. With a wide band the two edges never
    // both sit part-way, so `rise * fall` and `rise + fall - 1` agree
    // everywhere and the product could be replaced by the sum unnoticed. They
    // differ only where both edges are partial — a thin luminance slice, which
    // is exactly what someone reaches for to isolate a narrow tone.
    {
        auto narrow = base;
        narrow.rangeLo = -0.5f; narrow.rangeHi = 0.5f; narrow.rangeSoft = 1.0f;
        const auto got = run(narrow);

        // At the band's centre each edge is exactly half its ramp in, so
        // smootherstep gives 0.5 apiece: the product is 0.25 and the
        // sum-minus-one is 0.
        const int centre = int(std::lround((0.0 + 4.0) * 8.0));
        const double peak = at(got, centre);
        report(peak > 0.15 && peak < 0.4,
               "overlapping edges multiply rather than add, so a narrow band "
               "is partial everywhere instead of empty",
               std::to_string(peak));
    }

    // ── Invert and compose reach kind 5 too ───────────────────────────────
    {
        auto inv = base;
        inv.invert = 1;
        const auto plain = run(base);
        const auto flipped = run(inv);
        double worst = 0.0;
        for (std::uint32_t x = 0; x < kW; ++x) {
            worst = std::max(worst, std::abs((1.0 - at(plain, int(x)))
                                             - at(flipped, int(x))));
        }
        report(worst < 2e-3, "invert reaches a range component",
               "worst " + std::to_string(worst));
    }
}

// A raster mask component — research/masking.md §5, the shape a segmentation
// matte arrives in.
//
// Vision's own output cannot be unit-tested: it is a model whose result moves
// between OS releases, and "did it find the subject" is not a property this
// suite can assert. What *can* be pinned is everything between the matte and
// the picture, which is where the silent failures live — a half-texel offset in
// the lift, a live rectangle ignored, invert or compose skipping the new kind
// the way they once skipped the brush.
void testMaskMatteGpu() {
    section("Matte masks (GPU)");

    using orion::gpu::PixelFormat;
    namespace params = orion::pipe::params;
    constexpr std::uint32_t kW = 64, kH = 64;
    constexpr std::uint32_t kM = 8;          // the matte texture's allocation

    std::unique_ptr<orion::gpu::Device> device;
    try {
        device = orion::gpu::Device::create();
    } catch (const std::exception& e) {
        report(false, "Metal device available", e.what());
        return;
    }
    auto lib = orion::gpu::Library::createFromFile(
        *device, std::string(ORION_SHADER_DIR) + "/maskComponent.metallib");
    auto kernel = orion::gpu::Kernel::create(*device, *lib, "maskComponent");

    auto src   = orion::gpu::Texture::create(*device, kW, kH, PixelFormat::R16Float);
    auto dst   = orion::gpu::Texture::create(*device, kW, kH, PixelFormat::R16Float);
    auto matte = orion::gpu::Texture::create(*device, kM, kM, PixelFormat::R16Float);
    auto reference = orion::gpu::Texture::create(*device, kW, kH,
                                                 PixelFormat::RGBA16Float);

    const std::vector<__fp16> zeroes(std::size_t(kW) * kH, __fp16(0.0f));

    // The matte texture is allocated for the largest matte a producer might
    // hand over, and a smaller one lands in its top-left corner — exactly as
    // DevelopPipeline::setMaskMatte does it. `fill` writes junk outside the live
    // rectangle on purpose: if the kernel ever reads past `matteSize`, that junk
    // is what tells us.
    const auto upload = [&](int lw, int lh, const std::vector<float>& live,
                            float outside) {
        std::vector<__fp16> tex(std::size_t(kM) * kM, __fp16(outside));
        for (int y = 0; y < lh; ++y) {
            for (int x = 0; x < lw; ++x) {
                tex[std::size_t(y) * kM + std::size_t(x)] =
                    __fp16(live[std::size_t(y) * std::size_t(lw) + std::size_t(x)]);
            }
        }
        matte->upload(tex.data(), std::size_t(kM) * sizeof(__fp16));
    };

    const auto run = [&](const params::MaskComponent& m) {
        src->upload(zeroes.data(), std::size_t(kW) * sizeof(__fp16));
        orion::gpu::CommandBuffer cb(*device);
        cb.dispatch(*kernel, {src.get(), reference.get(), matte.get(), dst.get()}, &m, sizeof m, kW, kH);
        cb.commitAndWait();
        std::vector<__fp16> out(std::size_t(kW) * kH);
        dst->download(out.data(), std::size_t(kW) * sizeof(__fp16), kW, kH);
        return out;
    };
    const auto at = [&](const std::vector<__fp16>& a, int x, int y) {
        return double(a[std::size_t(y) * kW + std::size_t(x)]);
    };

    params::MaskComponent base{};
    base.size[0] = kW; base.size[1] = kH;
    base.kind = 4;
    base.matteSize[0] = 2; base.matteSize[1] = 1;

    // ── The half-texel convention, which is the whole risk ────────────────
    //
    // A 2 x 1 matte of [0, 1] is a ramp whose answer is analytic under the
    // stated convention — samples at pixel *centres*, clamped at the edge
    // texels. The texel centres sit at normalized x = 0.25 and 0.75, so the
    // output is flat 0 left of 0.25, flat 1 right of 0.75, and linear between,
    // passing through exactly 0.5 at the middle.
    //
    // Every plausible off-by-half convention breaks one of those three: drop
    // the -0.5 and the ramp shifts a quarter of the frame; sample at texel
    // corners and the midpoint stops being 0.5; forget the clamp and the ends
    // wrap or extrapolate.
    {
        upload(2, 1, {0.0f, 1.0f}, 0.75f);
        const auto got = run(base);

        // Mid-frame, by symmetry, exactly half.
        const double mid = 0.5 * (at(got, 31, 32) + at(got, 32, 32));
        report(std::abs(mid - 0.5) < 2e-3,
               "a matte is lifted about its texel centres, so a two-texel ramp "
               "is exactly half way across",
               std::to_string(mid));

        // Flat outside the texel centres, at both ends.
        report(at(got, 0, 32) < 2e-3 && at(got, 15, 32) < 2e-3,
               "flat at the value of the first texel left of its centre",
               std::to_string(at(got, 0, 32)) + ", " + std::to_string(at(got, 15, 32)));
        report(at(got, 48, 32) > 1.0 - 2e-3 && at(got, 63, 32) > 1.0 - 2e-3,
               "and flat at the last texel's value right of its centre",
               std::to_string(at(got, 48, 32)) + ", " + std::to_string(at(got, 63, 32)));

        // A quarter of the way from one centre to the other is a quarter up.
        const double q = at(got, 24, 32);
        report(std::abs(q - 0.25) < 0.02, "and linear between them",
               std::to_string(q));

        // ⚠ Nothing outside the live rectangle leaks in. The texture is 8 x 8
        // and holds 0.75 everywhere the 2 x 1 matte does not; if any of that
        // were read, the ramp's ends could not be 0 and 1.
        report(at(got, 0, 0) < 2e-3 && at(got, 63, 63) > 1.0 - 2e-3,
               "and the rows past the live rectangle are never sampled",
               std::to_string(at(got, 0, 0)) + ", " + std::to_string(at(got, 63, 63)));
    }

    // ── The edge clamp, with a first texel that is not zero ───────────────
    //
    // ⚠ Added because a mutation survived. The ramp above starts at 0, and an
    // out-of-bounds texture read on Metal also returns 0 — so deleting the
    // clamp on the sample coordinate changed nothing anywhere the test looked,
    // and the suite reported a clean run on a kernel that reads outside its
    // matte. A first texel of 0.25 makes the two answers differ: clamped gives
    // 0.25 at the left edge, unclamped blends it toward the out-of-bounds zero
    // and gives about 0.13.
    {
        upload(2, 1, {0.25f, 1.0f}, 0.0f);
        const auto got = run(base);
        report(std::abs(at(got, 0, 32) - 0.25) < 3e-3,
               "the sample coordinate is clamped to the edge texel, so the "
               "border is the matte's own value and not the void past it",
               std::to_string(at(got, 0, 32)));
    }

    // ── A matte with no live rectangle contributes nothing ────────────────
    //
    // Zero means "no matte uploaded", and a component in that state has to
    // behave like a component switched off — not sample an empty texture and
    // not cover the frame.
    {
        upload(2, 1, {0.0f, 1.0f}, 1.0f);
        auto m = base;
        m.matteSize[0] = 0; m.matteSize[1] = 0;
        const auto got = run(m);
        double worst = 0.0;
        for (std::uint32_t y = 0; y < kH; ++y) {
            for (std::uint32_t x = 0; x < kW; ++x) {
                worst = std::max(worst, at(got, int(x), int(y)));
            }
        }
        report(worst < 1e-4, "an empty matte covers nothing at all",
               std::to_string(worst));
    }

    // ── Invert and compose reach the new kind ─────────────────────────────
    //
    // ⚠ This is the check that exists because of history. When the brush was
    // added, invert was held by the gradient node and the brush node discarded
    // its output, so invert silently did nothing to a stroke — a dead control
    // nobody noticed for a session. A fourth kind is exactly the shape of that
    // mistake repeating.
    {
        upload(2, 1, {0.0f, 1.0f}, 0.0f);
        const auto plain = run(base);
        auto inv = base;
        inv.invert = 1;
        const auto inverted = run(inv);

        double worst = 0.0;
        for (std::uint32_t x = 0; x < kW; ++x) {
            worst = std::max(worst, std::abs((1.0 - at(plain, int(x), 32))
                                             - at(inverted, int(x), 32)));
        }
        report(worst < 2e-3, "invert reaches a matte component",
               "worst " + std::to_string(worst));
    }
    {
        // Subtract a matte from full coverage: the fold's input is 1 here
        // rather than the usual 0, so what is measured is the op and not the
        // component alone.
        upload(2, 1, {0.0f, 1.0f}, 0.0f);
        const std::vector<__fp16> ones(std::size_t(kW) * kH, __fp16(1.0f));
        auto m = base;
        m.compose = 1;   // subtract

        src->upload(ones.data(), std::size_t(kW) * sizeof(__fp16));
        orion::gpu::CommandBuffer cb(*device);
        cb.dispatch(*kernel, {src.get(), reference.get(), matte.get(), dst.get()}, &m, sizeof m, kW, kH);
        cb.commitAndWait();
        std::vector<__fp16> got(std::size_t(kW) * kH);
        dst->download(got.data(), std::size_t(kW) * sizeof(__fp16), kW, kH);

        // alpha1 * (1 - alpha2), with alpha1 = 1: the matte's complement.
        report(at(got, 0, 32) > 1.0 - 2e-3 && at(got, 63, 32) < 2e-3,
               "and subtract composes it like any other kind",
               std::to_string(at(got, 0, 32)) + ", " + std::to_string(at(got, 63, 32)));
    }

    // ── A two-dimensional matte, to catch a transposed lift ───────────────
    //
    // Everything above runs along x, so a kernel that swapped the two axes
    // would pass all of it. This one cannot: the matte varies only in y.
    {
        upload(1, 2, {0.0f, 1.0f}, 0.0f);
        auto m = base;
        m.matteSize[0] = 1; m.matteSize[1] = 2;
        const auto got = run(m);
        report(at(got, 32, 0) < 2e-3 && at(got, 32, 63) > 1.0 - 2e-3 &&
               std::abs(0.5 * (at(got, 32, 31) + at(got, 32, 32)) - 0.5) < 2e-3,
               "a matte that varies in y is lifted in y, not in x",
               std::to_string(at(got, 32, 0)) + " -> " + std::to_string(at(got, 32, 63)));
    }
}

// Guided feathering of the mask group — research/masking.md §4.
//
// He, Sun & Tang's guided filter with the mask as input and the photograph as
// guide. The whole seven-node chain is driven here exactly as DevelopPipeline
// wires it, because the wiring is most of what can go wrong: three kernels,
// two of them the existing box blurs, and a bilinear lift between two grids.
//
// ⚠ **The synthetic frame is chosen so the answers are identities, not
// magnitudes.** A test that only asked "did the boundary move" would pass on a
// plain blur; the case that separates the two is the flat guide, where the
// correct answer is that the boundary does *not* move.
void testMaskRefineGpu() {
    section("Guided mask refinement (GPU)");

    using orion::gpu::PixelFormat;
    namespace params = orion::pipe::params;

    // Wide and short: the interesting structure is one vertical edge, and the
    // rows are identical, so height buys nothing but time.
    constexpr std::uint32_t kW = 512, kH = 64;
    constexpr int kScale = 4;
    constexpr std::uint32_t kGW = kW / kScale, kGH = kH / kScale;
    constexpr int kRadiusFull = 60;
    constexpr int kRadius = kRadiusFull / kScale;   // 15, as the pipeline does
    constexpr float kEps = 0.01f;

    constexpr int kGuideEdge = 256;   // where the photograph's edge is
    constexpr int kMaskEdge  = 296;   // where the mask was placed — 40px off

    std::unique_ptr<orion::gpu::Device> device;
    try {
        device = orion::gpu::Device::create();
    } catch (const std::exception& e) {
        report(false, "Metal device available", e.what());
        return;
    }

    const std::string dir = std::string(ORION_SHADER_DIR) + "/";
    auto libPrep  = orion::gpu::Library::createFromFile(*device, dir + "maskGuidePrep.metallib");
    auto libAb    = orion::gpu::Library::createFromFile(*device, dir + "maskGuideAb.metallib");
    auto libApply = orion::gpu::Library::createFromFile(*device, dir + "maskGuideApply.metallib");
    auto libBox   = orion::gpu::Library::createFromFile(*device, dir + "boxBlur.metallib");
    auto libBox4  = orion::gpu::Library::createFromFile(*device, dir + "boxBlur4.metallib");

    auto kPrep  = orion::gpu::Kernel::create(*device, *libPrep,  "maskGuidePrep");
    auto kAb    = orion::gpu::Kernel::create(*device, *libAb,    "maskGuideAb");
    auto kApply = orion::gpu::Kernel::create(*device, *libApply, "maskGuideApply");
    auto kBox   = orion::gpu::Kernel::create(*device, *libBox,   "boxBlur");
    auto kBox4  = orion::gpu::Kernel::create(*device, *libBox4,  "boxBlur4");

    auto texGuide = orion::gpu::Texture::create(*device, kW, kH, PixelFormat::RG32Float);
    auto texMask  = orion::gpu::Texture::create(*device, kW, kH, PixelFormat::R16Float);
    auto texM0    = orion::gpu::Texture::create(*device, kGW, kGH, PixelFormat::RGBA32Float);
    auto texM1    = orion::gpu::Texture::create(*device, kGW, kGH, PixelFormat::RGBA32Float);
    auto texM2    = orion::gpu::Texture::create(*device, kGW, kGH, PixelFormat::RGBA32Float);
    auto texAb0   = orion::gpu::Texture::create(*device, kGW, kGH, PixelFormat::RG32Float);
    auto texAb1   = orion::gpu::Texture::create(*device, kGW, kGH, PixelFormat::RG32Float);
    auto texAb2   = orion::gpu::Texture::create(*device, kGW, kGH, PixelFormat::RG32Float);
    auto texOut   = orion::gpu::Texture::create(*device, kW, kH, PixelFormat::R16Float);

    // The guide as `guide_prep.slang` writes it: (l, l*l) with l = log2 luma.
    const auto uploadGuide = [&](const std::function<float(int)>& logLumaAt) {
        std::vector<float> g(std::size_t(kW) * kH * 2);
        for (std::uint32_t y = 0; y < kH; ++y) {
            for (std::uint32_t x = 0; x < kW; ++x) {
                const float l = logLumaAt(int(x));
                g[(std::size_t(y) * kW + x) * 2 + 0] = l;
                g[(std::size_t(y) * kW + x) * 2 + 1] = l * l;
            }
        }
        texGuide->upload(g.data(), std::size_t(kW) * 2 * sizeof(float));
    };

    const auto uploadMask = [&](const std::function<float(int)>& alphaAt) {
        std::vector<__fp16> m(std::size_t(kW) * kH);
        for (std::uint32_t y = 0; y < kH; ++y) {
            for (std::uint32_t x = 0; x < kW; ++x) {
                m[std::size_t(y) * kW + x] = __fp16(alphaAt(int(x)));
            }
        }
        texMask->upload(m.data(), std::size_t(kW) * sizeof(__fp16));
    };

    // The chain, in the order DevelopPipeline adds the nodes.
    const auto refine = [&](float strength) {
        params::MaskGuidePrep mp{};
        mp.outSize[0] = kGW; mp.outSize[1] = kGH;
        mp.inSize[0]  = kW;  mp.inSize[1]  = kH;
        mp.scale = kScale;

        params::BoxBlur bh{{kGW, kGH}, kRadius, 1};
        params::BoxBlur bv{{kGW, kGH}, kRadius, 0};
        params::MaskGuideAb mab{{kGW, kGH}, kEps, 0.0f};

        params::MaskGuideApply mga{};
        mga.size[0] = kW; mga.size[1] = kH;
        mga.coeffSize[0] = kGW; mga.coeffSize[1] = kGH;
        mga.strength = strength;

        orion::gpu::CommandBuffer cb(*device);
        cb.dispatch(*kPrep, {texGuide.get(), texMask.get(), texM0.get()},
                    &mp, sizeof mp, kGW, kGH);
        cb.dispatch(*kBox4, {texM0.get(), texM1.get()}, &bh, sizeof bh, kGW, kGH);
        cb.dispatch(*kBox4, {texM1.get(), texM2.get()}, &bv, sizeof bv, kGW, kGH);
        cb.dispatch(*kAb,   {texM2.get(), texAb0.get()}, &mab, sizeof mab, kGW, kGH);
        cb.dispatch(*kBox,  {texAb0.get(), texAb1.get()}, &bh, sizeof bh, kGW, kGH);
        cb.dispatch(*kBox,  {texAb1.get(), texAb2.get()}, &bv, sizeof bv, kGW, kGH);
        cb.dispatch(*kApply, {texMask.get(), texAb2.get(), texGuide.get(), texOut.get()},
                    &mga, sizeof mga, kW, kH);
        cb.commitAndWait();

        std::vector<__fp16> out(std::size_t(kW) * kH);
        texOut->download(out.data(), std::size_t(kW) * sizeof(__fp16), kW, kH);
        return out;
    };
    const auto at = [&](const std::vector<__fp16>& v, int x) {
        return double(v[std::size_t(kH / 2) * kW + x]);
    };

    const auto stepGuide = [&](int x) { return x < kGuideEdge ? 5.0f : 8.0f; };
    const auto stepMask  = [&](int x) { return x < kMaskEdge  ? 0.0f : 1.0f; };

    // The filter computed directly, one dimension, no subsampling.
    //
    // ⚠ This is the oracle, and it is why the test is worth having. Every
    // assertion below could have been written as a magnitude — "the boundary
    // moved", "the far side stayed clear" — and every one of those passes on a
    // plain blur or on a chain wired half right. Against a reference the answer
    // is a number the algorithm owes, and the subsampled GPU chain has to
    // reproduce it.
    //
    // Same radius, same epsilon, same clamp-to-edge windows the box passes use.
    // The rows of the synthetic frame are identical, so one row is the whole
    // problem.
    const auto exact = [&](const std::function<float(int)>& gAt,
                           const std::function<float(int)>& mAt) {
        std::vector<double> I(kW), pv(kW), a(kW), b(kW), q(kW);
        for (std::uint32_t x = 0; x < kW; ++x) { I[x] = gAt(int(x)); pv[x] = mAt(int(x)); }

        const auto mean = [&](const std::vector<double>& v, int at) {
            double sum = 0.0; int n = 0;
            for (int t = at - kRadiusFull; t <= at + kRadiusFull; ++t) {
                sum += v[std::size_t(std::clamp(t, 0, int(kW) - 1))]; ++n;
            }
            return sum / n;
        };
        std::vector<double> II(kW), IP(kW);
        for (std::uint32_t x = 0; x < kW; ++x) { II[x] = I[x] * I[x]; IP[x] = I[x] * pv[x]; }

        for (std::uint32_t x = 0; x < kW; ++x) {
            const double mI = mean(I, int(x)), mII = mean(II, int(x));
            const double mP = mean(pv, int(x)), mIP = mean(IP, int(x));
            const double var = std::max(mII - mI * mI, 0.0);
            a[x] = (mIP - mI * mP) / (var + kEps);
            b[x] = mP - a[x] * mI;
        }
        for (std::uint32_t x = 0; x < kW; ++x) {
            q[x] = std::clamp(mean(a, int(x)) * I[x] + mean(b, int(x)), 0.0, 1.0);
        }
        return q;
    };

    const auto crossing = [&](const std::vector<__fp16>& v) {
        for (std::uint32_t x = 1; x < kW; ++x) {
            if (at(v, int(x - 1)) < 0.5 && at(v, int(x)) >= 0.5) return int(x);
        }
        return -1;
    };

    // ── A constant mask is returned unchanged, corners included ───────────
    //
    // cov(I, p) is zero for constant p whatever the guide does, so a = 0 and
    // b = mean(p): the output is the constant back. It holds at the frame's
    // corners only if both box passes normalise by the window they actually
    // read, which is the one thing a separable blur most often gets wrong.
    {
        uploadGuide(stepGuide);
        uploadMask([](int) { return 0.5f; });
        const auto got = refine(1.0f);

        double worst = 0.0;
        int worstAt = 0;
        for (std::uint32_t y : {0u, kH / 2, kH - 1}) {
            for (std::uint32_t x = 0; x < kW; ++x) {
                const double d = std::abs(double(got[std::size_t(y) * kW + x]) - 0.5);
                if (d > worst) { worst = d; worstAt = int(x); }
            }
        }
        report(worst < 2e-3, "a constant mask survives refinement, corners included",
               "worst " + std::to_string(worst) + " at x=" + std::to_string(worstAt));
    }

    // ── Strength zero is the identity, bit for bit ────────────────────────
    {
        uploadGuide(stepGuide);
        uploadMask(stepMask);
        const auto got = refine(0.0f);
        bool same = true;
        for (std::uint32_t x = 0; x < kW; ++x) {
            if (at(got, int(x)) != double(stepMask(int(x)))) { same = false; break; }
        }
        report(same, "strength 0 returns the placed mask unchanged");
    }

    // ── The chain reproduces the filter, to a hundredth of coverage ───────
    int crossWithEdge = -1;
    {
        uploadGuide(stepGuide);
        uploadMask(stepMask);
        const auto got = refine(1.0f);
        const auto want = exact([&](int x) { return stepGuide(x); },
                                [&](int x) { return stepMask(x); });

        double worst = 0.0;
        int worstAt = 0;
        for (std::uint32_t x = 0; x < kW; ++x) {
            const double d = std::abs(at(got, int(x)) - want[x]);
            if (d > worst) { worst = d; worstAt = int(x); }
        }
        // The subsampled chain is not the exact filter and is not meant to be —
        // He & Sun's whole argument is that a and b vary slowly enough to be
        // sampled coarsely. Worst disagreement lands at the discontinuity,
        // where s = 4 smears the lift over four pixels.
        report(worst < 0.02,
               "the subsampled chain reproduces the exact filter",
               "worst " + std::to_string(worst) + " at x=" + std::to_string(worstAt));

        crossWithEdge = crossing(got);

        // ⚠ **The matte is discontinuous at the photograph's edge**, and that
        // is the claim of guided feathering. Left of the edge it is identically
        // zero — a cancellation of a, b and the full-resolution reconstruction
        // that nothing but a correct chain produces — and at the edge it jumps.
        //
        // The jump has a closed form for this geometry. With the mask placed d
        // past an edge and a window of radius r,
        //
        //     q(edge+) = 1 - (d/2r)(1 + ln(2r/d))
        //
        // which at d = 40, r = 60 is 1 - (1/3)(1 + ln 3) = 0.3005. The
        // reference above computes 0.3058 and the GPU 0.3145; all three agree,
        // and the point of quoting the closed form is that it depends on r, so
        // it dies if the radius is ever quietly changed.
        report(at(got, kGuideEdge - 1) < 0.03 && at(got, kGuideEdge) > 0.27,
               "the matte is zero up to the photograph's edge and jumps there",
               "q[255] " + std::to_string(at(got, kGuideEdge - 1)) +
               ", q[256] " + std::to_string(at(got, kGuideEdge)));

        report(at(got, 430) > 0.98, "and reaches full coverage well inside",
               std::to_string(at(got, 430)));
    }

    // ── The control: with no edge to snap to, nothing is attracted ────────
    //
    // ⚠ The case that separates guided feathering from a blur. A flat guide has
    // var(I) = 0 and cov(I, p) = 0, so a = 0 and q collapses to the local mean
    // of the mask — blurred, but still centred where it was placed, and with no
    // discontinuity anywhere. Without this, every assertion above is also
    // satisfied by a box blur of the mask.
    {
        uploadGuide([](int) { return 6.0f; });
        uploadMask(stepMask);
        const auto got = refine(1.0f);
        const int cross = crossing(got);

        report(cross >= 0 && std::abs(cross - kMaskEdge) < 4,
               "a flat guide leaves the boundary where it was placed",
               "crossing " + std::to_string(cross) +
               ", placed " + std::to_string(kMaskEdge));

        double jump = 0.0;
        for (int x = kGuideEdge - 4; x <= kGuideEdge + 4; ++x) {
            jump = std::max(jump, at(got, x + 1) - at(got, x));
        }
        report(jump < 0.02,
               "and puts no step where the other guide had its edge",
               std::to_string(jump));

        report(crossWithEdge >= 0 && cross - crossWithEdge > 8,
               "so the edge is what moved the boundary, by a measurable amount",
               std::to_string(cross - crossWithEdge) + " px");
    }

    // ── The filter is affine in the mask, so the complement is symmetric ──
    //
    // a and b are linear in p, so refine(1 - p) = 1 - refine(p) exactly. Free
    // to check and it catches an asymmetry anywhere in the chain — a stray
    // saturate, a sign lost in the covariance — that the step cases would read
    // as merely a slightly different boundary.
    {
        uploadGuide(stepGuide);
        uploadMask(stepMask);
        const auto direct = refine(1.0f);
        uploadMask([&](int x) { return 1.0f - stepMask(x); });
        const auto complement = refine(1.0f);

        double worst = 0.0;
        int worstAt = 0;
        for (std::uint32_t x = 0; x < kW; ++x) {
            const double d = std::abs((1.0 - at(direct, int(x))) - at(complement, int(x)));
            if (d > worst) { worst = d; worstAt = int(x); }
        }
        report(worst < 3e-3, "and refining the complement gives the complement",
               "worst " + std::to_string(worst) + " at x=" + std::to_string(worstAt));
    }

    // ── Epsilon decides which edges count, and both wrong answers are here ─
    //
    // The constant is Orion's own (UNSOURCED.md §20), so what is pinned is the
    // behaviour it was chosen for, measured as the height of the jump at the
    // edge. Reference values from the model above:
    //
    //     half-stop edge, eps = 0.01 (Orion)          jump 0.233
    //     half-stop edge, eps = 0.04 (recovery chain) jump 0.149
    //     tenth-stop edge, eps = 0.01 (Orion)         jump 0.047
    //     tenth-stop edge, eps = 1e-6 (the paper)     jump 0.306
    //
    // So the two thresholds below fail in opposite directions: reusing the
    // recovery chain's constant loses the half-stop edge, and copying the
    // paper's makes the matte snap to a tenth-stop one — which on a photograph
    // is shadow noise.
    {
        const auto jumpAtEdge = [&](const std::vector<__fp16>& v) {
            double j = 0.0;
            for (int x = kGuideEdge - 4; x <= kGuideEdge + 4; ++x) {
                j = std::max(j, at(v, x + 1) - at(v, x));
            }
            return j;
        };

        uploadMask(stepMask);
        uploadGuide([&](int x) { return x < kGuideEdge ? 5.0f : 5.5f; });
        const double strong = jumpAtEdge(refine(1.0f));
        uploadGuide([&](int x) { return x < kGuideEdge ? 5.0f : 5.1f; });
        const double faint = jumpAtEdge(refine(1.0f));

        report(strong > 0.20, "a half-stop edge is followed",
               "jump " + std::to_string(strong));
        report(faint < 0.10, "and a tenth-stop one is treated as texture",
               "jump " + std::to_string(faint));
    }
}

}  // namespace

int main() {
    std::printf("orion-tests\n\n");

    testWhiteBalance();
    testPlanckianLocus();
    testToneCurve();
    testCfa();
    testOrientation();
    testOrientGpu();
    testDisplayNeutrality();
    testCropGpu();
    testStraightenPivot();
    testNoiseEstimator();
    testLinearizeClipsToWhite();
    testDenoiseGpu();
    testHighlightRecoveryGpu();
    testGradeOffsets();
    testLensGpu();
    testLensAutoScale();
    testToneBandsWithoutGuide();
    testHueSatMapGpu();
    testColorGradeGpu();
    testLocalLaplacianGpu();
    testDehazeGpu();
    testCreativeLut();
    testExposureFusionMath();
    testExposureFusionGpu();
    testAutoEnhanceStats();
    testMaskGpu();
    testMaskBrushGpu();
    testMaskCompositeGpu();
    testMaskGeometry();
    testMaskRefineGpu();
    testMaskMatteGpu();
    testMaskRangeGpu();
    testSpotRemovalGpu();
    testBrushDabsFollowTheFrame();
    testHighlightHaloGpu();
    testOutputDepth();
    testLensDatabase();
    testBlackLevels();
    testExportFormats();

    std::printf("\n%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
