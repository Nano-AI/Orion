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
#include "util/DngWriter.h"

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
