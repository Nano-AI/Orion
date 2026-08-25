/*  tests_dng — the DNG writer, and the read-back that decides everything.
 *
 *  Story A of the HDR merge epic. The merged radiance map is only useful if
 *  the file it lands in comes back through LibRaw — the same door an ARW
 *  enters by — so the round-trip here is not a nicety, it is the spike that
 *  decides whether the fp16 LinearRaw layout survives contact with the
 *  installed decoder. If this file's checks go red after a LibRaw upgrade,
 *  the read strategy is what broke, not the writer.
 *
 *  House idioms observed: the round-trip asserts a mid-gray control region
 *  (a writer that flattened everything to the clip would pass a test that
 *  only looked at the highlight), and the truncated-file case is the negative
 *  control (a reader that accepts anything proves nothing by accepting ours).
 */

#include "harness.h"
#include "Engine.h"
#include "util/DngWriter.h"
#include "util/Half.h"

#include <libraw/libraw.h>

#include <cstdio>
#include <fstream>

namespace {

/// binary16 -> binary32, the inverse of the writer's quantization. Local to
/// the tests: the expectation must be computed independently of the code
/// under test, and this direction is not something the writer ships.
float halfToFloat(std::uint16_t h) {
    const std::uint32_t sign = (h & 0x8000u) << 16;
    std::uint32_t exponent   = (h >> 10) & 0x1fu;
    std::uint32_t mantissa   = h & 0x3ffu;

    std::uint32_t bits;
    if (exponent == 0) {
        if (mantissa == 0) {
            bits = sign;  // signed zero
        } else {
            // subnormal: renormalize
            int e = -1;
            do { ++e; mantissa <<= 1; } while ((mantissa & 0x400u) == 0);
            bits = sign | ((127 - 15 - e) << 23) | ((mantissa & 0x3ffu) << 13);
        }
    } else if (exponent == 0x1fu) {
        bits = sign | 0x7f800000u | (mantissa << 13);  // inf / NaN
    } else {
        bits = sign | ((exponent - 15 + 127) << 23) | (mantissa << 13);
    }
    float out;
    std::memcpy(&out, &bits, sizeof out);
    return out;
}

constexpr std::uint32_t kW = 64;
constexpr std::uint32_t kH = 48;

/// The synthetic frame: a horizontal ramp 0..1 in all three channels, with
/// two deliberate landmarks — a clipped white block top-left (the highlight
/// the merge exists to preserve) and an exact mid-gray block top-right (the
/// control region).
std::vector<float> gradientFrame() {
    std::vector<float> rgb(std::size_t(kW) * kH * 3);
    for (std::uint32_t y = 0; y < kH; ++y) {
        for (std::uint32_t x = 0; x < kW; ++x) {
            float v = float(x) / float(kW - 1);
            if (x < 8 && y < 8) v = 1.0f;    // clipped block
            if (x >= kW - 8 && y < 8) v = 0.5f;  // control block
            float* px = &rgb[(std::size_t(y) * kW + x) * 3];
            px[0] = v;
            px[1] = v * 0.75f;   // channels differ, so a channel swap cannot pass
            px[2] = v * 0.25f;
        }
    }
    return rgb;
}

// A plausible, deliberately asymmetric XYZ->camera matrix in the direction
// ColorMatrix1 wants. ⚠ Not asserted against cam_xyz: LibRaw recognizes
// "SONY ILCE-7RM3" and prefers its own Adobe table there, so the verbatim
// tag round-trip is read from dng_color[0].colormatrix instead — and that
// table preference is fine for the product, where the camera is real.
constexpr std::array<float, 9> kXyzToCam = {
    0.6640f, -0.1847f, -0.0503f,
    -0.5238f, 1.3465f,  0.1916f,
    -0.0879f,  0.1636f,  0.6271f,
};

constexpr std::array<float, 3> kNeutral = {0.4548f, 1.0f, 0.7130f};

}  // namespace

