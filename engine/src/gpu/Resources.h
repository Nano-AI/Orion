/*  Thin GPU abstraction — textures, kernels, command buffers.
 *
 *  Same contract as MetalDevice.h: no Objective-C in the header, all the Metal
 *  API surface behind an opaque Impl. Feature code touches these types and
 *  nothing lower.
 */

#pragma once

#include "gpu/MetalDevice.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace orion::gpu {

enum class PixelFormat {
    R16Uint,      // raw sensor samples
    R16Float,     // single-channel pyramids — log luminance in a bounded window
    R32Float,     // linearised mosaic, green plane
    RG32Float,    // guided-filter moments and coefficients
    RGBA16Float,  // the working format — scene-linear Rec.2020
    RGBA32Float,  // profile tables, where the entries are spec'd as 32-bit float
    RGBA8Unorm,   // display output
};

std::size_t bytesPerPixel(PixelFormat) noexcept;
const char* formatName(PixelFormat) noexcept;

class Texture {
public:
    static std::unique_ptr<Texture> create(Device&, std::uint32_t width,
                                           std::uint32_t height, PixelFormat);
    ~Texture();
    Texture(const Texture&)            = delete;
    Texture& operator=(const Texture&) = delete;

    void upload(const void* src, std::size_t bytesPerRow);
    void download(void* dst, std::size_t bytesPerRow) const;

    /// Reads only the top-left width x height rectangle. The orientation node
    /// allocates square so a rotation needs no recompile, so most of its
    /// texture is never written — copying all of it would emit a black band.
    void download(void* dst, std::size_t bytesPerRow,
                  std::uint32_t width, std::uint32_t height) const;

    /// Reads one pixel. Cheap on unified memory — no blit, no round trip.
    void readPixel(std::uint32_t x, std::uint32_t y, void* dst) const;

    [[nodiscard]] std::uint32_t width()  const noexcept { return width_; }
    [[nodiscard]] std::uint32_t height() const noexcept { return height_; }
    [[nodiscard]] PixelFormat   format() const noexcept { return format_; }
    [[nodiscard]] std::size_t   sizeBytes() const noexcept {
        return static_cast<std::size_t>(width_) * height_ * bytesPerPixel(format_);
    }
    [[nodiscard]] void* raw() const noexcept;

private:
    Texture() = default;
    struct Impl;
    std::unique_ptr<Impl> impl_;
    std::uint32_t width_ = 0, height_ = 0;
    PixelFormat   format_ = PixelFormat::R32Float;
};

/// A compiled .metallib. Kernels are looked up from it by entry-point name.
class Library {
public:
    static std::unique_ptr<Library> createFromFile(Device&, const std::string& path);
    ~Library();
    Library(const Library&)            = delete;
    Library& operator=(const Library&) = delete;

    [[nodiscard]] void* raw() const noexcept;

private:
    Library() = default;
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

class Kernel {
public:
    static std::unique_ptr<Kernel> create(Device&, Library&, const std::string& entryPoint);
    ~Kernel();
    Kernel(const Kernel&)            = delete;
    Kernel& operator=(const Kernel&) = delete;

    [[nodiscard]] const std::string& name() const noexcept { return name_; }
    [[nodiscard]] void* raw() const noexcept;

    /// Threadgroup width the hardware prefers for this kernel.
    [[nodiscard]] std::uint32_t threadExecutionWidth() const noexcept { return execWidth_; }
    [[nodiscard]] std::uint32_t maxThreadsPerGroup()  const noexcept { return maxThreads_; }

private:
    Kernel() = default;
    struct Impl;
    std::unique_ptr<Impl> impl_;
    std::string   name_;
    std::uint32_t execWidth_  = 32;
    std::uint32_t maxThreads_ = 1024;
};

/// One batch of work. Encode every dispatch, then commit once — a single
/// command buffer per frame is what keeps the pipeline resident on the GPU.
class CommandBuffer {
public:
    explicit CommandBuffer(Device&);
    ~CommandBuffer();
    CommandBuffer(const CommandBuffer&)            = delete;
    CommandBuffer& operator=(const CommandBuffer&) = delete;

    /// Textures bind in order from index 0; params go to buffer(0).
    void dispatch(const Kernel&,
                  const std::vector<const Texture*>& textures,
                  const void* params, std::size_t paramBytes,
                  std::uint32_t width, std::uint32_t height);

    void commitAndWait();

    /// GPU-side duration of the committed work. Valid after commitAndWait().
    [[nodiscard]] double gpuMilliseconds() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace orion::gpu
