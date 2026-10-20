// The graph's own invariants — the ones about wiring rather than pixels.
//
// Written 2026-07-31, after every mask silently covered zero for an hour. See
// `gpu::Kernel::textureSlotsUsed` for the mechanism; the short version is that
// Metal does not object to a missing texture binding, so a kernel one slot
// short of its shader runs happily and writes nothing.

#include "harness.h"
#include "pipe/Pipeline.h"

void testBindingCount() {
    section("Kernel binding counts");

    using orion::gpu::PixelFormat;
    namespace pipe = orion::pipe;

    std::unique_ptr<orion::gpu::Device> device;
    try {
        device = orion::gpu::Device::create();
    } catch (const std::exception& e) {
        report(false, "Metal device available", e.what());
        return;
    }

    const std::string shaders = std::string(ORION_SHADER_DIR);

    // `maskComponent` is the kernel this went wrong on, and it is the widest in
    // the graph: src, reference, matte, dabs, dabBounds, accum, dst. Reflection
    // has to see all seven, or the check below cannot fail for the right
    // reason. (`accum` is the brush accumulator, decision #108.)
    {
        auto lib = orion::gpu::Library::createFromFile(
            *device, shaders + "/maskComponent.metallib");
        auto k = orion::gpu::Kernel::create(*device, *lib, "maskComponent");
        report(k->textureSlotsUsed() == 7,
               "reflection sees every texture slot maskComponent uses",
               "saw " + std::to_string(k->textureSlotsUsed()));
    }

    // ── The failure, reproduced ────────────────────────────────────────────
    //
    // A node binding five textures to that six-slot kernel is precisely the
    // shape of the bug: the July 30 binary bound five because `dabBounds` did
    // not exist yet, and the July 31 metallib put the output in slot six.
    // Before the guard this compiled, dispatched, and left coverage untouched.
    {
        pipe::Pipeline p(*device, shaders);
        // One input (the source) plus three aux plus the output: five.
        const int a0 = p.addAuxTexture(4, 4, PixelFormat::RGBA16Float);
        const int a1 = p.addAuxTexture(4, 4, PixelFormat::R16Float);
        const int a2 = p.addAuxTexture(4, 4, PixelFormat::RGBA16Float);
        p.add({.name = "starved", .kernel = "maskComponent",
               .inputs = {pipe::kSource}, .format = PixelFormat::R16Float,
               .params = {}, .aux = {a0, a1, a2}});

        bool threw = false;
        std::string why;
        try {
            p.compile(16, 16);
        } catch (const std::exception& e) {
            threw = true;
            why = e.what();
        }
        report(threw, "a node that binds too few textures is refused at compile");
        // ⚠ The message has to name the kernel. A throw that says only "binding
        // mismatch" leaves the reader where this hour started — knowing
        // something is wrong and not which of thirty kernels it is.
        report(threw && why.find("maskComponent") != std::string::npos,
               "and the refusal names the kernel", why);
        report(threw && why.find("starved") != std::string::npos,
               "and the node", why);
    }

    // ── The positive control ───────────────────────────────────────────────
    //
    // Without this the check above passes on a guard that refuses *everything*,
    // which is the same test with none of the value. Seven bindings for seven
    // slots must compile.
    {
        pipe::Pipeline p(*device, shaders);
        const int a0 = p.addAuxTexture(4, 4, PixelFormat::RGBA16Float);
        const int a1 = p.addAuxTexture(4, 4, PixelFormat::R16Float);
        const int a2 = p.addAuxTexture(4, 4, PixelFormat::RGBA16Float);
        const int a3 = p.addAuxTexture(4, 4, PixelFormat::RGBA16Float);
        const int a4 = p.addAuxTexture(4, 4, PixelFormat::R32Float);
        p.add({.name = "fed", .kernel = "maskComponent",
               .inputs = {pipe::kSource}, .format = PixelFormat::R16Float,
               .params = {}, .aux = {a0, a1, a2, a3, a4}});

        bool ok = true;
        std::string why;
        try {
            p.compile(16, 16);
        } catch (const std::exception& e) {
            ok = false;
            why = e.what();
        }
        report(ok, "a node that binds exactly enough compiles", why);
    }

    // ── Every node the real graph builds ───────────────────────────────────
    //
    // The guard is only worth having if it runs on the pipeline the app uses,
    // so this asserts the develop graph itself satisfies it. It is also the
    // check that would have gone red the moment `dabBounds` landed in the
    // shader without the matching bind — which is the whole point.
    {
        orion::raw::BayerImage img;
        img.width = 32;
        img.height = 32;
        img.samples.assign(std::size_t(32) * 32, 512);
        img.filters = 0x94949494u;             // RGGB
        img.white = 4095;
        img.camMul = {2.0f, 1.0f, 1.5f, 1.0f};
        img.camToXyz = {0.4124f, 0.3576f, 0.1805f,
                        0.2126f, 0.7152f, 0.0722f,
                        0.0193f, 0.1192f, 0.9505f};

        bool ok = true;
        std::string why;
        try {
            pipe::DevelopPipeline dev(*device, shaders, img);
        } catch (const std::exception& e) {
            ok = false;
            why = e.what();
        }
        report(ok, "every node in the develop graph binds what its kernel uses", why);
    }
}