void testFloatToHalf() {
    section("float -> half quantization");
    using orion::util::floatToHalf;

    // The exact encodings, from the IEEE 754-2008 binary16 definition.
    CHECK(floatToHalf(0.0f) == 0x0000);
    CHECK(floatToHalf(1.0f) == 0x3c00);
    CHECK(floatToHalf(0.5f) == 0x3800);
    CHECK(floatToHalf(-2.0f) == 0xc000);
    CHECK(floatToHalf(65504.0f) == 0x7bff);   // largest finite half
    CHECK(floatToHalf(65536.0f) == 0x7c00);   // overflow -> inf
    CHECK(floatToHalf(5.9604645e-8f) == 0x0001);  // smallest subnormal

    // Round to nearest even, both directions. 1 + 2^-11 sits exactly between
    // 0x3c00 and 0x3c01 and must round down to the even mantissa; 1 + 3*2^-11
    // sits exactly between 0x3c01 and 0x3c02 and must round up to even.
    CHECK(floatToHalf(1.0f + 0x1p-11f) == 0x3c00);
    CHECK(floatToHalf(1.0f + 3 * 0x1p-11f) == 0x3c02);

    // Every value the writer emits must survive its own inverse within one
    // ULP of half precision — this is the quantization bound the merge tests
    // later lean on.
    for (int i = 0; i <= 1000; ++i) {
        const float v = float(i) / 1000.0f;
        const float back = halfToFloat(floatToHalf(v));
        if (std::abs(back - v) > 0x1p-11f * std::max(v, 0.5f)) {
            report(false, "half round-trip within one ULP",
                   "v=" + std::to_string(v) + " back=" + std::to_string(back));
            return;
        }
    }
    report(true, "half round-trip within one ULP for 0..1");
}

