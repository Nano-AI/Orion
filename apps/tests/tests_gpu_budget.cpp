// The GPU memory budget: clarity, denoise and texture on a 42 MP frame (#406).
//
// Each effect peaked above 2 GB at 24 MP, so above 3.5 GB at 42, on a card
// with about 3 GB free beside a desktop: the render ran out, retried lean, and
// could still fail. This renders a synthetic 42 MP mosaic (a Sony A7R III's
// 7952 x 5304) through the whole develop graph, once per effect and once with
// all three, and fails above 2.5 GB, so the storage and streaming that brought
// it under cannot quietly come undone.
//
// The figure is the device's own: the allocator's high-water mark where the
// backend reports one (Dawn, what `orion_engine_gpu_memory` reads), else the
// bytes of every texture alive (Metal). Sampled at each texture creation,
// which is the only moment it can rise. Each effect renders twice, on fresh
// graphs: the interactive policy a photo opens with, and the lean one the
// engine retries with when the card runs out (#392). Then the sequence the
// cache used to fail: with all three on, a white-balance move and a Luminance
// tick on one warm graph. The cache keeps every node a render rebuilds (#161,
// #219), and one such move held 2.8 GB at 42 MP with no effect on; the memory
// rule (`Pipeline::decideMemory`) must plan each render under the budget.

#include "harness.h"

namespace {

constexpr std::uint64_t kBudget = 2'500'000'000;   // bytes, the whole graph at 42 MP

orion::raw::BayerImage frame(std::uint32_t w, std::uint32_t h) {
    orion::raw::BayerImage in;
    in.width = w; in.height = h;
    in.filters = 0x94949494u;
    in.white = 16383;
    in.camMul = {2.0f, 1.0f, 1.5f, 1.0f};
    in.camToXyz = {0.4124f, 0.3576f, 0.1805f, 0.2126f, 0.7152f, 0.0722f,
                   0.0193f, 0.1192f, 0.9505f};
    // A ramp under a checker with shot noise: every effect has edges, flats
    // and noise to work on, and the noise estimator a model to fit, which is
    // what switches non-local means and texture on at all.
    in.samples.resize(in.pixelCount());
    std::uint32_t s = 12345;
    for (std::uint32_t y = 0; y < h; ++y) {
        for (std::uint32_t x = 0; x < w; ++x) {
            s = s * 1664525u + 1013904223u;
            const float n = float(int(s >> 20) - 2048) / 2048.0f;
            const float v = 200.0f + 12000.0f * float(x) / float(w) +
                            1500.0f * float(((x / 7) ^ (y / 5)) & 1);
            in.samples[std::size_t(y) * w + x] =
                std::uint16_t(std::clamp(v + n * std::sqrt(v) * 2.0f, 0.0f, 16383.0f));
        }
    }
    return in;
}

struct Effect {
    const char* name;
    void (*on)(orion::pipe::Adjustments&);
};

// Every node of each effect switched on: denoise with non-local means and
// Smoothness, so the chroma chain runs too.
void clarity(orion::pipe::Adjustments& a) { a.clarity = 0.5f; }
void denoise(orion::pipe::Adjustments& a) {
    a.denoiseLuma = 1.0f; a.denoiseColor = 1.0f; a.denoiseNlm = 0.4f;
    a.denoiseSmoothness = 0.5f;
}
void texture(orion::pipe::Adjustments& a) { a.texture = 0.5f; }
void all(orion::pipe::Adjustments& a) { clarity(a); denoise(a); texture(a); }

double mib(std::uint64_t b) { return double(b) / (1024.0 * 1024.0); }

}  // namespace

