/*  Thin GPU abstraction — textures, kernels, command buffers.
 *
 *  Same contract as Device.h: no Objective-C or WebGPU in the header, each
 *  backend's API surface behind an abstract Impl (gpu/Backend.h). Feature code
 *  touches these types and nothing lower.
 */

#pragma once

#include "gpu/Device.h"

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
    RG16Float,    // the masked denoise's two per-pixel strengths
    RG32Float,    // guided-filter moments and coefficients
    RGBA16Float,  // the working format — scene-linear Rec.2020
    RGBA32Float,  // profile tables, where the entries are spec'd as 32-bit float
    RGBA8Unorm,   // display output
};

std::size_t bytesPerPixel(PixelFormat) noexcept;
const char* formatName(PixelFormat) noexcept;

class Texture {
public:
    /// `linear` backs the texture with a plain row-major buffer, so `upload` is
    /// a copy instead of a CPU swizzle into the GPU's tiled layout: 2 ms
    /// against 17 for a 24 MP mosaic (#319). For a texture the CPU writes whole
    /// and the GPU reads once, like the source; tiled stays the default.
    static std::unique_ptr<Texture> create(Device&, std::uint32_t width,
                                           std::uint32_t height, PixelFormat,
                                           bool linear = false);
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
    /// The backend's own handle: `id<MTLTexture>` on Metal, `WGPUTexture` on
    /// Dawn. Identity only, outside the backend.
    [[nodiscard]] void* raw() const noexcept;

    /// What a shell can draw with no copy: an `id<MTLTexture>` over the same
    /// memory. On Metal, `raw()`. On Dawn, an RGBA8 texture is allocated in an
    /// IOSurface so a Metal view can alias it (#375); any other format answers
    /// null and a shell falls back to `download`.
    [[nodiscard]] void* displayHandle() const noexcept;

    struct Impl;   // backend-private
    [[nodiscard]] Impl& impl() const noexcept { return *impl_; }

private:
    Texture() = default;
    std::unique_ptr<Impl> impl_;
    std::uint32_t width_ = 0, height_ = 0;
    PixelFormat   format_ = PixelFormat::R32Float;
};

/// A compiled .metallib (Metal) or prepared .wgsl (Dawn, which swaps the
/// extension of the path it is given, so callers name the .metallib on both).
/// Kernels are looked up from it by entry-point name.
class Library {
public:
    static std::unique_ptr<Library> createFromFile(Device&, const std::string& path);
    ~Library();
    Library(const Library&)            = delete;
    Library& operator=(const Library&) = delete;

    [[nodiscard]] void* raw() const noexcept;

    struct Impl;   // backend-private
    [[nodiscard]] Impl& impl() const noexcept { return *impl_; }

private:
    Library() = default;
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
    [[nodiscard]] std::uint32_t threadExecutionWidth() const noexcept;
    [[nodiscard]] std::uint32_t maxThreadsPerGroup()  const noexcept;

    /// How many texture slots this kernel needs bound: one past the highest
    /// texture index it actually refers to.
    ///
    /// ⚠ **This exists because binding too few textures is silent.** Metal
    /// leaves an unbound slot nil; reads return zero and writes are discarded,
    /// with no error unless the validation layer is on. A caller that binds
    /// five textures to a kernel whose output sits in slot six therefore
    /// renders nothing at all, successfully.
    ///
    /// That is not hypothetical. Adding one texture to `mask_component.slang`
    /// moved its output from slot 4 to slot 5; a process still running the
    /// previous binary reloaded the new metallib from disk — `Pipeline::compile`
    /// reads them by path — and every mask silently covered zero, for an hour,
    /// with the whole test suite green because the tests ran the matching
    /// binary. `Pipeline::compile` compares this against what it will bind.
    ///
    /// On Dawn the same question is answered from the bindings the WGSL
    /// declares, and an auto layout refuses a short bind group outright.
    [[nodiscard]] std::uint32_t textureSlotsUsed() const noexcept;

    struct Impl;   // backend-private
    [[nodiscard]] Impl& impl() const noexcept { return *impl_; }

private:
    Kernel() = default;
    std::unique_ptr<Impl> impl_;
    std::string   name_;
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

    /// Submits and returns at once: what is committed later on this device runs
    /// after it, a `Readback` included (#386). Metal waits anyway.
    void commit();

    /// GPU-side duration of the committed work. Valid after commitAndWait().
    /// On Dawn, the wall time from submit to completion, which includes the
    /// wait's own latency (no timestamp queries yet).
    [[nodiscard]] double gpuMilliseconds() const noexcept;

    struct Impl;   // backend-private

private:
    std::unique_ptr<Impl> impl_;
};

/// A texture copied to the CPU without waiting for it (#386), for a canvas that
/// cannot draw the texture itself. `begin` queues the copy behind everything
/// committed before it and returns; `newest` names the newest copy that has
/// landed, `take` copies it out. Three copies may be in flight; a fourth
/// `begin` reuses a landed one nobody took, or waits for the oldest.
/// On Metal `begin` copies at once: there the window draws the texture itself
/// (present.mm), and only tests and the headless shell read back.
class Readback {
public:
    explicit Readback(Device&);
    ~Readback();
    Readback(const Readback&)            = delete;
    Readback& operator=(const Readback&) = delete;

    /// Queues a copy of `t`'s top-left width x height. Returns its ticket, from 1.
    std::uint64_t begin(const Texture& t, std::uint32_t width, std::uint32_t height);

    /// The newest copy that has landed since the last `take`: its ticket and
    /// size, or 0 when none has. Held for `take`; older landed copies are
    /// dropped. `wait` first waits for the last begun copy to land.
    std::uint64_t newest(bool wait, std::uint32_t& width, std::uint32_t& height);

    /// What `newest` last named and nobody took yet, without looking again.
    std::uint64_t held(std::uint32_t& width, std::uint32_t& height) const noexcept;

    /// Copies the copy `newest` named into rows of `bytesPerRow` and frees it.
    /// False when `newest` named none (or a `begin` since reused it).
    bool take(void* dst, std::size_t bytesPerRow);

    struct Slot;    // backend-private: one staging buffer (Backend.h)
    struct Entry;

private:
    std::vector<std::unique_ptr<Entry>> entries_;
    Entry*        held_    = nullptr;   // what `newest` named
    std::uint64_t tickets_ = 0, taken_ = 0;
};

}  // namespace orion::gpu
