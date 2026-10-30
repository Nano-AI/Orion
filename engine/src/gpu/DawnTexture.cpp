// The Dawn backend's textures (#375): upload, readback, the platform's share
// that lets a shell draw an RGBA8 result with no copy (Dawn.h), and the staging
// buffers of an asynchronous readback (#386).

#include "gpu/Dawn.h"

#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace orion::gpu::dawn {
namespace {

struct DawnTexture final : Texture::Impl {
    std::shared_ptr<Context> ctx;
    wgpu::Texture     tex;
    wgpu::TextureView view;
    std::size_t       bpp = 0;

    // The display path, RGBA8 only, where the platform has one. After `tex`,
    // so it goes first: it ends Dawn's access to the texture it shares.
    std::unique_ptr<DisplayShare> share;

    void upload(const void* src, std::size_t bytesPerRow, std::uint32_t width,
                std::uint32_t height) override {
        if (width == 0 || height == 0) throw std::runtime_error("Dawn texture upload: empty");
        ErrorScope scope(*ctx);
        wgpu::TexelCopyTextureInfo dst{.texture = tex};
        wgpu::TexelCopyBufferLayout layout{.bytesPerRow = static_cast<std::uint32_t>(bytesPerRow),
                                           .rowsPerImage = height};
        wgpu::Extent3D size{width, height, 1};
        // The last row need not be padded to bytesPerRow; Metal reads no more.
        const std::size_t bytes = bytesPerRow * (height - 1) + width * bpp;
        ctx->queue.WriteTexture(&dst, src, bytes, &layout, &size);
        scope.check("Dawn texture upload");
    }

    void download(void* dst, std::size_t bytesPerRow, std::uint32_t x, std::uint32_t y,
                  std::uint32_t width, std::uint32_t height) const override {
        // A buffer copy needs rows on 256 bytes; the caller's rows are packed.
        if (width == 0 || height == 0) return;
        ErrorScope scope(*ctx);
        const std::size_t row   = width * bpp;
        const std::size_t pitch = (row + 255) / 256 * 256;
        wgpu::BufferDescriptor bd{.usage = wgpu::BufferUsage::MapRead | wgpu::BufferUsage::CopyDst,
                                  .size = pitch * height};
        wgpu::Buffer staging = ctx->device.CreateBuffer(&bd);

        wgpu::TexelCopyTextureInfo from{.texture = tex, .origin = {x, y, 0}};
        wgpu::TexelCopyBufferInfo to{.layout = {.bytesPerRow = static_cast<std::uint32_t>(pitch),
                                                .rowsPerImage = height},
                                     .buffer = staging};
        wgpu::Extent3D size{width, height, 1};
        wgpu::CommandEncoder enc = ctx->device.CreateCommandEncoder();
        enc.CopyTextureToBuffer(&from, &to, &size);
        wgpu::CommandBuffer cmd = enc.Finish();
        ctx->queue.Submit(1, &cmd);

        bool mapped = false;
        ctx->wait(staging.MapAsync(wgpu::MapMode::Read, 0, pitch * height,
                                   wgpu::CallbackMode::WaitAnyOnly,
                                   [&](wgpu::MapAsyncStatus st, wgpu::StringView) {
                                       mapped = st == wgpu::MapAsyncStatus::Success;
                                   }));
        scope.check("Dawn texture readback");
        if (!mapped) throw std::runtime_error("Dawn texture readback: map failed");
        const auto* src = static_cast<const std::uint8_t*>(staging.GetConstMappedRange(0, pitch * height));
        auto* out = static_cast<std::uint8_t*>(dst);
        for (std::uint32_t r = 0; r < height; ++r) {
            std::memcpy(out + r * bytesPerRow, src + r * pitch, row);
        }
        staging.Unmap();
    }

    void* raw() const noexcept override { return tex.Get(); }
    void* displayHandle() const noexcept override { return share ? share->handle() : nullptr; }
};

wgpu::TextureUsage usageFor(PixelFormat) {
    // Every format the engine uses is storage-capable once TextureFormatsTier1
    // is on (DawnDevice.cpp refuses a device without it).
    return wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::StorageBinding |
           wgpu::TextureUsage::CopySrc | wgpu::TextureUsage::CopyDst |
           wgpu::TextureUsage::RenderAttachment;
}

/// One of a Readback's staging buffers (#386): the copy is submitted and the
/// map requested at `start`, and nothing waits for either until asked.
struct DawnStaging final : Readback::Slot {
    std::shared_ptr<Context> ctx;
    wgpu::Buffer  buffer;
    std::size_t   capacity = 0, row = 0, pitch = 0;
    std::uint32_t height = 0;
    wgpu::Future  future{};
    bool pending = false, mapped = false, ok = false;

