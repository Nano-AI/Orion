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
