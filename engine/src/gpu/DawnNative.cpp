// The Dawn backend's Windows and Linux half (#376): Dawn on D3D12 or Vulkan.
//
// No display share for the engine's own textures: a shell reads them back
// (orion_engine_display_*, #386), screen-sized and without waiting. On Windows
// the display copy can instead land in a texture Avalonia's ANGLE imports
// (shareForInterop, #354's recipe): a D3D11 texture behind a DXGI NT handle
// and a keyed mutex, opt-in in the shell (ORION_D3D_SHARE=1) until a Windows
// machine has run it in a window. Linux's route is #355's Vulkan allocator, not built.

#include "gpu/Dawn.h"

#include <stdexcept>

#ifdef _WIN32
#include <d3d11_1.h>
#include <dxgi1_2.h>
#include <wrl/client.h>

#include <dawn/native/D3DBackend.h>
#endif

namespace orion::gpu::dawn {

Platform platform() {
#ifdef _WIN32
    return {wgpu::BackendType::D3D12, {}, /*robust=*/true,
            {wgpu::FeatureName::SharedTextureMemoryDXGISharedHandle,
             wgpu::FeatureName::SharedFenceDXGISharedHandle}};
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

#ifdef _WIN32
namespace {
using Microsoft::WRL::ComPtr;

/// The D3D11 texture is made on Dawn's own adapter and handed to Dawn by its
/// NT handle; ANGLE opens the same handle on its device. Both take the keyed
/// mutex with key 0 (Dawn's kDXGIKeyedMutexAcquireKey; the shell passes 0, 0
/// to UpdateWithKeyedMutexAsync), Dawn between begin() and end().
struct D3DShare final : DisplayShare {
    HANDLE                    nt = nullptr;
    wgpu::SharedTextureMemory memory;
    wgpu::Texture             tex;

    ~D3DShare() override {
        if (nt) CloseHandle(nt);
    }
    void* handle() const noexcept override { return nullptr; }   // nothing for a Metal view
    void* ntHandle() const noexcept override { return nt; }
    void begin() override {
        wgpu::SharedTextureMemoryBeginAccessDescriptor d{};
        d.initialized = true;   // every pixel is written before anyone reads one
        if (memory.BeginAccess(tex, &d) != wgpu::Status::Success)
            throw std::runtime_error("Dawn: the shared D3D11 texture would not begin access");
    }
    void end() override {
        wgpu::SharedTextureMemoryEndAccessState state{};   // the keyed mutex orders the reader, not a fence
        if (memory.EndAccess(tex, &state) != wgpu::Status::Success)
            throw std::runtime_error("Dawn: the shared D3D11 texture would not end access");
    }
};
}  // namespace

std::unique_ptr<DisplayShare> shareForInterop(Context& ctx, std::uint32_t width,
                                              std::uint32_t height, wgpu::TextureUsage usage,
                                              wgpu::Texture& tex) {
    if (!ctx.device.HasFeature(wgpu::FeatureName::SharedTextureMemoryDXGISharedHandle)) return nullptr;
    ComPtr<IDXGIAdapter> adapter = ::dawn::native::d3d::GetDXGIAdapter(ctx.adapter.Get());
    ComPtr<ID3D11Device> d3d;
    if (!adapter || FAILED(D3D11CreateDevice(adapter.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr, 0, nullptr, 0,
                                             D3D11_SDK_VERSION, &d3d, nullptr, nullptr)))
        return nullptr;
    D3D11_TEXTURE2D_DESC td{};
    td.Width = width;
    td.Height = height;
    td.MipLevels = td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT;
    // Unordered access: display_reduce writes it as storage. Render target: Dawn's clears.
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_RENDER_TARGET;
    td.MiscFlags = D3D11_RESOURCE_MISC_SHARED_NTHANDLE | D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX;
    ComPtr<ID3D11Texture2D> texture;
    ComPtr<IDXGIResource1> resource;
    auto s = std::make_unique<D3DShare>();
    if (FAILED(d3d->CreateTexture2D(&td, nullptr, &texture)) || FAILED(texture.As(&resource)) ||
        FAILED(resource->CreateSharedHandle(nullptr, DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE,
                                            nullptr, &s->nt)))
        return nullptr;
    wgpu::SharedTextureMemoryDXGISharedHandleDescriptor dx{};
    dx.handle = s->nt;
    dx.useKeyedMutex = true;
    wgpu::SharedTextureMemoryDescriptor md{.nextInChain = &dx};
    s->memory = ctx.device.ImportSharedTextureMemory(&md);
    wgpu::TextureDescriptor desc{.usage = usage, .size = {width, height, 1},
                                 .format = wgpu::TextureFormat::RGBA8Unorm};
    if (s->memory) s->tex = s->memory.CreateTexture(&desc);
    if (!s->tex) return nullptr;
    tex = s->tex;
    return s;
}
#else
std::unique_ptr<DisplayShare> shareForInterop(Context&, std::uint32_t, std::uint32_t,
                                              wgpu::TextureUsage, wgpu::Texture&) {
    return nullptr;
}
#endif

}  // namespace orion::gpu::dawn
