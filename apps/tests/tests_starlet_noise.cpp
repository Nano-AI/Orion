// The starlet's per-scale noise after RCD, and what the capture chain leaves
// of it. research/high-iso.md §7, pipe/StarletNoise.h.
//
// One fixture: a flat grey mosaic under a held-out mix, the a, b and white
// balance measured on an ISO 8000 night frame (DSC09348), which the tables
// were not fitted on. Each scale's deviation is a MAD (Donoho & Johnstone
// 1994) of a double-precision starlet of the graph's own output.

#include "harness.h"

#include <algorithm>
#include <cmath>

namespace {

constexpr float kLumaW[3] = {0.2627f, 0.6780f, 0.0593f};
constexpr int kN = 512, kBorder = 128;

using Plane = std::vector<double>;   // kN*kN*3

Plane blur(const Plane& src, int step) {
    const double t[5] = {1.0 / 16, 4.0 / 16, 6.0 / 16, 4.0 / 16, 1.0 / 16};
    const auto at = [](int v) { return std::clamp(v, 0, kN - 1); };
    Plane tmp(src.size()), dst(src.size());
    for (int y = 0; y < kN; ++y)
        for (int x = 0; x < kN; ++x)
            for (int c = 0; c < 3; ++c) {
                double s = 0.0;
                for (int i = 0; i < 5; ++i) s += t[i] * src[(std::size_t(y) * kN + at(x + (i - 2) * step)) * 3 + c];
                tmp[(std::size_t(y) * kN + x) * 3 + c] = s;
            }
    for (int y = 0; y < kN; ++y)
        for (int x = 0; x < kN; ++x)
            for (int c = 0; c < 3; ++c) {
                double s = 0.0;
                for (int i = 0; i < 5; ++i) s += t[i] * tmp[(std::size_t(at(y + (i - 2) * step)) * kN + x) * 3 + c];
                dst[(std::size_t(y) * kN + x) * 3 + c] = s;
            }
    return dst;
}

double madSigma(std::vector<double> v) {
    const auto mid = v.begin() + std::ptrdiff_t(v.size() / 2);
    std::nth_element(v.begin(), mid, v.end());
    const double median = *mid;
    for (auto& x : v) x = std::fabs(x - median);
    std::nth_element(v.begin(), mid, v.end());
    return *mid / 0.6745;
}

/// Luma and chroma deviation at scales 0..5, and the mean luminance.
struct Scales { double luma[6], chroma[6], y; };

Scales measure(Plane c) {
    Scales s{};
    for (int y = kBorder; y < kN - kBorder; ++y)
        for (int x = kBorder; x < kN - kBorder; ++x)
            for (int k = 0; k < 3; ++k) s.y += kLumaW[k] * c[(std::size_t(y) * kN + x) * 3 + k];
    s.y /= double(kN - 2 * kBorder) * (kN - 2 * kBorder);
    for (int j = 0; j < 6; ++j) {
        const Plane next = blur(c, 1 << j);
        std::vector<double> luma, chroma[3];
        for (int y = kBorder; y < kN - kBorder; ++y)
            for (int x = kBorder; x < kN - kBorder; ++x) {
                const std::size_t i = (std::size_t(y) * kN + x) * 3;
                double d[3], l = 0.0;
                for (int k = 0; k < 3; ++k) { d[k] = c[i + k] - next[i + k]; l += kLumaW[k] * d[k]; }
                luma.push_back(l);
                for (int k = 0; k < 3; ++k) chroma[k].push_back(d[k] - l);
            }
        s.luma[j] = madSigma(std::move(luma));
        double v = 0.0;
        for (auto& ch : chroma) { const double m = madSigma(std::move(ch)); v += m * m; }
        s.chroma[j] = std::sqrt(v / 3.0);
        c = next;
    }
    return s;
}

Plane read(const orion::gpu::Texture& t) {
    std::vector<__fp16> px(std::size_t(kN) * kN * 4);
    t.download(px.data(), std::size_t(kN) * 8, kN, kN);
    Plane out(std::size_t(kN) * kN * 3);
    for (std::size_t i = 0; i < std::size_t(kN) * kN; ++i)
        for (int k = 0; k < 3; ++k) out[i * 3 + k] = double(px[i * 4 + k]);
    return out;
}

/// DSC09348's fit and white balance, on a grey at 0.02 after it, offset by a
/// black level so the noise below it survives as the camera's does.
constexpr double kA = 1.181e-3, kB = 2.934e-6, kLevel = 0.02;
const std::array<float, 4> kCamMul = {1820.0f, 1024.0f, 2772.0f, 1024.0f};

orion::raw::BayerImage card() {
    auto img = noisyGrey(kN, kN, 0.25, 0.0, 0.0, 7);
    img.camMul = kCamMul;
    // Rec.2020 primaries, so camera RGB is the working space and the chain's
    // luma/chroma split is the picture's: no matrix leaks one into the other.
    // ⚠ The field holds XYZ to camera, LibRaw's cam_xyz, despite its name.
    img.camToXyz = {1.7166512f, -0.3556708f, -0.2533663f,
                    -0.6666844f, 1.6164812f, 0.0157685f,
                    0.0176399f, -0.0427706f, 0.9421031f};
    img.black = {512, 512, 512, 512};
    const double range = img.white - 512.0;
    std::mt19937 rng(8);
    std::normal_distribution<double> g(0.0, 1.0);
    for (std::uint32_t y = 0; y < kN; ++y)
        for (std::uint32_t x = 0; x < kN; ++x) {
            const int c = int(img.channelAt(x, y)) == 3 ? 1 : int(img.channelAt(x, y));
            const double v = kLevel * kCamMul[1] / kCamMul[std::size_t(c)];
            const double s = v + std::sqrt(kA * v + kB) * g(rng);
            img.samples[std::size_t(y) * kN + x] =
                std::uint16_t(std::clamp(512.0 + s * range, 0.0, 65535.0) + 0.5);
        }
    return img;
}

int nodeNamed(const orion::pipe::DevelopPipeline& d, const std::string& name) {
    for (std::size_t i = 0; i < d.graph().nodeCount(); ++i)
        if (d.graph().nodeName(int(i)) == name) return int(i);
    return -1;
}

}  // namespace

