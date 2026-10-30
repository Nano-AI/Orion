#pragma once

// The CPU halves of export every platform's writer shares: ImageWriter.mm
// (ImageIO, macOS) and ImageWriterPortable.cpp (Windows, Linux; #376).

#include "util/ImageWriter.h"

#include <cstddef>
#include <cstdint>

namespace orion::util {

struct Unsharp {
    float sigma;   ///< Gaussian sigma in output pixels
    float amount;  ///< how much of the high-pass is added back
};

Unsharp unsharpFor(Sharpen s);

/// Unsharp mask in place over 16-bit RGBA, on luminance only.
void unsharpMask(std::uint16_t* rgba, std::size_t w, std::size_t h,
                 std::size_t bytesPerRow, Unsharp s);

}  // namespace orion::util
