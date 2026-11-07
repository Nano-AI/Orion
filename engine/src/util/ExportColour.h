/*  Export's colour spaces off macOS, in Little CMS: the ICC profile a file
 *  embeds and the conversion into it from the sRGB the graph renders. macOS
 *  takes both from ColorSync's named spaces (ImageWriter.mm); these are built
 *  from the same published definitions, cited in research/color-pipeline.md.
 */

#pragma once

#include "util/ImageWriter.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace orion::util {

/// An ICC v2.4 matrix/TRC profile for `space`, ready to embed.
[[nodiscard]] std::vector<std::uint8_t> iccProfile(ColorSpace space);

/// Converts sRGB RGBA samples to `space` in place. The fourth sample is left
/// as it was. sRGB is a no-op.
void convertFromSrgb(std::uint16_t* rgba, std::uint32_t width, std::uint32_t height,
                     std::size_t bytesPerRow, ColorSpace space);

}  // namespace orion::util
