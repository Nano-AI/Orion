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

#include <algorithm>
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
        a.cropX = adj->crop_x; a.cropY = adj->crop_y;
        a.cropW = adj->crop_w; a.cropH = adj->crop_h;
        a.cropPreview = adj->crop_preview != 0;
        a.previewX = adj->preview_x;
        a.previewY = adj->preview_y;
        a.previewSize = adj->preview_size;

        a.lensDistortion = adj->lens_distortion;
        a.lensVignette   = adj->lens_vignette;
        a.lensProfile    = adj->lens_profile != 0;
        if (a.lensProfile) {
            const auto& p = engine->impl.lensProfile();
            for (int i = 0; i < 3; ++i) {
                a.lensPoly[i]       = p.poly[i];
                a.lensVignettePa[i] = p.vignette[i];
            }
        }
        a.lensCaRed      = adj->lens_ca_red;
        a.lensCaBlue     = adj->lens_ca_blue;
        a.highlightRecovery = adj->highlight_recovery;
        for (int i = 0; i < 3; ++i) {
            a.gradeShadow[i]    = adj->grade_shadow[i];
            a.gradeMidtone[i]   = adj->grade_midtone[i];
            a.gradeHighlight[i] = adj->grade_highlight[i];
        }
        a.denoiseLuma   = adj->denoise_luma;
        a.denoiseColor = adj->denoise_color;

        a.curve.master = toChannel(adj->curve_master);
        a.curve.red    = toChannel(adj->curve_red);
        a.curve.green  = toChannel(adj->curve_green);
        a.curve.blue   = toChannel(adj->curve_blue);
        a.clarity         = adj->clarity;
        a.sharpenAmount   = adj->sharpen_amount;
        a.sharpenRadius   = adj->sharpen_radius;
        a.sharpenMasking  = adj->sharpen_masking;
        for (int i = 0; i < 8; ++i) {
            a.hueShift[i] = adj->hue_shift[i];
            a.satShift[i] = adj->sat_shift[i];
            a.lumShift[i] = adj->lum_shift[i];
        }
        engine->impl.setAdjustments(a);
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

OrionStatus orion_engine_set_wide_output(OrionEngine* engine, int wide) {
    if (engine == nullptr) return ORION_ERR_BAD_ARG;
    return guard(engine, [&]() -> OrionStatus {
        engine->impl.developMutable().setWideOutput(wide != 0);
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

OrionStatus orion_engine_sample(const OrionEngine* engine, float u, float v,
                                float* out_display, float* out_scene) {
    if (engine == nullptr) return ORION_ERR_BAD_ARG;
    return guard(const_cast<OrionEngine*>(engine), [&]() -> OrionStatus {
        engine->impl.sampleAt(u, v, out_display, out_scene);
        return ORION_OK;
    });
}

OrionStatus orion_engine_histogram(const OrionEngine* engine,
                                   uint32_t* out_bins, uint32_t bins) {
    if (engine == nullptr || out_bins == nullptr || bins == 0) return ORION_ERR_BAD_ARG;
    return guard(const_cast<OrionEngine*>(engine), [&]() -> OrionStatus {
        engine->impl.histogram(out_bins, bins);
        return ORION_OK;
    });
}

OrionStatus orion_read_info(const char* path, OrionRawInfo* out) {
    if (path == nullptr || out == nullptr) return ORION_ERR_BAD_ARG;
    return guard(nullptr, [&]() -> OrionStatus {
        const auto info = orion::raw::readInfo(path);
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
        return ORION_OK;
    });
}

OrionStatus orion_read_thumbnail(const char* path, uint8_t* buffer,
                                 uint32_t capacity, uint32_t* out_size) {
    if (path == nullptr || out_size == nullptr) return ORION_ERR_BAD_ARG;
    return guard(nullptr, [&]() -> OrionStatus {
        const auto jpeg = orion::raw::extractThumbnail(path);
        *out_size = static_cast<uint32_t>(jpeg.size());
        if (jpeg.empty()) return ORION_ERR_INTERNAL;
        if (buffer == nullptr) return ORION_OK;          // size query
        if (capacity < jpeg.size()) return ORION_ERR_BAD_ARG;
        std::memcpy(buffer, jpeg.data(), jpeg.size());
        return ORION_OK;
    });
}

namespace {
/// An unknown value falls back to sRGB rather than throwing. A colour space the
/// facade does not recognise is a caller from a newer build, and refusing the
/// whole export over it would be worse than writing the safe one.
orion::util::ColorSpace toColorSpace(int32_t v) {
    switch (v) {
        case 1:  return orion::util::ColorSpace::DisplayP3;
        case 2:  return orion::util::ColorSpace::AdobeRgb;
        default: return orion::util::ColorSpace::Srgb;
    }
}
}  // namespace

OrionStatus orion_engine_export(OrionEngine* engine, const char* path,
                                const OrionExportOptions* options) {
    if (engine == nullptr || path == nullptr) return ORION_ERR_BAD_ARG;

    return guard(engine, [&]() -> OrionStatus {
        orion::util::ExportOptions opts;
        opts.format = orion::util::formatForPath(path);

        if (options != nullptr) {
            if (options->format >= 0) {
                opts.format = static_cast<orion::util::ImageFormat>(options->format);
            }
            opts.quality      = options->quality;
            opts.maxDimension = options->max_dimension;
            opts.space        = toColorSpace(options->space);
            opts.rating       = options->rating;
            switch (options->metadata) {
                case ORION_METADATA_ALL:  opts.metadata = orion::util::Metadata::All;  break;
                case ORION_METADATA_NONE: opts.metadata = orion::util::Metadata::None; break;
                default: opts.metadata = orion::util::Metadata::NoLocation; break;
            }
        }

        engine->impl.exportImage(path, opts);
        return ORION_OK;
    });
}

OrionStatus orion_engine_export_size(OrionEngine* engine,
                                     const OrionExportOptions* options,
                                     uint64_t* out_bytes) {
    if (engine == nullptr || options == nullptr || out_bytes == nullptr) {
        return ORION_ERR_BAD_ARG;
    }
    return guard(engine, [&]() -> OrionStatus {
        orion::util::ExportOptions o{};
        o.format = (options->format < 0)
            ? orion::util::ImageFormat::Jpeg
            : static_cast<orion::util::ImageFormat>(options->format);
        o.quality = options->quality;
        o.maxDimension = options->max_dimension;
        o.space = toColorSpace(options->space);
        *out_bytes = static_cast<uint64_t>(engine->impl.exportedSize(o));
        return ORION_OK;
    });
}

OrionStatus orion_engine_lens_profile(const OrionEngine* engine,
                                      OrionLensProfile* out) {
    if (engine == nullptr || out == nullptr) return ORION_ERR_BAD_ARG;

    *out = OrionLensProfile{};
    const auto& p = engine->impl.lensProfile();
    out->found = p.found ? 1 : 0;
    out->approximate = p.approximate ? 1 : 0;
    std::strncpy(out->lens, p.lens.c_str(), sizeof out->lens - 1);
    std::strncpy(out->maker, p.maker.c_str(), sizeof out->maker - 1);
    return ORION_OK;
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
    return "unrecognized status";
}

const char* orion_last_error(const OrionEngine* engine) {
    return engine ? engine->impl.lastError() : "";
}

const char* orion_version(void) {
    return ORION_VERSION_STRING;
}
