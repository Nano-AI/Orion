// Dawn renders into an IOSurface that Avalonia imports (#317 spike).
//
// The pixels live in one IOSurface for the whole run. Dawn writes it through
// SharedTextureMemory; Avalonia reads it through ICompositionGpuInterop. The
// two sides hand it back and forth with MTLSharedEvent timeline values:
//   Dawn   BeginAccess waits  consumer >= n-1  (Avalonia finished reading frame n-1)
//   Dawn   EndAccess  exports producer = v     (Dawn's queue event, value of this submit)
//   Avalonia waits producer >= v, snapshots, signals consumer = n
// No call here maps, reads back or uploads pixels: the only CPU->GPU write is
// the 16-byte uniform block.
//
// A plain C facade: no C++ exception or Objective-C object crosses it.
#import <Foundation/Foundation.h>
#import <IOSurface/IOSurfaceObjC.h>
#import <Metal/Metal.h>
#import <AppKit/AppKit.h>
#include <webgpu/webgpu_cpp.h>
#include <cstdio>
#include <string>

namespace {
struct State {
    wgpu::Instance instance;
    wgpu::Device device;
    wgpu::Queue queue;
    wgpu::RenderPipeline pipeline;
    wgpu::Buffer uniforms;
    wgpu::BindGroup bindGroup;
    wgpu::SharedTextureMemory memory;
    wgpu::Texture texture;
    wgpu::SharedFence consumerFence;   // Avalonia signals this after it has read a frame
    wgpu::SharedFence producerFence;   // kept alive while Avalonia may still wait on it
    IOSurfaceRef surface = nullptr;
    id<MTLSharedEvent> consumerEvent = nil;
    uint64_t frame = 0;
    std::string error;
} s;

const char* kShader = R"(
struct U { t: f32, w: f32, h: f32, pad: f32 };
@group(0) @binding(0) var<uniform> u: U;

@vertex fn vs(@builtin(vertex_index) i: u32) -> @builtin(position) vec4f {
    let p = array(vec2f(-1, -1), vec2f(3, -1), vec2f(-1, 3));
    return vec4f(p[i], 0, 1);
}

@fragment fn fs(@builtin(position) pos: vec4f) -> @location(0) vec4f {
    let uv = pos.xy / vec2f(u.w, u.h);
    // Rotating colour field: changes every frame everywhere.
    let c = 0.5 + 0.5 * cos(6.2831 * (vec3f(0.0, 0.33, 0.67) + uv.x + 0.5 * uv.y + 0.25 * u.t));
    // A white bar that sweeps left to right every 4 s: where it is says when.
    let bar = step(abs(uv.x - fract(u.t * 0.25)), 0.01);
    // A black square in the bottom-left corner: shows the picture is not flipped.
    let corner = step(uv.x, 0.06) * step(1.0 - uv.y, 0.1);
    return vec4f(mix(mix(c, vec3f(1.0), bar), vec3f(0.0), corner), 1.0);
}
)";

bool fail(const std::string& m) { s.error = m; fprintf(stderr, "dawn_surface: %s\n", m.c_str()); return false; }

bool wait(wgpu::Future f) { return s.instance.WaitAny(f, UINT64_MAX) == wgpu::WaitStatus::Success; }
}