// Catches retaining disabled filter outputs, dropping active drag caches, and
// recycling a mask/pinned texture into pixels another reader still needs.
void testDisabledCacheReleaseGpu() {
    section("Disabled graph caches release memory (GPU)");
    using namespace orion;
    try {
        auto device = gpu::Device::create();
        raw::BayerImage input;
        input.width = 272; input.height = 256;
        input.filters = 0x94949494u;
        input.white = 4095;
        input.camMul = {2.0f, 1.0f, 1.5f, 1.0f};
        input.camToXyz = {0.4124f, 0.3576f, 0.1805f,
                          0.2126f, 0.7152f, 0.0722f,
                          0.0193f, 0.1192f, 0.9505f};
        // ⚠ A ramp under fixed noise at 272 x 256, not the 132 x 100 sawtooth
        // this was: `estimateNoise` fits neither a sawtooth nor fewer than 4000
        // samples, so both denoise chains below were off in `on` and the cycle
        // never released or rebuilt either of them.
        input.samples.resize(input.pixelCount());
        std::mt19937 rng(132);
        std::normal_distribution<double> g(0.0, 1.0);
        for (std::size_t i = 0; i < input.samples.size(); ++i) {
            const double v = 80.0 + 3000.0 * double(i % input.width) / input.width;
            input.samples[i] = std::uint16_t(std::clamp(v + std::sqrt(0.5 * v + 20.0) * g(rng),
                                                        0.0, 4095.0));
        }
        pipe::DevelopPipeline d(*device, ORION_SHADER_DIR, input);
        pipe::Adjustments off;
        off.wb = d.asShotWhiteBalance();
        off.contrast = 1.45f;
        auto on = off;
        on.clarity = 0.4f; on.dehaze = 0.3f; on.fusion = 0.5f;
        on.denoiseLuma = 0.5f; on.highlightRecovery = 0.5f;
        on.maskCount = 1; on.maskComponents[0].kind = 2;
        on.maskRefine = 0.5f; on.layers[0].exposureEv = 0.7f;
        on.layers[0].denoiseLuma = 1.5f;
        const auto pixels = [](const pipe::DevelopPipeline& p) {
            const auto row = p.outputWidth() * gpu::bytesPerPixel(p.output().format());
            std::vector<std::uint8_t> bytes(row * p.outputHeight());
            p.output().download(bytes.data(), row, p.outputWidth(), p.outputHeight());
            return bytes;
        };
        const auto render = [&](const pipe::Adjustments& a) {
            d.apply(a); d.render();
            return pixels(d);
        };
        const auto baseline = render(off);
        const auto edited = render(on);
        {
            // Each chain moves the edited frame, so the cycle below covers both.
            auto noCapture = on;
            noCapture.denoiseLuma = 0.0f;
            auto noLocal = on;
            noLocal.layers[0].denoiseLuma = 0.0f;
            report(render(noCapture) != edited && render(noLocal) != edited,
                   "both denoise chains are live in the edited frame");
        }
        report(baseline != edited, "enabled filters change actual pixels");
        for (int cycle = 0; cycle < 3; ++cycle) {
            // An upstream edit warms intermediates that the first pass pooled.
            auto warm = on;
            warm.wb.temperatureK += 200;
            render(warm);
            report(render(on) == edited, "re-enabled filters reproduce the edited frame");
            const auto activeBytes = d.graph().intermediateBytes();
            const auto allocated = d.graph().allocatedBytes();
            report(render(off) == baseline, "disabled filters reproduce the default frame");
            report(d.graph().intermediateBytes() < activeBytes / 2,
                   "turning filters off releases their cached textures",
                   std::to_string(activeBytes) + " -> " +
                   std::to_string(d.graph().intermediateBytes()));
            report(d.graph().allocatedBytes() < allocated - activeBytes / 2,
                   "released textures leave the pool, not only the node cache");
            // Preserve the active cache: after one recovery tick, an exposure
            // gesture must continue to touch only its three downstream nodes.
            auto exposure = off;
            exposure.exposureEv = 0.2f; render(exposure);
            exposure.exposureEv = 0.3f; render(exposure);
            std::set<std::string> ran;
            for (const auto& node : d.graph().lastRun())
                if (node.executed) ran.insert(node.name);
            report(ran == std::set<std::string>{"develop:linear", "develop:display", "geometry"},
                   "memory cleanup preserves warm exposure caching");
            d.setWideOutput(true); render(off);
            d.setWideOutput(false);
            report(render(off) == baseline, "export format round trip preserves screen pixels");
            d.reload(input);
            report(render(off) == baseline, "reloaded graph preserves screen pixels");
        }
    } catch (const std::exception& e) {
        report(false, "disabled cache fixture completes", e.what());
    }
}

