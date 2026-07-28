/*  orion-bench — the M0 gate.
 *
 *  Decodes a raw file, builds the seven-node GPU pipeline, and measures how
 *  long an adjustment re-render takes. M0 passes when that lands under 16 ms.
 *  It also writes a PNG, because a latency number nobody can look at is only
 *  half the evidence.
 */

#include "gpu/MetalDevice.h"
#include "gpu/Resources.h"
#include "pipe/Pipeline.h"
#include "pipe/ShaderParams.h"
#include "raw/RawImage.h"
#include "util/ImageWriter.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <exception>
#include <numeric>
#include <string>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;
namespace params = orion::pipe::params;

double msSince(Clock::time_point t) {
    return std::chrono::duration<double, std::milli>(Clock::now() - t).count();
}

/// Inverts a 3x3, for the camera-matrix step.
bool invert3x3(const float m[9], float out[9]) {
    const float det =
        m[0] * (m[4] * m[8] - m[5] * m[7]) -
        m[1] * (m[3] * m[8] - m[5] * m[6]) +
        m[2] * (m[3] * m[7] - m[4] * m[6]);
    if (std::abs(det) < 1e-12f) return false;

    const float inv = 1.0f / det;
    out[0] = (m[4] * m[8] - m[5] * m[7]) * inv;
    out[1] = (m[2] * m[7] - m[1] * m[8]) * inv;
    out[2] = (m[1] * m[5] - m[2] * m[4]) * inv;
    out[3] = (m[5] * m[6] - m[3] * m[8]) * inv;
    out[4] = (m[0] * m[8] - m[2] * m[6]) * inv;
    out[5] = (m[2] * m[3] - m[0] * m[5]) * inv;
    out[6] = (m[3] * m[7] - m[4] * m[6]) * inv;
    out[7] = (m[1] * m[6] - m[0] * m[7]) * inv;
    out[8] = (m[0] * m[4] - m[1] * m[3]) * inv;
    return true;
}

void multiply3x3(const float a[9], const float b[9], float out[9]) {
    for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 3; ++c)
            out[r * 3 + c] = a[r * 3 + 0] * b[0 * 3 + c] +
                             a[r * 3 + 1] * b[1 * 3 + c] +
                             a[r * 3 + 2] * b[2 * 3 + c];
}

// CIE XYZ (D65) -> linear Rec.2020, the working space.
constexpr float kXyzToRec2020[9] = {
     1.7166512f, -0.3556708f, -0.2533663f,
    -0.6666844f,  1.6164812f,  0.0157685f,
     0.0176399f, -0.0427706f,  0.9421031f,
};

/// Scales each row so it sums to 1, making camera (1,1,1) map to working
/// (1,1,1). Without this the white balance and the colour matrix fight each
/// other: the data is already neutral after WB, and an unnormalised matrix
/// then tints it. dcraw does the same thing to rgb_cam.
void normaliseRows(float m[9]) {
    for (int r = 0; r < 3; ++r) {
        const float sum = m[r * 3 + 0] + m[r * 3 + 1] + m[r * 3 + 2];
        if (std::abs(sum) < 1e-9f) continue;
        for (int c = 0; c < 3; ++c) m[r * 3 + c] /= sum;
    }
}

/// Min / max / mean of a texture, so a stage that silently collapses to zero
/// shows up immediately instead of as a black PNG at the end.
struct Range { double lo, hi, mean; };

Range rangeOf(const orion::gpu::Texture& tex) {
    using orion::gpu::PixelFormat;
    const std::size_t w = tex.width(), h = tex.height();
    const std::size_t bpp = orion::gpu::bytesPerPixel(tex.format());
    std::vector<std::uint8_t> buf(w * h * bpp);
    tex.download(buf.data(), w * bpp);

    Range r{1e30, -1e30, 0.0};
    double sum = 0.0;
    std::size_t n = 0;

    auto note = [&](double v) {
        r.lo = std::min(r.lo, v);
        r.hi = std::max(r.hi, v);
        sum += v;
        ++n;
    };

    // Sample a stride rather than every pixel — this is diagnostics, not a
    // histogram, and 24 MP of float conversion per stage is wasted time.
    const std::size_t stride = 37;
    switch (tex.format()) {
        case PixelFormat::R16Uint: {
            auto* p = reinterpret_cast<const std::uint16_t*>(buf.data());
            for (std::size_t i = 0; i < w * h; i += stride) note(p[i]);
            break;
        }
        case PixelFormat::R32Float: {
            auto* p = reinterpret_cast<const float*>(buf.data());
            for (std::size_t i = 0; i < w * h; i += stride) note(p[i]);
            break;
        }
        case PixelFormat::RGBA16Float: {
            auto* p = reinterpret_cast<const __fp16*>(buf.data());
            for (std::size_t i = 0; i < w * h; i += stride)
                for (int c = 0; c < 3; ++c) note(static_cast<double>(p[i * 4 + c]));
            break;
        }
        case PixelFormat::RGBA8Unorm: {
            auto* p = buf.data();
            for (std::size_t i = 0; i < w * h; i += stride)
                for (int c = 0; c < 3; ++c) note(p[i * 4 + c] / 255.0);
            break;
        }
    }
    r.mean = n ? sum / static_cast<double>(n) : 0.0;
    return r;
}