void testDngRoundTrip() {
    section("fp16 LinearRaw DNG round-trips through LibRaw");

    const std::string path = "/tmp/orion-dng-roundtrip.dng";
    const auto rgb = gradientFrame();

    orion::util::DngLinearImage img;
    img.width  = kW;
    img.height = kH;
    img.rgb    = rgb.data();
    img.xyzToCam = kXyzToCam;
    img.asShotNeutral = kNeutral;
    img.baselineExposureEv = 2.0f;
    img.camera = "SONY ILCE-7RM3";
    img.flip = 5;   // a portrait frame: 90 CCW, TIFF orientation 8

    try {
        orion::util::writeDngLinear(path, img);
    } catch (const std::exception& e) {
        report(false, "DNG write succeeds", e.what());
        return;
    }

    LibRaw proc;
    if (int rc = proc.open_file(path.c_str()); rc != LIBRAW_SUCCESS) {
        report(false, "LibRaw opens the DNG", libraw_strerror(rc));
        return;
    }
    report(true, "LibRaw opens the DNG");

    // What did the container declare, before unpack touches pixels?
    CHECK(proc.imgdata.idata.filters == 0);
    CHECK(proc.imgdata.idata.colors == 3);
    CHECK(proc.is_floating_point() != 0);
    checkNear(proc.imgdata.sizes.raw_width, kW, 0, "declared width");
    checkNear(proc.imgdata.sizes.raw_height, kH, 0, "declared height");
    // A portrait bracket must merge to a portrait DNG: flip 5 rides out as
    // Orientation 8 and comes back as flip 5 — the bug this catches showed
    // every merged photograph sideways.
    checkNear(proc.imgdata.sizes.flip, 5, 0, "orientation survives the round trip");

    // The color tags came back — this is what makes WB editable on re-open.
    // dng_color[0] holds the tag verbatim; cam_xyz is dcraw-cooked and, for a
    // camera LibRaw recognizes, comes from its own table rather than ours.
    for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 3; ++c) {
            checkNear(proc.imgdata.color.dng_color[0].colormatrix[r][c],
                      kXyzToCam[r * 3 + c], 1e-3,
                      "ColorMatrix1 tag read back verbatim");
        }
    }
    // LibRaw turns AsShotNeutral into cam_mul gains; scale is LibRaw's, so
    // assert the ratios, which are the physically meaningful part.
    const auto& mul = proc.imgdata.color.cam_mul;
    if (mul[1] > 0) {
        checkNear(mul[0] / mul[1], kNeutral[1] / kNeutral[0], 1e-3,
                  "red gain ratio from AsShotNeutral");
        checkNear(mul[2] / mul[1], kNeutral[1] / kNeutral[2], 1e-3,
                  "blue gain ratio from AsShotNeutral");
    } else {
        report(false, "cam_mul populated from AsShotNeutral");
    }
    checkNear(proc.imgdata.color.dng_levels.baseline_exposure, 2.0f, 1e-2,
              "BaselineExposure survives");

    // LibRaw's default is to flatten float data to 16-bit integers at
    // unpack. The whole point of the fp16 container is shadow precision, so
    // the read path keeps floats — story B's decodeLinear must clear the
    // same flag.
    proc.imgdata.rawparams.options &=
        ~std::uint32_t(LIBRAW_RAWOPTIONS_CONVERTFLOAT_TO_INT);

    if (int rc = proc.unpack(); rc != LIBRAW_SUCCESS) {
        report(false, "LibRaw unpacks fp16 LinearRaw strips", libraw_strerror(rc));
        return;
    }
    report(true, "LibRaw unpacks fp16 LinearRaw strips");

    const auto* fp = proc.imgdata.rawdata.float3_image;
    if (fp == nullptr) {
        report(false, "float3_image populated",
               "unpack succeeded but the float plane is null");
        return;
    }
    report(true, "float3_image populated");

    // Pixels, against the independently computed quantization. Three
    // regions: the ramp, the clipped block, and the mid-gray control.
    int bad = 0;
    for (std::uint32_t y = 0; y < kH && bad == 0; ++y) {
        for (std::uint32_t x = 0; x < kW && bad == 0; ++x) {
            const float* want = &rgb[(std::size_t(y) * kW + x) * 3];
            const float* got  = fp[std::size_t(y) * kW + x];
            for (int c = 0; c < 3; ++c) {
                const float expect = halfToFloat(orion::util::floatToHalf(want[c]));
                if (got[c] != expect) ++bad;
            }
        }
    }
    report(bad == 0, "every sample matches its half quantization exactly");

    const float* clipped = fp[2 * kW + 2];
    const float* control = fp[2 * kW + (kW - 4)];
    checkNear(clipped[0], 1.0f, 0.0f, "clipped block reads exactly 1.0");
    checkNear(control[0], 0.5f, 0.0f, "mid-gray control reads exactly 0.5");
    report(control[0] != clipped[0],
           "control differs from the clip — the ramp was not flattened");

    // ── Negative control: a truncated file must still be refused ──────────
    const std::string cut = "/tmp/orion-dng-truncated.dng";
    {
        std::ifstream in(path, std::ios::binary);
        std::vector<char> bytes((std::istreambuf_iterator<char>(in)),
                                std::istreambuf_iterator<char>());
        std::ofstream out(cut, std::ios::binary | std::ios::trunc);
        out.write(bytes.data(),
                  static_cast<std::streamsize>(bytes.size() / 3));
    }
    LibRaw refuse;
    const bool rejected = refuse.open_file(cut.c_str()) != LIBRAW_SUCCESS ||
                          refuse.unpack() != LIBRAW_SUCCESS;
    report(rejected, "a truncated DNG is refused, not misread");

    std::remove(cut.c_str());
    std::remove(path.c_str());
}

