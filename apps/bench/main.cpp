/*  orion-bench — the M0 gate, and the pipeline's regression harness.
 *
 *  Drives DevelopPipeline, the same graph the app uses, so what is measured
 *  here is what you see on screen. Writes PNGs too: a latency number nobody
 *  can look at is only half the evidence.
 */

#include "gpu/MetalDevice.h"
#include "gpu/Resources.h"
#include "pipe/DevelopPipeline.h"
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

double msSince(Clock::time_point t) {
    return std::chrono::duration<double, std::milli>(Clock::now() - t).count();
}

struct Stats { double min, median, p95, mean; };

/// Drops the first few frames: the first dispatch of a pipeline state pays for
/// shader warm-up and first-touch page faults, which is real but is not what a
/// slider drag costs.
constexpr int kWarmupFrames = 8;

Stats summarise(std::vector<double> v) {
    if (v.size() > kWarmupFrames) v.erase(v.begin(), v.begin() + kWarmupFrames);
    std::sort(v.begin(), v.end());
    return {v.front(),
            v[v.size() / 2],
            v[static_cast<std::size_t>(v.size() * 0.95)],
            std::accumulate(v.begin(), v.end(), 0.0) / static_cast<double>(v.size())};
}

void writeOut(const orion::pipe::DevelopPipeline& d, const std::string& path) {
    const std::uint32_t w = d.outputWidth(), h = d.outputHeight();
    const std::size_t rowBytes = static_cast<std::size_t>(w) * 4;
    std::vector<std::uint8_t> pixels(rowBytes * h);
    d.output().download(pixels.data(), rowBytes, w, h);
    orion::util::writePng(path, pixels.data(), w, h, rowBytes);
    std::printf("  wrote %s  (%u x %u)\n", path.c_str(), w, h);
}

/// What a control is expected to move.
///
/// Saturation and vibrance preserve luminance *by construction* — the mean of
/// y + (c - y)*a weighted by the luminance weights is exactly y — so measuring
/// mean luma reported them as doing nothing, every run. A benchmark that cries
/// wolf trains you to ignore it, which is worse than not having one.
enum class Metric { Luma, Chroma };

