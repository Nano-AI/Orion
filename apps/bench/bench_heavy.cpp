/*  orion-bench - the heavy tick: clarity 1, denoise luminance 2 and texture 1
 *  on, and the Luminance slider moved. The heaviest common edit, timed tick by
 *  tick on both graphs a drag renders, then node by node on each.
 *
 *  The `profile` lines are machine-read (tools/bench_run.py's PROFILE_LINE):
 *  the node's name goes last because names carry spaces.
 */
#include "bench.h"
#include "Engine.h"

#include <algorithm>
#include <cstdio>
#include <memory>
#include <vector>

namespace bench {
namespace {

orion::pipe::Adjustments heavy(const orion::pipe::DevelopPipeline& d) {
    orion::pipe::Adjustments a;
    a.wb = d.asShotWhiteBalance();
    a.clarity = 1.0f;
    a.denoiseLuma = 2.0f;
    a.texture = 1.0f;
    return a;
}

void tickAndProfile(const char* section, orion::pipe::DevelopPipeline& d, int ticks) {
    auto a = heavy(d);
    d.apply(a);
    d.render();
    const auto luminance = [&](int i) {
        a.denoiseLuma = (i % 2) ? 2.05f : 2.0f;
        d.apply(a);
    };
    std::vector<double> ms, applied, submitted;
    for (int i = 1; i <= ticks; ++i) {
        const auto t = Clock::now();
        luminance(i);
        applied.push_back(msSince(t));
        submitted.push_back(d.render());
        ms.push_back(msSince(t));
    }
    const Stats s = summarise(ms);
    // `render` returns the batch's submit to completion; the rest of a tick is the CPU's.
    std::printf("heavy %s tick  median %.2f ms  p95 %.2f ms  apply %.2f ms  batch %.2f ms  (%ux%u)\n", section,
                s.median, s.p95, summarise(applied).median, summarise(submitted).median, d.width(), d.height());

    luminance(ticks + 1);
    d.graph().setProfiling(true);
    d.render();
    d.graph().setProfiling(false);
    const auto& run = d.graph().lastRun();
    std::vector<int> ran;
    for (std::size_t i = 0; i < run.size(); ++i)
        if (run[i].executed) ran.push_back(int(i));
    std::sort(ran.begin(), ran.end(), [&](int x, int y) { return run[x].ms > run[y].ms; });
    double gpu = 0.0, wall = 0.0;
    for (int i : ran) {
        gpu += run[i].ms;
        wall += run[i].wallMs;
        std::printf("profile %s gpu_ms=%.3f wall_ms=%.3f bytes=%zu clock=%s node=%s\n", section, run[i].ms,
                    run[i].wallMs, d.graph().nodeBytes(i), run[i].gpuClock ? "gpu" : "wall",
                    run[i].name.c_str());
    }
    std::printf("heavy %s nodes %zu  sum gpu %.2f ms  sum wall %.2f ms\n", section, ran.size(), gpu, wall);
}

void previewTick(orion::gpu::Device& device, const orion::raw::BayerImage& img) {
    orion::pipe::DevelopPipeline preview(device, ORION_SHADER_DIR,
                                         orion::raw::decimate(img, orion::Engine::kPreviewScale));
    preview.setGridStep(float(orion::Engine::kPreviewScale));
    tickAndProfile("preview", preview, 24);
}

}  // namespace

void heavyProfiles(Bench& b) {
    std::printf("\nThe heavy tick: clarity 1, denoise luminance 2, texture 1; Luminance moved\n");
    tickAndProfile("full", b.develop, 16);
    previewTick(b.device, b.img);
}

bool heavyTick(const std::string& path) {
    const auto img = orion::raw::decodeBayer(path);
    std::printf("Source\n  mosaic         %u x %u  (%.1f MP)\n", img.width, img.height,
                double(img.pixelCount()) / 1.0e6);
    auto device = orion::gpu::Device::create();
    std::printf("Device\n  %s  (GPU timestamps: %s)\n", device->info().name.c_str(),
                device->info().gpuTimestamps ? "yes" : "no, wall time");
    {
        orion::pipe::DevelopPipeline full(*device, ORION_SHADER_DIR, img);
        tickAndProfile("full", full, 16);
    }
    previewTick(*device, img);
    return true;
}

}  // namespace bench
