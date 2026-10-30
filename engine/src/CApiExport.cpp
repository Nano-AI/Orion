/*  The facade's export calls: orion_engine_export, its size measurement, and
 *  the snapshot a size is measured from off the render thread. Split from
 *  CApi.cpp for its length; the same firewall: every entry point runs in
 *  guard() (CApiGuard.h), and nothing throws past this file.
 */

#include "CApiGuard.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

using orion::capi::guard;

namespace {
/// An unknown value falls back to sRGB rather than throwing. A color space the
/// facade does not recognize is a caller from a newer build, and refusing the
/// whole export over it would be worse than writing the safe one.
orion::util::ColorSpace toColorSpace(int32_t v) {
    switch (v) {
        case 1:  return orion::util::ColorSpace::DisplayP3;
        case 2:  return orion::util::ColorSpace::AdobeRgb;
        default: return orion::util::ColorSpace::Srgb;
    }
}

/// Only an explicit 8 narrows it. Zero — a struct the caller never filled in —
/// keeps the precision, because the alternative is losing it by omission.
orion::util::BitDepth toBitDepth(int32_t d) {
    return d == ORION_DEPTH_8 ? orion::util::BitDepth::Eight
                              : orion::util::BitDepth::Sixteen;
}

orion::util::Sharpen toSharpen(int32_t s) {
    switch (s) {
        case ORION_SHARPEN_SCREEN: return orion::util::Sharpen::Screen;
        case ORION_SHARPEN_PRINT:  return orion::util::Sharpen::Print;
        default:                   return orion::util::Sharpen::None;
    }
}

/// Borrowed straight through: the mask is the caller's until the call returns,
/// which is as long as the writer needs it.
orion::util::Watermark toWatermark(const OrionExportOptions& o) {
    orion::util::Watermark w;
    if (o.watermark_mask == nullptr || o.watermark_width == 0 || o.watermark_height == 0) {
        return w;
    }
    w.mask = o.watermark_mask;
    w.width = o.watermark_width;
    w.height = o.watermark_height;
    for (int i = 0; i < 3; ++i) w.rgb[i] = std::clamp(o.watermark_rgb[i], 0.0f, 1.0f);
    return w;
}
/// What a size measurement encodes with. The depth and the sharpening both
/// move the byte count (eight bits is about half the PNG, and sharpening gives
/// the JPEG encoder more to encode), and so does the watermark, which is detail
/// the encoder spends bytes on. A measured size that ignored them would be a
/// measurement of the wrong file. No metadata: see `encodedSize`.
orion::util::ExportOptions toSizeOptions(const OrionExportOptions& options) {
    orion::util::ExportOptions o{};
    o.format = (options.format < 0)
        ? orion::util::ImageFormat::Jpeg
        : static_cast<orion::util::ImageFormat>(options.format);
    o.quality = options.quality;
    o.maxDimension = options.max_dimension;
    o.space = toColorSpace(options.space);
    o.depth = toBitDepth(options.bit_depth);
    o.sharpen = toSharpen(options.sharpen);
    o.watermark = toWatermark(options);
    return o;
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
            opts.depth        = toBitDepth(options->bit_depth);
            opts.sharpen      = toSharpen(options->sharpen);
            opts.watermark    = toWatermark(*options);
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
        *out_bytes = static_cast<uint64_t>(engine->impl.exportedSize(toSizeOptions(*options)));
        return ORION_OK;
    });
}

struct OrionExportSnapshot {
    std::vector<std::uint16_t> pixels;
    std::uint32_t width = 0, height = 0;
    orion::util::ExportOptions options;
    std::vector<std::uint8_t> mask;  ///< owned copy; options.watermark points here
};

OrionStatus orion_engine_export_snapshot(OrionEngine* engine,
                                         const OrionExportOptions* options,
                                         OrionExportSnapshot** out) {
    if (engine == nullptr || options == nullptr || out == nullptr) return ORION_ERR_BAD_ARG;
    *out = nullptr;
    return guard(engine, [&]() -> OrionStatus {
        auto s = std::make_unique<OrionExportSnapshot>();
        s->options = toSizeOptions(*options);
        // The caller's mask is borrowed only for this call; the encode is later.
        if (auto& wm = s->options.watermark; wm.active()) {
            s->mask.assign(wm.mask, wm.mask + std::size_t(wm.width) * wm.height);
            wm.mask = s->mask.data();
        }
        s->pixels = engine->impl.exportPixels(s->options, s->width, s->height);
        *out = s.release();
        return ORION_OK;
    });
}

OrionStatus orion_export_snapshot_size(const OrionExportSnapshot* s, uint64_t* out_bytes) {
    if (s == nullptr || out_bytes == nullptr) return ORION_ERR_BAD_ARG;
    // No engine to hold an error message: the status is the whole answer.
    return guard(nullptr, [&]() -> OrionStatus {
        *out_bytes = static_cast<uint64_t>(orion::util::encodedSize(
            s->pixels.data(), s->width, s->height,
            std::size_t(s->width) * 4 * sizeof(std::uint16_t), s->options));
        return ORION_OK;
    });
}

void orion_export_snapshot_free(OrionExportSnapshot* snapshot) { delete snapshot; }