struct Stats {
    double min = 0, median = 0, p95 = 0, mean = 0;
};

Stats summarise(std::vector<double> v) {
    std::sort(v.begin(), v.end());
    Stats s;
    s.min    = v.front();
    s.median = v[v.size() / 2];
    s.p95    = v[static_cast<std::size_t>(v.size() * 0.95)];
    s.mean   = std::accumulate(v.begin(), v.end(), 0.0) / static_cast<double>(v.size());
    return s;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: orion-bench <raw-file> [output.png]\n");
        return 2;
    }
    const std::string path = argv[1];
    const std::string outPng = (argc > 2) ? argv[2] : "orion-out.png";

    try {
        // ── Decode ────────────────────────────────────────────────────────
        auto t0 = Clock::now();
        const auto img = orion::raw::decodeBayer(path);
        const double decodeMs = msSince(t0);
        const double mp = static_cast<double>(img.pixelCount()) / 1.0e6;

        std::printf("Source\n");
        std::printf("  file           %s\n", path.c_str());
        std::printf("  camera         %s\n", img.camera.c_str());
        std::printf("  mosaic         %u x %u  (%.1f MP, %s)\n",
                    img.width, img.height, mp, img.patternString().c_str());
        std::printf("  black / white  %u / %u\n", img.black[0], img.white);
        std::printf("  decode         %.1f ms  (%.0f MP/s)\n\n",
                    decodeMs, mp / (decodeMs / 1000.0));

        // ── GPU ───────────────────────────────────────────────────────────
        auto device = orion::gpu::Device::create();

        std::printf("Device\n  %s  (unified memory: %s)\n\n",
                    device->info().name.c_str(),
                    device->info().unifiedMemory ? "yes" : "no");

        // ── Build the graph ───────────────────────────────────────────────
        orion::pipe::Pipeline pipe(*device, ORION_SHADER_DIR);
        using orion::gpu::PixelFormat;
        using orion::pipe::kSource;

        const int nLinear = pipe.add({"linearize", "linearize", {kSource},
                                      PixelFormat::R32Float, {}});
        const int nDirs   = pipe.add({"rcd:dirs", "rcdDirs", {nLinear},
                                      PixelFormat::R32Float, {}});
        const int nGreen  = pipe.add({"rcd:green", "rcdGreen", {nLinear, nDirs},
                                      PixelFormat::R32Float, {}});
        const int nRgb    = pipe.add({"rcd:red/blue", "rcdRedBlue", {nLinear, nGreen},
                                      PixelFormat::RGBA16Float, {}});
        const int nMatrix = pipe.add({"camera->working", "cameraToWorking", {nRgb},
                                      PixelFormat::RGBA16Float, {}});
        const int nExpo   = pipe.add({"exposure", "exposure", {nMatrix},
                                      PixelFormat::RGBA16Float, {}});
        const int nAgx    = pipe.add({"agx", "agx", {nExpo},
                                      PixelFormat::RGBA8Unorm, {}});

        pipe.compile(img.width, img.height);

        // ── Parameters ────────────────────────────────────────────────────
        const std::uint32_t size[2] = {img.width, img.height};

        // White balance, normalised so green is unity.
        const float g = (img.camMul[1] != 0.0f) ? img.camMul[1] : 1.0f;
        params::Linearize lin{};
        for (int c = 0; c < 4; ++c) {
            lin.black[c] = static_cast<float>(img.black[c]);
            const float mul = (c == 3) ? img.camMul[1] : img.camMul[c];
            lin.whiteBalance[c] = (mul != 0.0f ? mul : g) / g;
        }
        lin.invRange = 1.0f / static_cast<float>(img.white - img.black[0]);
        lin.filters  = img.filters;
        lin.size[0] = size[0]; lin.size[1] = size[1];
        pipe.setParams(nLinear, &lin, sizeof lin);

        params::Dirs dirs{{size[0], size[1]}};
        pipe.setParams(nDirs, &dirs, sizeof dirs);

        params::Green green{{size[0], size[1]}, img.filters, 0};
        pipe.setParams(nGreen, &green, sizeof green);
        pipe.setParams(nRgb,   &green, sizeof green);

        // camera -> XYZ -> Rec.2020. LibRaw gives XYZ->camera, so invert first.
        float xyzToCam[9], camToXyz[9], camToWorking[9];
        std::copy_n(img.camToXyz.begin(), 9, xyzToCam);
        if (!invert3x3(xyzToCam, camToXyz)) {
            throw std::runtime_error("camera colour matrix is singular");
        }
        multiply3x3(kXyzToRec2020, camToXyz, camToWorking);
        normaliseRows(camToWorking);

        params::ColorMatrix mat{};
        for (int c = 0; c < 3; ++c) {
            mat.row0[c] = camToWorking[0 * 3 + c];
            mat.row1[c] = camToWorking[1 * 3 + c];
            mat.row2[c] = camToWorking[2 * 3 + c];
        }
        mat.size[0] = size[0]; mat.size[1] = size[1];
        pipe.setParams(nMatrix, &mat, sizeof mat);

        params::Exposure expo{0.0f, 0.0f, {size[0], size[1]}};
        pipe.setParams(nExpo, &expo, sizeof expo);

        params::Agx agx{1.0f, -2.5f, 1.0f, 0.0f, {size[0], size[1]}};
        pipe.setParams(nAgx, &agx, sizeof agx);

        std::printf("Pipeline  %zu nodes, %.0f MiB of intermediates\n",
                    pipe.nodeCount(),
                    static_cast<double>(pipe.intermediateBytes()) / (1024.0 * 1024.0));

        // ── Cold render (everything runs) ─────────────────────────────────
        pipe.setSource(img.samples.data(), static_cast<std::size_t>(img.width) * 2);
        const double coldMs = pipe.render();
        std::printf("  full render    %.2f ms   (all nodes)\n", coldMs);

        // ── Stage ranges ──────────────────────────────────────────────────
        std::printf("\nStage output ranges (min / max / mean)\n");
        {
            const Range src = rangeOf(pipe.sourceTexture());
            std::printf("  %-16s %10.4f %10.4f %10.4f\n", "source", src.lo, src.hi, src.mean);
        }
        for (int id = 0; id < static_cast<int>(pipe.nodeCount()); ++id) {
            const Range r = rangeOf(pipe.nodeOutput(id));
            std::printf("  %-16s %10.4f %10.4f %10.4f\n",
                        pipe.lastRun()[id].name.c_str(), r.lo, r.hi, r.mean);
        }
        std::printf("\n");

        // ── Warm render: the slider case ──────────────────────────────────
        // Only exposure changes, so linearize + all three RCD passes + the
        // colour matrix are cached; just exposure and AgX recompute. This is
        // the number the 16 ms budget is actually about.
        constexpr int kIterations = 60;
        std::vector<double> warm;
        warm.reserve(kIterations);

        for (int i = 0; i < kIterations; ++i) {
            expo.ev = std::sin(static_cast<float>(i) * 0.1f) * 1.5f;
            pipe.setParams(nExpo, &expo, sizeof expo);
            warm.push_back(pipe.render());
        }

        const Stats s = summarise(warm);
        int ran = 0;
        for (const auto& n : pipe.lastRun()) if (n.executed) ++ran;

        std::printf("  exposure drag  %.2f ms   (%d of %zu nodes recomputed)\n\n",
                    s.median, ran, pipe.nodeCount());

        std::printf("Exposure-slider latency over %d frames, full resolution\n", kIterations);
        std::printf("  min %.2f ms   median %.2f ms   p95 %.2f ms   mean %.2f ms\n",
                    s.min, s.median, s.p95, s.mean);

        const bool pass = s.p95 < 16.0;
        std::printf("\n  M0 gate (<16 ms at p95): %s  [%.2f ms]\n",
                    pass ? "PASS" : "FAIL", s.p95);

        // ── Write something we can look at ────────────────────────────────
        expo.ev = 0.0f;
        pipe.setParams(nExpo, &expo, sizeof expo);
        pipe.render();

        const auto& out = pipe.output();
        const std::size_t rowBytes = static_cast<std::size_t>(out.width()) * 4;
        std::vector<std::uint8_t> pixels(rowBytes * out.height());
        out.download(pixels.data(), rowBytes);
        orion::util::writePng(outPng, pixels.data(), out.width(), out.height(), rowBytes);
        std::printf("\nWrote %s  (%u x %u)\n", outPng.c_str(), out.width(), out.height());

        return pass ? 0 : 1;

    } catch (const std::exception& e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }
}
