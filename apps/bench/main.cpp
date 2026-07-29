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
#include <iterator>
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

/// The developed output as 16-bit unsigned. The graph ends in half float; the
/// image formats want integers.
/// Format-aware: the tail of the graph is eight bits for the screen and
/// sixteen only around an export. Reading an RGBA8Unorm texture with a stride
/// computed for half float does not fail, it just returns nonsense — which is
/// exactly what it did, and every mean in this file went with it.
std::vector<std::uint16_t> output16(const orion::pipe::DevelopPipeline& d,
                                    std::uint32_t w, std::uint32_t h) {
    const std::size_t count = std::size_t(w) * h * 4;
    std::vector<std::uint16_t> out(count);

    if (d.output().format() == orion::gpu::PixelFormat::RGBA16Float) {
        std::vector<__fp16> half(count);
        d.output().download(half.data(), std::size_t(w) * 4 * sizeof(__fp16), w, h);
        for (std::size_t i = 0; i < count; ++i) {
            const float v = std::clamp(float(half[i]), 0.0f, 1.0f);
            out[i] = static_cast<std::uint16_t>(v * 65535.0f + 0.5f);
        }
    } else {
        std::vector<std::uint8_t> bytes(count);
        d.output().download(bytes.data(), std::size_t(w) * 4, w, h);
        for (std::size_t i = 0; i < count; ++i) {
            out[i] = static_cast<std::uint16_t>(bytes[i] * 257);   // 255 -> 65535
        }
    }
    return out;
}

/// Mean absolute per-channel change between two renders, in 0..1 display units.
///
/// The summary metrics below answer *what* changed — brightness, colourfulness,
/// detail. This answers *how much*, and it is the one that cannot cancel. A
/// grading wheel rotates hue at constant saturation, so it moves every pixel
/// and changes the frame's mean luma, mean chroma and mean saturation by very
/// little; three separate instruments all reported it as doing nothing. A
/// per-pixel difference cannot be fooled that way, so it is what the pass/fail
/// gate reads.
double meanAbsDiff(const std::vector<std::uint16_t>& a,
                   const std::vector<std::uint16_t>& b) {
    if (a.size() != b.size() || a.empty()) return 0.0;
    double sum = 0.0;
    std::size_t n = 0;
    for (std::size_t i = 0; i + 3 < a.size(); i += 4 * 37) {
        for (int c = 0; c < 3; ++c) {
            sum += std::abs(double(a[i + c]) - double(b[i + c]));
            ++n;
        }
    }
    return n ? sum / double(n) / 65535.0 : 0.0;
}

void writeOut(const orion::pipe::DevelopPipeline& d, const std::string& path) {
    const std::uint32_t w = d.outputWidth(), h = d.outputHeight();
    const auto pixels = output16(d, w, h);
    orion::util::writePng(path, pixels.data(), w, h,
                          std::size_t(w) * 4 * sizeof(std::uint16_t));
    std::printf("  wrote %s  (%u x %u)\n", path.c_str(), w, h);
}

/// What a control is expected to move.
///
/// Saturation and vibrance preserve luminance *by construction* — the mean of
/// y + (c - y)*a weighted by the luminance weights is exactly y — so measuring
/// mean luma reported them as doing nothing, every run. A benchmark that cries
/// wolf trains you to ignore it, which is worse than not having one.
/// Detail is neighbour-to-neighbour luma difference. Sharpening adds as much
/// as it subtracts around an edge, so it barely moves a mean by construction —
/// on `_PIC8220.ARW` a full-strength unsharp mask moved mean luma by -0.0005,
/// under the noise floor of every other probe. Measuring an edge filter by the
/// image's average brightness is the same mistake as asserting that a slider
/// changed *something*: the instrument cannot see the thing it is pointed at.
/// Saturation is chroma *relative* to the pixel — `(max - min) / max`, the same
/// measure `--measure` reports. It is the right instrument for the grading
/// wheels, whose offsets scale with luminance on purpose: an absolute chroma
/// metric on a night frame reports a scale-invariant grade as almost nothing,
/// because near-black pixels have small absolute deviations however hard you
/// tint them. Measuring a relative control with an absolute ruler is the same
/// mistake as measuring an edge filter by mean brightness.
enum class Metric { Luma, Chroma, Detail, Saturation };

