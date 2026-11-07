// What the device holds between renders (#318, #406): a kept photo, its
// kernels and small tables; an idle canvas, no idle pool; a full render, no
// region beside it; every graph, one grain plate between them.
#include "harness.h"
#include "Engine.h"
#include "pipe/GrainPlate.h"

namespace {
using orion::pipe::Adjustments;

double mib(std::uint64_t b) { return double(b) / 1048576.0; }

/// The bytes a graph's pool holds idle.
std::size_t idle(const orion::pipe::Pipeline& g) {
    return g.allocatedBytes() - g.intermediateBytes() - g.auxBytes();
}

/// Grain and a brush stroke: the plate and the accumulator both in play.
Adjustments painted(orion::Engine& e) {
    static const float stroke[] = {0.3f, 0.3f, 0.4f, 0.35f, 0.5f, 0.4f, 0.6f, 0.45f};
    e.setBrushStroke(0, stroke, nullptr, 4);
    Adjustments a;
    a.wb = e.develop().asShotWhiteBalance();
    a.contrast = 1.3f;
    a.grainAmount = 0.3f;
    a.maskCount = 1;
    auto& c = a.maskComponents[0];
    c.kind = 3;
    c.brushRadius = 0.05f;
    c.brushFlow = 0.6f;
    c.brushRevision = 1;
    a.layers[0].exposureEv = 0.5f;
    return a;
}
}  // namespace

void testKeptPhotoHoldsNothingGpu() {
    section("A kept photo holds nothing on the device a render can make again (GPU, #318)");
    try {
        orion::Engine e;
        e.openDecoded(noisyGrey(1536, 1024));
        e.setAdjustments(painted(e));
        e.render();
        e.renderPreview();
        const auto full = pixels(e.develop()), small = pixels(e.previewDevelop());
        const std::uint64_t shown = e.device().textureBytes();
        e.releaseRenderMemory();
        const std::uint64_t kept = e.device().textureBytes();
        std::printf("  textures %.1f MiB shown, %.2f MiB kept\n", mib(shown), mib(kept));
        report(e.develop().brushAccumBytes() == 0 && kept < (1u << 20),
               "kept, under 1 MiB of textures: sources, outputs, plate and accumulator gone",
               std::to_string(kept) + " bytes");
        e.render();
        e.renderPreview();
        report(pixels(e.develop()) == full && pixels(e.previewDevelop()) == small,
               "and both graphs render the same pictures after, from the CPU's copies");
    } catch (const std::exception& x) {
        report(false, "kept photo fixture completes", x.what());
    }
}

void testIdleTrimGpu() {
    section("An idle trim drops the pool, and the cache only when memory is short (GPU, #406)");
    try {
        orion::Engine e;
        e.openDecoded(noisyGrey(1536, 1024));
        Adjustments a;
        a.wb = e.develop().asShotWhiteBalance();
        a.clarity = 0.4f;
        e.setAdjustments(a);
        e.render();
        e.releaseRenderMemory();   // from empty, a render frees at last readers into the pools
        e.render();
        e.renderPreview();
        const auto& g = e.develop().graph();
        const auto& pg = e.previewDevelop().graph();
        const std::size_t pooled = idle(g) + idle(pg);
        e.trim(false);
        report(pooled > 0 && idle(g) == 0 && idle(pg) == 0,
               "idle, the pools give back what they held", std::to_string(pooled) + " bytes idle before");

        a.wb.temperatureK += 300;   // reruns the graph, which keeps what it rebuilds
        e.setAdjustments(a);
        e.render();
        const auto moved = pixels(e.develop());
        const std::size_t cached = g.intermediateBytes();
        e.developMutable().graph().setMemoryBudget(1);   // the rest of the device past it
        e.trim(false);
        report(g.intermediateBytes() < cached && pixels(e.develop()) == moved,
               "short of memory, the cache goes and the picture stays",
               std::to_string(cached) + " -> " + std::to_string(g.intermediateBytes()));
        a.exposureEv = 0.2f;
        e.setAdjustments(a);
        e.render();
        const auto ticked = pixels(e.develop());
        e.releaseRenderMemory();
        e.render();
        report(ticked == pixels(e.develop()), "and the next tick is what a render from empty makes");
        e.trim(true);
        std::vector<std::uint32_t> bins(64 * 3);
        e.histogram(bins.data(), 64);
        std::uint64_t counted = 0;
        for (auto b : bins) counted += b;
        report(g.intermediateBytes() == g.sourceTexture().sizeBytes() && counted > 0,
               "trimmed whole, the full graph holds its source and the histogram reads the preview");
    } catch (const std::exception& x) {
        report(false, "idle trim fixture completes", x.what());
    }
}

void testRegionGoesAfterFullRenderGpu() {
    section("Back at fit, the full render gives the region's outputs back (GPU, #22)");
    try {
        orion::Engine e;
        e.openDecoded(noisyGrey(1536, 1024));
        Adjustments a;
        a.wb = e.develop().asShotWhiteBalance();
        a.exposureEv = 0.3f;
        e.setAdjustments(a);
        const orion::Engine::Region view{0.25f, 0.25f, 0.5f, 0.5f, 768, 512};
        report(e.renderRegion(view) >= 0.0, "the region renders");
        const auto region = pixels(e.regionDevelop());
        e.render();
        const auto& rg = e.regionDevelop().graph();
        report(rg.intermediateBytes() == rg.sourceTexture().sizeBytes(),
               "after the full render the region graph holds only its source",
               std::to_string(rg.intermediateBytes()) + " bytes");
        report(e.renderRegion(view) >= 0.0 && pixels(e.regionDevelop()) == region,
               "and the next region renders the same pixels");
    } catch (const std::exception& x) {
        report(false, "region release fixture completes", x.what());
    }
}

void testGrainPlateSharedGpu() {
    section("Every graph reads one grain plate, made when grain is first on (GPU)");
    try {
        auto device = orion::gpu::Device::create();
        const auto img = noisyGrey(512, 512);
        orion::pipe::DevelopPipeline p(*device, ORION_SHADER_DIR, img), q(*device, ORION_SHADER_DIR, img);
        report(p.graph().auxBytes() < (1u << 20), "a graph opens with under 1 MiB of plates",
               std::to_string(p.graph().auxBytes()) + " bytes");
        Adjustments a;
        a.wb = p.asShotWhiteBalance();
        for (auto* g : {&p, &q}) { g->apply(a); g->render(); }
        a.grainAmount = 0.4f;
        const std::uint64_t t0 = device->textureBytes();
        p.apply(a);
        p.render();
        const std::uint64_t t1 = device->textureBytes();
        q.apply(a);
        q.render();
        const std::uint64_t t2 = device->textureBytes();
        const std::uint64_t plate = std::uint64_t(orion::pipe::grain::kPlateSize) * orion::pipe::grain::kPlateHeight * 4;
        report((t1 - t0) - (t2 - t1) == plate, "the second graph's grain costs one plate less than the first's",
               std::to_string(t1 - t0) + " then " + std::to_string(t2 - t1));
    } catch (const std::exception& x) {
        report(false, "grain plate fixture completes", x.what());
    }
}