/// The tables against the demosaic they describe: each scale's deviation of
/// RCD's output on the held-out card, against `captureScaleNoise`.
void testStarletNoiseGpu() {
    section("Denoise: the starlet's noise per scale after RCD (GPU)");
    std::unique_ptr<orion::gpu::Device> device;
    try { device = orion::gpu::Device::create(); }
    catch (const std::exception& e) { report(false, "GPU device available", e.what()); return; }

    const auto img = card();
    orion::pipe::DevelopPipeline d(*device, ORION_SHADER_DIR, img);
    d.setNoiseProfile({float(kA), float(kB), true});
    orion::pipe::Adjustments adj;
    adj.wb = d.asShotWhiteBalance();
    d.apply(adj); d.render();
    const Scales s = measure(read(d.graph().nodeOutput(nodeNamed(d, "rcd:red/blue"))));

    // What the chain thresholded with before: white noise's norm on the
    // per-pixel luma model, for chroma too.
    double whiteVar = 0.0;
    for (int c = 0; c < 3; ++c) {
        const double m = kCamMul[std::size_t(c)] / kCamMul[1];
        whiteVar += kLumaW[c] * kLumaW[c] * (m * kA * s.y + m * m * kB);
    }
    std::printf("  Y %.4f; j: luma measured/model (/white), chroma measured/model (/luma's white)\n", s.y);
    double worstLuma = 0.0, worstChroma = 0.0;
    for (int j = 0; j < 6; ++j) {
        const auto n = d.captureScaleNoise(adj, j);
        const double luma = std::sqrt(n.lumaA * s.y + n.lumaB);
        const double chroma = std::sqrt(n.chromaA * s.y + n.chromaB);
        const double white = orion::pipe::starlet::kWhiteNorm[j] * std::sqrt(whiteVar);
        std::printf("    %d: %.3f (%.3f)  %.3f (%.3f)\n", j, s.luma[j] / luma, s.luma[j] / white,
                    s.chroma[j] / chroma, s.chroma[j] / white);
        if (j < 4) worstLuma = std::max(worstLuma, std::fabs(s.luma[j] / luma - 1.0));
        worstChroma = std::max(worstChroma, std::fabs(s.chroma[j] / chroma - 1.0));
    }
    report(worstLuma < 0.1, "the luma model is within 10% of RCD's noise at every scale the chain shrinks",
           std::to_string(worstLuma));
    report(worstChroma < 0.1, "and the chroma model within 10% at all six",
           std::to_string(worstChroma));
}