void testGpuBudget42MpGpu() {
    section("GPU memory budget: clarity, denoise and texture at 42 MP (#406)");
    using namespace orion;
    try {
        auto device = gpu::Device::create();
        const auto img = frame(7952, 5304);
        const Effect effects[] = {{"clarity", clarity}, {"denoise", denoise},
                                  {"texture", texture}, {"all three", all}};
        for (const auto& e : effects) {
            for (const bool lean : {false, true}) {
                const std::string what = std::string(e.name) + (lean ? ", lean," : "");
                std::uint64_t peak = 0;
                try {
                    pipe::DevelopPipeline p(*device, ORION_SHADER_DIR, img);
                    p.graph().setReleaseEveryRender(lean);
                    pipe::Adjustments a;
                    a.wb = p.asShotWhiteBalance();
                    a.contrast = 1.45f;
                    e.on(a);
                    device->reclaim();
                    device->resetPeak();
                    p.apply(a);
                    p.render();
                    peak = device->peakBytes();
                } catch (const gpu::OutOfMemory& oom) {
                    device->reclaim();
                    report(false, what + " renders at 42 MP", oom.what());
                    continue;
                }
                device->reclaim();
                std::printf("  %-16s peak %5.0f MiB (budget %.0f)\n", what.c_str(), mib(peak),
                            mib(kBudget));
                report(peak <= kBudget, what + " stays under 2.5 GB at 42 MP",
                       std::to_string(peak) + " bytes");
            }
        }

        // The warm sequence: open, a white-balance move, a Luminance tick.
        pipe::DevelopPipeline p(*device, ORION_SHADER_DIR, img);
        pipe::Adjustments a;
        a.wb = p.asShotWhiteBalance();
        a.contrast = 1.45f;
        all(a);
        const std::pair<const char*, void (*)(pipe::Adjustments&)> steps[] = {
            {"the first render", [](pipe::Adjustments&) {}},
            {"a white balance move", [](pipe::Adjustments& s) { s.wb.temperatureK += 300; }},
            {"a Luminance tick", [](pipe::Adjustments& s) { s.denoiseLuma = 1.3f; }},
            {"an exposure tick", [](pipe::Adjustments& s) { s.exposureEv = 0.3f; }},
        };
        device->reclaim();
        for (const auto& [what, step] : steps) {
            step(a);
            device->resetPeak();
            try {
                p.apply(a);
                p.render();
            } catch (const gpu::OutOfMemory& oom) {
                report(false, std::string(what) + ", all three, renders without the lean retry",
                       oom.what());
                break;
            }
            const auto& plan = p.graph().lastPlan();
            const std::uint64_t peak = device->peakBytes();
            std::printf("  all three, %-20s peak %5.0f MiB, planned %5.0f cached / %5.0f frugal, "
                        "%s, %zu kept\n", what, mib(peak), mib(plan.cached), mib(plan.frugal),
                        plan.frugalMode ? "frugal" : "cached", plan.kept);
            report(peak <= kBudget, std::string(what) + ", all three, stays under 2.5 GB at 42 MP",
                   std::to_string(peak) + " bytes");
        }
    } catch (const std::exception& e) {
        report(false, "GPU memory budget at 42 MP", e.what());
    }
}

/// The rule's other half: where the caching plan fits, a render caches as it
/// did before #406, which is what keeps a tick fast; force the budget under
/// that plan and the same render turns frugal and stays under it.
void testMemoryRuleGpu() {
    section("The memory rule caches when the plan fits and is frugal when it does not (GPU, #406)");
    using namespace orion;
    try {
        auto device = gpu::Device::create();
        const auto img = frame(3000, 2000);
        // The squeeze is four fifths of what the first render plans to cache:
        // under the caching plan, above what a frugal render needs.
        std::uint64_t firstPlan = 0;
        for (const bool squeeze : {false, true}) {
            pipe::DevelopPipeline p(*device, ORION_SHADER_DIR, img);
            const std::uint64_t budget = squeeze ? firstPlan * 4 / 5 : kBudget;
            p.graph().setMemoryBudget(budget);
            pipe::Adjustments a;
            a.wb = p.asShotWhiteBalance();
            a.contrast = 1.45f;
            clarity(a);
            device->reclaim();
            device->resetPeak();
            p.apply(a);
            p.render();
            const auto first = p.graph().lastPlan();
            a.clarity = 0.6f;   // a tick on the warm graph
            p.apply(a);
            p.render();
            const auto& tick = p.graph().lastPlan();
            const std::uint64_t peak = device->peakBytes();
            std::printf("  6 MP clarity, budget %5.0f MiB: peak %5.0f MiB, first %s, tick %s\n",
                        mib(budget), mib(peak), first.frugalMode ? "frugal" : "cached",
                        tick.frugalMode ? "frugal" : "cached");
            if (!squeeze) {
                firstPlan = first.cached;
                report(!first.frugalMode && !tick.frugalMode,
                       "with the budget to spare, the first render and a clarity tick cache",
                       std::to_string(first.cached) + " planned");
            } else {
                report(first.frugalMode, "under a squeezed budget, the first render is frugal");
                report(peak <= budget, "and it and the tick stay under that budget",
                       std::to_string(peak) + " of " + std::to_string(budget));
            }
            device->reclaim();
        }

        // Where the backend cannot say what is free (Dawn), a refusal's
        // ceiling stands in until the next photo: what was held when the card
        // refused, less what is held now.
        // Where the driver can say (NVML, amdgpu, Metal), the rule reads it:
        // a device-wide count, so another engine and other programs are off it.
        if (const std::uint64_t free = device->freeBytes(); free != gpu::Device::kUnknown) {
            std::printf("  the driver reports %.0f MiB free\n", mib(free));
            report(free > 0 && free < (1ull << 40), "the driver's free count is a plausible size",
                   std::to_string(free));
        } else {
            auto plane = gpu::Texture::create(*device, 4096, 4096, gpu::PixelFormat::RGBA16Float);
            device->noteOutOfMemory();
            const std::uint64_t atRefusal = device->freeBytes();
            plane.reset();
            device->reclaim();
            const std::uint64_t after = device->freeBytes();
            report(atRefusal == 0 && after >= (128ull << 20) * 9 / 10 && after != gpu::Device::kUnknown,
                   "a refusal's ceiling: nothing free at it, a freed 128 MiB plane free after",
                   std::to_string(atRefusal) + ", then " + std::to_string(after));
            device->forgetOutOfMemory();
            report(device->freeBytes() == gpu::Device::kUnknown, "and forgotten, unknown again");
        }
    } catch (const std::exception& e) {
        report(false, "memory rule fixture completes", e.what());
    }
}