void testLinearDngOpens() {
    section("a linear DNG develops through the linear-source graph");

    // Three landmark patches on a neutral-colored card: the pixel value is the
    // DNG's AsShotNeutral color scaled by k, so after the as-shot white
    // balance every patch must come out *gray* at exactly k — through the
    // row-normalized camera matrix and the profile's hue/sat table, both of
    // which must preserve neutrals. A clipped patch checks the other half of
    // the head node's contract: over-unity gains on blown data clip to the
    // common ceiling instead of tinting.
    const std::string path = "/tmp/orion-dng-linear-open.dng";

    std::vector<float> rgb(std::size_t(kW) * kH * 3);
    const auto patchLevel = [](std::uint32_t x, std::uint32_t y) {
        if (x < 16)  return 0.25f;             // control: must stay distinct
        if (x >= kW - 16 && y < 16) return 1.0f;   // clipped block
        return 0.5f;
    };
    for (std::uint32_t y = 0; y < kH; ++y) {
        for (std::uint32_t x = 0; x < kW; ++x) {
            const float k = patchLevel(x, y);
            float* px = &rgb[(std::size_t(y) * kW + x) * 3];
            px[0] = k * kNeutral[0];
            px[1] = k * kNeutral[1];
            px[2] = k * kNeutral[2];
        }
    }

    orion::util::DngLinearImage img;
    img.width  = kW;
    img.height = kH;
    img.rgb    = rgb.data();
    img.xyzToCam = kXyzToCam;
    img.asShotNeutral = kNeutral;
    img.camera = "SONY ILCE-7RM3";
    try {
        orion::util::writeDngLinear(path, img);
    } catch (const std::exception& e) {
        report(false, "linear-open DNG write succeeds", e.what());
        return;
    }

    std::unique_ptr<orion::gpu::Device> device;
    try {
        device = orion::gpu::Device::create();
    } catch (const std::exception& e) {
        report(false, "Metal device for linear DNG test", e.what());
        return;
    }

    try {
        const auto decoded = orion::raw::decode(path);
        const auto* linear = std::get_if<orion::raw::LinearImage>(&decoded);
        if (linear == nullptr) {
            report(false, "decode() classifies the DNG as linear");
            std::remove(path.c_str());
            return;
        }
        report(true, "decode() classifies the DNG as linear");
        checkNear(linear->width, kW, 0, "linear decode width");
        checkNear(linear->camMul[1] > 0 ? linear->camMul[0] / linear->camMul[1] : 0,
                  kNeutral[1] / kNeutral[0], 1e-3, "linear decode red gain ratio");

        orion::pipe::DevelopPipeline dev(*device, ORION_SHADER_DIR, *linear);
        dev.graph().render();

        // referenceImage() is the post-profile texture: white balance, the
        // camera matrix and the hue/sat table applied, no user adjustment.
        const auto& ref = dev.referenceImage();
        std::vector<std::uint16_t> half(std::size_t(kW) * kH * 4);
        ref.download(half.data(), std::size_t(kW) * 4 * sizeof(std::uint16_t),
                     kW, kH);
        const auto px = [&](std::uint32_t x, std::uint32_t y, int c) {
            return halfToFloat(half[(std::size_t(y) * kW + x) * 4 + std::size_t(c)]);
        };

        // fp16 quantizes twice (file, texture); 2^-9 covers both with margin.
        const float tol = 0x1p-9f;
        checkNear(px(32, 24, 0), 0.5, tol, "mid patch is gray at 0.5 (R)");
        checkNear(px(32, 24, 1), 0.5, tol, "mid patch is gray at 0.5 (G)");
        checkNear(px(32, 24, 2), 0.5, tol, "mid patch is gray at 0.5 (B)");
        checkNear(px(4, 24, 1), 0.25, tol, "control patch stays at 0.25");
        report(px(32, 24, 1) != px(4, 24, 1),
               "the two levels stay distinct — nothing flattened");

        // The blown patch: gains of 2.2/1.0/1.4 on 1.0 clip to the common
        // ceiling, which for these multipliers is 1.0 — white, not magenta.
        checkNear(px(kW - 8, 8, 0), 1.0, tol, "clipped patch is white (R)");
        checkNear(px(kW - 8, 8, 2), 1.0, tol, "clipped patch is white (B)");
    } catch (const std::exception& e) {
        report(false, "linear DNG develops", e.what());
    }

    std::remove(path.c_str());
}

