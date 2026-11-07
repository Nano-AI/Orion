// What the GPU can still give, from the driver, where Dawn cannot say (#406).
//
// Dawn exposes the bytes it allocated and nothing about the card, so the
// memory rule's budget had to guess 2.5 GB per engine and could not see a
// second engine (side by side, #400), a desktop or another program. On Linux
// the driver can say: NVIDIA's NVML (libnvidia-ml, loaded at run time, never
// linked: a machine without it simply answers unknown) and amdgpu's sysfs
// counters. Both are device-wide, so what this process and every other holds
// is already taken off. Anything else (Intel, Windows for now) answers
// unknown and the rule falls back to the cap and a refusal's ceiling.

#include "gpu/Backend.h"
#include "gpu/Device.h"

#ifdef __linux__
#include <dlfcn.h>

#include <filesystem>
#include <fstream>
#include <string>
#endif

namespace orion::gpu {

#ifdef __linux__
namespace {

struct Nvml {
    // NVML's own layouts (nvml.h, stable since the v1 API).
    struct Memory { unsigned long long total, free, used; };
    struct Pci {
        char busIdLegacy[16];
        unsigned int domain, bus, device, pciDeviceId, pciSubSystemId;
        char busId[32];
        char spare[64];   // room should a driver's struct ever grow: never read
    };
    using Init = int (*)();
    using Count = int (*)(unsigned int*);
    using Handle = int (*)(unsigned int, void**);
    using PciInfo = int (*)(void*, Pci*);
    using MemoryInfo = int (*)(void*, Memory*);

    Count count = nullptr;
    Handle handle = nullptr;
    PciInfo pci = nullptr;
    MemoryInfo memory = nullptr;
    bool ok = false;

    Nvml() {
        void* lib = dlopen("libnvidia-ml.so.1", RTLD_NOW | RTLD_LOCAL);
        if (!lib) return;
        auto init = reinterpret_cast<Init>(dlsym(lib, "nvmlInit_v2"));
        count = reinterpret_cast<Count>(dlsym(lib, "nvmlDeviceGetCount_v2"));
        handle = reinterpret_cast<Handle>(dlsym(lib, "nvmlDeviceGetHandleByIndex_v2"));
        pci = reinterpret_cast<PciInfo>(dlsym(lib, "nvmlDeviceGetPciInfo_v3"));
        memory = reinterpret_cast<MemoryInfo>(dlsym(lib, "nvmlDeviceGetMemoryInfo"));
        ok = init && count && handle && pci && memory && init() == 0;
    }

    /// Free bytes on the NVIDIA device with this PCI device id, else unknown.
    std::uint64_t free(std::uint32_t deviceId, std::uint64_t& total) const {
        unsigned int n = 0;
        if (!ok || count(&n) != 0) return Device::kUnknown;
        for (unsigned int i = 0; i < n; ++i) {
            void* h = nullptr;
            Pci p{};
            Memory m{};
            if (handle(i, &h) != 0 || pci(h, &p) != 0) continue;
            if ((p.pciDeviceId >> 16) != deviceId && n > 1) continue;   // one card: no need to match
            if (memory(h, &m) == 0) { total = m.total; return m.free; }
        }
        return Device::kUnknown;
    }
};

std::uint64_t readNumber(const std::filesystem::path& file) {
    std::ifstream in(file);
    std::string text;
    if (!(in >> text)) return 0;
    try { return std::stoull(text, nullptr, 0); } catch (...) { return 0; }
}

/// amdgpu's counters for the card with this PCI device id, else unknown.
std::uint64_t amdFree(std::uint32_t deviceId, std::uint64_t& total) {
    std::error_code ec;
    for (const auto& card : std::filesystem::directory_iterator("/sys/class/drm", ec)) {
        const auto dev = card.path() / "device";
        if (readNumber(dev / "vendor") != 0x1002 || readNumber(dev / "device") != deviceId) continue;
        total = readNumber(dev / "mem_info_vram_total");
        const std::uint64_t used = readNumber(dev / "mem_info_vram_used");
        if (total > 0) return total > used ? total - used : 0;
    }
    return Device::kUnknown;
}

}  // namespace
#endif

std::uint64_t driverFreeBytes(std::uint32_t vendorId, std::uint32_t deviceId,
                              std::uint64_t* total) noexcept {
    std::uint64_t card = 0;
    if (total) *total = 0;
#ifdef __linux__
    try {
        std::uint64_t free = Device::kUnknown;
        if (vendorId == 0x10DE) {
            static const Nvml nvml;
            free = nvml.free(deviceId, card);
        } else if (vendorId == 0x1002) {
            free = amdFree(deviceId, card);
        }
        if (total && free != Device::kUnknown) *total = card;
        return free;
    } catch (...) {}
#else
    (void)vendorId;
    (void)deviceId;
    (void)card;
#endif
    return Device::kUnknown;
}

}  // namespace orion::gpu
