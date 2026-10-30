/*  The exception firewall.
 *
 *  C# (P/Invoke) cannot catch a C++ exception; one that reaches it terminates
 *  the process. So every entry point here, and in CApiExport.cpp (export and
 *  its snapshot), funnels through guard() (CApiGuard.h), which converts
 *  any throw into an OrionStatus plus a message retrievable via
 *  orion_last_error(). Nothing throws past these files — that invariant is the
 *  whole point of the file existing.
 */

#include "orion/orion.h"

#include "pipe/CubeLut.h"
#include "util/Utf8Path.h"

#include <fstream>
#include <iterator>
#include <cstring>

#include "CApiGuard.h"
#include "Engine.h"

#include <algorithm>
#include <cstring>
#include <exception>
#include <memory>
#include <new>
#include <string>
#include <utility>
#include <vector>

using orion::capi::guard;

namespace {

/// Why the last orion_engine_create on this thread failed; empty after a success.
std::string& createError() {
    thread_local std::string why;
    return why;
}

/// Copies into a fixed C buffer, always NUL-terminated, never overrunning.
void copyInto(char* dst, std::size_t cap, const std::string& src) noexcept {
    if (cap == 0) return;
    const std::size_t n = src.size() < cap - 1 ? src.size() : cap - 1;
    std::memcpy(dst, src.data(), n);
    dst[n] = '\0';
}

/// A curve channel out of its C form.
///
/// Anything malformed becomes the identity rather than an error: the curve is
/// pushed on every slider tick, and failing a whole render because a caller
/// sent a stray point would be a poor trade. Points are clamped to 0..1 and
/// required to ascend, because the interpolator assumes both.
orion::pipe::CurveChannel toChannel(const OrionCurveChannel& c) {
    const orion::pipe::CurveChannel identity{{0.0f, 0.0f}, {1.0f, 1.0f}};

    const int n = c.count;
    if (n < 2 || n > ORION_CURVE_MAX_POINTS) return identity;

    orion::pipe::CurveChannel out;
    out.reserve(static_cast<std::size_t>(n));
    float previousX = -1.0f;
    for (int i = 0; i < n; ++i) {
        const float x = std::clamp(c.x[i], 0.0f, 1.0f);
        const float y = std::clamp(c.y[i], 0.0f, 1.0f);
        if (x <= previousX) return identity;
        previousX = x;
        out.push_back({x, y});
    }
    return out;
}

}  // namespace

