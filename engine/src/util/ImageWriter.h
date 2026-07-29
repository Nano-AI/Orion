#pragma once

#include <cstdint>
#include <string>

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

struct ExportOptions {
    ImageFormat format = ImageFormat::Jpeg;
    /// JPEG only, 0..1. Ignored by PNG and TIFF, which are lossless.
    float quality = 0.9f;
    /// Longest edge in pixels; 0 keeps full resolution.
    std::uint32_t maxDimension = 0;
    ColorSpace space = ColorSpace::Srgb;

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

/// Writes 8-bit RGBA pixels. Throws std::runtime_error on failure.
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

/// Picks a format from the path's extension, defaulting to JPEG.
ImageFormat formatForPath(const std::string& path);

}  // namespace orion::util
