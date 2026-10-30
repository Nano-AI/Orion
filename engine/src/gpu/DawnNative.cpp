// The Dawn backend's Windows and Linux half (#376): Dawn on D3D12 or Vulkan.
//
// ⚠ No display share yet. An RGBA8 texture is a plain Dawn texture and a shell
// reads it back (orion_engine_read_pixels), which is the CPU copy #354 measured
// at 0.7 ms for the shell's preview. The zero-copy routes are known and not
// built: a DXGI shared handle with a keyed mutex into Avalonia's ANGLE
// (`D3D11TextureNtHandle`) on Windows, and #355's Vulkan allocator on Linux.

#include "gpu/Dawn.h"

namespace orion::gpu::dawn {

Platform platform() {
#ifdef _WIN32
    return {wgpu::BackendType::D3D12, {}, /*robust=*/true};
#else
    return {wgpu::BackendType::Vulkan, {}, /*robust=*/true};
#endif
}

void describe(Context& c, const wgpu::Limits& limits) {
    wgpu::AdapterInfo ai{};
    c.adapter.GetInfo(&ai);
    const std::string device(ai.device.data ? ai.device.data : "",
                             ai.device.data ? ai.device.length : 0);
    c.info = DeviceInfo{
        .name                  = "Dawn on " + device,
        .recommendedWorkingSet = 0,   // not reported by WebGPU
        .maxBufferLength       = limits.maxBufferSize,
        .unifiedMemory         = ai.adapterType != wgpu::AdapterType::DiscreteGPU,
        .supportsApple7        = false,
    };
}

std::unique_ptr<DisplayShare> shareForDisplay(Context&, std::uint32_t, std::uint32_t,
                                              wgpu::TextureUsage, wgpu::Texture&) {
    return nullptr;
}

}  // namespace orion::gpu::dawn
