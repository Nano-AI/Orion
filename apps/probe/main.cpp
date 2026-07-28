/*  orion-probe — smallest possible consumer of the engine's C API.
 *
 *  Exists to prove two things at every build: the engine binds a Metal device,
 *  and the C facade is usable from a translation unit that knows nothing about
 *  the C++ inside. When the Swift shell arrives it will call exactly these
 *  functions, so if the probe works, the boundary works.
 */

#include "orion/orion.h"

#include <cstdio>

namespace {

double toGiB(uint64_t bytes) {
    return static_cast<double>(bytes) / (1024.0 * 1024.0 * 1024.0);
}

}  // namespace

int main() {
    std::printf("Orion engine %s\n", orion_version());

    OrionEngine* engine = nullptr;
    if (OrionStatus status = orion_engine_create(&engine); status != ORION_OK) {
        std::fprintf(stderr, "engine create failed: %s\n", orion_status_string(status));
        return 1;
    }

    OrionDeviceInfo info{};
    if (OrionStatus status = orion_engine_device_info(engine, &info); status != ORION_OK) {
        std::fprintf(stderr, "device query failed: %s (%s)\n",
                     orion_status_string(status), orion_last_error(engine));
        orion_engine_destroy(engine);
        return 1;
    }

    std::printf("  GPU              %s\n", info.name);
    std::printf("  Working set      %.1f GiB\n", toGiB(info.recommended_working_set));
    std::printf("  Max buffer       %.1f GiB\n", toGiB(info.max_buffer_length));
    std::printf("  Unified memory   %s\n", info.has_unified_memory ? "yes" : "no");
    std::printf("  Apple7 family    %s\n", info.supports_apple7 ? "yes" : "no");

    orion_engine_destroy(engine);
    std::printf("ok\n");
    return 0;
}
