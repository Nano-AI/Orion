// The white balance eyedropper and presets. research/white-balance.md.
//
// The inversion is held to what a photographer sees: a gray, rendered with the
// balance it solves for, comes out neutral. The parameters themselves are only
// recoverable to about a hundred kelvin along the locus's valley (tests_color.cpp's
// as-shot round trip says why), so they are never what is asserted.

#include "harness.h"

#include "orion/orion.h"
#include "util/DngWriter.h"

namespace {

constexpr float kXyzToCam[9] = {0.6640f, -0.1847f, -0.0503f, -0.5238f, 1.3465f,
                                0.1916f, -0.0879f, 0.1636f,  0.6271f};

double spread(const std::array<float, 3>& c) {
    const double hi = std::max({c[0], c[1], c[2]}), lo = std::min({c[0], c[1], c[2]});
    return hi > 0 ? (hi - lo) / hi : 1.0;
}

/// CIE 1960 UCS distance between two CIE 1931 chromaticities.
double deltaUv(double x1, double y1, double x2, double y2) {
    const auto uv = [](double x, double y) {
        const double d = -2 * x + 12 * y + 3;
        return std::pair{4 * x / d, 6 * y / d};
    };
    const auto [u1, v1] = uv(x1, y1);
    const auto [u2, v2] = uv(x2, y2);
    return std::hypot(u1 - u2, v1 - v2);
}

/// The CIE daylight locus (CIE 15:2004, the x_D and y_D equations) at correlated colour temperature t.
std::pair<double, double> daylightLocus(double t) {
    const double x = t <= 7000 ? -4.6070e9 / (t * t * t) + 2.9678e6 / (t * t) + 0.09911e3 / t + 0.244063
                               : -2.0064e9 / (t * t * t) + 1.9018e6 / (t * t) + 0.24748e3 / t + 0.237040;
    return {x, -3.0 * x * x + 2.870 * x - 0.275};
}

}  // namespace

void testWhiteBalanceNeutral() {
    section("White balance: the selector's inversion and the presets");
    using orion::pipe::multipliersFor;
    using orion::pipe::WhiteBalance;

    // A camera whose own estimate is off the locus, as a real one's is, so the
    // anchor the render divides by is not the identity.
    auto asShotMul = multipliersFor({5200.0f, 0.05f}, kXyzToCam);
    asShotMul[0] *= 1.03f;
    asShotMul[2] *= 0.97f;
    const auto asShotRef = multipliersFor(orion::pipe::estimateFrom(asShotMul, kXyzToCam), kXyzToCam);
    const auto gains = [&](const WhiteBalance& wb) {
        const auto m = multipliersFor(wb, kXyzToCam);
        return std::array<float, 3>{asShotMul[0] * m[0] / asShotRef[0], 1.0f, asShotMul[2] * m[2] / asShotRef[2]};
    };

    double worst = 0;
    for (const WhiteBalance light : {WhiteBalance{2850, 0}, {4000, 0.1f}, {5500, 0.067f}, {7500, -0.1f}}) {
        // A gray under that light, as the sensor records it: the light's own
        // multipliers are what neutralize it, so the response is their inverse.
        const auto m = multipliersFor(light, kXyzToCam);
        const std::array<float, 3> gray{0.2f / m[0], 0.2f, 0.2f / m[2]};
        for (const WhiteBalance current : {WhiteBalance{3200, 0.3f}, {6500, 0}, {9000, -0.2f}, light}) {
            const auto g = gains(current);
            const std::array<float, 3> patch{gray[0] * g[0], gray[1], gray[2] * g[2]};
            const auto wb = orion::pipe::neutralFor(patch, g, asShotMul, asShotRef, kXyzToCam);
            const auto fixed = gains(wb);
            worst = std::max(worst, spread({gray[0] * fixed[0], gray[1], gray[2] * fixed[2]}));
        }
    }
    report(worst < 1e-3, "a gray under any light, picked under any balance, renders neutral with the balance solved for",
           "worst spread " + std::to_string(worst));

    // Daylight, Cloudy and Shade are the CIE daylight locus at their
    // temperature, which sits +10 of Adobe's tint units above the Planckian.
    double day = 0;
    for (const auto& p : orion::pipe::kWhiteBalancePresets) {
        if (p.wb.tint != orion::pipe::kDaylightTint) continue;
        float x = 0, y = 0;
        orion::pipe::whitePointXy(p.wb, x, y);
        const auto [dx, dy] = daylightLocus(p.wb.temperatureK);
        day = std::max(day, deltaUv(x, y, dx, dy));
    }
    report(day < 1e-4, "Daylight, Cloudy and Shade lie on the CIE daylight locus", "worst duv " + std::to_string(day));
    // CIE illuminant A, 2856 K on the Planckian (CIE 15:2004), six kelvin from Tungsten.
    float ax = 0, ay = 0;
    orion::pipe::whitePointXy({2850.0f, 0.0f}, ax, ay);
    report(deltaUv(ax, ay, 0.44757, 0.40745) < 5e-4, "Tungsten is CIE illuminant A",
           "duv " + std::to_string(deltaUv(ax, ay, 0.44757, 0.40745)));
}

