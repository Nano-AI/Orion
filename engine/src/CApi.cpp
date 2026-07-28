/*  The exception firewall.
 *
 *  Swift cannot catch a C++ exception; one that reaches it terminates the
 *  process. So every entry point here funnels through guard(), which converts
 *  any throw into an OrionStatus plus a message retrievable via
 *  orion_last_error(). Nothing throws past this file — that invariant is the
 *  whole point of the file existing.
 */

#include "orion/orion.h"

#include "Engine.h"

#include <cstring>
#include <exception>
#include <new>
#include <string>
#include <utility>

struct OrionEngine {
    orion::Engine impl;
};

namespace {

/// Runs fn, converting any escaping exception into a status code. When an
/// engine is available the message is stashed for orion_last_error().
template <class Fn>
OrionStatus guard(OrionEngine* engine, Fn&& fn) noexcept {
    try {
        return fn();
    } catch (const std::bad_alloc&) {
        if (engine) engine->impl.setError("out of memory");
        return ORION_ERR_INTERNAL;
    } catch (const std::exception& e) {
        if (engine) engine->impl.setError(e.what());
        return ORION_ERR_INTERNAL;
    } catch (...) {
        if (engine) engine->impl.setError("unknown error");
        return ORION_ERR_INTERNAL;
    }
}

/// Copies into a fixed C buffer, always NUL-terminated, never overrunning.
void copyInto(char* dst, std::size_t cap, const std::string& src) noexcept {
    if (cap == 0) return;
    const std::size_t n = src.size() < cap - 1 ? src.size() : cap - 1;
    std::memcpy(dst, src.data(), n);
    dst[n] = '\0';
}

}  // namespace

OrionStatus orion_engine_create(OrionEngine** out) {
    if (out == nullptr) return ORION_ERR_BAD_ARG;
    *out = nullptr;

    // No engine exists yet, so a failure message has nowhere to live; the
    // status is all the caller gets for this one call.
    return guard(nullptr, [&]() -> OrionStatus {
        auto engine = new OrionEngine{};
        *out = engine;
        return ORION_OK;
    });
}

void orion_engine_destroy(OrionEngine* engine) {
    delete engine;
}

OrionStatus orion_engine_device_info(const OrionEngine* engine, OrionDeviceInfo* out) {
    if (engine == nullptr || out == nullptr) return ORION_ERR_BAD_ARG;

    return guard(const_cast<OrionEngine*>(engine), [&]() -> OrionStatus {
        const auto& info = engine->impl.deviceInfo();
        *out = OrionDeviceInfo{};
        copyInto(out->name, sizeof out->name, info.name);
        out->recommended_working_set = info.recommendedWorkingSet;
        out->max_buffer_length       = info.maxBufferLength;
        out->has_unified_memory      = info.unifiedMemory ? 1 : 0;
        out->supports_apple7         = info.supportsApple7 ? 1 : 0;
        return ORION_OK;
    });
}

OrionStatus orion_engine_open_raw(OrionEngine* engine, const char* path) {
    if (engine == nullptr || path == nullptr) return ORION_ERR_BAD_ARG;
    return guard(engine, [&]() -> OrionStatus {
        engine->impl.openRaw(path);
        return ORION_OK;
    });
}

OrionStatus orion_engine_set_adjustments(OrionEngine* engine, const OrionAdjustments* adj) {
    if (engine == nullptr || adj == nullptr) return ORION_ERR_BAD_ARG;
    return guard(engine, [&]() -> OrionStatus {
        engine->impl.setAdjustments(orion::pipe::Adjustments{
            adj->exposure_ev, adj->black, adj->contrast, adj->saturation});
        return ORION_OK;
    });
}

OrionStatus orion_engine_render(OrionEngine* engine, double* out_ms) {
    if (engine == nullptr) return ORION_ERR_BAD_ARG;
    return guard(engine, [&]() -> OrionStatus {
        const double ms = engine->impl.render();
        if (out_ms) *out_ms = ms;
        return ORION_OK;
    });
}

OrionStatus orion_engine_image_size(const OrionEngine* engine,
                                    uint32_t* out_width, uint32_t* out_height) {
    if (engine == nullptr || out_width == nullptr || out_height == nullptr) {
        return ORION_ERR_BAD_ARG;
    }
    return guard(const_cast<OrionEngine*>(engine), [&]() -> OrionStatus {
        const auto& d = engine->impl.develop();
        *out_width  = d.width();
        *out_height = d.height();
        return ORION_OK;
    });
}

void* orion_engine_output_texture(const OrionEngine* engine) {
    if (engine == nullptr || !engine->impl.hasImage()) return nullptr;
    try {
        return engine->impl.develop().output().raw();
    } catch (...) {
        return nullptr;
    }
}

void* orion_engine_metal_device(const OrionEngine* engine) {
    if (engine == nullptr) return nullptr;
    try {
        return const_cast<OrionEngine*>(engine)->impl.device().rawDevice();
    } catch (...) {
        return nullptr;
    }
}

const char* orion_engine_camera(const OrionEngine* engine) {
    return engine ? engine->impl.camera().c_str() : "";
}

const char* orion_status_string(OrionStatus status) {
    switch (status) {
        case ORION_OK:           return "ok";
        case ORION_ERR_NO_GPU:   return "no Metal device available";
        case ORION_ERR_BAD_ARG:  return "invalid argument";
        case ORION_ERR_INTERNAL: return "internal error";
    }
    return "unrecognised status";
}

const char* orion_last_error(const OrionEngine* engine) {
    return engine ? engine->impl.lastError() : "";
}

const char* orion_version(void) {
    return ORION_VERSION_STRING;
}
