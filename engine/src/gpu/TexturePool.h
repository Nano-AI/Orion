/*  A pool of textures, so a graph pays for what is live rather than what exists.
 *
 *  ⚠ **Why this exists, measured rather than assumed.** A 24 MP frame allocates
 *  **7,186 MiB** of intermediates across 173 nodes — one texture per node, held
 *  from construction to destruction — and an 8 GB Mac has about **6,144 MiB**
 *  to give. It cannot open there, and `CLAUDE.md`'s macOS 14 floor admits the
 *  base M1 and M2 Air. Decision #152.
 *
 *  Decision #153 then measured what the graph actually needs at once, by
 *  walking the execution order and keeping each output alive only from the node
 *  that writes it to the last node that reads it: **1,202 MiB**. Five sixths of
 *  that memory is held for nothing. That number is what ruled out the two
 *  expensive answers — tiling and lower intermediate precision both shrink the
 *  *working set*, and the working set already fits with room. What does not fit
 *  is the **lifetime**.
 *
 *  So this is deliberately not clever. It is a free list keyed by the exact
 *  shape of a texture, and the whole idea is that a graph hands a texture back
 *  when its last reader has run and the next node of the same shape gets that
 *  one instead of a new allocation.
 *
 *  ## What this file does NOT do yet
 *
 *  ⚠ **Nothing in the engine uses it.** It is built and tested on its own, and
 *  wiring it into `Pipeline` is a separate change — the swap is where the
 *  correctness risk lives, because a texture handed back while a later node
 *  still reads it renders a plausible picture made of another node's pixels,
 *  which is exactly the class of bug this repository has shipped before and
 *  caught only with a byte-for-byte test. A pool that is merely *probably*
 *  right is worse than 7 GiB that is definitely right.
 */

#pragma once

#include "gpu/Resources.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <utility>
#include <vector>

namespace orion::gpu {

class TexturePool {
public:
    explicit TexturePool(Device& device) noexcept : device_(&device) {}

    TexturePool(const TexturePool&)            = delete;
    TexturePool& operator=(const TexturePool&) = delete;

    /// A texture of exactly this shape — reused if one is free, allocated if
    /// not.
    ///
    /// ⚠ **Exact shape, never "big enough".** Handing back a 6000 × 4000
    /// texture for a request of 3000 × 2000 would work for a kernel that writes
    /// what it is told and reads what it wrote, and would quietly break the
    /// moment anything samples by normalized coordinate or asks the texture its
    /// own size — which the mask kernels, the guided chain and every downsample
    /// all do. The saving from loose matching is not worth a class of bug that
    /// renders a plausible picture.
    ///
    /// ⚠ The contents are **undefined**: this is whatever the last user left,
    /// not zeroes. Every node in this pipeline writes every pixel it owns, so
    /// clearing would be pure cost — but a node that ever writes only part of
    /// its output must clear first, and this is the sentence that says so.
    [[nodiscard]] std::unique_ptr<Texture> acquire(std::uint32_t width,
                                                   std::uint32_t height,
                                                   PixelFormat format);

    /// Hand a texture back. Null is accepted and ignored, so a caller releasing
    /// an optional slot needs no branch.
    ///
    /// ⚠ **The caller promises nothing else holds it.** There is no reference
    /// counting here on purpose: a pool that tries to be safe by counting hides
    /// the question of *when* a texture dies, and that question is the whole
    /// point — `Pipeline` knows the answer exactly, from the execution order,
    /// and anything less precise gives back less memory than #153 measured.
    void release(std::unique_ptr<Texture> texture);

    /// Bytes currently held by free textures — the pool's own overhead.
    [[nodiscard]] std::size_t idleBytes() const noexcept;

    /// Bytes handed out and not yet returned.
    [[nodiscard]] std::size_t liveBytes() const noexcept { return liveBytes_; }

    /// The most `liveBytes` has ever been. This is the number #153 predicted at
    /// 1,202 MiB, and comparing the two is how the wiring gets checked.
    [[nodiscard]] std::size_t peakLiveBytes() const noexcept { return peakLive_; }

    /// How many `acquire` calls were served from the free list rather than the
    /// driver. A pool that never hits is a pool that is not helping, and a test
    /// that only checks correctness would not notice.
    [[nodiscard]] std::size_t hits() const noexcept { return hits_; }
    [[nodiscard]] std::size_t misses() const noexcept { return misses_; }

    /// Drop every free texture. Live ones are untouched — they are not the
    /// pool's to free.
    void shrink() noexcept;

private:
    /// Width, height, format. Ordered so `std::map` can key on it directly.
    struct Shape {
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        PixelFormat   format = PixelFormat::R32Float;

        friend bool operator<(const Shape& a, const Shape& b) noexcept {
            if (a.width != b.width) return a.width < b.width;
            if (a.height != b.height) return a.height < b.height;
            return static_cast<int>(a.format) < static_cast<int>(b.format);
        }
    };

    Device* device_ = nullptr;
    std::map<Shape, std::vector<std::unique_ptr<Texture>>> free_;
    std::size_t liveBytes_ = 0;
    std::size_t peakLive_  = 0;
    std::size_t hits_      = 0;
    std::size_t misses_    = 0;
};

}  // namespace orion::gpu
