// What the facade's two files share (CApi.cpp, CApiExport.cpp): the engine
// behind the opaque handle, and guard(), the exception firewall every entry
// point runs through. Private to the engine; the UI sees only orion.h.
#pragma once

#include "orion/orion.h"
#include "Engine.h"

#include <exception>
#include <new>

struct OrionEngine {
    orion::Engine impl;
};

namespace orion::capi {

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

}  // namespace orion::capi
