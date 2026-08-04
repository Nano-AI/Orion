/*  orion-bench — the M0 gate, and the pipeline's regression harness.
 *
 *  Drives DevelopPipeline, the same graph the app uses, so what is measured
 *  here is what you see on screen. Writes PNGs too: a latency number nobody
 *  can look at is only half the evidence.
 *
 *  This file is the spine — decode, device, pipeline, the list of sections and
 *  the exit code. Every check lives in one of the `bench_*.cpp` beside it; see
 *  `bench.h` for which, and for why the list below may not be reordered.
 */
#include "bench.h"

#include "raw/RawImage.h"

#include <cstdio>
#include <exception>
#include <string>

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: orion-bench <raw-file> [output-prefix]\n");
        return 2;
    }
    const std::string path = argv[1];
    const std::string prefix = (argc > 2) ? argv[2] : "orion";

    try {
        using bench::Clock;
        using bench::msSince;
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

        const double intermediatesMiB =
            static_cast<double>(develop.graph().intermediateBytes()) / (1024.0 * 1024.0);
        std::printf("Pipeline  %zu nodes, %.0f MiB of intermediates\n",
                    develop.graph().nodeCount(), intermediatesMiB);

        // ⚠ **Both of these numbers were printed already and never compared**,
        // which is how a 7.2 GiB pipeline shipped without anybody saying what it
        // needs. Metal reports what it is willing to hold resident; the graph
        // reports what it wants. A ratio is the whole finding.
        //
        // The 8 GB line is not hypothetical: `CLAUDE.md` sets a macOS 14 floor,
        // and the base M1 and M2 Air are 8 GB machines that run it. Metal
        // recommends roughly three quarters of unified memory, so ~6 GiB — and
        // a 24 MP frame wants more than that here.
        // ⚠ **What a pooled allocator could reach**, walked over the execution
        // order rather than guessed. The gap between this and the figure above
        // is the size of the prize, and it decides whether #152 wants tiling,
        // lower precision, or simply not holding dead textures.
        const double peakMiB =
            static_cast<double>(develop.graph().peakLiveBytes()) / (1024.0 * 1024.0);
        std::printf("          %.0f MiB if a texture were freed at its last read"
                    " (%.0f MiB held for nothing)\n",
                    peakMiB, intermediatesMiB - peakMiB);

        const double workingSetMiB =
            static_cast<double>(device->info().recommendedWorkingSet) / (1024.0 * 1024.0);
        if (workingSetMiB > 0.0) {
            const double share = 100.0 * intermediatesMiB / workingSetMiB;
            std::printf("          %.0f MiB working set on this GPU — the graph wants %.0f%% of it\n",
                        workingSetMiB, share);
            // Three quarters of 8 GiB, the smallest machine the floor admits.
            constexpr double kSmallestMachineMiB = 8192.0 * 0.75;
            if (intermediatesMiB > kSmallestMachineMiB) {
                std::printf("          ⚠ MORE THAN AN 8 GB MAC WILL HOLD (~%.0f MiB)."
                            " A frame this size cannot open there, and nothing in"
                            " the engine checks before trying.\n", kSmallestMachineMiB);
            }
        }

        develop.apply(adj);
        std::printf("  full render    %.2f ms   (all nodes)\n", develop.render());

        // ── The A/B oracle the texture pool needs ────────────────────────────
        //
        // ⚠ **A prerequisite, not a finishing touch.** Decision #158: switching
        // the pool on means two nodes that never overlap in time share one
        // texture, and the failure mode when the liveness is wrong is a
        // *plausible photograph* — right size, right format, another node's
        // pixels. Nothing about that trips a gate, shows in a screenshot or
        // moves a mean. Only a byte comparison settles it.
        //
        // It lives here because nothing in `orion-tests` builds a full
        // `DevelopPipeline` — those tests assemble node chains by hand — and
        // this is the one place that already constructs the shipping graph
        // against a real photograph.
        //
        // What it asserts **today** is that the graph is deterministic: the same
        // adjustments rendered twice give the same bytes. That is worth having
        // on its own — a render that is not reproducible cannot be compared
        // against anything, so this would have to pass before a pooled figure
        // meant anything either. When pooling lands, the second render is the
        // pooled one and this line stops being a tautology.
        {
            const auto first = bench::output16(develop, develop.outputWidth(),
                                        develop.outputHeight());
            develop.apply(adj);
            develop.render();
            const auto second = bench::output16(develop, develop.outputWidth(),
                                         develop.outputHeight());

            std::size_t differing = 0;
            const std::size_t n = std::min(first.size(), second.size());
            for (std::size_t i = 0; i < n; ++i) {
                if (first[i] != second[i]) ++differing;
            }
            const bool same = first.size() == second.size() && differing == 0;
            std::printf("  render A/B     %s  (%zu of %zu samples differ)\n",
                        same ? "bit-identical" : "⚠ DIFFERENT", differing, n);
            // ⚠ Non-fatal here on purpose: this is an instrument, and the
            // gate it will guard does not exist until pooling does. A
            // non-deterministic render is loud in the report and would fail the
            // A/B the moment the second render is the pooled one.
            (void)same;
        }

        bench::Bench b{*device, img, develop, path, prefix};

        // ⚠ The printed order, and it is the call order. Nothing here may be
        // sorted or moved: each section leaves the pipeline in the state the
        // next one expects, `controlProbes` loads the LUT `composed` measures,
        // and `exposureGate` measures the node count three of the invariants
        // are stated against.
        bench::exposureGate(b);
        bench::controlProbes(b);
        bench::invariants(b);
        bench::highlightFill(b);
        bench::brushAccumulation(b);
        bench::pathAgreement(b);
        bench::highlightCensus(b);
        bench::toneCurve(b);
        bench::composed(b);
        bench::autoEnhance(b);
        bench::nodeProfiles(b);
        bench::brushProfiles(b);
        bench::exportTiming(b);

        return b.green() ? 0 : 1;

    } catch (const std::exception& e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }
}
