// A focused strength sweep: no PNG encoding or unrelated filters in the timing.
// Run old/new binaries alternately; the checksum is outside the timed region.
#include "bench.h"

#include <cstdio>
#include <set>

namespace bench {

bool fusionSweep(const std::string& path) {
    const auto image = orion::raw::decodeBayer(path);
    auto device = orion::gpu::Device::create();
    bool good = true;
    const std::set<std::string> expected{
        "fusion", "develop:linear", "develop:display", "geometry"};
    for (int scale : {1, 4}) {
        const auto input = scale == 1 ? image : orion::raw::decimate(image, scale);
        orion::pipe::DevelopPipeline d(*device, ORION_SHADER_DIR, input);
        orion::pipe::Adjustments a;
        a.wb = d.asShotWhiteBalance();
        a.contrast = 1.45f;
        a.fusion = 0.2f;
        d.apply(a);
        d.render();
        // Resolve any one-time pool recovery before measuring steady state.
        a.fusion = 0.25f;
        d.apply(a);
        d.render();
        std::printf("Fusion %ux%u, scale %d, contrast 1.45\n",
                    input.width, input.height, scale);
        for (int round = 0; round < 3; ++round) {
            std::vector<double> wall, gpu;
            std::set<std::string> ran;
            for (int i = 0; i < kIterations; ++i) {
                a.fusion = 0.3f + 0.6f * float(i) / float(kIterations - 1);
                const auto t = Clock::now();
                d.apply(a);
                gpu.push_back(d.render());
                wall.push_back(msSince(t));
                std::set<std::string> tick;
                for (const auto& n : d.graph().lastRun()) {
                    if (n.executed) { ran.insert(n.name); tick.insert(n.name); }
                }
                good = good && tick == expected;
            }
            const auto w = summarise(wall), g = summarise(gpu);
            const auto pixels = output16(d, d.outputWidth(), d.outputHeight());
            std::uint64_t hash = 14695981039346656037ull;
            for (auto value : pixels) { hash ^= value; hash *= 1099511628211ull; }
            std::printf("  round %d wall median %.3f p95 %.3f; GPU median %.3f p95 %.3f ms; nodes %zu; hash %016llx\n",
                        round + 1, w.median, w.p95, g.median, g.p95, ran.size(),
                        static_cast<unsigned long long>(hash));
            if (round == 0) {
                for (const auto& name : ran) std::printf("    %s\n", name.c_str());
            }
        }
    }
    std::printf("Warm fusion named-node invariant: %s\n", good ? "PASS" : "FAIL");
    return good;
}

} // namespace bench
