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

void Device::reclaim() noexcept { impl_->reclaim(); }

std::uint64_t Device::allocatedBytes() const noexcept { return impl_->allocatedBytes(); }

std::uint64_t Device::peakBytes() const noexcept { return peak_.load(); }

void Device::resetPeak() noexcept {
    const std::uint64_t now = impl_->allocatedBytes();
    peak_.store(now ? now : textureBytes_->load());
}

std::uint64_t Device::textureBytes() const noexcept { return textureBytes_->load(); }

std::uint64_t Device::freeBytes() const noexcept {
    const std::uint64_t reported = impl_->freeBytes();
    if (reported != kUnknown) return reported;
    const std::uint64_t ceiling = ceiling_.load();
    if (ceiling == 0) return kUnknown;
    const std::uint64_t allocated = impl_->allocatedBytes();
    const std::uint64_t held = allocated ? allocated : textureBytes_->load();
    return ceiling > held ? ceiling - held : 0;
}

void Device::noteOutOfMemory() noexcept {
    const std::uint64_t allocated = impl_->allocatedBytes();
    ceiling_.store(allocated ? allocated : textureBytes_->load());
}

std::shared_ptr<std::atomic<std::uint64_t>> Device::noteTexture(std::uint64_t bytes) noexcept {
    const std::uint64_t live = textureBytes_->fetch_add(bytes) + bytes;
    // The allocator's figure includes what the texture just took; where the
    // backend has none, the textures alive are the honest stand-in.
    const std::uint64_t allocated = impl_->allocatedBytes();
    const std::uint64_t now = allocated ? allocated : live;
    std::uint64_t seen = peak_.load();
    while (now > seen && !peak_.compare_exchange_weak(seen, now)) {}
    // A ceiling learned from one refusal is not the card's forever: what was
    // granted since raises it (another process gave memory back).
    std::uint64_t ceiling = ceiling_.load();
    while (ceiling != 0 && now > ceiling && !ceiling_.compare_exchange_weak(ceiling, now)) {}
    return textureBytes_;
}

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

Texture::~Texture() {
    if (account_) account_->fetch_sub(sizeBytes());
}

std::unique_ptr<Texture> Texture::create(Device& device, std::uint32_t width,
                                         std::uint32_t height, PixelFormat format,
                                         bool linear) {
    if (width == 0 || height == 0) throw std::runtime_error("zero-sized texture");
    auto t = std::unique_ptr<Texture>(new Texture());
    t->width_  = width;
    t->height_ = height;
    t->format_ = format;
    t->impl_   = device.impl().texture(width, height, format, linear);
    t->account_ = device.noteTexture(t->sizeBytes());
    return t;
}

std::unique_ptr<Texture> Texture::createShared(Device& device, std::uint32_t width,
                                               std::uint32_t height) {
    if (width == 0 || height == 0) throw std::runtime_error("zero-sized texture");
    auto impl = device.impl().sharedTexture(width, height);
    if (!impl) return nullptr;
    auto t = std::unique_ptr<Texture>(new Texture());
    t->width_  = width;
    t->height_ = height;
    t->format_ = PixelFormat::RGBA8Unorm;
    t->impl_   = std::move(impl);
    t->account_ = device.noteTexture(t->sizeBytes());
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

void Texture::downloadRect(void* dst, std::size_t bytesPerRow, std::uint32_t x, std::uint32_t y,
                           std::uint32_t width, std::uint32_t height) const {
    if (x >= width_ || y >= height_) return;
    impl_->download(dst, bytesPerRow, x, y, std::min(width, width_ - x), std::min(height, height_ - y));
}

void Texture::readPixel(std::uint32_t x, std::uint32_t y, void* dst) const {
    if (x >= width_ || y >= height_) return;
    impl_->download(dst, bytesPerPixel(format_), x, y, 1, 1);
}

void* Texture::raw() const noexcept { return impl_->raw(); }
void* Texture::displayHandle() const noexcept { return impl_->displayHandle(); }
void* Texture::sharedHandle() const noexcept { return impl_->sharedHandle(); }
void Texture::beginShared() { impl_->beginShared(); }
void Texture::endShared() { impl_->endShared(); }

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
void CommandBuffer::commit() { impl_->commit(); }
void CommandBuffer::commitTracked() { impl_->commitTracked(); }
bool CommandBuffer::finished(bool wait) { return impl_->finished(wait); }

void CommandBuffer::timeOnGpu() { impl_->timeOnGpu(); }

double CommandBuffer::gpuMilliseconds() const noexcept { return impl_->gpuMs; }
bool CommandBuffer::gpuClocked() const noexcept { return impl_->gpuClock; }
double CommandBuffer::wallMilliseconds() const noexcept { return impl_->wallMs; }

}  // namespace orion::gpu
