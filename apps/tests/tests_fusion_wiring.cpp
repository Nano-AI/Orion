#include "harness.h"

// Scheduling equivalence, not a second implementation of fusion's mathematics.
// The reference takes the same edit history but reuploads its source every time,
// forcing every live producer to run even if apply() forgot to invalidate it.
void testFusionInvalidationGpu() {
    section("Fusion invalidation and forced-render equivalence (GPU)");
    using namespace orion;
    try {
        auto device = gpu::Device::create();
        raw::BayerImage image;
        image.width = 132; image.height = 100;
        image.filters = 0x94949494u;
        image.white = 4095;
        image.camMul = {2.0f, 1.0f, 1.5f, 1.0f};
        image.camToXyz = {0.4124f, 0.3576f, 0.1805f,
                          0.2126f, 0.7152f, 0.0722f,
                          0.0193f, 0.1192f, 0.9505f};
        image.samples.resize(image.pixelCount());
        for (std::uint32_t y = 0; y < image.height; ++y)
            for (std::uint32_t x = 0; x < image.width; ++x)
                image.samples[std::size_t(y) * image.width + x] =
                    std::uint16_t(40 + x * 17 + y * 3 + ((x / 12 + y / 10) % 2) * 300);

        const std::set<std::string> tail{
            "fusion", "develop:linear", "develop:display", "geometry"};
        for (int scale : {1, 4}) {
            auto input = scale == 1 ? image : raw::decimate(image, scale);
            pipe::DevelopPipeline cached(*device, ORION_SHADER_DIR, input);
            pipe::DevelopPipeline reference(*device, ORION_SHADER_DIR, input);
            const auto frame = [](const pipe::DevelopPipeline& d) {
                const auto& tex = d.output();
                std::vector<std::uint8_t> out(tex.sizeBytes());
                tex.download(out.data(), tex.width() * gpu::bytesPerPixel(tex.format()));
                return out;
            };
            const auto names = [&] {
                std::set<std::string> out;
                for (const auto& n : cached.graph().lastRun())
                    if (n.executed) out.insert(n.name);
                return out;
            };
            pipe::Adjustments a;
            a.wb = cached.asShotWhiteBalance();
            a.contrast = 1.45f;
            const auto compare = [&](const std::string& label, bool warm = false,
                                     bool upstream = false) {
                cached.apply(a);
                if (upstream) {
                    bool proxyDirty = false;
                    const auto& nodes = cached.graph().lastRun();
                    for (std::size_t i = 0; i < nodes.size(); ++i)
                        if (nodes[i].name == "fusion:proxy")
                            proxyDirty = cached.graph().nodeDirty(int(i));
                    report(proxyDirty, label + " invalidates fusion:proxy");
                }
                cached.render();
                if (warm) report(names() == tail, label + " runs only fusion and its tail");
                reference.apply(a);
                reference.graph().setSource(input.samples.data(), input.width * 2);
                reference.render();
                const auto pixels = frame(cached);
                report(pixels == frame(reference), label + " is byte-identical to forced recomputation",
                       "scale " + std::to_string(scale));
                return pixels;
            };
            compare("initially off");
            a.fusion = 0.2f;
            auto previous = compare("first enable");
            for (float strength : {0.35f, 0.7f, 1.0f, 0.1f}) {
                a.fusion = strength;
                auto next = compare("warm strength " + std::to_string(strength), true);
                report(previous != next, "strength changes actual pixels");
                previous = std::move(next);
            }
            a.fusion = 0;
            compare("disabled again");
            bool anyFusion = false;
            for (const auto& name : names())
                anyFusion |= name == "fusion" || name.starts_with("fusion:");
            report(!anyFusion, "off dispatches no fusion nodes");
            a.fusion = 0.6f;
            compare("re-enabled");
            a.fusion = 0.65f;
            compare("warm after re-enable", true);

            a.wb.temperatureK = 3200;
            compare("white balance", false, true);
            a.clarity = 0.4f;
            compare("clarity", false, true);
            a.dehaze = 0.3f;
            compare("dehaze", false, true);
            a.lensVignette = 0.5f;
            compare("lens correction", false, true);
            a.fusion = 0.8f;
            compare("warm with upstream filters", true);

            for (auto& value : input.samples) value = std::uint16_t(value / 2 + 20);
            cached.reload(input);
            reference.reload(input);
            compare("reload while fusion active");
            a.fusion = 0.5f;
            compare("warm after reload", true);

            cached.setWideOutput(true);
            reference.setWideOutput(true);
            compare("wide output");
            a.fusion = 0.9f;
            compare("warm wide output", true);
        }
    } catch (const std::exception& e) {
        report(false, "fusion scheduling fixture completes", e.what());
    }
}
