/*  The facade's readbacks: orion_engine_read_pixels, the CPU copy of a whole
 *  render, and the display copy (#386), the canvas's asynchronous one at the
 *  size it shows. Split from CApi.cpp for its length; the same firewall: every
 *  entry point runs in guard() (CApiGuard.h), and nothing throws past this file.
 */

#include "CApiGuard.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>

using orion::capi::guard;

namespace {
/// The graph orion.h's `graph` names (0 full, 1 preview, 2 region), or null
/// when there is none or it has not rendered.
const orion::pipe::DevelopPipeline* graphOf(OrionEngine* e, int graph) {
    if (graph == 1) return e->impl.hasPreview() ? &e->impl.previewDevelop() : nullptr;
    if (graph == 2) return e->impl.hasRegion() ? &e->impl.regionDevelop() : nullptr;
    return graph == 0 && e->impl.hasImage() ? &e->impl.develop() : nullptr;
}

/// The texture is square so a straightened frame fits; the picture is its top left.
void pictureSize(const orion::pipe::DevelopPipeline& d, std::uint32_t& w, std::uint32_t& h) {
    const orion::gpu::Texture& t = d.output();
    w = std::min(t.width(), d.outputWidth());
    h = std::min(t.height(), d.outputHeight());
}
}  // namespace

OrionStatus orion_engine_read_pixels(const OrionEngine* engine, int graph,
                                     uint8_t* dst, uint32_t capacity,
                                     uint32_t* out_width, uint32_t* out_height) {
    if (engine == nullptr || out_width == nullptr || out_height == nullptr) return ORION_ERR_BAD_ARG;
    if (graph < 0 || graph > 2) return ORION_ERR_BAD_ARG;
    auto* e = const_cast<OrionEngine*>(engine);
    if (!graphOf(e, graph)) return ORION_ERR_BAD_ARG;
    return guard(e, [&]() -> OrionStatus {
        const auto& d = *graphOf(e, graph);
        std::uint32_t w = 0, h = 0;
        pictureSize(d, w, h);
        *out_width = w;
        *out_height = h;
        if (dst == nullptr) return ORION_OK;
        if (w == 0 || h == 0 || std::uint64_t(capacity) < std::uint64_t(w) * h * 4) return ORION_ERR_BAD_ARG;
        const auto f = d.output().format();
        if (f != orion::gpu::PixelFormat::RGBA8Unorm && f != orion::gpu::PixelFormat::RGBA16Float)
            return ORION_ERR_BAD_ARG;
        e->impl.displayCopy().read(d.output(), w, h, dst);   // a wide output converted on the GPU
        return ORION_OK;
    });
}

OrionStatus orion_engine_display_begin(OrionEngine* engine, int graph, int level,
                                       uint32_t* out_width, uint32_t* out_height,
                                       uint64_t* out_ticket) {
    if (engine == nullptr || out_width == nullptr || out_height == nullptr || out_ticket == nullptr)
        return ORION_ERR_BAD_ARG;
    if (graph < 0 || graph > 2 || level < 0 || !graphOf(engine, graph)) return ORION_ERR_BAD_ARG;
    return guard(engine, [&]() -> OrionStatus {
        const auto& d = *graphOf(engine, graph);
        std::uint32_t w = 0, h = 0;
        pictureSize(d, w, h);
        if (w == 0 || h == 0) return ORION_ERR_BAD_ARG;
        *out_ticket = engine->impl.displayCopy().begin(d.output(), w, h, level, *out_width, *out_height);
        return ORION_OK;
    });
}

OrionStatus orion_engine_display_take(OrionEngine* engine, int wait, uint8_t* dst,
                                      uint32_t row_bytes, uint32_t capacity,
                                      uint32_t* out_width, uint32_t* out_height,
                                      uint64_t* out_ticket) {
    if (engine == nullptr || out_width == nullptr || out_height == nullptr || out_ticket == nullptr)
        return ORION_ERR_BAD_ARG;
    return guard(engine, [&]() -> OrionStatus {
        auto& rb = engine->impl.displayCopy().readback();
        std::uint32_t w = 0, h = 0;
        // A copy into dst takes what the size query named, never a newer one
        // of another size; with nothing named it looks first.
        std::uint64_t ticket = dst != nullptr ? rb.held(w, h) : 0;
        if (ticket == 0) ticket = rb.newest(wait != 0, w, h);
        *out_width = w;
        *out_height = h;
        *out_ticket = ticket;
        if (ticket == 0 || dst == nullptr) return ORION_OK;
        if (std::uint64_t(row_bytes) < std::uint64_t(w) * 4 ||
            std::uint64_t(capacity) < std::uint64_t(row_bytes) * (h - 1) + std::uint64_t(w) * 4)
            return ORION_ERR_BAD_ARG;   // kept: a call with room takes it
        rb.take(dst, row_bytes);
        return ORION_OK;
    });
}