// Decision #312: a headless engine releases every intermediate on every
// render rather than once ever. The bytes have to go down and the pixels must
// not move - not across an upstream edit, and not across a downstream one that
// makes `ensure()` rebuild inputs the pool took back.
void testReleaseEveryRenderGpu() {
    section("Release every render: headless graphs keep nothing they rebuilt (GPU)");
    using namespace orion;
    try {
        auto device = gpu::Device::create();
        raw::BayerImage input;
        input.width = 272; input.height = 256;
        input.filters = 0x94949494u;
        input.white = 4095;
        input.camMul = {2.0f, 1.0f, 1.5f, 1.0f};
        input.camToXyz = {0.4124f, 0.3576f, 0.1805f,
                          0.2126f, 0.7152f, 0.0722f,
                          0.0193f, 0.1192f, 0.9505f};
        // The fixture above's noisy ramp, so both denoise chains are live.
        input.samples.resize(input.pixelCount());
        std::mt19937 rng(132);
        std::normal_distribution<double> g(0.0, 1.0);
        for (std::size_t i = 0; i < input.samples.size(); ++i) {
            const double v = 80.0 + 3000.0 * double(i % input.width) / input.width;
            input.samples[i] = std::uint16_t(std::clamp(v + std::sqrt(0.5 * v + 20.0) * g(rng),
                                                        0.0, 4095.0));
        }
        // ⚠ Two graphs over one input, side by side: the interactive policy is
        // the reference for every pixel, and its residency is the number the
        // headless one has to beat. A ratio against a constant would pass on a
        // graph that simply got smaller.
        pipe::DevelopPipeline once(*device, ORION_SHADER_DIR, input);
        pipe::DevelopPipeline every(*device, ORION_SHADER_DIR, input);
        every.graph().setReleaseEveryRender(true);

        // Dehaze and fusion on: `DevelopPipeline::render` renders twice for
        // them, which is the "even a single restore" case.
        pipe::Adjustments on;
        on.wb = once.asShotWhiteBalance();
        on.contrast = 1.45f;
        on.clarity = 0.4f; on.dehaze = 0.3f; on.fusion = 0.5f;
        on.denoiseLuma = 0.5f; on.highlightRecovery = 0.5f;
        on.maskCount = 1; on.maskComponents[0].kind = 2;
        on.maskRefine = 0.5f; on.layers[0].exposureEv = 0.7f;
        on.layers[0].denoiseLuma = 1.5f;
        auto warm = on;
        warm.wb.temperatureK += 200;
        auto brighter = on;
        brighter.exposureEv = 0.3f;

        const auto pixels = [](const pipe::DevelopPipeline& p) {
            const auto row = p.outputWidth() * gpu::bytesPerPixel(p.output().format());
            std::vector<std::uint8_t> bytes(row * p.outputHeight());
            p.output().download(bytes.data(), row, p.outputWidth(), p.outputHeight());
            return bytes;
        };
        const auto render = [&](pipe::DevelopPipeline& p, const pipe::Adjustments& a) {
            p.apply(a); p.render();
            return pixels(p);
        };

        report(render(every, on) == render(once, on),
               "the first render is the same picture under either policy");
        report(render(every, warm) == render(once, warm),
               "an upstream white balance edit is the same picture");
        const auto edited = render(once, on);
        report(render(every, on) == edited,
               "and the edited state again, byte for byte");

        const auto keptOnce  = once.graph().intermediateBytes();
        const auto keptEvery = every.graph().intermediateBytes();
        report(keptEvery * 2 < keptOnce,
               "after the third render the headless graph holds under half",
               std::to_string(keptEvery) + " vs once-ever " + std::to_string(keptOnce));
        // The pool, not only the node cache: once-ever cannot reuse what it
        // kept, so it allocated past what every-render's pool settled at.
        report(every.graph().allocatedBytes() < once.graph().allocatedBytes(),
               "and allocates less in all, idle pool included",
               std::to_string(every.graph().allocatedBytes()) + " vs " +
               std::to_string(once.graph().allocatedBytes()));

        // ⚠ The rebuild path. Exposure dirties only the tail, so its first node
        // reads an input the pool took back on the last render - nothing
        // upstream is dirty, and `ensure()` alone brings it back. Wrong here
        // is the right size, the right format and someone else's pixels.
        report(render(every, brighter) == render(once, brighter),
               "a downstream edit rebuilds released inputs to the same picture");
        report(render(every, on) == edited, "and back to the edited state");
        bool rebuilt = false;
        render(every, brighter);
        for (const auto& node : every.graph().lastRun())
            if (node.executed && node.name != "develop:linear" &&
                node.name != "develop:display" && node.name != "geometry")
                rebuilt = true;
        report(rebuilt, "the headless graph really did rebuild something upstream");
    } catch (const std::exception& e) {
        report(false, "release-every-render fixture completes", e.what());
    }
}
