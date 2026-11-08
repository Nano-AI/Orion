// The Dawn backend's command buffer (#375).
//
// Dispatches are recorded on the CPU and encoded at commit, because every
// dispatch's parameters go into one uniform buffer whose size is only known
// then: one buffer and one upload per frame rather than one per node. Each
// parameter block is repacked from the Metal layout the host writes into the
// WGSL layout the shader reads, by the copies wgsl_prep.py listed.

#include "gpu/DawnKernel.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <stdexcept>

namespace orion::gpu::dawn {
namespace {

struct Recorded {
    const Shader::Variant*         variant = nullptr;
    std::vector<wgpu::BindGroupEntry> entries;   // textures; params appended at commit
    bool          hasParams    = false;
    std::uint32_t paramBinding = 0;
    std::size_t   paramOffset  = 0, paramBytes = 0;
    std::uint32_t groupsX = 0, groupsY = 0;
};

struct DawnCommandBuffer final : CommandBuffer::Impl {
    std::shared_ptr<Context> ctx;
    std::vector<Recorded>    recorded;
    std::vector<std::uint8_t> params;   // every block, each on a 256-byte boundary
    // commitTracked's completion. Shared with the callback, which a WaitAnyOnly
    // future runs only inside a WaitAny, possibly after this buffer is gone.
    std::shared_ptr<bool> succeeded;
    wgpu::Future done{};
    std::chrono::steady_clock::time_point submitted;
    bool         timed = false;
    wgpu::Buffer stamps;   // a timed submit's two timestamps, until `clock` reads them

    ~DawnCommandBuffer() override {
        try { finished(true); } catch (...) {}   // a lost device: nothing left to wait for
    }

    void dispatch(const Kernel& kernel, const std::vector<const Texture*>& textures,
                  const void* data, std::size_t dataBytes,
                  std::uint32_t width, std::uint32_t height) override {
        auto* k = dynamic_cast<const DawnKernel*>(&kernel.impl());
        if (!k) throw std::runtime_error("a Metal kernel was dispatched on a Dawn device");
        Shader& s = *k->shader;

        Recorded r;
        for (const auto& b : s.textures) {
            // ⚠ Metal would bind nil and render nothing; say so instead.
            if (b.slot >= textures.size()) {
                throw std::runtime_error("kernel '" + s.entry + "' reads texture slot " +
                                         std::to_string(b.slot) + " but " +
                                         std::to_string(textures.size()) + " are bound");
            }
            r.entries.push_back(wgpu::BindGroupEntry{.binding = b.binding,
                                                     .textureView = viewOf(*textures[b.slot])});
        }

        std::vector<PixelFormat> formats;
        for (const auto& st : s.storage) {
            for (const auto& b : s.textures) {
                if (b.binding == st.binding) formats.push_back(textures[b.slot]->format());
            }
        }
        r.variant = &s.pipeline(formats);

        if (s.hasParams) {
            // ⚠ The host's struct must be the shader's, byte for byte: a short
            // block would otherwise be zero-filled and a long one truncated,
            // and either renders a plausible wrong picture. Metal's setBytes
            // never checked this; the facade catches the throw.
            if (data == nullptr || dataBytes != s.hostBytes) {
                throw std::runtime_error("kernel '" + s.entry + "' takes a " +
                                         std::to_string(s.hostBytes) + "-byte parameter block, given " +
                                         std::to_string(data ? dataBytes : 0));
            }
            r.hasParams    = true;
            r.paramBinding = s.paramBinding;
            r.paramBytes   = s.paramBytes;
            r.paramOffset  = (params.size() + 255) / 256 * 256;
            params.resize(r.paramOffset + r.paramBytes, 0);
            const auto* from = static_cast<const std::uint8_t*>(data);
            for (const auto& c : s.copies) {
                std::memcpy(params.data() + r.paramOffset + c.to, from + c.from, c.bytes);
            }
        }

        // Shaders declare [numthreads(16,16,1)]; keep the host side in step.
        r.groupsX = (width + 15) / 16;
        r.groupsY = (height + 15) / 16;
        recorded.push_back(std::move(r));
    }

    void timeOnGpu() override { timed = ctx->timestamps; }

    void commit() override {
        ErrorScope scope(*ctx);
        submit();
        scope.check("GPU work failed");
    }

    void commitTracked() override {
        if (recorded.empty()) return;
        submitted = std::chrono::steady_clock::now();
        ErrorScope scope(*ctx);
        submit();
        succeeded = std::make_shared<bool>(false);
        done = ctx->queue.OnSubmittedWorkDone(wgpu::CallbackMode::WaitAnyOnly,
            [ok = succeeded](wgpu::QueueWorkDoneStatus st, wgpu::StringView) {
                *ok = st == wgpu::QueueWorkDoneStatus::Success;
            });
        scope.check("GPU work failed");
    }

    bool finished(bool wait) override {
        if (!succeeded) return true;
        if (ctx->instance.WaitAny(done, wait ? UINT64_MAX : 0) == wgpu::WaitStatus::TimedOut) return false;
        const bool ok = *succeeded;
        succeeded.reset();
        wallMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - submitted).count();
        if (!ok) throw std::runtime_error("GPU work failed: the queue did not finish");
        ErrorScope scope(*ctx);
        clock();
        scope.check("GPU timestamps");
        return true;
    }

