#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace orion::util {

enum class ImageFormat { Png, Jpeg, Tiff };

/// What the written file is tagged as, and converted to.
///
/// ⚠️ The pipeline's display transform ends in Rec.709 primaries and saturates
/// there, so today no pixel Orion produces lies outside sRGB's gamut. Choosing
/// a wider space converts and tags correctly — which is what a print shop or a
/// managed workflow needs — but it cannot add saturation that the transform
/// never generated. Widening the gamut for real means giving the display node
/// its output primaries as a parameter; see research/color-pipeline.md.
enum class ColorSpace { Srgb, DisplayP3, AdobeRgb };

/// How much of the RAW's own metadata the export carries.
///
/// The default is **not** `All`. A photograph taken at home carries the home
/// coordinates, and a file put on the web publishes them to everyone who
/// downloads it — silently, with nothing in the interface to say so. Keeping
/// location is a choice the photographer has to make on purpose.
enum class Metadata {
    All,          ///< everything the container held, GPS included
    NoLocation,   ///< everything except GPS — the default
    None,         ///< nothing but "developed in Orion" and the star rating
};

/// Bits per component in the written file.
///
/// PNG and TIFF carry either. JPEG is an eight-bit container, so the choice
/// does not reach the file — the interface greys the control out rather than
/// offering a setting that does nothing.
enum class BitDepth { Eight, Sixteen };

/// Output sharpening: the correction for the softening that resampling causes.
///
/// Applied **after** the resize, which is the whole point — sharpening before
/// the resample is undone by it. See `research/detail.md`; the placement is
/// Fraser's, the amounts are ours and are listed in `research/UNSOURCED.md`.
enum class Sharpen { None, Screen, Print };

struct ExportOptions {
    ImageFormat format = ImageFormat::Jpeg;
    /// JPEG only, 0..1. Ignored by PNG and TIFF, which are lossless.
    float quality = 0.9f;
    /// Longest edge in pixels; 0 keeps full resolution.
    std::uint32_t maxDimension = 0;
    ColorSpace space = ColorSpace::Srgb;

    /// ⚠ Sixteen, not eight, when nothing says otherwise: an unspecified export
    /// keeps what it was given. The opposite default would mean a caller that
    /// forgot the field silently halved its precision, which is the bug this
    /// path already shipped once — a resize context that was eight bits undid
    /// the wide path for every export with a size limit.
    ///
    /// The export panel always states it, and states eight, because eight bits
    /// is what a file handed to someone else should be.
    BitDepth depth = BitDepth::Sixteen;

    Sharpen sharpen = Sharpen::None;

    /// A file whose EXIF, TIFF and GPS blocks are copied onto the export.
    /// Empty writes no metadata.
    ///
    /// Read with ImageIO rather than exiv2, which is GPL — DECISIONS #10. It
    /// reads the RAW's own blocks straight out of the container, so the export
    /// carries the exposure, lens and date the picture was actually taken with.
    std::string metadataFrom;

    Metadata metadata = Metadata::NoLocation;

    /// XMP rating, 0-5, written as the EXIF user rating. Negative writes none.
    int rating = -1;
};

/// Writes 16-bit RGBA samples, whatever depth the file ends up at. Throws
/// std::runtime_error on failure.
///
/// Resampling, when requested, happens in CoreGraphics rather than on the GPU:
/// export is not on the interaction path, and a correct downscale with proper
/// filtering matters more here than shaving milliseconds.
void writeImage(const std::string& path, const std::uint16_t* rgba,
                std::uint32_t width, std::uint32_t height, std::size_t bytesPerRow,
                const ExportOptions& options);

/// Convenience for the PNG case, used by the bench harness.
/// Encodes to memory and reports the byte count, without writing anything.
/// The export panel needs a size it can trust before you commit to the write,
/// and only a real encode gives one.
[[nodiscard]] std::size_t encodedSize(const std::uint16_t* rgba,
                                      std::uint32_t width, std::uint32_t height,
                                      std::size_t bytesPerRow,
                                      const ExportOptions& options);

void writePng(const std::string& path, const std::uint16_t* rgba,
              std::uint32_t width, std::uint32_t height, std::size_t bytesPerRow);

/// JPEG-encodes 16-bit RGBA to memory. Exists for the DNG preview: the
/// merged file embeds a viewable JPEG so the filmstrip and Finder have
/// something to show without decoding a quarter-gigabyte of half floats.
/// Returns empty on failure.
[[nodiscard]] std::vector<std::uint8_t> encodeJpeg(
    const std::uint16_t* rgba, std::uint32_t width, std::uint32_t height,
    std::size_t bytesPerRow, float quality);

/// Picks a format from the path's extension, defaulting to JPEG.
ImageFormat formatForPath(const std::string& path);

}  // namespace orion::util
