// The Dawn backend's device (#375): instance, adapter, device and queue, the
// error plumbing every Dawn object shares, and the format table.

#import <Metal/Metal.h>

#include "gpu/Dawn.h"

#include <dawn/native/MetalBackend.h>

#include <stdexcept>
#include <vector>

namespace orion::gpu::dawn {

void Context::wait(const wgpu::Future& future) const {
    if (instance.WaitAny(future, UINT64_MAX) != wgpu::WaitStatus::Success) {
        throw std::runtime_error("Dawn: a wait did not complete");
    }
}

void Context::report(const std::string& message) {
    std::lock_guard<std::mutex> lock(errorLock_);
    if (error_.empty()) error_ = message;   // the first is the cause
}

void Context::check(const char* what) {
    std::string why;
    {
        std::lock_guard<std::mutex> lock(errorLock_);
        why.swap(error_);
    }
    if (!why.empty()) throw std::runtime_error(std::string(what) + ": " + why);
}

void Context::finish() {
    wait(queue.OnSubmittedWorkDone(wgpu::CallbackMode::WaitAnyOnly,
                                   [](wgpu::QueueWorkDoneStatus, wgpu::StringView) {}));
}

wgpu::TextureFormat toWgpu(PixelFormat f) {
    switch (f) {
        case PixelFormat::R16Uint:     return wgpu::TextureFormat::R16Uint;
        case PixelFormat::R16Float:    return wgpu::TextureFormat::R16Float;
        case PixelFormat::R32Float:    return wgpu::TextureFormat::R32Float;
        case PixelFormat::RG16Float:   return wgpu::TextureFormat::RG16Float;
        case PixelFormat::RG32Float:   return wgpu::TextureFormat::RG32Float;
        case PixelFormat::RGBA16Float: return wgpu::TextureFormat::RGBA16Float;
        case PixelFormat::RGBA32Float: return wgpu::TextureFormat::RGBA32Float;
        case PixelFormat::RGBA8Unorm:  return wgpu::TextureFormat::RGBA8Unorm;
    }
    throw std::runtime_error("unknown pixel format");
}

const char* wgslFormat(PixelFormat f) {
    switch (f) {
        case PixelFormat::R16Uint:     return "r16uint";
        case PixelFormat::R16Float:    return "r16float";
        case PixelFormat::R32Float:    return "r32float";
        case PixelFormat::RG16Float:   return "rg16float";
        case PixelFormat::RG32Float:   return "rg32float";
        case PixelFormat::RGBA16Float: return "rgba16float";
        case PixelFormat::RGBA32Float: return "rgba32float";
        case PixelFormat::RGBA8Unorm:  return "rgba8unorm";
    }
    throw std::runtime_error("unknown pixel format");
}

namespace {

struct DawnDevice final : Device::Impl {
    std::shared_ptr<Context> ctx;

    Backend backend() const noexcept override { return Backend::Dawn; }
    DeviceInfo info() const override { return ctx->info; }
    void* metalDevice() const noexcept override { return ctx->metalDevice; }

    std::unique_ptr<Texture::Impl> texture(std::uint32_t w, std::uint32_t h,
                                           PixelFormat f, bool /*linear*/) override {
        // `linear` is a Metal upload optimisation (#319); Dawn's WriteTexture
        // does its own copy, so there is nothing to choose.
        return makeTexture(ctx, w, h, f);
    }
    std::unique_ptr<Library::Impl> library(const std::string& path) override {
        return makeLibrary(ctx, path);
    }
    std::unique_ptr<Kernel::Impl> kernel(Library& lib, const std::string& entry) override {
        return makeKernel(lib, entry);
    }
    std::unique_ptr<CommandBuffer::Impl> commandBuffer() override {
        return makeCommandBuffer(ctx);
    }
    // No explicit Destroy(): a texture may outlive this object, and the device
    // goes when the last Context reference does.
};

std::string text(wgpu::StringView s) { return std::string(s.data ? s.data : "", s.data ? s.length : 0); }

}  // namespace
}  // namespace orion::gpu::dawn