    void commitAndWait() override {
        // Nothing recorded: a submit would only wait on work queued before it.
        if (recorded.empty()) { gpuMs = wallMs = 0.0; return; }
        const auto start = std::chrono::steady_clock::now();
        ErrorScope scope(*ctx);
        submit();
        // This submit's own completion: the future is taken straight after it.
        bool done = false;
        ctx->wait(ctx->queue.OnSubmittedWorkDone(wgpu::CallbackMode::WaitAnyOnly,
            [&](wgpu::QueueWorkDoneStatus st, wgpu::StringView) {
                done = st == wgpu::QueueWorkDoneStatus::Success;
            }));
        wallMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        if (done) clock();
        scope.check("GPU work failed");
        if (!done) throw std::runtime_error("GPU work failed: the queue did not finish");
    }

    /// gpuMs from a timed submit's timestamps, once its work is done; wall time otherwise.
    void clock() {
        gpuMs = wallMs;
        gpuClock = false;
        if (!stamps) return;
        wgpu::Buffer b = std::move(stamps);
        stamps = nullptr;
        bool mapped = false;
        ctx->wait(b.MapAsync(wgpu::MapMode::Read, 0, 16, wgpu::CallbackMode::WaitAnyOnly,
                             [&](wgpu::MapAsyncStatus st, wgpu::StringView) {
                                 mapped = st == wgpu::MapAsyncStatus::Success;
                             }));
        if (!mapped) return;
        const auto* t = static_cast<const std::uint64_t*>(b.GetConstMappedRange(0, 16));
        // Nanoseconds: Dawn converts from the device's ticks.
        if (t[1] >= t[0]) {
            gpuMs = double(t[1] - t[0]) / 1.0e6;
            gpuClock = true;
        }
        b.Unmap();
    }

    /// Encodes everything recorded and submits it; the recording is emptied either way.
    void submit() {
        struct Clear {
            DawnCommandBuffer* c;
            ~Clear() { c->recorded.clear(); c->params.clear(); }
        } clear{this};
        wgpu::Buffer uniforms;
        if (!params.empty()) {
            wgpu::BufferDescriptor bd{.usage = wgpu::BufferUsage::Uniform,
                                      .size = (params.size() + 15) / 16 * 16,
                                      .mappedAtCreation = true};
            uniforms = ctx->device.CreateBuffer(&bd);
            std::memcpy(uniforms.GetMappedRange(), params.data(), params.size());
            uniforms.Unmap();
        }

        wgpu::CommandEncoder enc = ctx->device.CreateCommandEncoder();
        wgpu::QuerySet queries;
        wgpu::PassTimestampWrites writes{};
        wgpu::ComputePassDescriptor pd{};
        if (timed) {
            wgpu::QuerySetDescriptor qd{.type = wgpu::QueryType::Timestamp, .count = 2};
            queries = ctx->device.CreateQuerySet(&qd);
            writes.querySet = queries;
            writes.beginningOfPassWriteIndex = 0;
            writes.endOfPassWriteIndex = 1;
            pd.timestampWrites = &writes;
        }
        wgpu::ComputePassEncoder pass = enc.BeginComputePass(&pd);
        for (auto& r : recorded) {
            if (r.hasParams) {
                r.entries.push_back(wgpu::BindGroupEntry{.binding = r.paramBinding,
                                                         .buffer = uniforms,
                                                         .offset = r.paramOffset,
                                                         .size = r.paramBytes});
            }
            wgpu::BindGroupDescriptor bgd{.layout = r.variant->layout,
                                          .entryCount = r.entries.size(),
                                          .entries = r.entries.data()};
            pass.SetPipeline(r.variant->pipeline);
            pass.SetBindGroup(0, ctx->device.CreateBindGroup(&bgd));
            pass.DispatchWorkgroups(r.groupsX, r.groupsY, 1);
        }
        pass.End();
        if (timed) {
            wgpu::BufferDescriptor rd{.usage = wgpu::BufferUsage::QueryResolve | wgpu::BufferUsage::CopySrc,
                                      .size = 16};
            wgpu::Buffer resolved = ctx->device.CreateBuffer(&rd);
            wgpu::BufferDescriptor md{.usage = wgpu::BufferUsage::MapRead | wgpu::BufferUsage::CopyDst,
                                      .size = 16};
            stamps = ctx->device.CreateBuffer(&md);
            enc.ResolveQuerySet(queries, 0, 2, resolved, 0);
            enc.CopyBufferToBuffer(resolved, 0, stamps, 0, 16);
        }
        wgpu::CommandBuffer cmd = enc.Finish();
        ctx->queue.Submit(1, &cmd);
    }
};

}  // namespace

std::unique_ptr<CommandBuffer::Impl> makeCommandBuffer(std::shared_ptr<Context> ctx) {
    auto c = std::make_unique<DawnCommandBuffer>();
    c->ctx = std::move(ctx);
    return c;
}

}  // namespace orion::gpu::dawn