void testDngEmbeddedPreview() {
    section("an embedded preview rides IFD0 and the raw still decodes");

    const std::string path = "/tmp/orion-dng-preview.dng";
    const auto rgb = gradientFrame();

    // A real JPEG, made by the same encoder the merge uses.
    std::vector<std::uint16_t> tiny(std::size_t(16) * 12 * 4, 0);
    for (std::size_t i = 0; i < tiny.size(); i += 4) {
        tiny[i] = 30000; tiny[i + 1] = 40000; tiny[i + 2] = 50000;
        tiny[i + 3] = 65535;
    }
    const auto jpeg = orion::util::encodeJpeg(tiny.data(), 16, 12,
                                              16 * 4 * sizeof(std::uint16_t), 0.9f);
    report(!jpeg.empty(), "the preview encoder produces a JPEG");
    if (jpeg.empty()) return;

    orion::util::DngLinearImage img;
    img.width  = kW;
    img.height = kH;
    img.rgb    = rgb.data();
    img.xyzToCam = kXyzToCam;
    img.asShotNeutral = kNeutral;
    img.camera = "SONY ILCE-7RM3";
    img.flip = 5;                       // the two-IFD file must keep this
    img.previewJpeg = jpeg.data();
    img.previewJpegBytes = jpeg.size();
    img.previewWidth = 16;
    img.previewHeight = 12;

    try {
        orion::util::writeDngLinear(path, img);
    } catch (const std::exception& e) {
        report(false, "preview DNG write succeeds", e.what());
        return;
    }

    LibRaw proc;
    if (int rc = proc.open_file(path.c_str()); rc != LIBRAW_SUCCESS) {
        report(false, "LibRaw opens the preview DNG", libraw_strerror(rc));
        return;
    }
    // The raw image keeps its identity behind the preview...
    CHECK(proc.imgdata.idata.filters == 0);
    CHECK(proc.imgdata.idata.colors == 3);
    checkNear(proc.imgdata.sizes.raw_width, kW, 0, "raw width behind the preview");
    checkNear(proc.imgdata.sizes.flip, 5, 0,
              "orientation still comes from the raw IFD");

    // ...the thumbnail path finds the JPEG...
    if (int rc = proc.unpack_thumb(); rc != LIBRAW_SUCCESS) {
        report(false, "unpack_thumb finds the preview", libraw_strerror(rc));
    } else {
        CHECK(proc.imgdata.thumbnail.tformat == LIBRAW_THUMBNAIL_JPEG);
        report(proc.imgdata.thumbnail.tlength == jpeg.size(),
               "and it is byte-for-byte the embedded stream");
    }

    // ...and the pixels are untouched by the restructuring.
    proc.imgdata.rawparams.options &=
        ~std::uint32_t(LIBRAW_RAWOPTIONS_CONVERTFLOAT_TO_INT);
    if (int rc = proc.unpack(); rc != LIBRAW_SUCCESS) {
        report(false, "raw unpacks behind the preview", libraw_strerror(rc));
    } else if (proc.imgdata.rawdata.float3_image == nullptr) {
        report(false, "float plane behind the preview");
    } else {
        const float* got = proc.imgdata.rawdata.float3_image[2 * kW + 2];
        checkNear(got[0], 1.0f, 0.0f, "clipped block still exact behind the preview");
    }
    std::remove(path.c_str());
}

