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

        std::printf("Pipeline  %zu nodes, %.0f MiB of intermediates\n",
                    develop.graph().nodeCount(),
                    static_cast<double>(develop.graph().intermediateBytes()) / (1024.0 * 1024.0));

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
