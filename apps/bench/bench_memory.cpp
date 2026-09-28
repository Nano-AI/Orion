// A session, not a cold-open allocation count. Uses reduced RAW data to avoid
// a multi-gigabyte stress test on the photographer's machine. No file writes.
#include "bench.h"
#include "Engine.h"

#include <mach/mach.h>
#include <cstdio>

namespace bench {
namespace {
std::uint64_t footprint() {
    task_vm_info_data_t info{};
    mach_msg_type_number_t count = TASK_VM_INFO_COUNT;
    if (task_info(mach_task_self(), TASK_VM_INFO,
                  reinterpret_cast<task_info_t>(&info), &count) != KERN_SUCCESS)
        throw std::runtime_error("cannot measure process footprint");
    return info.phys_footprint;
}

std::uint64_t hash(const orion::pipe::DevelopPipeline& d) {
    const auto& tex = d.output();
    const auto row = d.outputWidth() * orion::gpu::bytesPerPixel(tex.format());
    std::vector<std::uint8_t> pixels(row * d.outputHeight());
    tex.download(pixels.data(), row, d.outputWidth(), d.outputHeight());
    std::uint64_t value = 14695981039346656037ull;
    for (auto byte : pixels) { value ^= byte; value *= 1099511628211ull; }
    return value;
}
} // namespace

bool memoryCycle(const std::string& path) {
    auto input = orion::raw::decodeBayer(path);
    int reduction = 1;
    while (std::uint64_t(input.width) * input.height > 3'000'000) {
        input = orion::raw::decimate(input, 2);
        reduction *= 2;
    }
    const auto small = orion::raw::decimate(input, orion::Engine::kPreviewScale);
    auto device = orion::gpu::Device::create();
    orion::pipe::DevelopPipeline full(*device, ORION_SHADER_DIR, input);
    orion::pipe::DevelopPipeline preview(*device, ORION_SHADER_DIR, small);
    preview.setGridStep(float(orion::Engine::kPreviewScale));
    std::printf("Memory cycle at %ux%u; preview %ux%u (source reduced %dx per axis)\n",
                input.width, input.height, small.width, small.height, reduction);
    orion::pipe::Adjustments off;
    off.wb = full.asShotWhiteBalance();
    off.contrast = 1.45f;
    const auto render = [&](const orion::pipe::Adjustments& a) {
        full.apply(a); preview.apply(a);
        preview.render(); full.render();
    };
    const auto snapshot = [&](const char* label) {
        // Sample before readback allocates a CPU frame.
        const double mib = double(footprint()) / (1024 * 1024);
        const auto fullHash = hash(full), previewHash = hash(preview);
        const auto bytes = full.graph().allocatedBytes() + preview.graph().allocatedBytes();
        std::printf("  %-16s footprint %.1f MiB, textures %.1f MiB; hashes %016llx / %016llx\n",
                    label, mib, double(bytes) / (1024 * 1024),
                    static_cast<unsigned long long>(fullHash),
                    static_cast<unsigned long long>(previewHash));
        std::fflush(stdout);
        return std::pair{fullHash, previewHash};
    };
    render(off);
    const auto baseline = snapshot("default");
    bool good = true;
    auto on = off;
    on.clarity = 0.4f; on.dehaze = 0.3f; on.fusion = 0.5f;
    on.denoiseLuma = 0.5f; on.highlightRecovery = 0.5f;
    on.maskCount = 1; on.maskComponents[0].kind = 2;
    on.maskRefine = 0.5f; on.layers[0].exposureEv = 0.7f;
    for (int cycle = 0; cycle < 3; ++cycle) {
        std::printf("Cycle %d\n", cycle + 1);
        render(on);
        auto warm = on;
        warm.wb.temperatureK += 200;
        render(warm);
        render(on);
        snapshot("filters active");
        render(off);
        good &= snapshot("filters off") == baseline;
        full.reload(input); preview.reload(small);
        render(off);
        good &= snapshot("reopen") == baseline;
    }
    for (bool usePreview : {true, false}) {
        auto a = off;
        a.exposureEv = 0.1f; render(a); // resolve one-time pool recovery
        std::vector<double> times;
        for (int i = 0; i < kIterations; ++i) {
            a.exposureEv = 0.2f + float(i) * 0.01f;
            const auto start = Clock::now();
            full.apply(a); preview.apply(a);
            if (usePreview) preview.render(); else full.render();
            times.push_back(msSince(start));
        }
        const auto stats = summarise(times);
        std::printf("  exposure %s wall median %.3f p95 %.3f ms\n",
                    usePreview ? "preview" : "full", stats.median, stats.p95);
    }
    std::printf("Off/reopen pixel invariant: %s\n", good ? "PASS" : "FAIL");
    return good;
}
} // namespace bench