OrionStatus orion_engine_create(OrionEngine** out) {
    if (out == nullptr) return ORION_ERR_BAD_ARG;
    *out = nullptr;

    // No engine exists yet, so the reason is kept here, for orion_last_error(NULL):
    // "no GPU the engine can use" and "out of memory" need different answers (#376).
    try {
        *out = new OrionEngine{};
        createError().clear();
        return ORION_OK;
    } catch (const std::exception& e) {
        // The copy can throw bad_alloc when create failed from OOM; it must not escape.
        try { createError() = e.what(); } catch (...) {}
    } catch (...) {
        try { createError() = "unknown error"; } catch (...) {}
    }
    return ORION_ERR_INTERNAL;
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

namespace {
/// The C adjustment block, as the engine's own type.
///
/// Factored out because auto-enhance needs the caller's *whole* state to
/// measure against, not just the handful of fields it is going to write:
/// solving exposure against a frame rendered with everything else at its
/// default would be solving for a different photograph.
///
/// Takes the engine because the lens profile is not in the C block — it comes
/// from the database, keyed by what the EXIF named.
orion::pipe::Adjustments toAdjustments(OrionEngine* engine, const OrionAdjustments* adj) {
    orion::pipe::Adjustments a;
    a.wb.temperatureK = adj->temperature_k;
    a.wb.tint         = adj->tint;
    a.exposureEv      = adj->exposure_ev;
    a.highlights      = adj->highlights;
    a.shadows         = adj->shadows;
    a.whites          = adj->whites;
    a.blacks          = adj->blacks;
    a.vibrance        = adj->vibrance;
    a.saturation      = adj->saturation;
    a.contrast        = adj->contrast;
    a.rotateQuarters  = adj->rotate_quarters;
    a.straightenDeg   = adj->straighten_deg;
    a.perspectiveVertical   = adj->perspective_vertical;
    a.perspectiveHorizontal = adj->perspective_horizontal;
    a.perspectiveAspect     = adj->perspective_aspect;
    a.cropX = adj->crop_x; a.cropY = adj->crop_y;
    a.cropW = adj->crop_w; a.cropH = adj->crop_h;
    a.cropPreview = adj->crop_preview != 0;
    a.previewX = adj->preview_x;
    a.previewY = adj->preview_y;
    a.previewSize = adj->preview_size;

    a.lensDistortion = adj->lens_distortion;
    a.lensVignette   = adj->lens_vignette;
    a.lensProfile    = adj->lens_profile != 0;
    if (a.lensProfile) a.lensModel = engine->impl.lensProfile().model;
    a.lensCaRed      = adj->lens_ca_red;
    a.lensCaBlue     = adj->lens_ca_blue;
    a.highlightRecovery = adj->highlight_recovery;
    for (int i = 0; i < 3; ++i) {
        a.gradeShadow[i]    = adj->grade_shadow[i];
        a.gradeMidtone[i]   = adj->grade_midtone[i];
        a.gradeHighlight[i] = adj->grade_highlight[i];
    }
    // Zero is the neutral here, so a zeroed options struct is the old
    // behaviour — no `> 0` reading like the field angle below needs.
    a.gradeBalance   = adj->grade_balance;
    a.vignetteAmount     = adj->vignette_amount;
    // ⚠ A zeroed options struct must not mean a zero-degree lens: the field
    // angle divides nothing, but tan(0) is 0 and the falloff would be flat, so
    // a caller that set only the amount would get a vignette that does nothing
    // anywhere. Zero is read as the default, the same shape as
    // `ORION_DEPTH_DEFAULT` (#93).
    a.vignetteFieldAngle = adj->vignette_field_angle > 0.0f
                         ? adj->vignette_field_angle : 45.0f;
    a.denoiseLuma   = adj->denoise_luma;
    a.denoiseColor = adj->denoise_color;
    a.denoiseDetail = std::clamp(adj->denoise_detail, 0.0f, 1.0f);
    a.denoiseNlm    = std::clamp(adj->denoise_nlm, 0.0f, 1.0f);
    a.texture       = std::clamp(adj->texture, -1.0f, 1.0f);
    a.denoiseSmoothness = std::clamp(adj->denoise_smoothness, 0.0f, 1.0f);

    a.curve.master = toChannel(adj->curve_master);
    a.curve.red    = toChannel(adj->curve_red);
    a.curve.green  = toChannel(adj->curve_green);
    a.curve.blue   = toChannel(adj->curve_blue);
    static_assert(ORION_MAX_MASK_COMPONENTS == orion::pipe::kMaxMaskComponents,
                  "the facade and the engine disagree about the group's size");
    // Out-of-range counts are clamped rather than rejected: a caller one field
    // behind on the format should lose the excess components, not the frame.
    a.maskCount = std::clamp(adj->mask_count, 0, ORION_MAX_MASK_COMPONENTS);
    for (int i = 0; i < a.maskCount; ++i) {
        const OrionMaskComponent& s = adj->mask_components[i];
        orion::pipe::MaskComponentEdit& d = a.maskComponents[std::size_t(i)];
        d.kind          = s.kind;
        d.compose       = s.compose;
        d.invert        = s.invert != 0;
        d.hidden        = s.hidden != 0;
        d.startsLayer   = s.starts_layer != 0;
        d.center[0]     = s.center_x;
        d.center[1]     = s.center_y;
        d.angle         = s.angle;
        d.length        = s.length;
        d.radius[0]     = s.radius_x;
        d.radius[1]     = s.radius_y;
        d.feather       = s.feather;
        d.roundness     = s.roundness;
        d.rangeLo       = s.range_lo;
        d.rangeHi       = s.range_hi;
        d.rangeSoft     = s.range_soft;
        d.color[0]     = s.color_r;
        d.color[1]     = s.color_g;
        d.color[2]     = s.color_b;
        // Clamped rather than trusted. A negative tolerance would make the
        // falloff run backwards and select everything *except* the picked
        // color, which is a plausible-looking mask and not an obvious break.
        d.colorTol     = std::clamp(s.color_tol, 0.0f, 4.0f);
        d.colorSoft    = std::clamp(s.color_soft, 1e-4f, 4.0f);
        d.brushRadius   = s.brush_radius;
        d.brushFlow     = s.brush_flow;
        d.brushHardness = s.brush_hardness;
        d.brushRevision = s.brush_revision;
    }
    for (int i = 0; i < ORION_MAX_MASK_COMPONENTS; ++i) {
        auto& e = a.layers[std::size_t(i)];
        e.exposureEv = adj->local_exposure_ev[i];
        e.contrast   = std::clamp(adj->local_contrast[i], -1.0f, 1.0f);
        e.saturation = std::clamp(adj->local_saturation[i], -1.0f, 1.0f);
        e.warmth     = std::clamp(adj->local_warmth[i], -1.0f, 1.0f);
        e.tint       = std::clamp(adj->local_tint[i], -1.0f, 1.0f);
        e.highlights = std::clamp(adj->local_highlights[i], -1.0f, 1.0f);
        e.shadows    = std::clamp(adj->local_shadows[i], -1.0f, 1.0f);
        e.whites     = std::clamp(adj->local_whites[i], -1.0f, 1.0f);
        e.blacks     = std::clamp(adj->local_blacks[i], -1.0f, 1.0f);
        e.denoiseLuma  = std::clamp(adj->local_denoise_luma[i], 0.0f, 4.0f);
        e.denoiseColor = std::clamp(adj->local_denoise_color[i], 0.0f, 4.0f);
    }
    a.maskRefine = std::clamp(adj->mask_refine, 0.0f, 1.0f);
    // Zero is the bands as shipped, and anything newer than this build knows
    // renders as the newest it does know - never as the legacy bands.
    a.process = adj->process <= 1 ? 1 : 2;

    a.spotCount = std::clamp(adj->spot_count, 0, ORION_MAX_SPOTS);
    for (int i = 0; i < a.spotCount; ++i) {
        const OrionSpot& in = adj->spots[i];
        orion::pipe::SpotEdit& out = a.spots[std::size_t(i)];
        out.destX = in.dest_x; out.destY = in.dest_y;
        out.srcX  = in.src_x;  out.srcY  = in.src_y;
        out.radius  = std::clamp(in.radius, 0.001f, 0.5f);
        out.feather = std::clamp(in.feather, 0.0f, 1.0f);
        out.heal = in.heal != 0;
    }
    a.maskOverlay     = adj->mask_overlay != 0;
    a.maskOverlayLayer = std::clamp(adj->mask_overlay_layer, 0,
                                    ORION_MAX_MASK_COMPONENTS - 1);
    a.fusion          = adj->fusion;
    a.dehaze          = adj->dehaze;
    a.lutStrength     = adj->lut_strength;
    a.grainAmount     = adj->grain_amount;
    a.grainSize       = adj->grain_size;
    a.clarity         = adj->clarity;
    a.sharpenAmount   = adj->sharpen_amount;
    a.sharpenRadius   = adj->sharpen_radius;
    a.sharpenMasking  = adj->sharpen_masking;
    for (int i = 0; i < 8; ++i) {
        a.hueShift[i] = adj->hue_shift[i];
        a.satShift[i] = adj->sat_shift[i];
        a.lumShift[i] = adj->lum_shift[i];
    }
    return a;
}
}  // namespace

OrionStatus orion_engine_set_adjustments(OrionEngine* engine, const OrionAdjustments* adj) {
    if (engine == nullptr || adj == nullptr) return ORION_ERR_BAD_ARG;
    return guard(engine, [&]() -> OrionStatus {
        orion::pipe::Adjustments a = toAdjustments(engine, adj);
        engine->impl.setAdjustments(a);
        return ORION_OK;
    });
}

OrionStatus orion_engine_set_brush_stroke(OrionEngine* engine, int component,
                                          const float* xy, const float* erase,
                                          int count) {
    if (engine == nullptr) return ORION_ERR_BAD_ARG;
    // Rejected rather than clamped: paint landing in the wrong component is
    // worse than nothing happening.
    if (component < 0 || component >= ORION_MAX_MASK_COMPONENTS)
        return ORION_ERR_BAD_ARG;
    // A null buffer with a positive count is a caller bug, not an empty stroke.
    if (xy == nullptr && count > 0) return ORION_ERR_BAD_ARG;
    return guard(engine, [&]() -> OrionStatus {
        engine->impl.setBrushStroke(component, xy, erase, count);
        return ORION_OK;
    });
}

OrionStatus orion_engine_set_mask_matte(OrionEngine* engine, int component,
                                        const float* alpha, int width, int height) {
    if (engine == nullptr) return ORION_ERR_BAD_ARG;
    // Rejected rather than clamped, like a brush stroke: a matte in the wrong
    // component covers something nobody selected.
    if (component < 0 || component >= ORION_MAX_MASK_COMPONENTS)
        return ORION_ERR_BAD_ARG;
    if (alpha == nullptr && (width > 0 || height > 0)) return ORION_ERR_BAD_ARG;
    return guard(engine, [&]() -> OrionStatus {
        const bool ok = engine->impl.setMaskMatte(component, alpha, width, height);
        // Too large for the aux texture. Reported rather than resampled — see
        // the header. A caller that gets this back should downscale on its own
        // terms, where it can choose the filter.
        return ok ? ORION_OK : ORION_ERR_BAD_ARG;
    });
}

OrionStatus orion_engine_render_preview(OrionEngine* engine, double* out_ms) {
    if (engine == nullptr) return ORION_ERR_BAD_ARG;
    if (!engine->impl.hasPreview()) return ORION_ERR_BAD_ARG;
    return guard(engine, [&]() -> OrionStatus {
        const double ms = engine->impl.renderPreview();
        if (out_ms) *out_ms = ms;
        return ORION_OK;
    });
}

OrionStatus orion_engine_render_region(OrionEngine* engine, float x, float y,
                                       float w, float h, unsigned out_w,
                                       unsigned out_h, int interacting,
                                       double* out_ms) {
    if (engine == nullptr || !engine->impl.hasImage()) return ORION_ERR_BAD_ARG;
    return guard(engine, [&]() -> OrionStatus {
        const double ms = engine->impl.renderRegion({x, y, w, h, out_w, out_h}, interacting != 0);
        if (ms < 0.0) return ORION_ERR_BAD_ARG;   // not this edit: render the whole graph
        if (out_ms) *out_ms = ms;
        return ORION_OK;
    });
}

void* orion_engine_region_texture(const OrionEngine* engine) {
    if (engine == nullptr || !engine->impl.hasRegion()) return nullptr;
    try {
        return engine->impl.regionDevelop().output().displayHandle();
    } catch (...) {
        return nullptr;
    }
}

OrionStatus orion_engine_region_size(const OrionEngine* engine,
                                     unsigned* out_w, unsigned* out_h) {
    if (engine == nullptr || out_w == nullptr || out_h == nullptr ||
        !engine->impl.hasRegion())
        return ORION_ERR_BAD_ARG;
    return guard(const_cast<OrionEngine*>(engine), [&]() -> OrionStatus {
        *out_w = engine->impl.regionDevelop().outputWidth();
        *out_h = engine->impl.regionDevelop().outputHeight();
        return ORION_OK;
    });
}

OrionStatus orion_engine_release_render_memory(OrionEngine* engine) {
    if (engine == nullptr) return ORION_ERR_BAD_ARG;
    engine->impl.releaseRenderMemory();   // noexcept: nothing to guard
    return ORION_OK;
}

OrionStatus orion_engine_preview_size(const OrionEngine* engine,
                                      unsigned* out_w, unsigned* out_h) {
    if (engine == nullptr || out_w == nullptr || out_h == nullptr)
        return ORION_ERR_BAD_ARG;
    if (!engine->impl.hasPreview()) return ORION_ERR_BAD_ARG;
    return guard(const_cast<OrionEngine*>(engine), [&]() -> OrionStatus {
        *out_w = engine->impl.previewDevelop().outputWidth();
        *out_h = engine->impl.previewDevelop().outputHeight();
        return ORION_OK;
    });
}

OrionStatus orion_engine_quarter_turns(const OrionEngine* engine, int* out_turns) {
    if (engine == nullptr || out_turns == nullptr) return ORION_ERR_BAD_ARG;
    return guard(const_cast<OrionEngine*>(engine), [&]() -> OrionStatus {
        *out_turns = engine->impl.develop().quarterTurns();
        return ORION_OK;
    });
}

OrionStatus orion_engine_to_frame(const OrionEngine* engine,
                                  float x, float y, float* out_x, float* out_y) {
    if (engine == nullptr || out_x == nullptr || out_y == nullptr)
        return ORION_ERR_BAD_ARG;
    return guard(const_cast<OrionEngine*>(engine), [&]() -> OrionStatus {
        const auto p = engine->impl.develop().displayedToFrame(x, y);
        *out_x = p.first;
        *out_y = p.second;
        return ORION_OK;
    });
}

OrionStatus orion_engine_from_frame(const OrionEngine* engine,
                                    float x, float y, float* out_x, float* out_y) {
    if (engine == nullptr || out_x == nullptr || out_y == nullptr)
        return ORION_ERR_BAD_ARG;
    return guard(const_cast<OrionEngine*>(engine), [&]() -> OrionStatus {
        const auto p = engine->impl.develop().frameToDisplayed(x, y);
        *out_x = p.first;
        *out_y = p.second;
        return ORION_OK;
    });
}

OrionStatus orion_engine_display_map(const OrionEngine* engine,
                                     float* out_to_display9,
                                     float* out_to_frame9) {
    if (engine == nullptr || out_to_display9 == nullptr ||
        out_to_frame9 == nullptr)
        return ORION_ERR_BAD_ARG;
    return guard(const_cast<OrionEngine*>(engine), [&]() -> OrionStatus {
        engine->impl.develop().displayMap(out_to_display9, out_to_frame9);
        return ORION_OK;
    });
}

OrionStatus orion_engine_mask_place_to_frame(
    const OrionEngine* engine, const OrionMaskLegacyGeometry* geometry,
    int kind, OrionMaskShape* shape, float* dab_xy, int dab_count) {
    if (engine == nullptr || geometry == nullptr || shape == nullptr)
        return ORION_ERR_BAD_ARG;
    // A null buffer with a positive count is a caller bug, not an empty stroke.
    if (dab_xy == nullptr && dab_count > 0) return ORION_ERR_BAD_ARG;
    return guard(const_cast<OrionEngine*>(engine), [&]() -> OrionStatus {
        const orion::pipe::mask::PlacedShape in{
            shape->center_x, shape->center_y, shape->angle, shape->length,
            shape->radius_x, shape->radius_y, shape->brush_radius};
        const auto out = engine->impl.develop().placeMaskToFrame(
            in, kind, geometry->crop_x, geometry->crop_y, geometry->crop_w,
            geometry->crop_h, geometry->rotate_quarters,
            geometry->straighten_deg, geometry->perspective_vertical,
            geometry->perspective_horizontal, geometry->perspective_aspect,
            dab_xy, dab_count);
        shape->center_x = out.centerX;
        shape->center_y = out.centerY;
        shape->angle    = out.angle;
        shape->length   = out.length;
        shape->radius_x = out.semiX;
        shape->radius_y = out.semiY;
        shape->brush_radius = out.brushRadius;
        return ORION_OK;
    });
}

OrionStatus orion_engine_max_matte_size(const OrionEngine* engine,
                                        unsigned* out_w, unsigned* out_h) {
    if (engine == nullptr || out_w == nullptr || out_h == nullptr)
        return ORION_ERR_BAD_ARG;
    return guard(const_cast<OrionEngine*>(engine), [&]() -> OrionStatus {
        *out_w = engine->impl.develop().maxMatteWidth();
        *out_h = engine->impl.develop().maxMatteHeight();
        return ORION_OK;
    });
}

OrionStatus orion_engine_as_shot(const OrionEngine* engine, OrionAdjustments* out) {
    if (engine == nullptr || out == nullptr) return ORION_ERR_BAD_ARG;
    return guard(const_cast<OrionEngine*>(engine), [&]() -> OrionStatus {
        const auto wb = engine->impl.develop().asShotWhiteBalance();
        *out = OrionAdjustments{};
        out->temperature_k = wb.temperatureK;
        out->tint          = wb.tint;
        out->contrast      = 1.0f;
        out->sharpen_radius = 1.0f;
        out->crop_w = 1.0f;
        out->crop_h = 1.0f;
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
        *out_width  = d.outputWidth();
        *out_height = d.outputHeight();
        return ORION_OK;
    });
}

OrionStatus orion_engine_auto_enhance(OrionEngine* engine, OrionAdjustments* adj) {
    if (engine == nullptr || adj == nullptr) return ORION_ERR_BAD_ARG;
    return guard(engine, [&]() -> OrionStatus {
        orion::pipe::Adjustments a = toAdjustments(engine, adj);
        engine->impl.autoEnhance(a);

        // Only the controls auto-enhance is allowed to move are written back.
        // Everything else the caller sent is theirs and stays theirs.
        adj->exposure_ev = a.exposureEv;
        adj->blacks      = a.blacks;
        adj->whites      = a.whites;
        adj->fusion      = a.fusion;
        adj->clarity     = a.clarity;
        return ORION_OK;
    });
}

OrionStatus orion_engine_load_lut(OrionEngine* engine, const char* path) {
    if (engine == nullptr || path == nullptr) return ORION_ERR_BAD_ARG;
    return guard(engine, [&]() -> OrionStatus {
        std::ifstream in(orion::util::utf8Path(path), std::ios::binary);
        if (!in) throw std::runtime_error(std::string("cannot open ") + path);

        std::string text((std::istreambuf_iterator<char>(in)),
                          std::istreambuf_iterator<char>());

        const auto parsed = orion::pipe::parseCube(text);
        // Thrown, not returned, so the reason reaches orion_last_error — a
        // status code alone would tell the user their LUT failed and nothing
        // about which line of it did.
        if (!parsed.ok) throw std::runtime_error(parsed.error);

        engine->impl.setCreativeLut(parsed.lut);
        return ORION_OK;
    });
}

OrionStatus orion_engine_clear_lut(OrionEngine* engine) {
    if (engine == nullptr) return ORION_ERR_BAD_ARG;
    return guard(engine, [&]() -> OrionStatus {
        engine->impl.clearCreativeLut();
        return ORION_OK;
    });
}

OrionStatus orion_engine_lut_title(const OrionEngine* engine, char* out, int capacity) {
    if (engine == nullptr || out == nullptr || capacity <= 0) return ORION_ERR_BAD_ARG;
    return guard(const_cast<OrionEngine*>(engine), [&]() -> OrionStatus {
        const std::string& title = engine->impl.develop().creativeLutTitle();
        const int n = std::min<int>(capacity - 1, static_cast<int>(title.size()));
        std::memcpy(out, title.data(), static_cast<std::size_t>(n));
        out[n] = '\0';
        return ORION_OK;
    });
}

OrionStatus orion_engine_set_wide_output(OrionEngine* engine, int wide) {
    if (engine == nullptr) return ORION_ERR_BAD_ARG;
    return guard(engine, [&]() -> OrionStatus {
        // ⚠ The full graph only, and the one piece of out-of-band state that is
        // deliberately not fanned out to the preview. Sixteen bits exist for
        // export, export reads `develop()`, and widening the preview's tail
        // would reallocate two textures for a picture nothing measures.
        engine->impl.developMutable().setWideOutput(wide != 0);
        return ORION_OK;
    });
}

OrionStatus orion_engine_set_headless(OrionEngine* engine, int headless) {
    if (engine == nullptr) return ORION_ERR_BAD_ARG;
    return guard(engine, [&]() -> OrionStatus {
        // No photograph needed: it is a policy the next open reads (#312).
        engine->impl.setHeadless(headless != 0);
        return ORION_OK;
    });
}

OrionStatus orion_engine_frame_size(const OrionEngine* engine,
                                    uint32_t* out_width, uint32_t* out_height) {
    if (engine == nullptr || out_width == nullptr || out_height == nullptr) {
        return ORION_ERR_BAD_ARG;
    }
    return guard(const_cast<OrionEngine*>(engine), [&]() -> OrionStatus {
        const auto& d = engine->impl.develop();
        *out_width  = d.frameWidth();
        *out_height = d.frameHeight();
        return ORION_OK;
    });
}

void* orion_engine_output_texture(const OrionEngine* engine) {
    if (engine == nullptr || !engine->impl.hasImage()) return nullptr;
    try {
        return engine->impl.develop().output().displayHandle();
    } catch (...) {
        return nullptr;
    }
}

void* orion_engine_preview_texture(const OrionEngine* engine) {
    if (engine == nullptr || !engine->impl.hasPreview()) return nullptr;
    // Guarded exactly as the full graph's texture is. `output()` throws on a
    // pipeline that never compiled, and a throw crossing this file terminates
    // the process — the one thing this file exists to stop.
    try {
        return engine->impl.previewDevelop().output().displayHandle();
    } catch (...) {
        return nullptr;
    }
}

// orion_engine_read_pixels and the display copy are in CApiDisplay.cpp (#386).

void* orion_engine_metal_device(const OrionEngine* engine) {
    if (engine == nullptr) return nullptr;
    try {
        return const_cast<OrionEngine*>(engine)->impl.device().rawDevice();
    } catch (...) {
        return nullptr;
    }
}

OrionStatus orion_engine_sample(const OrionEngine* engine, float u, float v,
                                float* out_display, float* out_scene) {
    if (engine == nullptr) return ORION_ERR_BAD_ARG;
    return guard(const_cast<OrionEngine*>(engine), [&]() -> OrionStatus {
        const_cast<OrionEngine*>(engine)->impl.sampleAt(u, v, out_display, out_scene);
        return ORION_OK;
    });
}

OrionStatus orion_engine_histogram(const OrionEngine* engine,
                                   uint32_t* out_bins, uint32_t bins) {
    if (engine == nullptr || out_bins == nullptr || bins == 0) return ORION_ERR_BAD_ARG;
    return guard(const_cast<OrionEngine*>(engine), [&]() -> OrionStatus {
        const_cast<OrionEngine*>(engine)->impl.histogram(out_bins, bins);
        return ORION_OK;
    });
}

namespace {
/// One copy of the field mapping for both doors: the file on disk and the
/// photograph already open.
void fillInfo(const orion::raw::RawInfo& info, OrionRawInfo* out) noexcept {
    *out = OrionRawInfo{};
    out->width  = info.width;
    out->height = info.height;
    copyInto(out->camera, sizeof out->camera, info.camera);
    copyInto(out->lens, sizeof out->lens, info.lens);
    out->iso          = info.isoSpeed;
    out->shutter      = info.shutter;
    out->aperture     = info.aperture;
    out->focal_length = info.focalLength;
    out->timestamp    = info.timestamp;
}
}  // namespace

OrionStatus orion_read_info(const char* path, OrionRawInfo* out) {
    if (path == nullptr || out == nullptr) return ORION_ERR_BAD_ARG;
    return guard(nullptr, [&]() -> OrionStatus {
        const auto info = orion::raw::readInfo(path);
        fillInfo(info, out);
        return ORION_OK;
    });
}

OrionStatus orion_read_thumbnail(const char* path, uint8_t* buffer,
                                 uint32_t capacity, uint32_t* out_size,
                                 int32_t* out_turns) {
    if (path == nullptr || out_size == nullptr) return ORION_ERR_BAD_ARG;
    return guard(nullptr, [&]() -> OrionStatus {
        const auto thumb = orion::raw::extractThumbnail(path);
        *out_size = static_cast<uint32_t>(thumb.jpeg.size());
        // Quarter turns rather than LibRaw's flip encoding, so the private
        // vocabulary stays on this side of the facade.
        if (out_turns != nullptr) {
            *out_turns = orion::raw::quarterTurnsFor(thumb.flip);
        }
        if (thumb.jpeg.empty()) return ORION_ERR_INTERNAL;
        if (buffer == nullptr) return ORION_OK;          // size query
        if (capacity < thumb.jpeg.size()) return ORION_ERR_BAD_ARG;
        std::memcpy(buffer, thumb.jpeg.data(), thumb.jpeg.size());
        return ORION_OK;
    });
}

OrionStatus orion_engine_hdr_merge(OrionEngine* engine,
                                   const char* const* paths, int32_t count,
                                   int32_t reference_index,
                                   const char* output_path) {
    if (engine == nullptr || paths == nullptr || count < 2 ||
        output_path == nullptr) {
        return ORION_ERR_BAD_ARG;
    }
    return guard(engine, [&]() -> OrionStatus {
        std::vector<std::string> files;
        files.reserve(std::size_t(count));
        for (int32_t i = 0; i < count; ++i) {
            if (paths[i] == nullptr) return ORION_ERR_BAD_ARG;
            files.emplace_back(paths[i]);
        }
        engine->impl.hdrMerge(files, reference_index, output_path);
        return ORION_OK;
    });
}

OrionStatus orion_engine_hdr_merge_progress(OrionEngine* engine, float* out) {
    if (engine == nullptr || out == nullptr) return ORION_ERR_BAD_ARG;
    *out = engine->impl.hdrMergeProgress();
    return ORION_OK;
}

OrionStatus orion_engine_hdr_merge_cancel(OrionEngine* engine) {
    if (engine == nullptr) return ORION_ERR_BAD_ARG;
    engine->impl.hdrMergeCancel();
    return ORION_OK;
}

OrionStatus orion_engine_lens_profile(const OrionEngine* engine,
                                      OrionLensProfile* out) {
    if (engine == nullptr || out == nullptr) return ORION_ERR_BAD_ARG;

    *out = OrionLensProfile{};
    const auto& p = engine->impl.lensProfile();
    out->found = p.found ? 1 : 0;
    out->approximate = p.approximate ? 1 : 0;
    out->source = p.found ? static_cast<int32_t>(p.source) : 0;
    std::strncpy(out->lens, p.lens.c_str(), sizeof out->lens - 1);
    std::strncpy(out->maker, p.maker.c_str(), sizeof out->maker - 1);
    return ORION_OK;
}

const char* orion_engine_camera(const OrionEngine* engine) {
    return engine ? engine->impl.camera().c_str() : "";
}

const char* orion_engine_lens(const OrionEngine* engine) {
    return engine ? engine->impl.photoLens().c_str() : "";
}

OrionStatus orion_engine_info(const OrionEngine* engine, OrionRawInfo* out) {
    if (engine == nullptr || out == nullptr) return ORION_ERR_BAD_ARG;
    return guard(const_cast<OrionEngine*>(engine), [&]() -> OrionStatus {
        fillInfo(engine->impl.info(), out);
        return ORION_OK;
    });
}

namespace {
/// The name list, built once. `names()` sorts and de-duplicates ~1,450 entries
/// every call, and a picker filtering as somebody types would pay for that on
/// every keystroke.
const std::vector<std::string>& lensNames() {
    static const std::vector<std::string> names =
        orion::Engine::lensDatabaseNames();
    return names;
}
}  // namespace

// No status to return, so a throw while the list is first built (out of memory,
// an unreadable database) is an empty list; the static retries on the next call.
int32_t orion_lens_name_count(void) {
    try {
        return static_cast<int32_t>(lensNames().size());
    } catch (...) {
        return 0;
    }
}

const char* orion_lens_name_at(int32_t index) {
    try {
        const auto& n = lensNames();
        if (index < 0 || static_cast<std::size_t>(index) >= n.size()) return "";
        return n[static_cast<std::size_t>(index)].c_str();
    } catch (...) {
        return "";
    }
}

OrionStatus orion_engine_set_lens_choice(OrionEngine* engine, const char* name) {
    if (engine == nullptr) return ORION_ERR_BAD_ARG;
    // ⚠ No exception may cross this boundary — it would terminate the process.
    try {
        return engine->impl.setLensChoice(name ? name : "")
            ? ORION_OK : ORION_ERR_BAD_ARG;
    } catch (...) {
        return ORION_ERR_BAD_ARG;
    }
}

const char* orion_engine_lens_choice(const OrionEngine* engine) {
    return engine ? engine->impl.lensChoice().c_str() : "";
}

const char* orion_status_string(OrionStatus status) {
    switch (status) {
        case ORION_OK:           return "ok";
        case ORION_ERR_NO_GPU:   return "no usable GPU";
        case ORION_ERR_BAD_ARG:  return "invalid argument";
        case ORION_ERR_INTERNAL: return "internal error";
    }
    return "unrecognized status";
}

const char* orion_last_error(const OrionEngine* engine) {
    return engine ? engine->impl.lastError() : createError().c_str();
}

const char* orion_version(void) {
    return ORION_VERSION_STRING;
}
