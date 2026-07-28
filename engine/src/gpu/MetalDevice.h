/*  Thin GPU abstraction — the Device half.
 *
 *  Written once, then left alone: all the fiddly Metal API surface lives behind
 *  here so no feature code ever touches it. No Objective-C types appear in this
 *  header, so plain C++ translation units can include it. Texture, Kernel and
 *  CommandQueue join it as the DAG lands (S0.5).
 */

#pragma once

#include <cstdint>
#include <memory>
#include <string>

namespace orion::gpu {

struct DeviceInfo {
    std::string   name;
    std::uint64_t recommendedWorkingSet = 0;
    std::uint64_t maxBufferLength       = 0;
    bool          unifiedMemory         = false;
    bool          supportsApple7        = false;
};

class Device {
public:
    /// Binds the system default Metal device and creates its command queue.
    /// Throws std::runtime_error when no usable GPU is present.
    static std::unique_ptr<Device> create();

    ~Device();

    Device(const Device&)            = delete;
    Device& operator=(const Device&) = delete;

    [[nodiscard]] const DeviceInfo& info() const noexcept { return info_; }

    /// Escape hatches for translation units that are already Objective-C++.
    /// Cast to id<MTLDevice> / id<MTLCommandQueue>. Non-owning.
    [[nodiscard]] void* rawDevice() const noexcept;
    [[nodiscard]] void* rawQueue() const noexcept;

private:
    Device();

    struct Impl;
    std::unique_ptr<Impl> impl_;
    DeviceInfo            info_;
};

}  // namespace orion::gpu
