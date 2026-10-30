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
            r.hasParams    = true;
            r.paramBinding = s.paramBinding;
            r.paramBytes   = s.paramBytes;
            r.paramOffset  = (params.size() + 255) / 256 * 256;
            params.resize(r.paramOffset + r.paramBytes, 0);
            const auto* from = static_cast<const std::uint8_t*>(data);
            for (const auto& c : s.copies) {
                if (from == nullptr || c.from >= dataBytes) continue;
                const std::size_t n = std::min<std::size_t>(c.bytes, dataBytes - c.from);
                std::memcpy(params.data() + r.paramOffset + c.to, from + c.from, n);
            }
        }

        // Shaders declare [numthreads(16,16,1)]; keep the host side in step.
        r.groupsX = (width + 15) / 16;
        r.groupsY = (height + 15) / 16;
        recorded.push_back(std::move(r));
    }

    void commitAndWait() override {
        const auto start = std::chrono::steady_clock::now();
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
        wgpu::ComputePassEncoder pass = enc.BeginComputePass();
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
        wgpu::CommandBuffer cmd = enc.Finish();
        ctx->queue.Submit(1, &cmd);
        ctx->finish();
        gpuMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        recorded.clear();
        params.clear();
        ctx->check("GPU work failed");
    }
};

}  // namespace

std::unique_ptr<CommandBuffer::Impl> makeCommandBuffer(std::shared_ptr<Context> ctx) {
    auto c = std::make_unique<DawnCommandBuffer>();
    c->ctx = std::move(ctx);
    return c;
}

}  // namespace orion::gpu::dawn
