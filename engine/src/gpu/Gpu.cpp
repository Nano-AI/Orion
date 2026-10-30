// The backend-neutral half of Device.h and Resources.h: choosing a backend,
// the format table, and forwarding to the Impl the backend made (#375).

#include "gpu/Backend.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <stdexcept>

namespace orion::gpu {

// ── Device ─────────────────────────────────────────────────────────────────

Device::Device(std::unique_ptr<Impl> impl)
    : impl_(std::move(impl)), info_(impl_->info()), backend_(impl_->backend()) {}

Device::~Device() = default;

std::unique_ptr<Device> Device::create() {
    const char* env = std::getenv("ORION_GPU");
    if (env == nullptr || *env == '\0') {
#ifdef __APPLE__
        return create(Backend::Metal);
#else
        return create(Backend::Dawn);   // the only backend off macOS (#376)
#endif
    }
    if (std::strcmp(env, "metal") == 0) return create(Backend::Metal);
    if (std::strcmp(env, "dawn") == 0) return create(Backend::Dawn);
    throw std::runtime_error(std::string("ORION_GPU must be metal or dawn, not ") + env);
}

std::unique_ptr<Device> Device::create(Backend backend) {
    auto impl = backend == Backend::Dawn ? makeDawnDevice() : makeMetalDevice();
    return std::unique_ptr<Device>(new Device(std::move(impl)));
}

void* Device::rawDevice() const noexcept { return impl_->metalDevice(); }

#ifndef ORION_HAS_DAWN
std::unique_ptr<Device::Impl> makeDawnDevice() {
    throw std::runtime_error("this engine was built without Dawn (third_party/dawn-build)");
}
#endif

#ifndef __APPLE__
std::unique_ptr<Device::Impl> makeMetalDevice() {
    throw std::runtime_error("Metal is macOS only; this system renders through Dawn");
}
#endif

// ── Formats ────────────────────────────────────────────────────────────────

std::size_t bytesPerPixel(PixelFormat f) noexcept {
    switch (f) {
        case PixelFormat::R16Uint:     return 2;
        case PixelFormat::R16Float:    return 2;
        case PixelFormat::R32Float:    return 4;
        case PixelFormat::RG16Float:   return 4;
        case PixelFormat::RG32Float:   return 8;
        case PixelFormat::RGBA16Float: return 8;
        case PixelFormat::RGBA32Float: return 16;
        case PixelFormat::RGBA8Unorm:  return 4;
    }
    return 0;
}

const char* formatName(PixelFormat f) noexcept {
    switch (f) {
        case PixelFormat::R16Uint:     return "r16u";
        case PixelFormat::R16Float:    return "r16f";
        case PixelFormat::R32Float:    return "r32f";
        case PixelFormat::RG16Float:   return "rg16f";
        case PixelFormat::RG32Float:   return "rg32f";
        case PixelFormat::RGBA16Float: return "rgba16f";
        case PixelFormat::RGBA32Float: return "rgba32f";
        case PixelFormat::RGBA8Unorm:  return "rgba8";
    }
    return "?";
}

// ── Texture ────────────────────────────────────────────────────────────────

Texture::~Texture() = default;

std::unique_ptr<Texture> Texture::create(Device& device, std::uint32_t width,
                                         std::uint32_t height, PixelFormat format,
                                         bool linear) {
    if (width == 0 || height == 0) throw std::runtime_error("zero-sized texture");
    auto t = std::unique_ptr<Texture>(new Texture());
    t->width_  = width;
    t->height_ = height;
    t->format_ = format;
    t->impl_   = device.impl().texture(width, height, format, linear);
    return t;
}

void Texture::upload(const void* src, std::size_t bytesPerRow) {
    impl_->upload(src, bytesPerRow, width_, height_);
}

void Texture::download(void* dst, std::size_t bytesPerRow) const {
    impl_->download(dst, bytesPerRow, 0, 0, width_, height_);
}

void Texture::download(void* dst, std::size_t bytesPerRow,
                       std::uint32_t width, std::uint32_t height) const {
    impl_->download(dst, bytesPerRow, 0, 0, std::min(width, width_),
                    std::min(height, height_));
}

void Texture::readPixel(std::uint32_t x, std::uint32_t y, void* dst) const {
    if (x >= width_ || y >= height_) return;
    impl_->download(dst, bytesPerPixel(format_), x, y, 1, 1);
}

void* Texture::raw() const noexcept { return impl_->raw(); }
void* Texture::displayHandle() const noexcept { return impl_->displayHandle(); }

// ── Library, Kernel ────────────────────────────────────────────────────────

Library::~Library() = default;

std::unique_ptr<Library> Library::createFromFile(Device& device, const std::string& path) {
    auto l = std::unique_ptr<Library>(new Library());
    l->impl_ = device.impl().library(path);
    return l;
}

void* Library::raw() const noexcept { return impl_->raw(); }

Kernel::~Kernel() = default;

std::unique_ptr<Kernel> Kernel::create(Device& device, Library& library,
                                       const std::string& entryPoint) {
    auto k = std::unique_ptr<Kernel>(new Kernel());
    k->name_ = entryPoint;
    k->impl_ = device.impl().kernel(library, entryPoint);
    return k;
}

void* Kernel::raw() const noexcept { return impl_->raw(); }
std::uint32_t Kernel::threadExecutionWidth() const noexcept { return impl_->execWidth; }
std::uint32_t Kernel::maxThreadsPerGroup() const noexcept { return impl_->maxThreads; }
std::uint32_t Kernel::textureSlotsUsed() const noexcept { return impl_->textureSlots; }

// ── CommandBuffer ──────────────────────────────────────────────────────────

CommandBuffer::CommandBuffer(Device& device) : impl_(device.impl().commandBuffer()) {}

CommandBuffer::~CommandBuffer() = default;

void CommandBuffer::dispatch(const Kernel& kernel,
                             const std::vector<const Texture*>& textures,
                             const void* params, std::size_t paramBytes,
                             std::uint32_t width, std::uint32_t height) {
    impl_->dispatch(kernel, textures, params, paramBytes, width, height);
}

void CommandBuffer::commitAndWait() { impl_->commitAndWait(); }

double CommandBuffer::gpuMilliseconds() const noexcept { return impl_->gpuMs; }

}  // namespace orion::gpu
