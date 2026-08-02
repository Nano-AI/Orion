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
