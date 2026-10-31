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
    /// next render but one (#DEC-memory). Waits for queued GPU work.
    void reclaim() noexcept;

    /// GPU memory this device holds from the driver, in bytes; 0 where the
    /// backend cannot say (Metal, whose textures the process footprint counts).
    /// Shared by every engine in the process, as the device is.
    [[nodiscard]] std::uint64_t allocatedBytes() const noexcept;

    /// Backend-private: how the backend's own files reach their half.
    struct Impl;
    [[nodiscard]] Impl& impl() const noexcept { return *impl_; }

private:
    explicit Device(std::unique_ptr<Impl>);

    std::unique_ptr<Impl> impl_;
    DeviceInfo            info_;
    Backend               backend_ = Backend::Metal;
};

}  // namespace orion::gpu