/// What #406 changed about the cache (levels freed as read, last readers
/// recomputed when a node is switched, stale outputs handed back when a render
/// starts) changes which textures a warm graph holds, never its pixels: a graph
/// taken through a run of edits renders, after each, exactly what the same
/// graph renders from empty. A pooled texture handed to the wrong reader is the
/// right size and format and someone else's picture (#158), so bytes, not means.
void testWarmGraphMatchesFreshGpu() {
    section("A warm graph through edits renders what it renders from empty (GPU, #406)");
    using namespace orion;
    try {
        auto device = gpu::Device::create();
        raw::BayerImage input;
        input.width = 272; input.height = 256;
        input.filters = 0x94949494u;
        input.white = 4095;
        input.camMul = {2.0f, 1.0f, 1.5f, 1.0f};
        input.camToXyz = {0.4124f, 0.3576f, 0.1805f, 0.2126f, 0.7152f, 0.0722f,
                          0.0193f, 0.1192f, 0.9505f};
        input.samples.resize(input.pixelCount());
        std::mt19937 rng(406);
        std::normal_distribution<double> g(0.0, 1.0);
        for (std::size_t i = 0; i < input.samples.size(); ++i) {
            const double v = 80.0 + 3000.0 * double(i % input.width) / input.width +
                             600.0 * double(((i % input.width) / 9 + i / input.width / 7) & 1);
            input.samples[i] = std::uint16_t(std::clamp(v + std::sqrt(0.5 * v + 20.0) * g(rng),
                                                        0.0, 4095.0));
        }
        for (const bool squeeze : {false, true}) {
        const std::string mode = squeeze ? " (frugal)" : " (cached)";
        pipe::DevelopPipeline warm(*device, ORION_SHADER_DIR, input);
        pipe::DevelopPipeline fresh(*device, ORION_SHADER_DIR, input);
        // A budget no render fits makes every render frugal: the same pixels.
        if (squeeze) warm.graph().setMemoryBudget(1);

        pipe::Adjustments a;
        a.wb = warm.asShotWhiteBalance();
        a.contrast = 1.45f;
        a.clarity = 0.4f; a.texture = 0.5f;
        a.denoiseLuma = 1.0f; a.denoiseColor = 1.0f; a.denoiseNlm = 0.4f;
        a.denoiseSmoothness = 0.5f; a.highlightRecovery = 0.5f;
        a.dehaze = 0.3f; a.fusion = 0.4f;
        a.maskCount = 1; a.maskComponents[0].kind = 2;
        a.maskRefine = 0.5f; a.layers[0].exposureEv = 0.7f;
        a.layers[0].denoiseLuma = 1.5f;

        const auto pixels = [](const pipe::DevelopPipeline& p) {
            const auto row = p.outputWidth() * gpu::bytesPerPixel(p.output().format());
            std::vector<std::uint8_t> bytes(row * p.outputHeight());
            p.output().download(bytes.data(), row, p.outputWidth(), p.outputHeight());
            return bytes;
        };
        // Each edit, then the warm graph's render against the other graph's
        // render of the same state from empty (`releaseOutputs`: a first render).
        const std::pair<const char*, void (*)(pipe::Adjustments&)> edits[] = {
            {"the first render", [](pipe::Adjustments&) {}},
            {"a Luminance tick", [](pipe::Adjustments& s) { s.denoiseLuma = 1.3f; }},
            {"a clarity tick", [](pipe::Adjustments& s) { s.clarity = 0.6f; }},
            {"an exposure tick", [](pipe::Adjustments& s) { s.exposureEv = 0.3f; }},
            {"Smoothness off", [](pipe::Adjustments& s) { s.denoiseSmoothness = 0.0f; }},
            {"clarity off", [](pipe::Adjustments& s) { s.clarity = 0.0f; }},
            {"a texture tick", [](pipe::Adjustments& s) { s.texture = -0.4f; }},
            {"a white balance move", [](pipe::Adjustments& s) { s.wb.temperatureK += 300; }},
            {"clarity and Smoothness back", [](pipe::Adjustments& s) {
                 s.clarity = 0.5f; s.denoiseSmoothness = 0.7f; }},
        };
        for (const auto& [what, edit] : edits) {
            edit(a);
            warm.apply(a);
            warm.render();
            fresh.apply(a);
            fresh.graph().releaseOutputs();
            fresh.render();
            report(pixels(warm) == pixels(fresh),
                   std::string(what) + mode + ": the warm graph's picture is the fresh one's");
        }
        if (squeeze) report(warm.graph().lastPlan().frugalMode, "the squeezed graph rendered frugal");
        }
    } catch (const std::exception& e) {
        report(false, "warm against fresh fixture completes", e.what());
    }
}