/// What the capture chain leaves on the same card: the four scales' sum, the
/// picture less the input's own c_4, read as an RMS (the garrote leaves a
/// sparse residual a MAD would not see). Luma at Luminance 2 past a 2 px
/// blur, which is where j = 2, 3 live; chroma at Color 3 past a 1 px blur,
/// the 2 to 16 px speckle. With white noise's norms those scales were
/// thresholded at 1.6x (luma) and 2.2-3.3x (chroma) under their noise, and
/// kept 0.50 and 0.46 (measured); the measured tables keep 0.43 and 0.18.
void testDenoiseResidualGpu() {
    section("Denoise: residual noise on a high-ISO card (GPU)");
    std::unique_ptr<orion::gpu::Device> device;
    try { device = orion::gpu::Device::create(); }
    catch (const std::exception& e) { report(false, "GPU device available", e.what()); return; }

    orion::pipe::DevelopPipeline d(*device, ORION_SHADER_DIR, card());
    d.setNoiseProfile({float(kA), float(kB), true});
    orion::pipe::Adjustments base;
    base.wb = d.asShotWhiteBalance();
    const auto render = [&](float luma, float color) {
        auto a = base;
        a.denoiseLuma = luma;
        a.denoiseColor = color;
        d.apply(a); d.render();
        return read(d.referenceImage());
    };
    const Plane off = render(0.0f, 0.0f);
    Plane residual = off;
    for (int j = 0; j < 4; ++j) residual = blur(residual, 1 << j);
    // RMS of the picture's detail (less the input's c_4), luma or chroma,
    // after `steps` blurs of the transform's own.
    const auto rms = [&](const Plane& p, int steps, bool chroma) {
        Plane detail(p.size());
        for (std::size_t i = 0; i < p.size(); ++i) detail[i] = p[i] - residual[i];
        for (int j = 0; j < steps; ++j) detail = blur(detail, 1 << j);
        double sum = 0.0; long n = 0;
        for (int y = kBorder; y < kN - kBorder; ++y)
            for (int x = kBorder; x < kN - kBorder; ++x) {
                const double* v = &detail[(std::size_t(y) * kN + x) * 3];
                const double l = kLumaW[0] * v[0] + kLumaW[1] * v[1] + kLumaW[2] * v[2];
                if (chroma) for (int k = 0; k < 3; ++k) { sum += (v[k] - l) * (v[k] - l) / 3.0; }
                else sum += l * l;
                ++n;
            }
        return std::sqrt(sum / double(n));
    };
    const Plane luma = render(2.0f, 0.0f), color = render(0.0f, 3.0f);
    const double lumaAll = rms(luma, 0, false) / rms(off, 0, false);
    const double lumaCoarse = rms(luma, 2, false) / rms(off, 2, false);
    const double speckle = rms(color, 1, true) / rms(off, 1, true);
    std::printf("  kept: luma %.3f of all four scales, %.3f past 2 px; chroma %.3f past 1 px\n",
                lumaAll, lumaCoarse, speckle);
    report(lumaCoarse < 0.47, "Luminance 2 leaves under 0.47 of the luma noise past 2 px",
           std::to_string(lumaCoarse));
    report(speckle < 0.25, "Color 3 leaves under a quarter of the 2 to 16 px colour speckle",
           std::to_string(speckle));
}