/// Mean luma, or mean distance from gray, for asserting that an adjustment
/// actually did something rather than silently no-op'ing.
double meanOf(const orion::pipe::DevelopPipeline& d, Metric metric) {
    const std::uint32_t tw = d.outputWidth(), th = d.outputHeight();
    const auto pixels = output16(d, tw, th);

    const auto lumaAt = [&](std::size_t i) {
        const double r = pixels[i * 4 + 0] / 257.0;   // 16-bit to the 0..255
        const double g = pixels[i * 4 + 1] / 257.0;   // scale these numbers
        const double b = pixels[i * 4 + 2] / 257.0;   // have always been on
        return 0.2126 * r + 0.7152 * g + 0.0722 * b;
    };

    const std::size_t count = static_cast<std::size_t>(tw) * th;
    double sum = 0.0;
    std::size_t n = 0;
    for (std::size_t i = 0; i + 1 < count; i += 37) {
        const double y = lumaAt(i);
        switch (metric) {
            case Metric::Luma:
                sum += y;
                break;
            case Metric::Chroma: {
                const double r = pixels[i * 4 + 0] / 257.0;
                const double g = pixels[i * 4 + 1] / 257.0;
                const double b = pixels[i * 4 + 2] / 257.0;
                sum += (std::abs(r - y) + std::abs(g - y) + std::abs(b - y)) / 3.0;
                break;
            }
            case Metric::Saturation: {
                const double r = pixels[i * 4 + 0] / 257.0;
                const double g = pixels[i * 4 + 1] / 257.0;
                const double b = pixels[i * 4 + 2] / 257.0;
                const double mx = std::max(r, std::max(g, b));
                const double mn = std::min(r, std::min(g, b));
                // Below a quarter of an 8-bit step there is no colour to speak
                // of, only quantisation, and dividing by it manufactures
                // saturation out of noise.
                sum += mx > 0.25 ? (mx - mn) / mx * 255.0 : 0.0;
                break;
            }
            case Metric::Detail:
                // The horizontal neighbour. The stride steps within a row for
                // all but one sample in 163, so wrapping is noise.
                sum += std::abs(y - lumaAt(i + 1));
                break;
        }
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
        // color matrix stay cached. This is the number the budget is about.
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
        // Every check below can fail the run. They could not before: the
        // thirteen control probes printed NO EFFECT and the exit code ignored
        // them, so a silently dead slider still exited 0.
        bool controlsPass = true;
        bool invariantsPass = true;
        std::printf("\n  M0 gate (<16 ms at p95): %s  [%.2f ms]\n\n",
                    pass ? "PASS" : "FAIL", s.p95);

        // ── What the sixteen-bit tail costs ───────────────────────────────
        //
        // The numbers above are the screen path: display and geometry write
        // eight bits, because the drawable is bgra8Unorm and anything wider is
        // bytes moved for precision nothing can show. Export widens the tail
        // and pays this.
        //
        // Measured in the same process, interleaved, because this machine
        // throttles across a long bench session — two runs minutes apart differ
        // by more than the effect being measured. The second narrow run is the
        // drift check: if it disagrees with the first, the comparison is noise.
        {
            develop.setWideOutput(true);
            std::vector<double> narrow;   // holds the wide run
            narrow.reserve(kIterations);
            for (int i = 0; i < kIterations; ++i) {
                adj.exposureEv = -1.5f + 3.0f * static_cast<float>(i) / (kIterations - 1);
                develop.apply(adj);
                narrow.push_back(develop.render());
            }
            const Stats ns = summarise(narrow);
            std::printf("Wide tail (RGBA16F display + geometry), same process\n");
            std::printf("  min %.2f   median %.2f   p95 %.2f   mean %.2f  (ms)\n",
                        ns.min, ns.median, ns.p95, ns.mean);
            std::printf("  vs screen path:  median %+.2f   p95 %+.2f   intermediates %.0f MiB\n\n",
                        ns.median - s.median, ns.p95 - s.p95,
                        double(develop.graph().intermediateBytes()) / (1024.0 * 1024.0));

            develop.setWideOutput(false);
            std::vector<double> again;
            again.reserve(kIterations);
            for (int i = 0; i < kIterations; ++i) {
                adj.exposureEv = -1.5f + 3.0f * static_cast<float>(i) / (kIterations - 1);
                develop.apply(adj);
                again.push_back(develop.render());
            }
            const Stats as = summarise(again);
            std::printf("Narrow again (drift check - must match the first block)\n");
            std::printf("  min %.2f   median %.2f   p95 %.2f   mean %.2f  (ms)\n\n",
                        as.min, as.median, as.p95, as.mean);
            adj.exposureEv = 0.0f;
            develop.apply(adj);
            develop.render();
        }

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
                /// The state the control is judged in. A control is measured
                /// against *this* rendered, not against the identity frame:
                /// comparing a probe that lifts exposure 5.5 EV against an
                /// unlifted baseline reports the exposure lift as the control's
                /// own effect. It flattered the highlight grading wheel by more
                /// than tenfold before this was split out — the same mistake,
                /// in the tool built to catch it.
                void (*context)(orion::pipe::Adjustments&);
                /// The control itself, applied on top of the context.
                void (*set)(orion::pipe::Adjustments&);
                Metric metric = Metric::Luma;
                /// The smallest move that counts, as a fraction of what the
                /// reference control moves on this same frame — exposure +1 EV
                /// for luma, saturation -1 for chroma. A fraction rather than
                /// an absolute, because the absolute depends on the photo and
                /// this bench takes the photo as an argument.
                ///
                /// Each value is half the *smallest* ratio measured across all
                /// three sample frames: a night sky, a lit forecourt, and a
                /// bright daylight cityscape.
                ///
                /// Two frames were not enough, and neither was one. Calibrating
                /// on the night shot alone tripped four probes on the forecourt;
                /// calibrating on both night frames then tripped six on the
                /// daylight one — a bright frame has almost no deep blacks, very
                /// little noise to remove, and few shadows to grade, so those
                /// controls genuinely move less in it. None of that is a
                /// regression, and a floor that cannot tell the difference is
                /// worse than no floor.
                ///
                /// Half the minimum over three very different pictures leaves a
                /// fourth room to differ while a control that loses a third of
                /// its strength still trips.
                /// That margin is the point: `blacks -1` was being diluted to
                /// 39% of its correct effect and printed `ok` for weeks,
                /// because the bar was "moved by more than 2e-4".
                ///
                /// The gate reads `meanAbsDiff` — how far the pixels actually
                /// moved — not the summary metric beside it. A summary can
                /// cancel: a grading wheel rotates hue at constant saturation,
                /// and mean luma, mean chroma and mean saturation each reported
                /// that as almost nothing. The metric is printed for insight
                /// into *what* changed; the movement decides whether it did.
                double least = 0.25;
                /// A known, filed defect. Printed every run with its reason,
                /// never silently skipped, and it does not gate the build.
                /// Nothing else may use this without a reason string.
                const char* waived = nullptr;
            };

            const auto flat = [](orion::pipe::Adjustments&) {};
            // These offsets are what the *user* would dial, and the pipeline
            // now adds a silent +1.2 EV baseline on top (see
            // kBaselineExposureEv). Both were reduced by that amount when the
            // baseline landed, so each probe still runs at the effective
            // exposure it was calibrated at — otherwise every context silently
            // moved 1.2 stops and the grading probes lost their zone.
            const auto lift = [](orion::pipe::Adjustments& a) { a.exposureEv = 4.3f; };
            // A moderate lift, so the frame actually spans the three grading
            // zones. This is a night shot: at base exposure everything sits
            // five stops under middle gray and only the shadow wheel has
            // anything to act on.
            const auto mid  = [](orion::pipe::Adjustments& a) { a.exposureEv = 1.8f; };

            const Probe probes[] = {
                {"exposure +1 EV", flat, [](auto& a) { a.exposureEv = 1.0f; }, Metric::Luma, 0.5},
                {"highlights -1",  lift, [](auto& a) { a.highlights = -1.0f; }, Metric::Luma, 0.237},
                {"shadows +1",     flat, [](auto& a) { a.shadows = 1.0f; }, Metric::Luma, 0.311},
                // Whites and blacks are endpoint controls: they move the ends
                // and leave the middle alone, so their means move less than
                // exposure's. The floors are low; the guide-chain pair check
                // under Invariants is what actually pins them.
                {"whites +1",      lift, [](auto& a) { a.whites = 1.0f; }, Metric::Luma, 0.045},
                {"blacks -1",      flat, [](auto& a) { a.blacks = -1.0f; }, Metric::Luma, 0.023},
                {"vibrance +1",    flat, [](auto& a) { a.vibrance = 1.0f; }, Metric::Chroma, 0.068},
                {"saturation -1",  flat, [](auto& a) { a.saturation = -1.0f; }, Metric::Chroma, 0.213},
                {"contrast 1.5",   flat, [](auto& a) { a.contrast = 1.5f; }, Metric::Luma, 0.348},
                {"temp 3000K",     flat, [](auto& a) { a.wb.temperatureK = 3000.0f; }, Metric::Luma, 0.107},
                {"tint +0.5",      flat, [](auto& a) { a.wb.tint += 0.5f; }, Metric::Luma, 0.097},
                // Measured as detail, not as brightness — see Metric::Detail.
                {"sharpen 1.0",    flat, [](auto& a) { a.sharpenAmount = 1.0f; }, Metric::Detail, 0.023},
                // Clarity is a local-contrast filter, so like sharpening it is
                // measured as detail. Mean luma is the wrong instrument twice
                // over here: the filter is built to leave the frame's overall
                // brightness alone.
                // Floors are half the smallest ratio measured over all three
                // sample frames: +1 moved 0.125 / 0.185 / 0.125 of the
                // reference, -1 moved 0.120 / 0.158 / 0.115.
                {"clarity +1",     flat, [](auto& a) { a.clarity = 1.0f; }, Metric::Detail, 0.062},
                {"clarity -1",     flat, [](auto& a) { a.clarity = -1.0f; }, Metric::Detail, 0.055},
                {"denoise 2.0",    flat, [](auto& a) { a.denoiseLuma = 2.0f; }, Metric::Detail, 0.018},
                {"mixer blue lum", flat, [](auto& a) { a.lumShift[5] = -1.0f; }, Metric::Luma, 0.068},
                {"mixer blue sat", flat, [](auto& a) { a.satShift[5] = 1.0f; }, Metric::Chroma, 0.03},
                // Lens. Distortion only resamples, so mean luma barely moves —
                // vignetting is the readable one.
                {"lens vignette",  flat, [](auto& a) { a.lensVignette = 1.0f; }, Metric::Luma, 0.1},

                // ── Three-way grading ────────────────────────────────────
                // The newest node in the graph, and it had neither a probe
                // here nor a GPU test. It has both now, and they say the
                // shadow wheel works and the other two barely reach.
                {"grade shadows",  mid, [](auto& a) { a.gradeShadow[0] = -0.6f; a.gradeShadow[1] = -0.6f; }, Metric::Saturation, 0.01},
                {"grade midtones", mid, [](auto& a) { a.gradeMidtone[0] = 0.6f; a.gradeMidtone[1] = -0.6f; }, Metric::Saturation, 0.043},
                {"grade highlights", mid, [](auto& a) { a.gradeHighlight[0] = 0.6f; a.gradeHighlight[1] = 0.4f; }, Metric::Saturation, 0.008},
            };

            // Measured first, checked second: the floors are ratios against the
            // reference controls, and those are probes like any other.
            struct Result { double value, delta, moved, ms; int nodes; };
            std::vector<Result> results;
            results.reserve(std::size(probes));

            for (const auto& probe : probes) {
                auto ctx = base;
                probe.context(ctx);
                develop.apply(ctx);
                develop.render();
                const double against = meanOf(develop, probe.metric);
                const auto before = output16(develop, develop.outputWidth(),
                                             develop.outputHeight());

                auto a = ctx;
                probe.set(a);
                develop.apply(a);
                const double ms = develop.render();
                const double value = meanOf(develop, probe.metric);
                const auto after = output16(develop, develop.outputWidth(),
                                            develop.outputHeight());

                int nodes = 0;
                for (const auto& n : develop.graph().lastRun()) if (n.executed) ++nodes;
                results.push_back({value, value - against,
                                   meanAbsDiff(before, after), ms, nodes});
            }

            // The reference each floor is a fraction of.
            double refMoved = 0.0;
            for (std::size_t i = 0; i < std::size(probes); ++i) {
                if (std::string(probes[i].name) == "exposure +1 EV")
                    refMoved = results[i].moved;
            }

            for (std::size_t i = 0; i < std::size(probes); ++i) {
                const auto& p = probes[i];
                const auto& r = results[i];
                const double floor = p.least * refMoved;
                const bool moved = r.moved > 2e-4;
                const bool enough = r.moved >= floor;

                const char* verdict = !moved  ? "NO EFFECT"
                                    : !enough ? "TOO WEAK"
                                              : "ok";
                if ((!moved || !enough) && !p.waived) controlsPass = false;

                std::printf("  %-18s moved %.4f  %-6s %+.4f  %6.2f ms  %2d nodes  %-9s",
                            p.name, r.moved,
                            p.metric == Metric::Chroma ? "chroma"
                              : p.metric == Metric::Detail ? "detail"
                              : p.metric == Metric::Saturation ? "sat" : "luma",
                            r.delta, r.ms, r.nodes, verdict);
                // The floor prints on every line, passing or not. A threshold
                // nobody can see is a threshold nobody maintains.
                std::printf(" [>= %.4f]", floor);
                if (p.waived && (!moved || !enough))
                    std::printf("  WAIVED: %s", p.waived);
                std::printf("\n");
            }
            develop.apply(base);
            develop.render();
        }

        // ── Invariants, not magnitudes ────────────────────────────────────
        //
        // The probes above ask "did this move the image, and by roughly the
        // amount it should". That is a net, not a proof. These two ask
        // something exact, and each one exists because the loose version of it
        // passed while the code was wrong.
        std::printf("\nInvariants\n");
        {
            orion::pipe::Adjustments base;
            base.wb = develop.asShotWhiteBalance();

            // 1. A disabled guide chain must not change what the endpoint
            //    controls do.
            //
            //    The guided filter is seven nodes feeding only highlights and
            //    shadows, so it is switched off when both are zero — and a
            //    disabled node resolves to the last live producer, so the two
            //    guide bindings then carried the color matrix's linear RGB.
            //    The shader read it as log2 luminance and as filter
            //    coefficients. The offsets were zero so nothing moved
            //    directly, but the four band weights normalize to a partition
            //    of unity and two of them came from that garbage — diluting
            //    whites and blacks per pixel by an amount that varied with the
            //    pixel's own color. `whites +1` was moving mean luma +0.1105
            //    when it should move +0.0064: an endpoint control acting as a
            //    second exposure slider, on every frame, printing `ok`.
            //
            //    A shadow slider at 1e-6 turns the chain on and is worth
            //    2e-6 EV, so the two runs must agree to the last digit.
            const auto endpointPair = [&](const char* name,
                                          void (*set)(orion::pipe::Adjustments&)) {
                auto off = base;
                set(off);
                develop.apply(off);
                develop.render();
                const double a = meanOf(develop, Metric::Luma);

                auto on = off;
                on.shadows = 1e-6f;
                develop.apply(on);
                develop.render();
                const double b = meanOf(develop, Metric::Luma);

                const bool ok = std::abs(a - b) < 5e-4;
                if (!ok) invariantsPass = false;
                std::printf("  %-24s guide off %.4f  guide on %.4f  (%+.5f)  %s\n",
                            name, a, b, b - a, ok ? "ok" : "DILUTED");
            };
            endpointPair("blacks -1, guide off/on",
                         [](auto& a) { a.blacks = -1.0f; });
            endpointPair("whites +1, guide off/on",
                         [](auto& a) { a.exposureEv = 5.5f; a.whites = 1.0f; });

            // 2. Lens corrections must not cost incremental invalidation.
            //
            //    `correctingLens` was a condition for re-pushing the lens
            //    params, and it is true whenever a slider is *nonzero* rather
            //    than when one *changed*. setParams dirties everything
            //    downstream, so a vignette left on turned every exposure tick
            //    into seven full-resolution passes instead of three. No
            //    latency number caught it because every latency number was
            //    taken with the lens sliders at zero — the state that stops
            //    being normal the moment a lens database lands.
            auto lens = base;
            lens.lensVignette = 0.5f;
            lens.lensDistortion = -0.4f;
            develop.apply(lens);
            develop.render();

            std::vector<double> lensDrag;
            int lensNodes = 0;
            for (int i = 0; i < 12; ++i) {
                lens.exposureEv = -1.5f + 3.0f * i / 11.0f;
                develop.apply(lens);
                lensDrag.push_back(develop.render());
                lensNodes = 0;
                for (const auto& n : develop.graph().lastRun())
                    if (n.executed) ++lensNodes;
            }
            std::sort(lensDrag.begin(), lensDrag.end());
            const bool lensOk = lensNodes == ran;
            if (!lensOk) invariantsPass = false;
            std::printf("  %-24s %d nodes, %.2f ms median  (clean: %d nodes)  %s\n",
                        "exposure drag, lens on", lensNodes,
                        lensDrag[lensDrag.size() / 2], ran,
                        lensOk ? "ok" : "LENS DIRTIES THE GRAPH");

            // 3. The screen path and the export path must agree.
            //
            //    They are different formats now — eight bits for the screen,
            //    sixteen around an export — and a wrong dither magnitude, a
            //    stale parameter push after the mode switch, or a format that
            //    did not actually change would all show up here and nowhere
            //    else. The bound is one eight-bit step: that is the whole
            //    difference the narrow path is allowed to make.
            auto look = base;
            look.exposureEv = 1.4f;
            look.blacks = -0.5f;
            look.straightenDeg = 3.0f;   // make the geometry node resample

            develop.setWideOutput(false);
            develop.apply(look);
            develop.render();
            const double screenLuma = meanOf(develop, Metric::Luma);
            const double screenChroma = meanOf(develop, Metric::Chroma);

            develop.setWideOutput(true);
            develop.apply(look);
            develop.render();
            const double exportLuma = meanOf(develop, Metric::Luma);
            const double exportChroma = meanOf(develop, Metric::Chroma);
            develop.setWideOutput(false);

            const double lumaGap = std::abs(screenLuma - exportLuma);
            const double chromaGap = std::abs(screenChroma - exportChroma);
            const bool agree = lumaGap < 1.0 / 255.0 && chromaGap < 1.0 / 255.0;
            if (!agree) invariantsPass = false;
            std::printf("  %-24s screen %.5f  export %.5f  (%+.5f luma, %+.5f chroma)  %s\n",
                        "screen vs export path", screenLuma, exportLuma,
                        screenLuma - exportLuma, screenChroma - exportChroma,
                        agree ? "ok" : "PATHS DISAGREE");

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
        // Counted, not recalled. This line read "1 of 8 nodes" while the graph
        // had 28, and the quality doc repeated it as if it had been measured.
        int curveNodes = 0;
        for (const auto& n : develop.graph().lastRun()) if (n.executed) ++curveNodes;
        const double curvedLuma = meanOf(develop, Metric::Luma);
        writeOut(develop, prefix + "-curved.png");

        std::printf("  curve drag     %.2f ms median, %.2f ms p95  (%d of %zu nodes)\n",
                    cs.median, cs.p95, curveNodes, develop.graph().nodeCount());
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
            const std::size_t rowBytes =
                static_cast<std::size_t>(ew) * 4 * sizeof(std::uint16_t);
            const auto pixels = output16(develop, ew, eh);

            // The same options the app builds, metadata and colour space
            // included. The bench used to pass a bare struct, so it measured a
            // write the product never performs — and it would have reported
            // green on an export that carried no EXIF at all.
            using orion::util::ColorSpace;
            struct Case { const char* suffix; orion::util::ExportOptions opts; };
            const Case cases[] = {
                // Metadata::All here on purpose: the point of the probe is
                // that a full block survives the encode, and the app's own
                // default strips location.
                {"-full.jpg", {orion::util::ImageFormat::Jpeg, 0.92f, 0,
                               ColorSpace::Srgb, path, orion::util::Metadata::All, 4}},
                {"-web.jpg",  {orion::util::ImageFormat::Jpeg, 0.85f, 2048,
                               ColorSpace::DisplayP3, path,
                               orion::util::Metadata::All, 4}},
                // Named for the format, not the depth: the container decides
                // how much of the sixteen bits survives, and JPEG keeps eight.
                {"-full.tif", {orion::util::ImageFormat::Tiff, 1.0f, 0,
                               ColorSpace::AdobeRgb, path,
                               orion::util::Metadata::All, 4}},
            };

            for (const auto& c : cases) {
                const std::string out = prefix + c.suffix;
                const auto t = Clock::now();
                orion::util::writeImage(out, pixels.data(), ew, eh, rowBytes, c.opts);
                std::printf("  %-14s %6.0f ms\n", c.suffix, msSince(t));
            }
        }

        return (pass && curveWorks && controlsPass && invariantsPass) ? 0 : 1;

    } catch (const std::exception& e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }
}