/// Through the facade, as the selector drives it: a tungsten-cast gray block on
/// a coloured frame, picked, renders neutral; a blown block is refused.
void testWhiteBalanceAtGpu() {
    section("White balance selector through the facade (GPU)");
    constexpr std::uint32_t kW = 96, kH = 64;
    const std::string path = "/tmp/orion-white-balance.dng";
    std::vector<float> rgb(std::size_t(kW) * kH * 3);
    for (std::uint32_t y = 0; y < kH; ++y)
        for (std::uint32_t x = 0; x < kW; ++x) {
            float* px = &rgb[(std::size_t(y) * kW + x) * 3];
            const bool gray = x >= 16 && x < 48 && y >= 16 && y < 48, blown = x >= 64 && x < 88 && y >= 20 && y < 44;
            const float hue = float(x) / float(kW);
            const float c[3] = {0.1f + 0.3f * hue, 0.25f - 0.1f * hue, 0.05f + 0.2f * (1 - hue)};
            for (int k = 0; k < 3; ++k) px[k] = blown ? 1.0f : gray ? std::array{0.26f, 0.2f, 0.11f}[std::size_t(k)] : c[k];
        }
    orion::util::DngLinearImage img;
    img.width = kW; img.height = kH; img.rgb = rgb.data();
    img.xyzToCam = {kXyzToCam[0], kXyzToCam[1], kXyzToCam[2], kXyzToCam[3], kXyzToCam[4],
                    kXyzToCam[5], kXyzToCam[6], kXyzToCam[7], kXyzToCam[8]};
    img.asShotNeutral = {1, 1, 1};
    img.camera = "SONY ILCE-7RM3";
    OrionEngine* e = nullptr;
    try {
        orion::util::writeDngLinear(path, img);
    } catch (const std::exception& ex) {
        report(false, "white balance fixture DNG written", ex.what());
        return;
    }
    if (orion_engine_create(&e) != ORION_OK || orion_engine_open_raw(e, path.c_str()) != ORION_OK) {
        report(false, "facade engine opens the fixture", e ? orion_last_error(e) : gpuProblem());
        orion_engine_destroy(e);
        std::remove(path.c_str());
        return;
    }
    OrionAdjustments adj{};
    orion_engine_as_shot(e, &adj);
    adj.contrast = 1.45f; adj.process = 2; adj.crop_w = adj.crop_h = 1;
    const auto scene = [&](float u, float v) {
        orion_engine_set_adjustments(e, &adj);
        orion_engine_render(e, nullptr);
        std::array<float, 3> s{};
        orion_engine_sample(e, u, v, nullptr, s.data());
        return s;
    };
    const float u = 32.0f / kW, v = 32.0f / kH;
    const double before = spread(scene(u, v));
    const OrionStatus picked = orion_engine_white_balance_at(e, u, v, 0.05f, &adj);
    const double after = spread(scene(u, v));
    report(picked == ORION_OK && before > 0.2 && after < 0.01, "a click on the cast gray block makes it neutral",
           std::to_string(before) + " -> " + std::to_string(after) + " at " + std::to_string(adj.temperature_k) + " K");

    const OrionAdjustments kept = adj;
    const OrionStatus blown = orion_engine_white_balance_at(e, 76.0f / kW, 32.0f / kH, 0.05f, &adj);
    report(blown == ORION_ERR_BAD_ARG && adj.temperature_k == kept.temperature_k && adj.tint == kept.tint,
           "a click on a blown block is refused and changes nothing", orion_last_error(e));
    orion_engine_destroy(e);
    std::remove(path.c_str());
}
