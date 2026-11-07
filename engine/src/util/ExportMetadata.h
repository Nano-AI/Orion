/*  Export's metadata off macOS: the camera facts LibRaw reads from the RAW,
 *  under the same policy ImageWriter.mm applies through ImageIO, written as
 *  EXIF directories (CIPA DC-008, Exif 2.32, on TIFF 6.0's structure) and an
 *  XMP packet (ISO 16684-1). exiv2 is GPL (#10); libexif is LGPL and would be
 *  the first LGPL library shipped, for ten tags, so the directories are
 *  written with util/TiffIfd.h.
 */

#pragma once

#include "raw/RawImage.h"
#include "util/ImageWriter.h"
#include "util/TiffIfd.h"

#include <cstdint>
#include <string>
#include <vector>

namespace orion::util {

struct CaptureFacts {
    bool software = false;            ///< says Orion developed it
    std::string make, model, lens;
    std::string taken;                ///< "YYYY:MM:DD HH:MM:SS", the camera's clock
    float exposure = 0, fNumber = 0, iso = 0, focalLength = 0;
    raw::RawInfo::Gps gps;            ///< parsed only under Metadata::All
    int rating = -1;                  ///< 0-5; negative writes none

    [[nodiscard]] bool hasExif() const { return software || !make.empty() || !model.empty(); }
    [[nodiscard]] bool any() const { return hasExif() || rating >= 0; }
};

/// What `options` asks to carry from `options.metadataFrom`. A source LibRaw
/// cannot open still gets the Software tag, as on macOS.
[[nodiscard]] CaptureFacts captureFacts(const ExportOptions& options);

/// IFD0's, the EXIF IFD's and the GPS IFD's entries. Orientation is 1: the
/// geometry node has already turned the pixels. The pixel dimensions are the
/// export's, `width` x `height`.
void exifDirectories(const CaptureFacts& facts, std::uint32_t width, std::uint32_t height,
                     tiff::Ifd& ifd0, tiff::Ifd& exif, tiff::Ifd& gps);

/// TIFF-header EXIF without the "Exif\0\0" prefix, as JPEG's APP1, PNG's eXIf
/// and JPEG XL's Exif box carry it. Empty when `facts` has none.
[[nodiscard]] std::vector<std::uint8_t> exifBlock(const CaptureFacts& facts,
                                                  std::uint32_t width, std::uint32_t height);

/// An XMP packet. Every value is ASCII, anything wider a character reference,
/// so PNG's Latin-1 text chunk carries it intact. Empty when `facts` has none.
[[nodiscard]] std::string xmpPacket(const CaptureFacts& facts);

}  // namespace orion::util
