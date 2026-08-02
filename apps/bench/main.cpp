/*  orion-bench — the M0 gate, and the pipeline's regression harness.
 *
 *  Drives DevelopPipeline, the same graph the app uses, so what is measured
 *  here is what you see on screen. Writes PNGs too: a latency number nobody
 *  can look at is only half the evidence.
 */

#include "Engine.h"   // kPreviewScale — the brush section builds the preview graph
#include "gpu/MetalDevice.h"
#include "gpu/Resources.h"
#include "pipe/AutoEnhance.h"
#include "pipe/CubeLut.h"
#include "pipe/DevelopPipeline.h"
#include "raw/RawImage.h"
#include "util/ImageWriter.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <exception>
#include <iterator>
#include <memory>
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
/// The summary metrics below answer *what* changed — brightness, colorfulness,
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
/// Detail is neighbor-to-neighbor luma difference. Sharpening adds as much
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
                // Below a quarter of an 8-bit step there is no color to speak
                // of, only quantisation, and dividing by it manufactures
                // saturation out of noise.
                sum += mx > 0.25 ? (mx - mn) / mx * 255.0 : 0.0;
                break;
            }
            case Metric::Detail:
                // The horizontal neighbor. The stride steps within a row for
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
                /// Out-of-band state, set on the pipeline before either render.
                ///
                /// Not everything a mask can be is expressible in `Adjustments`.
                /// A brush stroke is a variable-length list of centers and a
                /// matte is a raster, so both are uploaded through their own
                /// calls — which is precisely why neither had a probe: the two
                /// hooks above take an `Adjustments&` and cannot reach them.
                /// Runs for the context render as well as the measured one, so
                /// a probe can put a mask in place and then measure a control
                /// *through* it. research/masking.md §5.
                void (*prepare)(orion::pipe::DevelopPipeline&) = nullptr;
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

            // A creative LUT is a file, not an adjustment, so the probe
            // measures the strength control against a LUT loaded here. Mildly
            // warm and not separable — the cross term is there so the probe
            // exercises the 3D path rather than something a per-channel curve
            // could have done.
            {
                orion::pipe::CubeLut look;
                look.size = 17;
                look.data.resize(std::size_t(17) * 17 * 17 * 3);
                std::size_t w = 0;
                for (int b = 0; b < 17; ++b) {
                    for (int g = 0; g < 17; ++g) {
                        for (int r = 0; r < 17; ++r) {
                            const float fr = float(r) / 16.0f;
                            const float fg = float(g) / 16.0f;
                            const float fb = float(b) / 16.0f;
                            look.data[w++] = std::clamp(std::pow(fr, 0.85f) + 0.05f * (fg - fb),
                                                        0.0f, 1.0f);
                            look.data[w++] = fg;
                            look.data[w++] = std::clamp(std::pow(fb, 1.15f), 0.0f, 1.0f);
                        }
                    }
                }
                develop.setCreativeLut(look);
            }

            // The look is loaded, so "no look" is the context and the control
            // being measured is the strength slider moving off zero.
            const auto noLook = [](orion::pipe::Adjustments& a) { a.lutStrength = 0.0f; };

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
                // Floor is half the smaller ratio over the two frames that
                // have any haze in them: 0.123 and 0.057 of the reference.
                //
                // Waived rather than floored across all three, because dehaze
                // is the one control whose correct output on some frames is no
                // change at all. On the night frame the dark channel is near
                // zero everywhere, the atmospheric light lands on a light
                // source, and Eq. (12) gives t = 1 — the method reporting,
                // correctly, that there is no veil to remove. A floor that
                // failed there would be a floor demanding a filter invent haze.
                // What pins this control is testDehazeGpu, not this line.
                // Half the smallest ratio over the three frames: 0.26, 0.25,
                // 0.22 of the reference.
                {"look 1.0",       noLook, [](auto& a) { a.lutStrength = 1.0f; }, Metric::Chroma, 0.11},
                // Half the smallest ratio over the three frames: 1.15, 2.43,
                // 2.62 of the reference. It moves more than an exposure stop
                // does, which is what a shadow lift at full strength should do.
                // A local exposure through a linear gradient. Half the frame
                // is untouched by construction, so this moves less than a
                // global exposure of the same size — which is the point, and
                // what the floor is calibrated against.
                {"local +2 EV",    flat, [](auto& a) {
                     auto& c = a.maskComponents[0];
                     c.kind = 1;
                     c.center[0] = 0.5f; c.center[1] = 0.5f;
                     c.angle = 0.0f;
                     c.length = 0.8f;
                     a.maskCount = 1;
                     a.layers[0].exposureEv = 2.0f;
                 }, Metric::Luma, 0.36},
                // The same edit with a radial subtracted from the ramp's full
                // side — the only thing in the product that drives a mask group
                // of more than one component through the real pipeline, so this
                // is what catches the chain being miswired: a second component
                // whose params never arrive, an enable that follows the wrong
                // count, a fold against the wrong input. The compose *algebra*
                // is pinned exactly in orion-tests; what this pins is that the
                // graph delivers it. Moves noticeably less than the probe above
                // by construction, since the hole covers most of where that
                // edit lands.
                {"local +2 EV, hole", flat, [](auto& a) {
                     auto& c = a.maskComponents[0];
                     c.kind = 1;
                     c.center[0] = 0.5f; c.center[1] = 0.5f;
                     c.angle = 0.0f;
                     c.length = 0.8f;
                     auto& s = a.maskComponents[1];
                     s.kind = 2;
                     s.compose = 1;      // subtract
                     s.center[0] = 0.75f; s.center[1] = 0.5f;
                     s.radius[0] = 0.3f; s.radius[1] = 0.55f;
                     s.feather = 0.5f;
                     a.maskCount = 2;
                     a.layers[0].exposureEv = 2.0f;
                 // Half the smallest ratio over the three frames: 0.47, 0.44,
                 // 0.60 of the reference.
                 }, Metric::Luma, 0.22},
                // Spot removal — research/spot-removal.md.
                //
                // ⚠ Four large spots, not one dust speck. A real dust spot is a
                // few thousand pixels of twenty-four million and moves a
                // whole-frame mean by nothing measurable — the same dilution
                // the mask-refine probe hit. What this asks is that the graph
                // still delivers the node on a photograph; the *behavior*
                // (heal keeps the destination's tone, clone does not) is pinned
                // exactly by testSpotRemovalGpu on a synthetic frame where the
                // right answer is a number rather than a tolerance.
                //
                // Clone rather than heal, deliberately: heal is designed to
                // leave the local tone alone, so it moves a mean by as little
                // as it can manage. Measuring the operation whose whole purpose
                // is to be invisible would be calibrating a floor against a
                // control working correctly.
                {"spots clone x4", flat, [](auto& a) {
                     a.spotCount = 4;
                     for (int i = 0; i < 4; ++i) {
                         auto& sp = a.spots[std::size_t(i)];
                         sp.destX = 0.20f + 0.20f * float(i);
                         sp.destY = 0.30f;
                         sp.srcX  = 0.20f + 0.20f * float(i);
                         sp.srcY  = 0.75f;
                         sp.radius = 0.10f;
                         sp.feather = 0.4f;
                         sp.heal = false;
                     }
                 // Half the smallest ratio over the three frames: 0.46, 0.20,
                 // 0.11 of the reference. The spread is what the four discs
                 // happen to land on in each photograph, which is the honest
                 // reason to calibrate against the smallest.
                 }, Metric::Luma, 0.056},
                // A luminance range mask — research/masking.md §4b. A band
                // on the reference image, biased by the global exposure so its
                // numbers mean what is on screen.
                //
                // Everything from half a stop above middle gray up, darkened.
                // On all three frames that is a real part of the picture and
                // not the whole of it, which is the property the floor is
                // calibrated against.
                {"range +2 EV shadows", [](orion::pipe::Adjustments& a) {
                     // ⚠ Judged on a normally exposed frame, not on the raw at
                     // zero. These are night and dusk captures: at exposure 0
                     // almost every pixel sits below middle gray, so a genuine
                     // highlight band selects nothing and the probe reported NO
                     // EFFECT on one of the three. Widening the band until it
                     // moved would have "fixed" it by selecting the whole
                     // picture, which measures nothing about a *band*.
                     a.exposureEv = 2.6f;
                 }, [](auto& a) {
                     auto& c = a.maskComponents[0];
                     c.kind = 5;
                     // ⚠ A *shadow* band, not a highlight one. A highlight
                     // band measured NO EFFECT on the night frame, and
                     // correctly so — it has almost nothing above middle gray,
                     // the same shape as dehaze finding no haze in a clear
                     // sky. Every photograph has shadows, so this is the end
                     // that exercises the band on all three.
                     c.rangeLo = -99.0f; c.rangeHi = -1.0f; c.rangeSoft = 1.0f;
                     a.maskCount = 1;
                     a.layers[0].exposureEv = 2.0f;
                 // Half the smallest ratio over the three frames: 2.15, 3.08,
                 // 0.34 of the reference. The spread is the band doing its job —
                 // the two dark frames have far more below middle gray than the
                 // daylight one, so the same band moves six times as much in
                 // them.
                 }, Metric::Luma, 0.16},
                // Local adjustments beyond exposure — research/masking.md §2b.
                //
                // ⚠ Measured on **chroma**, not luma, and that is the point of
                // the probe rather than a detail: a color cast that also moved
                // the exposure would be caught by a luma floor and a correct
                // one would not. The shader renormalizes the cast on luminance
                // precisely so it does not move brightness, so a luma probe
                // here would measure zero on a working control.
                {"local grade on a mask", [](orion::pipe::Adjustments& a) {
                     a.exposureEv = 2.6f;
                 }, [](auto& a) {
                     auto& c = a.maskComponents[0];
                     c.kind = 2;                       // a radial over the middle
                     c.center[0] = 0.5f; c.center[1] = 0.5f;
                     c.radius[0] = 0.35f; c.radius[1] = 0.35f;
                     c.feather = 0.4f;
                     a.maskCount = 1;
                     a.layers[0].warmth = 1.0f;
                     a.layers[0].saturation = 0.8f;
                 // Measured 0.159, 0.210 and 0.049 of reference across the
                 // three frames; half the smallest, on the same rule as every
                 // other probe here.
                 //
                 // ⚠ The daylight cityscape moves a third of what the two dark
                 // frames do, and that is the control working rather than
                 // noise: it is already the most saturated of the three, so a
                 // cast and a saturation lift over its middle have proportionally
                 // less room. A floor set from the night frames alone would trip
                 // on it — which is the mistake `DECISIONS.md` #47 records
                 // paying for twice already.
                 }, Metric::Chroma, 0.024},
                // A color range mask — research/masking.md §4c.
                //
                // ⚠ The target is a **neutral**, and deliberately so. A probe
                // that picked a saturated shade would be measuring whether
                // these three particular photographs happen to contain it —
                // and the sample set is a night sky, a lit forecourt and a
                // daylight cityscape, which share almost no saturated color.
                // Every photograph contains near-neutrals: tarmac, concrete,
                // cloud, shadow. That is the same argument the band above makes
                // for choosing shadows over highlights.
                //
                // ⚠ And the metric ignores lightness, which is what makes a
                // neutral target a *large* selection rather than a token one:
                // it takes every gray in the frame at every brightness. The
                // tolerance is tight — 0.06 against the 0.126 that separates
                // the closest pair the research measured — so this is still a
                // selection and not the whole picture.
                {"color range, neutrals", [](orion::pipe::Adjustments& a) {
                     a.exposureEv = 2.6f;
                 }, [](auto& a) {
                     auto& c = a.maskComponents[0];
                     c.kind = 6;
                     c.color[0] = c.color[1] = c.color[2] = 0.18f;
                     c.colorTol = 0.06f;
                     c.colorSoft = 0.02f;
                     a.maskCount = 1;
                     a.layers[0].exposureEv = 2.0f;
                 // Measured 0.826, 0.226 and 0.221 of reference on the three
                 // frames; half the smallest, on the same rule as every other
                 // probe here. ⚠ The spread is the probe working rather than
                 // noise: the forecourt is concrete, tarmac and a white
                 // building, so a neutral target takes most of it, while the
                 // night sky and the daylight cityscape are mostly colored.
                 // A probe that measured the same on all three would be
                 // selecting something that is not about color.
                 }, Metric::Luma, 0.11},
                // A raster mask component — research/masking.md §5, the
                // shape a segmentation matte arrives in.
                //
                // ⚠ This probe exists because of the `prepare` hook above. A
                // matte is uploaded out of band, like a brush stroke, so
                // neither of the two `Adjustments&` hooks could reach it —
                // which is exactly why the brush has gone unprobed since it was
                // built. Both are reachable now.
                //
                // A centerd disc of radius 0.25 covers about a fifth of the
                // frame, so a two-stop local exposure through it moves roughly a
                // fifth of what an unmasked one would. That ratio is the point:
                // a matte that silently covered everything, or nothing, would
                // not land near it.
                {"matte +2 EV", flat, [](auto& a) {
                     auto& c = a.maskComponents[0];
                     c.kind = 4;
                     a.maskCount = 1;
                     a.layers[0].exposureEv = 2.0f;
                 // Half the smallest ratio over the three frames: 0.43, 0.48,
                 // 0.35 of the reference.
                 }, Metric::Luma, 0.17, nullptr,
                 [](orion::pipe::DevelopPipeline& d) {
                     const std::uint32_t mw = d.maxMatteWidth();
                     const std::uint32_t mh = d.maxMatteHeight();
                     std::vector<float> a(std::size_t(mw) * mh, 0.0f);
                     for (std::uint32_t y = 0; y < mh; ++y) {
                         for (std::uint32_t x = 0; x < mw; ++x) {
                             const double u = (x + 0.5) / mw - 0.5;
                             const double v = (y + 0.5) / mh - 0.5;
                             if (u * u + v * v < 0.25 * 0.25) {
                                 a[std::size_t(y) * mw + x] = 1.0f;
                             }
                         }
                     }
                     d.setMaskMatte(0, a.data(), int(mw), int(mh));
                 }},
                // A brush stroke — research/masking.md §1 and §3.
                //
                // ⚠ **The oldest gap in `STATUS.md` closes here.** The brush has
                // been unprobed since it was built, because a stroke is uploaded
                // out of band — like a matte — and neither `Adjustments&` hook
                // could reach it. The `prepare` hook that the matte probe needed
                // is what makes this one possible; it has existed for several
                // sessions and nothing had used it twice.
                //
                // ⚠ Dabs are in **displayed** coordinates and go through
                // `mask::toFrame` on the way in, so a stroke laid on a diagonal
                // exercises the transform as well as the kernel. A stroke along
                // one axis would still land correctly under a transform that had
                // swapped or dropped a term.
                //
                // A stroke of 120 dabs at radius 0.05 covers a band roughly a
                // tenth of the frame, so a two-stop local exposure through it
                // moves about a tenth of what an unmasked one would — the same
                // shape of ratio the matte probe is calibrated against, and what
                // separates a working stroke from one that covered everything or
                // nothing.
                {"brush +2 EV", flat, [](auto& a) {
                     auto& c = a.maskComponents[0];
                     c.kind = 3;
                     c.brushRadius = 0.05f;
                     c.brushFlow = 1.0f;
                     c.brushHardness = 0.7f;
                     // ⚠ The revision has to move or `apply` skips the
                     // component entirely — the trap the header documents and
                     // the reason a caller cannot be trusted to remember it.
                     c.brushRevision = 1;
                     a.maskCount = 1;
                     a.layers[0].exposureEv = 2.0f;
                 // Measured 0.205, 0.199 and 0.147 of reference across the
                 // three frames; half the smallest, on the rule every probe
                 // here follows.
                 //
                 // ⚠ **And the number this probe existed to find: a 120-dab
                 // stroke costs 112-162 ms to render.** The kernel loops every
                 // dab at every pixel, rejecting on a bounding square first, so
                 // the cost is linear in the stroke's length — a long stroke is
                 // not free the way a gradient is. Degrade-then-refine hides it
                 // while the hand is moving, because the preview graph has a
                 // sixteenth of the pixels; the full render on settle is what
                 // this measures. Worth watching if strokes get longer: at the
                 // 16,384-dab cap this shape of loop would be unusable, and the
                 // answer then is a bounding box per dab-block rather than a
                 // faster inner test.
                 }, Metric::Luma, 0.073, nullptr,
                 [](orion::pipe::DevelopPipeline& d) {
                     // A diagonal, corner to corner, at even spacing.
                     constexpr int kDabs = 120;
                     std::vector<float> xy;
                     xy.reserve(std::size_t(kDabs) * 2);
                     for (int i = 0; i < kDabs; ++i) {
                         const float t = float(i) / float(kDabs - 1);
                         xy.push_back(0.10f + 0.80f * t);
                         xy.push_back(0.15f + 0.70f * t);
                     }
                     d.setBrushStroke(0, xy.data(), nullptr, kDabs);
                 }},
                // Guided feathering, research/masking.md §4.
                //
                // ⚠ The context is the *same mask, unrefined*, so what is
                // measured is what the refinement itself does. Against a flat
                // frame this would be reporting the local exposure's effect and
                // would pass with the entire chain disabled.
                //
                // ⚠ **The floor is small, and the reason is structural rather
                // than a weakness in the filter.** Refinement only moves the
                // mask's *boundary*, so at a radius of 60 pixels it can touch a
                // band about 120 pixels wide — one or two percent of a 24 MP
                // frame — and a whole-frame mean divides its effect by the other
                // ninety-eight. A near-binary full-width gradient was tried as a
                // more sensitive shape and measured *lower* (0.008-0.018 of
                // reference), for the same reason. What pins this control is
                // testMaskRefineGpu, which checks the chain against the filter
                // computed directly; this line's job is that the graph still
                // delivers it on a photograph and has not become a no-op.
                //
                // A hard-edged radial across the silver car, deliberately: the
                // filter can only move a boundary onto an edge near it, so a
                // mask placed in open sky would correctly measure nothing and
                // the floor would be demanding the filter invent structure.
                // Half the smallest ratio over the three frames: 0.021, 0.023,
                // 0.016 of the reference.
                {"mask refine 1.0", [](orion::pipe::Adjustments& a) {
                     auto& c = a.maskComponents[0];
                     c.kind = 2;
                     c.center[0] = 0.42f; c.center[1] = 0.55f;
                     c.radius[0] = 0.18f; c.radius[1] = 0.18f;
                     c.feather = 0.02f;
                     a.maskCount = 1;
                     a.layers[0].exposureEv = 2.0f;
                 }, [](auto& a) { a.maskRefine = 1.0f; }, Metric::Luma, 0.008},
                {"fusion 1.0",     flat, [](auto& a) { a.fusion = 1.0f; }, Metric::Luma, 0.57},
                {"dehaze 1.0",     flat, [](auto& a) { a.dehaze = 1.0f; }, Metric::Luma, 0.028,
                 "a haze-free frame has nothing to remove; t = 1 is the right answer"},
                {"clarity +1",     flat, [](auto& a) { a.clarity = 1.0f; }, Metric::Detail, 0.062},
                {"clarity -1",     flat, [](auto& a) { a.clarity = -1.0f; }, Metric::Detail, 0.055},
                {"denoise 2.0",    flat, [](auto& a) { a.denoiseLuma = 2.0f; }, Metric::Detail, 0.018},
                // ⚠ **Detail, not luma.** Film grain is zero-mean by
                // construction — that is the property that keeps it from
                // shifting exposure — so a probe on mean brightness reads
                // exactly zero for a working grain node and exactly zero for
                // one that was never dispatched. `Metric::Detail` is the mean
                // *absolute* difference between neighbors, which is the one
                // thing grain unambiguously raises.
                // ⚠ A real floor, not a 0.0 that only looks like one. It sat at
                // 0.0 for an afternoon, and 0.0 is what this probe reads when
                // the node was never dispatched — which is what the retarget
                // did when it pushed the previous frame's Amount.
                //
                // Measured 0.127, 0.123 and 0.125 of the exposure reference on
                // `_PIC8220`, `_PIC8095` and `_PIC8148`. That the three agree
                // to a percent is the point: grain's amplitude comes from the
                // slider rather than from the scene, so unlike every filter
                // around it this floor does not have to be set from the frame
                // that shows the effect least. Half of it, which leaves room
                // for the plate to be reseeded without a false red.
                {"grain 0.04",     flat, [](auto& a) { a.grainAmount = 0.04f; }, Metric::Detail, 0.06},
                {"mixer blue lum", flat, [](auto& a) { a.lumShift[5] = -1.0f; }, Metric::Luma, 0.068},
                {"mixer blue sat", flat, [](auto& a) { a.satShift[5] = 1.0f; }, Metric::Chroma, 0.03},
                // Lens. Distortion only resamples, so mean luma barely moves —
                // vignetting is the readable one.
                {"lens vignette",  flat, [](auto& a) { a.lensVignette = 1.0f; }, Metric::Luma, 0.1},
                // Perspective, for the same reason and with the same shape: a
                // homography only *moves* pixels, so mean luma is close to
                // silent and `moved` is the whole signal. research/perspective.md.
                //
                // ⚠ The floor is high because a geometry change is among the
                // loudest things in this table, and it is calibrated across
                // three frames per decision #47: 1.84, 0.98 and 1.43 times the
                // exposure reference on `_PIC8220`, `_PIC8095` and `_PIC8148`.
                // Half the smallest. The spread is nearly two to one and it is
                // the frame's own texture, not the correction — a warp of a
                // smooth night sky moves fewer values than a warp of a detailed
                // forecourt, and a floor set from `_PIC8220` alone would have
                // been red on the other two.
                //
                // ⚠ It is also the only probe here that runs **one node**: the
                // homography lives inside the geometry pass that was already
                // there, so the graph does not grow. A day when this line reads
                // 2 nodes is the day somebody chained a second resample.
                {"perspective 0.6", flat, [](auto& a) { a.perspectiveVertical = 0.6f; },
                 Metric::Luma, 0.48},
                // ⚠ The line above **removes** a falloff and this one puts one
                // in. They are different controls in different nodes and the
                // two probes sit together so nobody has to take that on trust:
                // if the creative one were ever wired into the lens correction,
                // this would still pass and `testCreativeVignetteGpu` would go
                // red. Keeping the pair adjacent is the reminder.
                //
                // Luma, and it is unambiguous: a vignette is an exposure change
                // over most of the frame, so unlike the wheels there is no
                // question of a mean cancelling it.
                //
                // Measured against the exposure reference on the three sample
                // frames: **0.79, 1.05 and 0.70**. Half the smallest, on the
                // rule every probe here follows.
                //
                // ⚠ Those three do *not* agree the way grain's do, and the
                // reason is worth writing down rather than averaging away.
                // Grain's amplitude is defined in display units, so it is the
                // same wherever the scene sits; a vignette is defined in stops
                // of scene-linear light, and what two stops down is worth on
                // screen depends on where the corner started on AgX's curve.
                // The night frame's corners are already low on the toe, where
                // the curve is shallow and a stop buys less.
                {"vignette -2 EV", flat, [](auto& a) { a.vignetteAmount = -2.0f; }, Metric::Luma, 0.35},

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
                if (probe.prepare) probe.prepare(develop);
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

            int waivedHere = 0;
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
                if (p.waived && (!moved || !enough)) {
                    std::printf("  WAIVED: %s", p.waived);
                    ++waivedHere;
                }
                std::printf("\n");
            }

            // ⚠ A waiver that only appears mid-table is a waiver nobody reads.
            //
            // Measured 2026-07-31: with dehaze disabled at the host in one line,
            // this bench exited 0 and both test suites passed — the control was
            // gone from the product and every instrument said fine. The waiver
            // itself is right (on a frame with no veil the correct output is no
            // change, and a floor there would demand a filter invent haze); what
            // was wrong is that it excused the control on *every* frame while
            // saying so in one line among thirty.
            //
            // The wiring is pinned by `repro/dehaze-reaches-the-picture.txt`
            // now. This line exists so the next waiver cannot be quiet.
            if (waivedHere > 0) {
                std::printf("  ⚠ %d control%s waived on this frame — not pinned "
                            "here. See repro/ for what covers them.\n",
                            waivedHere, waivedHere == 1 ? "" : "s");
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

            // 3. The dehaze slider must not re-run the dark-channel chain.
            //
            //    Same shape as 2, and it cost twice as much. Only omega moves
            //    with the slider; the dark channel, the six rank passes and the
            //    candidate pooling are functions of the frame's size, the
            //    paper's constants and A. They were re-pushed on every tick,
            //    and `setParams` dirties the whole downstream subgraph whether
            //    or not the bytes changed — so a drag paid for nine extra
            //    nodes, six of them full-resolution rank passes over 24 MP.
            //
            //    Asserted by *name*, not by a total. A count alone would be
            //    satisfied by any nine nodes, and the whole point is which ones.
            //    A wall-clock threshold would be worse still: this machine has
            //    measured the same binary at 8.97 and 44.53 ms p95 within an
            //    hour, and node identity does not care.
            static const char* const sliderIndependent[] = {
                "dehaze:channel min", "dehaze:channel min/A",
                "dehaze:dark h", "dehaze:dark v", "dehaze:candidates",
                "dehaze:min h", "dehaze:min v", "dehaze:max h", "dehaze:max v",
            };

            auto haze = base;
            haze.dehaze = 0.3f;
            develop.apply(haze);
            develop.render();   // pays for the chain, and settles A

            // ⚠ The **worst** tick, not the last one. Reporting the last would
            // pass a chain that redid itself eleven times out of twelve, which
            // is the whole cost back for a green line.
            int hazeNodes = 0, hazeStale = 0;
            std::string firstStale;
            for (int i = 0; i < 12; ++i) {
                haze.dehaze = 0.3f + 0.6f * static_cast<float>(i) / 11.0f;
                develop.apply(haze);
                develop.render();
                int ranHere = 0, staleHere = 0;
                for (const auto& n : develop.graph().lastRun()) {
                    if (!n.executed) continue;
                    ++ranHere;
                    for (const char* s : sliderIndependent) {
                        if (n.name != s) continue;
                        ++staleHere;
                        if (firstStale.empty()) firstStale = n.name;
                    }
                }
                hazeNodes = std::max(hazeNodes, ranHere);
                hazeStale = std::max(hazeStale, staleHere);
            }
            const bool hazeOk = hazeStale == 0;
            if (!hazeOk) invariantsPass = false;
            const std::string staleDetail =
                firstStale.empty() ? std::string{} : " (" + firstStale + " ...)";
            std::printf("  %-24s %d nodes, %d of them slider-independent%s  %s\n",
                        "dehaze drag", hazeNodes, hazeStale, staleDetail.c_str(),
                        hazeOk ? "ok" : "DEHAZE REDOES THE DARK CHANNEL");

            develop.apply(base);
            develop.render();

            // 3b. A Balance drag on an ungraded photograph must run nothing.
            //
            //     Balance slides the three zone centres, and with every wheel
            //     centred those weights multiply an all-zero offset and a slope
            //     of one — the kernel would be an exact identity over the whole
            //     frame. Decision #82 is a node run at zero strength and #92 is
            //     a block re-pushed for a value nothing reads; this control is
            //     both of them at once if it is wired the obvious way.
            //
            //     By name, like 3. `grade + vignette` is the node, and it is
            //     full-resolution.
            auto bal = base;
            int balRuns = 0, balNodes = 0;
            for (int i = 0; i < 8; ++i) {
                bal.gradeBalance = -1.0f + 2.0f * static_cast<float>(i) / 7.0f;
                develop.apply(bal);
                develop.render();
                int ranHere = 0;
                for (const auto& n : develop.graph().lastRun()) {
                    if (!n.executed) continue;
                    ++ranHere;
                    if (n.name == "grade + vignette") ++balRuns;
                }
                balNodes = std::max(balNodes, ranHere);
            }
            const bool balOk = balRuns == 0;
            if (!balOk) invariantsPass = false;
            std::printf("  %-24s %d nodes, grade ran %d times  %s\n",
                        "balance with no grade", balNodes, balRuns,
                        balOk ? "ok" : "BALANCE RUNS THE GRADE FOR NOTHING");

            // And the mirror of it: with a wheel off centre, Balance is a real
            // control and the node *must* re-run. A guard that switched the
            // grade off for Balance entirely would satisfy the line above and
            // silently break the feature, so the two are asserted together.
            auto balOn = base;
            balOn.gradeShadow[0] = -0.6f;
            balOn.gradeShadow[1] = -0.6f;
            develop.apply(balOn);
            develop.render();
            int balOnRuns = 0;
            for (int i = 0; i < 4; ++i) {
                balOn.gradeBalance = -0.8f + 0.5f * static_cast<float>(i);
                develop.apply(balOn);
                develop.render();
                for (const auto& n : develop.graph().lastRun())
                    if (n.executed && n.name == "grade + vignette") ++balOnRuns;
            }
            const bool balOnOk = balOnRuns == 4;
            if (!balOnOk) invariantsPass = false;
            std::printf("  %-24s grade ran %d of 4 ticks  %s\n",
                        "balance with a grade", balOnRuns,
                        balOnOk ? "ok" : "BALANCE DOES NOT REACH THE GRADE");

            develop.apply(base);
            develop.render();

            // 3c. The harmonic highlight fill must disable to nothing, and must
            //     not re-run while a slider it does not depend on moves.
            //
            //     Twenty-four nodes hang off `highlightRecovery`, which is off
            //     by default — so the expensive state here is the *on* one, and
            //     both states are asserted. Decisions #82 and #92 are the two
            //     ways this goes wrong and they are different: #82 is a node
            //     left running at zero strength, #92 is a parameter block
            //     re-pushed for a value nothing reads, which dirties everything
            //     downstream whether or not the bytes changed.
            //
            //     ⚠ By name, like the dehaze check above and for its reason: a
            //     total would be satisfied by any twenty-four nodes, and which
            //     ones is the entire question. The third check is the one that
            //     stops the other two being decoration — a chain that never ran
            //     at all would pass both.
            {
                const auto sweep = [&](orion::pipe::Adjustments a, int& worstRan,
                                       int& worstFill, std::string& firstFill) {
                    worstRan = 0;
                    worstFill = 0;
                    for (int i = 0; i < 12; ++i) {
                        a.exposureEv = -1.5f + 3.0f * static_cast<float>(i) / 11.0f;
                        develop.apply(a);
                        develop.render();
                        int ranHere = 0, fillHere = 0;
                        for (const auto& n : develop.graph().lastRun()) {
                            if (!n.executed) continue;
                            ++ranHere;
                            if (n.name.rfind("hl:", 0) != 0) continue;
                            ++fillHere;
                            if (firstFill.empty()) firstFill = n.name;
                        }
                        worstRan = std::max(worstRan, ranHere);
                        worstFill = std::max(worstFill, fillHere);
                    }
                };

                const auto fillNodesLastRun = [&] {
                    int n = 0;
                    for (const auto& t : develop.graph().lastRun()) {
                        if (t.executed && t.name.rfind("hl:", 0) == 0) ++n;
                    }
                    return n;
                };

                // Turning it on must actually cost the chain, once.
                auto on = base;
                on.highlightRecovery = 0.8f;
                develop.apply(on);
                develop.render();
                const int builtFill = fillNodesLastRun();
                const bool builtOk = builtFill >= 20;
                if (!builtOk) invariantsPass = false;
                std::printf("  %-24s %d fill nodes ran  %s\n",
                            "highlights on, once", builtFill,
                            builtOk ? "ok" : "THE CHAIN NEVER RAN");

                // ⚠ **The full render, not the drag, and that distinction is not
                // cosmetic.** This check was written on the drag first, and the
                // mutation that leaves `filling` permanently true passed it: the
                // fill sits upstream of exposure, so once it has run it stays
                // cached and an exposure tick never touches it either way. A
                // chain running on every photograph opened, forever, for nothing.
                // Switching the control off dirties the subgraph, so the render
                // below is where a node that ignores the control shows up.
                auto off = base;
                off.highlightRecovery = 0.0f;
                develop.apply(off);
                develop.render();
                const int offFullFill = fillNodesLastRun();
                const bool offFullOk = offFullFill == 0;
                if (!offFullOk) invariantsPass = false;
                std::printf("  %-24s %d fill nodes ran on a full render  %s\n",
                            "highlights off", offFullFill,
                            offFullOk ? "ok" : "THE FILL RUNS WHEN IT IS OFF");

                int offRan = 0, offFill = 0;
                std::string offName;
                sweep(off, offRan, offFill, offName);

                const bool offOk = offFill == 0 && offRan == ran;
                if (!offOk) invariantsPass = false;
                std::printf("  %-24s %d nodes, %d of them the fill%s  (clean: %d)  %s\n",
                            "exposure drag, fill off", offRan, offFill,
                            offName.empty() ? "" : (" (" + offName + " ...)").c_str(),
                            ran, offOk ? "ok" : "THE FILL RUNS WHEN IT IS OFF");

                develop.apply(on);
                develop.render();

                int onRan = 0, onFill = 0;
                std::string onName;
                sweep(on, onRan, onFill, onName);

                const bool onOk = onFill == 0 && onRan == ran;
                if (!onOk) invariantsPass = false;
                std::printf("  %-24s %d nodes, %d of them the fill%s  (clean: %d)  %s\n",
                            "exposure drag, fill on", onRan, onFill,
                            onName.empty() ? "" : (" (" + onName + " ...)").c_str(),
                            ran, onOk ? "ok" : "AN EXPOSURE TICK REDOES THE PYRAMID");

                develop.apply(base);
                develop.render();
            }

            // 4. The screen path and the export path must agree.
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

        // ── Everything at once ────────────────────────────────────────────
        //
        // Each M3 feature is verified alone. Nothing verified them *composed*,
        // and they are exactly the kind that interact: dehaze divides by a
        // transmission, exposure fusion divides one proxy luminance by another,
        // clarity raises a normalized amplitude to a fractional power, and the
        // creative LUT indexes a grid with whatever comes out. A NaN from any
        // one of them is invisible on screen — it renders as a black or white
        // pixel — and propagates through everything downstream of it.
        {
            std::printf("\nAll M3 features together\n");

            orion::pipe::Adjustments all;
            all.wb = develop.asShotWhiteBalance();
            all.dehaze  = 1.0f;
            all.clarity = 1.0f;
            all.fusion  = 1.0f;
            all.lutStrength = 1.0f;      // the look loaded for the probes above
            all.exposureEv = 0.75f;      // and a tone move underneath them
            all.blacks = -0.3f;
            all.whites = 0.4f;

            develop.apply(all);
            const double ms = develop.render();

            int nodes = 0;
            for (const auto& n : develop.graph().lastRun()) if (n.executed) ++nodes;

            const std::uint32_t w = develop.outputWidth();
            const std::uint32_t h = develop.outputHeight();
            const auto pixels = output16(develop, w, h);

            // The output is 16-bit unsigned, so a NaN cannot survive as a NaN —
            // it has already been converted, and what it leaves behind is a
            // pixel pinned hard at one end. Count those instead, and compare
            // against the same count with the four features off: a photograph
            // legitimately contains black and white pixels, and the question is
            // whether these features *added* them.
            const auto extremes = [&](const std::vector<std::uint16_t>& p) {
                std::size_t n = 0;
                for (std::size_t i = 0; i < std::size_t(w) * h; ++i) {
                    const bool low  = p[i * 4] == 0 && p[i * 4 + 1] == 0 && p[i * 4 + 2] == 0;
                    const bool high = p[i * 4] == 65535 && p[i * 4 + 1] == 65535 &&
                                      p[i * 4 + 2] == 65535;
                    if (low || high) ++n;
                }
                return double(n) / double(std::size_t(w) * h);
            };
            const double withAll = extremes(pixels);

            orion::pipe::Adjustments toneOnly = all;
            toneOnly.dehaze = toneOnly.clarity = toneOnly.fusion = 0.0f;
            toneOnly.lutStrength = 0.0f;
            develop.apply(toneOnly);
            develop.render();
            const double withoutAll = extremes(output16(develop, w, h));

            std::printf("  %d nodes, %.2f ms\n", nodes, ms);
            std::printf("  pinned pixels: %.4f%% with the four on, %.4f%% with them off\n",
                        withAll * 100.0, withoutAll * 100.0);

            // Composed, they may legitimately clip more than the tone controls
            // alone — dehaze darkens and fusion lifts. What they may not do is
            // produce a frame that is largely pinned, which is what arithmetic
            // gone wrong looks like once it has been converted to integers.
            const bool sane = withAll < 0.05;
            std::printf("  composes cleanly: %s\n", sane ? "yes" : "NO");
            if (!sane) controlsPass = false;

            orion::pipe::Adjustments restore;
            restore.wb = develop.asShotWhiteBalance();
            develop.apply(restore);
            develop.render();
        }

        // ── Auto-enhance, checked by outcome ──────────────────────────────
        //
        // Everything else about auto-enhance is tested against a stand-in for
        // the pipeline. This is the only check that runs the real one, on a
        // real photograph, and it asks the only question that matters: did the
        // median land where it was aimed?
        //
        // Not a magnitude probe with a floor, because "it moved" is not the
        // claim. The claim is that it converged.
        {
            std::printf("\nAuto-enhance\n");
            namespace ae = orion::pipe::auto_enhance;
            constexpr std::uint32_t kBins = 256;

            const auto measure = [&](const orion::pipe::Adjustments& a) {
                develop.apply(a);
                develop.render();
                const std::uint32_t w = develop.outputWidth();
                const std::uint32_t h = develop.outputHeight();
                const auto pixels = output16(develop, w, h);

                std::vector<std::uint32_t> bins(kBins, 0u);
                // The same prime stride the engine's histogram uses, for the
                // same reason: a picket fence must not alias into a peak.
                constexpr std::size_t kStride = 31;
                for (std::size_t i = 0; i < std::size_t(w) * h; i += kStride) {
                    for (int c = 0; c < 3; ++c) {
                        const float v = float(pixels[i * 4 + c]) / 65535.0f;
                        ++bins[std::min<std::uint32_t>(kBins - 1,
                              std::uint32_t(std::clamp(v, 0.0f, 1.0f) * float(kBins)))];
                    }
                }
                return ae::measure(bins.data(), kBins, ae::kClipPerSide);
            };

            orion::pipe::Adjustments a;
            a.wb = develop.asShotWhiteBalance();

            const ae::Stats before = measure(a);
            const ae::Controls look = ae::look(before);
            a.fusion = look.fusion;
            a.clarity = look.clarity;

            ae::Controls c{};
            // Mirrors Engine::autoEnhance: stop when the median arrives rather
            // than after a fixed count, or this probe measures a different
            // solver from the one the product runs.
            for (int pass = 0; pass < ae::kMaxPasses; ++pass) {
                a.exposureEv = c.exposureEv;
                a.blacks     = c.blacks;
                a.whites     = c.whites;
                const auto st = measure(a);
                if (std::abs(st.median - ae::kMidGray) < ae::kSettled) break;
                c = ae::refine(c, st);
            }
            a.exposureEv = c.exposureEv;
            a.blacks = c.blacks; a.whites = c.whites;
            const ae::Stats after = measure(a);

            const double err = std::abs(double(after.median) - double(ae::kMidGray));
            std::printf("  median %.4f -> %.4f  (anchor %.4f, off by %.4f)\n",
                        before.median, after.median, ae::kMidGray, err);
            std::printf("  set: exposure %+.2f EV, blacks %+.2f, whites %+.2f, "
                        "lift %.2f, clarity %.2f\n",
                        a.exposureEv, a.blacks, a.whites, a.fusion, a.clarity);

            // A frame needing more than the clamp allows cannot reach the
            // anchor, and should not be failed for it — but it must have gone
            // as far as it is permitted to.
            const bool clamped = std::abs(std::abs(a.exposureEv) - ae::kMaxExposureEv) < 1e-3f;
            const bool landed = err < 0.05 || clamped;
            std::printf("  converged: %s%s\n", landed ? "yes" : "NO",
                        clamped ? " (at the exposure clamp)" : "");
            if (!landed) controlsPass = false;

            // Put the graph back where the sections after this expect it.
            orion::pipe::Adjustments restore;
            restore.wb = develop.asShotWhiteBalance();
            develop.apply(restore);
            develop.render();
        }

        // ── Where a clarity drag actually goes ────────────────────────────
        //
        // Printed as a breakdown rather than a total because a total cannot be
        // acted on. The first attempt at making this cheaper optimized the
        // collapse kernels on a hunch and made it *slower*; this is what that
        // should have consulted.
        //
        // One command buffer per node, so these do not sum to the batched
        // figure above — they rank, they do not total.
        {
            const auto profileDrag = [&](const char* label,
                                         void (*set)(orion::pipe::Adjustments&)) {
                std::printf("\n%s, node by node\n", label);
                orion::pipe::Adjustments base;
                base.wb = develop.asShotWhiteBalance();
                develop.apply(base);
                develop.render();

                orion::pipe::Adjustments moved = base;
                set(moved);
                develop.apply(moved);

                develop.graph().setProfiling(true);
                develop.render();
                develop.graph().setProfiling(false);

                std::vector<std::pair<double, std::string>> ran;
                double sum = 0.0;
                for (const auto& t : develop.graph().lastRun()) {
                    if (!t.executed) continue;
                    ran.emplace_back(t.ms, t.name);
                    sum += t.ms;
                }
                std::sort(ran.begin(), ran.end(),
                          [](const auto& a, const auto& b) { return a.first > b.first; });

                std::printf("  %zu nodes ran, %.2f ms serialized\n", ran.size(), sum);
                for (std::size_t i = 0; i < ran.size() && i < 8; ++i) {
                    std::printf("    %-22s %6.2f ms  %4.1f%%\n", ran[i].second.c_str(),
                                ran[i].first, 100.0 * ran[i].first / std::max(sum, 1e-9));
                }
            };

            profileDrag("Clarity", [](orion::pipe::Adjustments& a) { a.clarity = 1.0f; });
            profileDrag("Dehaze",  [](orion::pipe::Adjustments& a) { a.dehaze  = 1.0f; });

            // ── Where a brush stroke actually goes ────────────────────────
            //
            // ⚠ This exists because the host side had been eliminated as the
            // cause and the slope did not move. Painting costs 0.2 ms an event
            // at 49 dabs and 1.5 at 490 — linear, forever — and on 2026-08-01
            // every host-side O(N) *in the Swift layer* was removed (the
            // per-event re-flatten, the `DevelopState` copy, the autosave
            // compare) with no change to the slope.
            //
            // Profiled at two stroke lengths on purpose: a single length gives
            // a ranking, and a ranking cannot tell a node that is merely
            // expensive from one that is expensive *in the number of dabs*.
            //
            // ⚠ **Two stroke shapes, and the difference between them is the
            // finding.** The first version of this section grew the dab count by
            // *subdividing a stroke of fixed extent* — the same sine wave, more
            // samples along it. Under that shape decision #80's boxes are
            // perfect: sixteen times the dabs is sixteen times the blocks, each
            // box a sixteenth the size, and the product — which is what the
            // kernel actually pays — does not move. It measured flat, and flat
            // was read as "the mask kernel is not the cost".
            //
            // No hand makes that stroke. Painting *appends*: the dab spacing is
            // fixed by the nib, so more dabs is a longer path over more of the
            // picture, and each new block of 64 arrives with a box the same size
            // as the last one. The block count grows and the boxes do not
            // shrink, so the product is linear — which is the slope.
            //
            // `refined` is the old shape, kept because it is the evidence that
            // #80 works. `appended` is what repeated `paint` verbs lay into one
            // component — 49 dabs a line, one line per stroke — which is the
            // scenario that reports 0.2 ms an event at 49 dabs rising to 0.7 at
            // 294. A component accumulates every stroke ever laid on it, so
            // more than one stroke is the ordinary case and not a contrived one.
            //
            // ⚠ And on both graphs, with the host cost beside the GPU one. The
            // paint the app measures runs on the *preview* graph, and a GPU node
            // timer cannot see a host loop at all.
            {
                // Fixed extent, more samples along it. Boxes shrink with the
                // block count; this is the shape that measures flat.
                const auto refinedStroke = [](int dabs) {
                    std::vector<float> xy;
                    xy.reserve(std::size_t(dabs) * 2);
                    for (int i = 0; i < dabs; ++i) {
                        const float t = float(i) / float(std::max(dabs - 1, 1));
                        xy.push_back(0.05f + 0.90f * t);
                        xy.push_back(0.50f + 0.25f * std::sin(t * 6.283f * 3.0f));
                    }
                    return xy;
                };

                /// `lines` strokes across the frame, 49 dabs each, one every
                /// tenth of the height — what the `paint` verb lays.
                constexpr int kDabsPerLine = 49;
                const auto appendedStroke = [](int lines) {
                    std::vector<float> xy;
                    xy.reserve(std::size_t(lines) * kDabsPerLine * 2);
                    for (int l = 0; l < lines; ++l) {
                        for (int i = 0; i < kDabsPerLine; ++i) {
                            const float t = float(i) / float(kDabsPerLine - 1);
                            xy.push_back(0.02f + 0.96f * t);
                            xy.push_back(0.05f + 0.10f * float(l));
                        }
                    }
                    return xy;
                };
                const auto brushAdjustments = [&](int revision) {
                    orion::pipe::Adjustments base;
                    base.wb = develop.asShotWhiteBalance();
                    auto& c = base.maskComponents[0];
                    c.kind = 3;
                    c.brushRadius = 0.02f;
                    c.brushFlow = 1.0f;
                    c.brushHardness = 0.7f;
                    c.brushRevision = revision;
                    base.maskCount = 1;
                    base.layers[0].exposureEv = 2.0f;
                    return base;
                };

                // The preview graph, built exactly as `Engine::openRaw` builds
                // it — same decimation, same grid step. Allowed to fail for the
                // same reason the engine's is: a machine short of room can still
                // report the full graph's numbers.
                std::unique_ptr<orion::pipe::DevelopPipeline> preview;
                try {
                    preview = std::make_unique<orion::pipe::DevelopPipeline>(
                        *device, ORION_SHADER_DIR,
                        orion::raw::decimate(img, orion::Engine::kPreviewScale));
                    preview->setGridStep(float(orion::Engine::kPreviewScale));
                } catch (const std::exception&) {
                    preview.reset();
                }

                const auto profileStroke = [&](orion::pipe::DevelopPipeline& d,
                                               const char* label,
                                               const std::vector<float>& xy) {
                    const int dabs = int(xy.size() / 2);
                    const auto base = brushAdjustments(1);
                    const std::vector<float> erase(std::size_t(dabs), 0.0f);

                    d.setBrushStroke(0, xy.data(), erase.data(), dabs);
                    d.apply(base);
                    d.render();

                    // Move the revision so the component is re-evaluated, which
                    // is what a dab being appended does.
                    d.apply(brushAdjustments(2));

                    d.graph().setProfiling(true);
                    d.render();
                    d.graph().setProfiling(false);

                    std::vector<std::pair<double, std::string>> ran;
                    double sum = 0.0;
                    for (const auto& t : d.graph().lastRun()) {
                        if (!t.executed) continue;
                        ran.emplace_back(t.ms, t.name);
                        sum += t.ms;
                    }
                    std::sort(ran.begin(), ran.end(),
                              [](const auto& a, const auto& b) { return a.first > b.first; });

                    std::printf("\n%s (%d dabs), node by node\n", label, dabs);
                    std::printf("  %zu nodes ran, %.2f ms serialized\n", ran.size(), sum);
                    for (std::size_t i = 0; i < ran.size() && i < 5; ++i) {
                        std::printf("    %-22s %6.2f ms  %4.1f%%\n", ran[i].second.c_str(),
                                    ran[i].first, 100.0 * ran[i].first / std::max(sum, 1e-9));
                    }
                };
                // Refined: the boxes shrink with the block count, so this pair
                // is flat — decision #80 doing its job.
                profileStroke(develop,  "Brush refined, full graph",  refinedStroke(60));
                profileStroke(develop,  "Brush refined, full graph",  refinedStroke(960));
                // Appended: the boxes keep their size, so this pair is not.
                profileStroke(develop,  "Brush appended, full graph", appendedStroke(1));
                profileStroke(develop,  "Brush appended, full graph", appendedStroke(6));
                if (preview) {
                    profileStroke(*preview, "Brush refined, preview graph",
                                  refinedStroke(60));
                    profileStroke(*preview, "Brush refined, preview graph",
                                  refinedStroke(960));
                    profileStroke(*preview, "Brush appended, preview graph",
                                  appendedStroke(1));
                    profileStroke(*preview, "Brush appended, preview graph",
                                  appendedStroke(6));
                }

                // ── One appended dab, host and GPU, side by side ───────────
                //
                // What `MaskOverlay` does on a pointer event: hand both
                // pipelines the whole stroke again, `apply` both, render the
                // preview. Timed as three columns so a slope can be attributed
                // rather than guessed at.
                //
                // ⚠ Interleaved between the two stroke lengths, not run in two
                // blocks. This machine's GPU swings better than three to one on
                // an identical binary under load, so two blocks minutes apart
                // compare the load average and not the code.
                if (preview) {
                    constexpr int kReps = 24;
                    std::vector<double> setMs[2], applyMs[2], renderMs[2];
                    std::vector<float> xy[2], erase[2];
                    int lengths[2] = {0, 0};
                    // The `paint` verb's own stroke lengths, one line and six,
                    // so this table and the scenario report the same case.
                    for (int k = 0; k < 2; ++k) {
                        xy[k] = appendedStroke(k == 0 ? 1 : 6);
                        lengths[k] = int(xy[k].size() / 2);
                        erase[k].assign(std::size_t(lengths[k]), 0.0f);
                    }

                    int revision = 8;
                    for (int rep = 0; rep < kReps; ++rep) {
                        for (int k = 0; k < 2; ++k) {
                            const auto adjs = brushAdjustments(++revision);

                            const auto tSet = Clock::now();
                            develop.setBrushStroke(0, xy[k].data(), erase[k].data(),
                                                   lengths[k]);
                            preview->setBrushStroke(0, xy[k].data(), erase[k].data(),
                                                    lengths[k]);
                            setMs[k].push_back(msSince(tSet));

                            const auto tApply = Clock::now();
                            develop.apply(adjs);
                            preview->apply(adjs);
                            applyMs[k].push_back(msSince(tApply));

                            renderMs[k].push_back(preview->render());
                        }
                    }

                    std::printf("\nOne pointer event of paint, host and GPU\n");
                    std::printf("  %-6s %-22s %-22s %s\n", "dabs",
                                "setBrushStroke x2", "apply x2 (host)",
                                "preview render (GPU)");
                    for (int k = 0; k < 2; ++k) {
                        const Stats st = summarise(setMs[k]);
                        const Stats ap = summarise(applyMs[k]);
                        const Stats re = summarise(renderMs[k]);
                        std::printf("  %-6d %6.3f ms (p95 %5.3f)  %6.3f ms (p95 %5.3f)  "
                                    "%6.2f ms (p95 %5.2f)\n",
                                    lengths[k], st.median, st.p95, ap.median, ap.p95,
                                    re.median, re.p95);
                    }
                }

                // Put the graph back: the sections after this expect no mask.
                orion::pipe::Adjustments clear;
                clear.wb = develop.asShotWhiteBalance();
                develop.setBrushStroke(0, nullptr, nullptr, 0);
                develop.apply(clear);
                develop.render();
            }
        }

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

            // The same options the app builds, metadata and color space
            // included. The bench used to pass a bare struct, so it measured a
            // write the product never performs — and it would have reported
            // green on an export that carried no EXIF at all.
            using orion::util::ColorSpace;
            struct Case { const char* suffix; orion::util::ExportOptions opts; };
            // ⚠ Designated initializers, not positional. This list was
            // positional and broke the moment two fields were inserted into
            // `ExportOptions` — silently assigning a path to a bit depth, which
            // is the good outcome; the bad one is two fields of the same type
            // swapping places and the bench measuring something else while
            // still exiting 0.
            const Case cases[] = {
                // Metadata::All here on purpose: the point of the probe is
                // that a full block survives the encode, and the app's own
                // default strips location.
                {"-full.jpg", {.format = orion::util::ImageFormat::Jpeg,
                               .quality = 0.92f,
                               .space = ColorSpace::Srgb,
                               .metadataFrom = path,
                               .metadata = orion::util::Metadata::All,
                               .rating = 4}},
                {"-web.jpg",  {.format = orion::util::ImageFormat::Jpeg,
                               .quality = 0.85f,
                               .maxDimension = 2048,
                               .space = ColorSpace::DisplayP3,
                               // The web case is what the panel's defaults
                               // produce: eight bits, and sharpened because it
                               // has been resized.
                               .depth = orion::util::BitDepth::Eight,
                               .sharpen = orion::util::Sharpen::Screen,
                               .metadataFrom = path,
                               .metadata = orion::util::Metadata::All,
                               .rating = 4}},
                {"-full.tif", {.format = orion::util::ImageFormat::Tiff,
                               .quality = 1.0f,
                               .space = ColorSpace::AdobeRgb,
                               .depth = orion::util::BitDepth::Sixteen,
                               .metadataFrom = path,
                               .metadata = orion::util::Metadata::All,
                               .rating = 4}},
                // The print case: resized, eight bits, the heavier sharpening
                // pass. It is the most expensive combination the panel can ask
                // for, so it is the one worth timing.
                {"-print.tif", {.format = orion::util::ImageFormat::Tiff,
                                .quality = 1.0f,
                                .maxDimension = 3000,
                                .space = ColorSpace::AdobeRgb,
                                .depth = orion::util::BitDepth::Eight,
                                .sharpen = orion::util::Sharpen::Print,
                                .metadataFrom = path,
                                .metadata = orion::util::Metadata::All,
                                .rating = 4}},
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