void testLinearDngBaselineExposure() {
    section("BaselineExposure is a decode-time gain, and the clip rides with it");

    // Same neutral-card scene as above at 0.25 and 1.0, but the file says
    // +1 EV. The head must render the card at 0.5 — and the blown patch at
    // 2.0, still neutral: the ceiling scales with the gain, so the headroom
    // the merge bought is above 1.0 rather than cut at it.
    const std::string path = "/tmp/orion-dng-baseline.dng";

    std::vector<float> rgb(std::size_t(kW) * kH * 3);
    for (std::uint32_t y = 0; y < kH; ++y) {
        for (std::uint32_t x = 0; x < kW; ++x) {
            const float k = (x >= kW - 16 && y < 16) ? 1.0f : 0.25f;
            float* px = &rgb[(std::size_t(y) * kW + x) * 3];
            px[0] = k * kNeutral[0];
            px[1] = k * kNeutral[1];
            px[2] = k * kNeutral[2];
        }
    }

    orion::util::DngLinearImage img;
    img.width  = kW;
    img.height = kH;
    img.rgb    = rgb.data();
    img.xyzToCam = kXyzToCam;
    img.asShotNeutral = kNeutral;
    img.baselineExposureEv = 1.0f;
    img.camera = "SONY ILCE-7RM3";
    try {
        orion::util::writeDngLinear(path, img);
    } catch (const std::exception& e) {
        report(false, "baseline DNG write succeeds", e.what());
        return;
    }

    try {
        auto device = orion::gpu::Device::create();
        const auto decoded = orion::raw::decode(path);
        const auto& linear = std::get<orion::raw::LinearImage>(decoded);
        checkNear(linear.baselineExposureEv, 1.0, 1e-2, "the tag decodes");

        orion::pipe::DevelopPipeline dev(*device, ORION_SHADER_DIR, linear);
        dev.graph().render();

        const auto& ref = dev.referenceImage();
        std::vector<std::uint16_t> half(std::size_t(kW) * kH * 4);
        ref.download(half.data(), std::size_t(kW) * 4 * sizeof(std::uint16_t),
                     kW, kH);
        const auto px = [&](std::uint32_t x, std::uint32_t y, int c) {
            return halfToFloat(half[(std::size_t(y) * kW + x) * 4 + std::size_t(c)]);
        };

        const float tol = 0x1p-8f;
        checkNear(px(16, 30, 0), 0.5, tol, "0.25 renders at 0.5 (R)");
        checkNear(px(16, 30, 1), 0.5, tol, "0.25 renders at 0.5 (G)");
        // The blown patch: above the old 1.0 ceiling, not cut at it.
        checkNear(px(kW - 8, 8, 1), 2.0, 4.0f * tol, "the clip scaled to 2.0");
        checkNear(px(kW - 8, 8, 0) / px(kW - 8, 8, 2), 1.0, 1e-2,
                  "and the blown patch is still neutral");
    } catch (const std::exception& e) {
        report(false, "baseline-exposure DNG develops", e.what());
    }

    std::remove(path.c_str());
}

void testLinearDngEngineOpen() {
    section("the engine opens a linear DNG like any photo");

    const std::string path = "/tmp/orion-dng-engine-open.dng";
    std::vector<float> rgb(std::size_t(kW) * kH * 3, 0.25f);
    orion::util::DngLinearImage img;
    img.width  = kW;
    img.height = kH;
    img.rgb    = rgb.data();
    img.xyzToCam = kXyzToCam;
    img.asShotNeutral = kNeutral;
    img.camera = "SONY ILCE-7RM3";
    try {
        orion::util::writeDngLinear(path, img);
    } catch (const std::exception& e) {
        report(false, "engine-open DNG write succeeds", e.what());
        return;
    }

    try {
        orion::Engine engine;
        engine.openRaw(path);
        report(true, "openRaw takes the DNG");
        engine.render();
        report(true, "the full graph renders it");
        // The preview graph is the decimate + reload path in one: built from
        // the quarter-scale linear image at open.
        report(engine.renderPreview() >= 0.0, "the preview graph exists or reports zero");

        // Opening the same file again exercises the linear reload path.
        engine.openRaw(path);
        engine.render();
        report(true, "a second open reloads the compiled linear graph");
    } catch (const std::exception& e) {
        report(false, "engine opens and renders the linear DNG", e.what());
    }

    std::remove(path.c_str());
}