namespace orion::gpu {

namespace dawn {
namespace {

std::shared_ptr<Context> open() {
    auto ctx = std::make_shared<Context>();
    Context& c = *ctx;

    wgpu::InstanceFeatureName timed = wgpu::InstanceFeatureName::TimedWaitAny;
    wgpu::InstanceDescriptor desc{.requiredFeatureCount = 1, .requiredFeatures = &timed};
    c.instance = wgpu::CreateInstance(&desc);
    if (!c.instance) throw std::runtime_error("Dawn: CreateInstance failed");

    std::string why;
    wgpu::RequestAdapterOptions ao{.powerPreference = wgpu::PowerPreference::HighPerformance,
                                   .backendType = wgpu::BackendType::Metal};
    c.wait(c.instance.RequestAdapter(&ao, wgpu::CallbackMode::WaitAnyOnly,
        [&](wgpu::RequestAdapterStatus st, wgpu::Adapter a, wgpu::StringView msg) {
            if (st == wgpu::RequestAdapterStatus::Success) c.adapter = std::move(a);
            else why = text(msg);
        }));
    if (!c.adapter) throw std::runtime_error("Dawn: no Metal adapter: " + why);

    // ⚠ Each of these is load-bearing; the engine refuses to run without them
    // rather than render something subtly different.
    //   TextureFormatsTier1      r16float, rg16float, r16uint as storage
    //   Float32Filterable        32-bit float textures bound as texture_2d<f32>
    //   SharedTextureMemoryIOSurface, SharedFenceMTLSharedEvent  the display hand-off
    //   ImplicitDeviceSynchronization  Metal objects are thread-safe and the
    //                            engine was written against that
    std::vector<wgpu::FeatureName> features = {
        wgpu::FeatureName::TextureFormatsTier1,
        wgpu::FeatureName::Float32Filterable,
        wgpu::FeatureName::SharedTextureMemoryIOSurface,
        wgpu::FeatureName::SharedFenceMTLSharedEvent,
        wgpu::FeatureName::ImplicitDeviceSynchronization,
    };
    for (auto f : features) {
        if (!c.adapter.HasFeature(f)) {
            throw std::runtime_error("Dawn: adapter lacks feature " +
                                     std::to_string(static_cast<std::uint32_t>(f)));
        }
    }

    // Everything the adapter offers: the defaults cap a texture at 8192 and a
    // buffer at 256 MiB, and a 24 MP RGBA32F readback is 386 MB.
    wgpu::Limits limits{};
    c.adapter.GetLimits(&limits);

    // ⚠ **Robustness off.** Dawn otherwise clamps every texture load and
    // store against the texture's size, as WebGPU requires of untrusted code;
    // that doubled the GPU time of a 24 MP frame (settled at fit 18.8 ms
    // against Metal's 9.0, the drag frame 1.47 against 0.81). These shaders
    // are ours, they already clamp what they read (`clampToImage`), and Metal
    // has never checked them, so this is the behaviour the engine was written
    // and tested against. `skip_validation` was not needed to match Metal and
    // stays off: validation is what turns a host mistake into an exception.
    const char* toggleNames[] = {"disable_robustness"};
    wgpu::DawnTogglesDescriptor toggles{};
    toggles.enabledToggleCount = 1;
    toggles.enabledToggles = toggleNames;

    wgpu::DeviceDescriptor dd{};
    dd.nextInChain = &toggles;
    dd.requiredFeatureCount = features.size();
    dd.requiredFeatures     = features.data();
    dd.requiredLimits       = &limits;
    Context* raw = &c;
    dd.SetUncapturedErrorCallback(
        [](const wgpu::Device&, wgpu::ErrorType, wgpu::StringView msg, Context* ctx) {
            ctx->report(text(msg));
        }, raw);
    dd.SetDeviceLostCallback(wgpu::CallbackMode::AllowSpontaneous,
        [](const wgpu::Device&, wgpu::DeviceLostReason r, wgpu::StringView msg, Context* ctx) {
            if (r != wgpu::DeviceLostReason::Destroyed) ctx->report("device lost: " + text(msg));
        }, raw);
    c.wait(c.adapter.RequestDevice(&dd, wgpu::CallbackMode::WaitAnyOnly,
        [&](wgpu::RequestDeviceStatus st, wgpu::Device d, wgpu::StringView msg) {
            if (st == wgpu::RequestDeviceStatus::Success) c.device = std::move(d);
            else why = text(msg);
        }));
    if (!c.device) throw std::runtime_error("Dawn: RequestDevice failed: " + why);
    c.queue = c.device.GetQueue();

    id<MTLDevice> mtl = ::dawn::native::metal::GetMTLDevice(c.device.Get());
    c.metalDevice = (__bridge void*)mtl;

    wgpu::AdapterInfo ai{};
    c.adapter.GetInfo(&ai);
    c.info = DeviceInfo{
        .name                  = "Dawn on " + text(ai.device),
        .recommendedWorkingSet = mtl ? mtl.recommendedMaxWorkingSetSize : 0,
        .maxBufferLength       = limits.maxBufferSize,
        .unifiedMemory         = mtl ? static_cast<bool>(mtl.hasUnifiedMemory) : false,
        .supportsApple7        = mtl ? static_cast<bool>([mtl supportsFamily:MTLGPUFamilyApple7]) : false,
    };
    return ctx;
}

}  // namespace
}  // namespace dawn

/// ⚠ **One Dawn device per process**, shared by every `gpu::Device`, because
/// that is what Metal does: `MTLCreateSystemDefaultDevice` hands every caller
/// the same device, and the engine and its tests freely mix textures from two
/// `gpu::Device`s. Two Dawn devices refuse each other's objects.
std::unique_ptr<Device::Impl> makeDawnDevice() {
    static std::mutex lock;
    static std::weak_ptr<dawn::Context> shared;
    std::lock_guard<std::mutex> hold(lock);
    auto dev = std::make_unique<dawn::DawnDevice>();
    dev->ctx = shared.lock();
    if (!dev->ctx) {
        dev->ctx = dawn::open();
        shared = dev->ctx;
    }
    return dev;
}

}  // namespace orion::gpu
