/*  Thin GPU abstraction — the Device half.
 *
 *  Written once, then left alone: all the fiddly GPU API surface lives behind
 *  here so no feature code ever touches it. No Objective-C or WebGPU types
 *  appear in this header, so plain C++ translation units can include it.
 *
 *  Two backends sit behind it (#375): Metal, the default, and Dawn (WebGPU in
 *  C++, #317), which is what Windows and Linux will run. Each implements the
 *  abstract `Impl`s in `gpu/Backend.h`; nothing above this directory knows which
 *  one it got.
 */

#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>

namespace orion::gpu {

/// The device had no memory for an allocation. Its own type so a caller can
/// give memory back and try once more, which no other failure deserves.
struct OutOfMemory : std::runtime_error {
    using std::runtime_error::runtime_error;
};

enum class Backend { Metal, Dawn };

struct DeviceInfo {
    std::string   name;
    std::uint64_t recommendedWorkingSet = 0;
    std::uint64_t maxBufferLength       = 0;
    bool          unifiedMemory         = false;
    bool          supportsApple7        = false;
    /// A profiled command buffer is timed on the GPU's clock: always on Metal,
    /// on Dawn where the adapter has `timestamp-query`.
    bool          gpuTimestamps         = false;
};

class Device {
public:
    /// The backend `ORION_GPU` names (`metal` or `dawn`); when unset, Metal on
    /// macOS and Dawn everywhere else (#376).
    /// The environment variable is how the whole test suite, the bench and
    /// the app are run on Dawn without a rebuild.
    /// Throws std::runtime_error when no usable GPU is present.
    static std::unique_ptr<Device> create();

    /// A specific backend. Dawn throws when the engine was built without it.
    static std::unique_ptr<Device> create(Backend);

    ~Device();

    Device(const Device&)            = delete;
    Device& operator=(const Device&) = delete;

    [[nodiscard]] const DeviceInfo& info() const noexcept { return info_; }
    [[nodiscard]] Backend backend() const noexcept { return backend_; }

    /// Escape hatch for the shells' zero-copy canvas: the `id<MTLDevice>` the
    /// engine renders on, under either backend (Dawn runs on Metal on macOS).
    /// Non-owning.
    [[nodiscard]] void* rawDevice() const noexcept;

    /// Hands the memory of textures already dropped back to the driver now.
    /// Metal does that by itself; Dawn frees a dropped texture only after a
    /// later submit has finished, so without this a release lasts until the
    /// next render but one (#392). Waits for queued GPU work.
    void reclaim() noexcept;

    /// GPU memory this device holds from the driver, in bytes; 0 where the
    /// backend cannot say (Metal, whose textures the process footprint counts).
    /// Shared by every engine in the process, as the device is.
    [[nodiscard]] std::uint64_t allocatedBytes() const noexcept;

    /// The most this device has held since `resetPeak`, sampled at every
    /// texture creation: the allocator's bytes where the backend reports them
    /// (Dawn, `allocatedBytes`), else the bytes of the textures alive (Metal).
    /// What the memory budget gate reads (#406). Starts at zero.
    [[nodiscard]] std::uint64_t peakBytes() const noexcept;
    void resetPeak() noexcept;
    /// What the device can still give, in bytes: the backend's own figure
    /// where it has one (Metal's recommended working set less what it holds),
    /// else, once an allocation has been refused, that refusal's ceiling less
    /// what is held now; `kUnknown` before either. The memory rule's budget
    /// reads it (#406).
    static constexpr std::uint64_t kUnknown = ~std::uint64_t(0);
    [[nodiscard]] std::uint64_t freeBytes() const noexcept;
    /// An allocation was refused with this much held: the ceiling `freeBytes`
    /// falls back to where the backend cannot say (Dawn).
    void noteOutOfMemory() noexcept;
    /// Forgets that ceiling: memory others held may have come back. The engine
    /// calls it when a photo opens.
    void forgetOutOfMemory() noexcept { ceiling_.store(0); }

    /// Bytes of the textures this device has created and not yet destroyed.
    [[nodiscard]] std::uint64_t textureBytes() const noexcept;

    /// Texture::create's bookkeeping, backend-neutral: one more texture of
    /// `bytes` exists. Returns the counter its destructor subtracts from.
    [[nodiscard]] std::shared_ptr<std::atomic<std::uint64_t>> noteTexture(std::uint64_t bytes) noexcept;

    /// Backend-private: how the backend's own files reach their half.
    struct Impl;
    [[nodiscard]] Impl& impl() const noexcept { return *impl_; }

private:
    explicit Device(std::unique_ptr<Impl>);

    std::unique_ptr<Impl> impl_;
    DeviceInfo            info_;
    Backend               backend_ = Backend::Metal;
    /// Shared with every texture, which may outlive the device (Dawn).
    std::shared_ptr<std::atomic<std::uint64_t>> textureBytes_ =
        std::make_shared<std::atomic<std::uint64_t>>(0);
    std::atomic<std::uint64_t> peak_{0};
    std::atomic<std::uint64_t> ceiling_{0};   // 0: no allocation refused yet
};

}  // namespace orion::gpu
