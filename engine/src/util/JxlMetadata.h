#pragma once

#include <CoreFoundation/CoreFoundation.h>

#include <cstdint>
#include <vector>

namespace orion::util {

/// The EXIF and XMP that ImageIO would write for `props`, as bytes, for the
/// JPEG XL `Exif` and `xml ` boxes. `exif` is TIFF-header EXIF without the
/// "Exif\0\0" prefix; its pixel dimensions are set to `w` x `h`. Either may
/// come back empty. Throws std::runtime_error if ImageIO refuses. DECISIONS #363.
void serialiseMetadata(CFDictionaryRef props, std::uint32_t w, std::uint32_t h,
                       std::vector<std::uint8_t>& exif, std::vector<std::uint8_t>& xmp);

}  // namespace orion::util