    ~DawnStaging() override {
        try { release(); } catch (...) {}   // the callback holds `this`: let it run first
    }

    void start(const Texture& t, std::uint32_t width, std::uint32_t h) override {
        auto* d = dynamic_cast<const DawnTexture*>(&t.impl());
        if (!d) throw std::runtime_error("a Metal texture was read back on a Dawn device");
        if (width == 0 || h == 0) throw std::runtime_error("Dawn readback: empty");
        ErrorScope scope(*ctx);
        row    = width * d->bpp;
        pitch  = (row + 255) / 256 * 256;   // a buffer copy's rows sit on 256 bytes
        height = h;
        if (pitch * h > capacity) {   // grows only: no allocation a frame
            wgpu::BufferDescriptor bd{.usage = wgpu::BufferUsage::MapRead | wgpu::BufferUsage::CopyDst,
                                      .size = pitch * h};
            buffer   = ctx->device.CreateBuffer(&bd);
            capacity = pitch * h;
        }
        wgpu::TexelCopyTextureInfo from{.texture = d->tex};
        wgpu::TexelCopyBufferInfo to{.layout = {.bytesPerRow = static_cast<std::uint32_t>(pitch),
                                                .rowsPerImage = h},
                                     .buffer = buffer};
        wgpu::Extent3D size{width, h, 1};
        wgpu::CommandEncoder enc = ctx->device.CreateCommandEncoder();
        enc.CopyTextureToBuffer(&from, &to, &size);
        wgpu::CommandBuffer cmd = enc.Finish();
        ctx->queue.Submit(1, &cmd);
        future = buffer.MapAsync(wgpu::MapMode::Read, 0, pitch * h, wgpu::CallbackMode::WaitAnyOnly,
                                 [this](wgpu::MapAsyncStatus st, wgpu::StringView) {
                                     ok = st == wgpu::MapAsyncStatus::Success;
                                 });
        pending = true;
        scope.check("Dawn readback");
    }

    bool landed(bool wait) override {
        if (!pending) return mapped;
        const auto st = ctx->instance.WaitAny(future, wait ? UINT64_MAX : 0);
        if (st == wgpu::WaitStatus::TimedOut) return false;
        pending = false;
        if (st != wgpu::WaitStatus::Success || !ok) throw std::runtime_error("Dawn readback: map failed");
        mapped = true;
        return true;
    }

    void copy(void* dst, std::size_t bytesPerRow) const override {
        const auto* src = static_cast<const std::uint8_t*>(buffer.GetConstMappedRange(0, pitch * height));
        if (!src) throw std::runtime_error("Dawn readback: not mapped");
        for (std::uint32_t r = 0; r < height; ++r)
            std::memcpy(static_cast<std::uint8_t*>(dst) + r * bytesPerRow, src + r * pitch, row);
    }

    void release() override {
        if (pending) ctx->instance.WaitAny(future, UINT64_MAX);   // a map cannot be cancelled under us
        if (pending || mapped) buffer.Unmap();
        pending = mapped = false;
    }
};

}  // namespace

std::unique_ptr<Readback::Slot> makeStaging(std::shared_ptr<Context> ctx) {
    auto s = std::make_unique<DawnStaging>();
    s->ctx = std::move(ctx);
    return s;
}

std::unique_ptr<Texture::Impl> makeTexture(std::shared_ptr<Context> ctx, std::uint32_t width,
                                           std::uint32_t height, PixelFormat format) {
    auto t = std::make_unique<DawnTexture>();
    t->ctx = std::move(ctx);
    ErrorScope scope(*t->ctx);
    t->bpp = bytesPerPixel(format);
    if (format == PixelFormat::RGBA8Unorm) {
        t->share = shareForDisplay(*t->ctx, width, height, usageFor(format), t->tex);
    }
    if (!t->tex) {
        wgpu::TextureDescriptor td{.usage = usageFor(format),
                                   .size = {width, height, 1},
                                   .format = toWgpu(format)};
        t->tex = t->ctx->device.CreateTexture(&td);
    }
    scope.check("Dawn texture allocation");
    if (!t->tex) throw std::runtime_error("Dawn texture allocation failed");
    t->view = t->tex.CreateView();
    return t;
}

const wgpu::TextureView& viewOf(const Texture& t) {
    auto* d = dynamic_cast<const DawnTexture*>(&t.impl());
    if (!d) throw std::runtime_error("a Metal texture was bound on a Dawn device");
    return d->view;
}

}  // namespace orion::gpu::dawn