/// Mean luma, or mean distance from grey, for asserting that an adjustment
/// actually did something rather than silently no-op'ing.
double meanOf(const orion::pipe::DevelopPipeline& d, Metric metric) {
    const std::uint32_t tw = d.outputWidth(), th = d.outputHeight();
    const std::size_t rowBytes = static_cast<std::size_t>(tw) * 4;
    std::vector<std::uint8_t> pixels(rowBytes * th);
    d.output().download(pixels.data(), rowBytes, tw, th);

    double sum = 0.0;
    std::size_t n = 0;
    for (std::size_t i = 0; i < static_cast<std::size_t>(tw) * th; i += 37) {
        const double r = pixels[i * 4 + 0];
        const double g = pixels[i * 4 + 1];
        const double b = pixels[i * 4 + 2];
        const double y = 0.2126 * r + 0.7152 * g + 0.0722 * b;
        sum += (metric == Metric::Luma)
             ? y
             : (std::abs(r - y) + std::abs(g - y) + std::abs(b - y)) / 3.0;
        ++n;
    }
    return n ? sum / static_cast<double>(n) / 255.0 : 0.0;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: orion-bench <raw-file> [output-prefix]\n");
        return 2;
    }
    const std::string path = argv[1];
    const std::string prefix = (argc > 2) ? argv[2] : "orion";

    try {
        // ── Decode ────────────────────────────────────────────────────────
        const auto t0 = Clock::now();
        const auto img = orion::raw::decodeBayer(path);
        const double decodeMs = msSince(t0);
        const double mp = static_cast<double>(img.pixelCount()) / 1.0e6;

        std::printf("Source\n");
        std::printf("  camera         %s\n", img.camera.c_str());
        std::printf("  mosaic         %u x %u  (%.1f MP, %s)\n",
                    img.width, img.height, mp, img.patternString().c_str());
        std::printf("  decode         %.1f ms  (%.0f MP/s)\n\n",
                    decodeMs, mp / (decodeMs / 1000.0));

        auto device = orion::gpu::Device::create();
        std::printf("Device\n  %s  (unified memory: %s)\n\n",
                    device->info().name.c_str(),
                    device->info().unifiedMemory ? "yes" : "no");

        orion::pipe::DevelopPipeline develop(*device, ORION_SHADER_DIR, img);
        orion::pipe::Adjustments adj;

        std::printf("Pipeline  %zu nodes, %.0f MiB of intermediates\n",
                    develop.graph().nodeCount(),
                    static_cast<double>(develop.graph().intermediateBytes()) / (1024.0 * 1024.0));

        develop.apply(adj);
        std::printf("  full render    %.2f ms   (all nodes)\n", develop.render());

        // ── The slider case ───────────────────────────────────────────────
        // Only exposure moves, so linearize, all three demosaic passes and the
        // colour matrix stay cached. This is the number the budget is about.
        constexpr int kIterations = 60;
        std::vector<double> warm;
        warm.reserve(kIterations);

        for (int i = 0; i < kIterations; ++i) {
            // Every value distinct, so no frame is accidentally a no-op.
            adj.exposureEv = -1.5f + 3.0f * static_cast<float>(i) / (kIterations - 1);
            develop.apply(adj);
            warm.push_back(develop.render());
        }

        int ran = 0;
        for (const auto& n : develop.graph().lastRun()) if (n.executed) ++ran;
        const Stats s = summarise(warm);

        std::printf("  exposure drag  %.2f ms   (%d of %zu nodes recomputed)\n\n",
                    s.median, ran, develop.graph().nodeCount());
        std::printf("Exposure-slider latency over %d frames, full resolution\n", kIterations);
        std::printf("  min %.2f   median %.2f   p95 %.2f   mean %.2f  (ms)\n",
                    s.min, s.median, s.p95, s.mean);

        const bool pass = s.p95 < 16.0;
        std::printf("\n  M0 gate (<16 ms at p95): %s  [%.2f ms]\n\n",
                    pass ? "PASS" : "FAIL", s.p95);

        // ── Every control does something ──────────────────────────────────
        // A slider that silently no-ops is worse than one that is missing, so
        // assert each moves the image before trusting any of it.
        std::printf("Control check (mean luma, identity = %.4f)\n", [&] {
            orion::pipe::Adjustments base;
            base.wb = develop.asShotWhiteBalance();
            develop.apply(base);
            develop.render();
            return meanOf(develop, Metric::Luma);
        }());

        {
            orion::pipe::Adjustments base;
            base.wb = develop.asShotWhiteBalance();

            struct Probe {
                const char* name;
                void (*set)(orion::pipe::Adjustments&);
                Metric metric = Metric::Luma;
            };
            const Probe probes[] = {
                {"exposure +1 EV", [](auto& a) { a.exposureEv = 1.0f; }},
                {"highlights -1*", [](auto& a) { a.exposureEv = 5.5f; a.highlights = -1.0f; }},
                {"shadows +1",     [](auto& a) { a.shadows = 1.0f; }},
                {"whites +1*",     [](auto& a) { a.exposureEv = 5.5f; a.whites = 1.0f; }},
                {"blacks -1",      [](auto& a) { a.blacks = -1.0f; }},
                {"vibrance +1",    [](auto& a) { a.vibrance = 1.0f; }, Metric::Chroma},
                {"saturation -1",  [](auto& a) { a.saturation = -1.0f; }, Metric::Chroma},
                {"contrast 1.5",   [](auto& a) { a.contrast = 1.5f; }},
                {"temp 3000K",     [](auto& a) { a.wb.temperatureK = 3000.0f; }},
                {"tint +0.5",      [](auto& a) { a.wb.tint += 0.5f; }},
                {"sharpen 1.0",    [](auto& a) { a.sharpenAmount = 1.0f; }},
                {"mixer blue lum", [](auto& a) { a.lumShift[5] = -1.0f; }},
                {"mixer blue sat", [](auto& a) { a.satShift[5] = 1.0f; }, Metric::Chroma},
            };

            develop.apply(base);
            develop.render();
            const double ref = meanOf(develop, Metric::Luma);
            const double refChroma = meanOf(develop, Metric::Chroma);

            // Starred probes are measured against a +3 EV baseline: this frame
            // is a night sky, and highlight recovery correctly does nothing
            // when there are no highlights to recover.
            auto lifted = base;
            lifted.exposureEv = 5.5f;
            develop.apply(lifted);
            develop.render();
            const double liftedRef = meanOf(develop, Metric::Luma);

            for (const auto& probe : probes) {
                auto a = base;
                probe.set(a);
                develop.apply(a);
                const double ms = develop.render();
                const double value = meanOf(develop, probe.metric);
                const bool starred = std::string(probe.name).find('*') != std::string::npos;
                const double against = (probe.metric == Metric::Chroma)
                    ? refChroma
                    : (starred ? liftedRef : ref);
                int nodes = 0;
                for (const auto& n : develop.graph().lastRun()) if (n.executed) ++nodes;
                std::printf("  %-16s %7.4f %-7s (%+.4f)  %6.2f ms  %2d nodes  %s\n",
                            probe.name, value,
                            probe.metric == Metric::Chroma ? "chroma" : "luma",
                            value - against, ms, nodes,
                            std::abs(value - against) > 2e-4 ? "ok" : "NO EFFECT");
            }
            develop.apply(base);
            develop.render();
        }

        // ── Tone curve ────────────────────────────────────────────────────
        std::printf("\nTone curve\n");

        adj = {};
        adj.wb = develop.asShotWhiteBalance();
        develop.apply(adj);
        develop.render();
        const double flatLuma = meanOf(develop, Metric::Luma);
        writeOut(develop, prefix + "-flat.png");

        // A film-style S: lift the shoulder, drop the toe.
        adj.curve.master = {{0.0f, 0.0f}, {0.25f, 0.14f}, {0.75f, 0.86f}, {1.0f, 1.0f}};
        develop.apply(adj);

        std::vector<double> curveTimes;
        for (int i = 0; i < kIterations; ++i) {
            // Nudge a control point so the LUT genuinely rebuilds each frame.
            adj.curve.master[1].y = 0.14f + 0.02f * std::sin(static_cast<float>(i) * 0.3f);
            develop.apply(adj);
            curveTimes.push_back(develop.render());
        }
        const Stats cs = summarise(curveTimes);
        const double curvedLuma = meanOf(develop, Metric::Luma);
        writeOut(develop, prefix + "-curved.png");

        std::printf("  curve drag     %.2f ms median, %.2f ms p95  (1 of 8 nodes)\n",
                    cs.median, cs.p95);
        std::printf("  mean luma      %.4f flat -> %.4f curved\n", flatLuma, curvedLuma);

        const bool curveWorks = std::abs(curvedLuma - flatLuma) > 1e-4;
        std::printf("  curve changed the image: %s\n", curveWorks ? "yes" : "NO — BUG");

        // ── Export ────────────────────────────────────────────────────────
        std::printf("\nExport\n");
        {
            orion::pipe::Adjustments base;
            base.wb = develop.asShotWhiteBalance();
            develop.apply(base);
            develop.render();

            const std::uint32_t ew = develop.outputWidth();
            const std::uint32_t eh = develop.outputHeight();
            const std::size_t rowBytes = static_cast<std::size_t>(ew) * 4;
            std::vector<std::uint8_t> pixels(rowBytes * eh);
            develop.output().download(pixels.data(), rowBytes, ew, eh);

            struct Case { const char* suffix; orion::util::ExportOptions opts; };
            const Case cases[] = {
                {"-full.jpg", {orion::util::ImageFormat::Jpeg, 0.92f, 0}},
                {"-web.jpg",  {orion::util::ImageFormat::Jpeg, 0.85f, 2048}},
                {"-16bit.tif",{orion::util::ImageFormat::Tiff, 1.0f,  0}},
            };

            for (const auto& c : cases) {
                const std::string out = prefix + c.suffix;
                const auto t = Clock::now();
                orion::util::writeImage(out, pixels.data(), ew, eh, rowBytes, c.opts);
                std::printf("  %-14s %6.0f ms\n", c.suffix, msSince(t));
            }
        }

        return (pass && curveWorks) ? 0 : 1;

    } catch (const std::exception& e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }
}
