#include "gpu/TexturePool.h"

namespace orion::gpu {

std::unique_ptr<Texture> TexturePool::acquire(std::uint32_t width,
                                              std::uint32_t height,
                                              PixelFormat format) {
    const Shape shape{width, height, format};

    std::unique_ptr<Texture> texture;
    const auto it = free_.find(shape);
    if (it != free_.end() && !it->second.empty()) {
        texture = std::move(it->second.back());
        it->second.pop_back();
        // ⚠ The empty vector is left in place rather than erased. A graph asks
        // for the same handful of shapes thousands of times, so the entry will
        // be wanted again within microseconds, and erasing it means a node
        // insertion on the next release — the allocation this class exists to
        // avoid, moved from the driver into the map.
        ++hits_;
    } else {
        texture = Texture::create(*device_, width, height, format);
        ++misses_;
    }

    liveBytes_ += texture->sizeBytes();
    if (liveBytes_ > peakLive_) peakLive_ = liveBytes_;
    return texture;
}

void TexturePool::release(std::unique_ptr<Texture> texture) {
    if (!texture) return;

    const std::size_t bytes = texture->sizeBytes();
    // ⚠ Guarded rather than assumed. A texture released twice, or one released
    // that this pool never handed out, would drive an unsigned counter through
    // zero and report a live figure in the exabytes — which is precisely how a
    // memory instrument becomes a source of false alarms. Clamping keeps the
    // number honest; the double release is still a bug, and the caller's own
    // ownership is what has to prevent it.
    liveBytes_ = (bytes > liveBytes_) ? 0 : liveBytes_ - bytes;

    const Shape shape{texture->width(), texture->height(), texture->format()};
    free_[shape].push_back(std::move(texture));
}

std::size_t TexturePool::idleBytes() const noexcept {
    std::size_t total = 0;
    for (const auto& [shape, textures] : free_) {
        for (const auto& t : textures) {
            if (t) total += t->sizeBytes();
        }
    }
    return total;
}

void TexturePool::shrink() noexcept {
    free_.clear();
}

}  // namespace orion::gpu
