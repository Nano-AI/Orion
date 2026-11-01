/*  Available memory — what this machine could hand out now without swapping,
 *  for the stack's memory check (#391). One call, one #if per OS:
 *    macOS    kern.memorystatus_level (% neither wired nor compressed) x hw.memsize;
 *             host_statistics64 free + inactive + purgeable pages if that will not read
 *    Windows  GlobalMemoryStatusEx: ullAvailPhys
 *    Linux    /proc/meminfo: MemAvailable
 */

#pragma once

#include <cstdint>

namespace orion::util {

/// Bytes available now; 0 when the OS will not say (a caller then treats it as unknown, not as none).
[[nodiscard]] std::uint64_t availableMemory() noexcept;

/// Test seam: non-zero makes availableMemory() answer this instead of the OS; 0 restores the OS.
void setAvailableMemoryOverride(std::uint64_t bytes) noexcept;

}  // namespace orion::util