extern "C" {

const char* ds_error() { return s.error.c_str(); }

/// Creates the device, the IOSurface and the pipeline. 1 on success.
int ds_create(int width, int height) {
    @autoreleasepool {
        wgpu::InstanceFeatureName timed = wgpu::InstanceFeatureName::TimedWaitAny;
        wgpu::InstanceDescriptor id{.requiredFeatureCount = 1, .requiredFeatures = &timed};
        s.instance = wgpu::CreateInstance(&id);
        if (!s.instance) return fail("CreateInstance failed");

        wgpu::Adapter adapter;
        wgpu::RequestAdapterOptions ao{.backendType = wgpu::BackendType::Metal};
        if (!wait(s.instance.RequestAdapter(&ao, wgpu::CallbackMode::WaitAnyOnly,
                [&](wgpu::RequestAdapterStatus st, wgpu::Adapter a, wgpu::StringView msg) {
                    if (st == wgpu::RequestAdapterStatus::Success) adapter = std::move(a);
                    else fail(std::string("RequestAdapter: ") + std::string(msg));
                })) || !adapter) return fail("no adapter");

        wgpu::FeatureName features[] = {wgpu::FeatureName::SharedTextureMemoryIOSurface,
                                        wgpu::FeatureName::SharedFenceMTLSharedEvent};
        for (auto f : features)
            if (!adapter.HasFeature(f)) return fail("adapter lacks feature " + std::to_string(uint32_t(f)));

        wgpu::DeviceDescriptor dd{};
        dd.requiredFeatureCount = 2;
        dd.requiredFeatures = features;
        dd.SetUncapturedErrorCallback([](const wgpu::Device&, wgpu::ErrorType, wgpu::StringView msg) {
            fail(std::string("uncaptured: ") + std::string(msg));
        });
        dd.SetDeviceLostCallback(wgpu::CallbackMode::AllowSpontaneous,
            [](const wgpu::Device&, wgpu::DeviceLostReason r, wgpu::StringView msg) {
                if (r != wgpu::DeviceLostReason::Destroyed) fail(std::string("device lost: ") + std::string(msg));
            });
        if (!wait(adapter.RequestDevice(&dd, wgpu::CallbackMode::WaitAnyOnly,
                [&](wgpu::RequestDeviceStatus st, wgpu::Device d, wgpu::StringView msg) {
                    if (st == wgpu::RequestDeviceStatus::Success) s.device = std::move(d);
                    else fail(std::string("RequestDevice: ") + std::string(msg));
                })) || !s.device) return fail("no device");
        s.queue = s.device.GetQueue();

        // The shared pixels. BGRA because Avalonia's OpenGL importer takes nothing else.
        NSDictionary* props = @{IOSurfacePropertyKeyWidth: @(width), IOSurfacePropertyKeyHeight: @(height),
                                IOSurfacePropertyKeyBytesPerElement: @4,
                                IOSurfacePropertyKeyPixelFormat: @((uint32_t)'BGRA')};
        s.surface = IOSurfaceCreate((__bridge CFDictionaryRef)props);
        if (!s.surface) return fail("IOSurfaceCreate failed");

        wgpu::SharedTextureMemoryIOSurfaceDescriptor io{};
        io.ioSurface = s.surface;
        wgpu::SharedTextureMemoryDescriptor md{.nextInChain = &io};
        s.memory = s.device.ImportSharedTextureMemory(&md);
        s.texture = s.memory.CreateTexture();
        if (!s.texture || s.texture.GetFormat() != wgpu::TextureFormat::BGRA8Unorm)
            return fail("shared texture is not BGRA8Unorm");

        // Avalonia's side of the handshake, made here so both sides can import it.
        s.consumerEvent = [MTLCreateSystemDefaultDevice() newSharedEvent];
        wgpu::SharedFenceMTLSharedEventDescriptor ev{};
        ev.sharedEvent = (__bridge void*)s.consumerEvent;
        wgpu::SharedFenceDescriptor fd{.nextInChain = &ev};
        s.consumerFence = s.device.ImportSharedFence(&fd);

        wgpu::ShaderSourceWGSL wgsl{};
        wgsl.code = kShader;
        wgpu::ShaderModuleDescriptor sd{.nextInChain = &wgsl};
        auto module = s.device.CreateShaderModule(&sd);
        wgpu::ColorTargetState target{.format = wgpu::TextureFormat::BGRA8Unorm};
        wgpu::FragmentState fs{.module = module, .entryPoint = "fs", .targetCount = 1, .targets = &target};
        wgpu::RenderPipelineDescriptor pd{.vertex = {.module = module, .entryPoint = "vs"}, .fragment = &fs};
        s.pipeline = s.device.CreateRenderPipeline(&pd);

        wgpu::BufferDescriptor bd{.usage = wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst, .size = 16};
        s.uniforms = s.device.CreateBuffer(&bd);
        wgpu::BindGroupEntry be{.binding = 0, .buffer = s.uniforms, .size = 16};
        wgpu::BindGroupDescriptor bgd{.layout = s.pipeline.GetBindGroupLayout(0), .entryCount = 1, .entries = &be};
        s.bindGroup = s.device.CreateBindGroup(&bgd);
        s.instance.ProcessEvents();
        return s.error.empty() ? 1 : 0;
    }
}

void* ds_iosurface() { return s.surface; }
void* ds_consumer_event() { return (__bridge void*)s.consumerEvent; }

/// Renders one frame at time `t` seconds. Waits (on the GPU) for the consumer
/// event to reach `consumerValue` first. Returns 1 and the producer event and
/// value Avalonia must wait for.
int ds_frame(float t, uint64_t consumerValue, void** producerEvent, uint64_t* producerValue) {
    @autoreleasepool {
        wgpu::SharedTextureMemoryBeginAccessDescriptor begin{};
        begin.initialized = s.frame > 0;
        if (consumerValue > 0) {
            begin.fenceCount = begin.signaledValueCount = 1;
            begin.fences = &s.consumerFence;
            begin.signaledValues = &consumerValue;
        }
        if (s.memory.BeginAccess(s.texture, &begin) != wgpu::Status::Success) return fail("BeginAccess failed");

        float u[4] = {t, float(s.texture.GetWidth()), float(s.texture.GetHeight()), 0};
        s.queue.WriteBuffer(s.uniforms, 0, u, sizeof u);
        wgpu::RenderPassColorAttachment ca{.view = s.texture.CreateView(), .loadOp = wgpu::LoadOp::Clear,
                                           .storeOp = wgpu::StoreOp::Store};
        wgpu::RenderPassDescriptor rp{.colorAttachmentCount = 1, .colorAttachments = &ca};
        auto enc = s.device.CreateCommandEncoder();
        auto pass = enc.BeginRenderPass(&rp);
        pass.SetPipeline(s.pipeline);
        pass.SetBindGroup(0, s.bindGroup);
        pass.Draw(3);
        pass.End();
        auto cmd = enc.Finish();
        s.queue.Submit(1, &cmd);

        wgpu::SharedTextureMemoryEndAccessState end{};
        if (s.memory.EndAccess(s.texture, &end) != wgpu::Status::Success || end.fenceCount != 1)
            return fail("EndAccess failed or returned " + std::to_string(end.fenceCount) + " fences");
        wgpu::SharedFenceMTLSharedEventExportInfo mtl{};
        wgpu::SharedFenceExportInfo info{.nextInChain = &mtl};
        end.fences[0].ExportInfo(&info);
        if (info.type != wgpu::SharedFenceType::MTLSharedEvent) return fail("producer fence is not an MTLSharedEvent");
        s.producerFence = end.fences[0];
        *producerEvent = mtl.sharedEvent;
        *producerValue = end.signaledValues[0];
        s.frame++;
        s.instance.ProcessEvents();
        return s.error.empty() ? 1 : 0;
    }
}

/// CGWindowID of an NSWindow, for `screencapture -l`.
long ds_window_number(void* nsWindow) { return [(__bridge NSWindow*)nsWindow windowNumber]; }

void ds_destroy() {
    @autoreleasepool {
        if (s.surface) CFRelease(s.surface);
        s = State{};
    }
}
}
