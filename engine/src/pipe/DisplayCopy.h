/*  The canvas's copy of a render (#386), where a shell cannot draw the
 *  engine's texture itself: Windows and Linux, and on macOS headless, OpenGL
 *  or a failed import (#357, #376).
 *
 *  On the GPU first: the picture `level` 2x2 halvings down, the size present.mm
 *  reduces it to for the shared surface, converted to RGBA8
 *  (display_reduce.slang). Then read back without waiting (gpu::Readback):
 *  the canvas takes the newest copy that has landed and never queues behind
 *  the engine.
 */

#pragma once

#include "gpu/Resources.h"

#include <cstdint>
#include <memory>
#include <string>

namespace orion::pipe {

class DisplayCopy {
public:
    DisplayCopy(gpu::Device&, const std::string& shaderDir);

    /// The size `level` halvings of a width x height picture come to: present.mm's
    /// chain, one 2x2 average rounding up, then mip levels rounding down. Level
    /// 0, and a picture too small to halve, is the picture as it is.
    static void sizeAt(std::uint32_t width, std::uint32_t height, int& level,
                       std::uint32_t& outWidth, std::uint32_t& outHeight) noexcept;

    /// Queues the copy of `src`'s top-left width x height at `level` and returns
    /// at once with its ticket and size. `readback()` hands it out once it lands.
    std::uint64_t begin(const gpu::Texture& src, std::uint32_t width, std::uint32_t height,
                        int level, std::uint32_t& outWidth, std::uint32_t& outHeight);

    /// Now: `src`'s top-left width x height as RGBA8 rows of width * 4 bytes
    /// (orion_engine_read_pixels).
    void read(const gpu::Texture& src, std::uint32_t width, std::uint32_t height,
              std::uint8_t* dst);

    [[nodiscard]] gpu::Readback& readback() noexcept { return readback_; }

    /// Instead of a readback, into the texture another API draws (Windows,
    /// gpu::Texture::createShared): `src` at `level`, queued behind the render.
    /// Returns the texture's handle, with a generation that changes whenever the
    /// texture does (import it again); null where this system has none.
    void* share(const gpu::Texture& src, std::uint32_t width, std::uint32_t height, int level,
                int& generation, std::uint32_t& outWidth, std::uint32_t& outHeight);

private:
    /// The texture holding `src` at `level` as RGBA8: `src` itself when there
    /// is nothing to do, else `target_`, its work committed (and `wait`ed for).
    const gpu::Texture& reduce(const gpu::Texture& src, std::uint32_t width, std::uint32_t height,
                               int level, bool wait, std::uint32_t& outWidth, std::uint32_t& outHeight);
    /// display_reduce from `src`'s top-left width x height into all of `dst`, committed.
    void dispatch(const gpu::Texture& src, std::uint32_t width, std::uint32_t height, int level,
                  const gpu::Texture& dst, bool wait);

    gpu::Device&                  device_;
    std::unique_ptr<gpu::Library> library_;
    std::unique_ptr<gpu::Kernel>  kernel_;
    /// Reused while the size holds: one per graph switch, not one a frame.
    std::unique_ptr<gpu::Texture> target_;
    gpu::Readback                 readback_;
    std::unique_ptr<gpu::Texture> shared_;   // `share`'s
    int                           generation_ = 0;
};

}  // namespace orion::pipe
