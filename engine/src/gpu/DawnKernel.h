/*  The Dawn backend's view of one prepared .wgsl (#375). Shared by
 *  DawnKernel.cpp, which loads it, and DawnCommands.cpp, which binds by it.
 *  The header lines it parses are documented in engine/shaders/wgsl_prep.py.
 */

#pragma once

#include "gpu/Dawn.h"

#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace orion::gpu::dawn {

struct Shader {
    struct Binding { std::uint32_t slot = 0, binding = 0; };            // Metal slot → WGSL
    struct Storage { std::uint32_t binding = 0; std::string slangFormat; };
    struct Copy    { std::uint32_t to = 0, from = 0, bytes = 0; };       // WGSL ← Metal offset

    struct Variant {
        std::vector<PixelFormat> formats;   // one per storage binding, in header order
        wgpu::ComputePipeline    pipeline;
        wgpu::BindGroupLayout    layout;
    };

    std::shared_ptr<Context> ctx;
    std::string path, source, entry;
    std::vector<Binding> textures;
    std::vector<Storage> storage;
    std::vector<Copy>    copies;
    bool          hasParams    = false;
    std::uint32_t paramBinding = 0;
    std::uint32_t paramBytes   = 0;   // the WGSL struct, rounded to 16

    /// The pipeline for these storage formats, compiled on first request.
    /// ⚠ A deque so the reference survives later variants being added.
    const Variant& pipeline(const std::vector<PixelFormat>& formats);

private:
    std::mutex          lock_;
    std::deque<Variant> variants_;
};

struct DawnLibrary final : Library::Impl {
    std::shared_ptr<Shader> shader;
    void* raw() const noexcept override { return shader.get(); }
};

struct DawnKernel final : Kernel::Impl {
    std::shared_ptr<Shader> shader;
    void* raw() const noexcept override { return shader.get(); }
};

}  // namespace orion::gpu::dawn
