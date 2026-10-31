#include "util/Memory.h"

#if defined(__APPLE__)
#include <mach/mach.h>
#elif defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <cstdio>
#endif

namespace orion::util {

std::uint64_t availableMemory() noexcept {
#if defined(__APPLE__)
    // Inactive and purgeable pages are given up on demand, so they count as free, as Activity Monitor's
    // "memory available" does.
    vm_statistics64_data_t vm{};
    mach_msg_type_number_t count = HOST_VM_INFO64_COUNT;
    const mach_port_t host = mach_host_self();
    const kern_return_t kr = host_statistics64(host, HOST_VM_INFO64, reinterpret_cast<host_info64_t>(&vm), &count);
    mach_port_deallocate(mach_task_self(), host);
    if (kr != KERN_SUCCESS) return 0;
    return (std::uint64_t(vm.free_count) + vm.inactive_count + vm.purgeable_count) * std::uint64_t(vm_page_size);
#elif defined(_WIN32)
    MEMORYSTATUSEX status{};
    status.dwLength = sizeof status;
    return GlobalMemoryStatusEx(&status) ? std::uint64_t(status.ullAvailPhys) : 0;
#else
    std::FILE* f = std::fopen("/proc/meminfo", "r");
    if (!f) return 0;
    char line[256];
    unsigned long long kb = 0;
    bool found = false;
    while (!found && std::fgets(line, sizeof line, f)) found = std::sscanf(line, "MemAvailable: %llu kB", &kb) == 1;
    std::fclose(f);
    return found ? std::uint64_t(kb) * 1024u : 0;
#endif
}

}  // namespace orion::util
