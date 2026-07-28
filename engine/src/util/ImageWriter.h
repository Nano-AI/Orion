#pragma once

#include <cstdint>
#include <string>

namespace orion::util {

/// Writes 8-bit RGBA pixels to a PNG via ImageIO. Throws on failure.
/// Exists so the bench tool can produce something you can actually look at —
/// a latency number nobody can eyeball is only half the evidence.
void writePng(const std::string& path, const std::uint8_t* rgba,
              std::uint32_t width, std::uint32_t height, std::size_t bytesPerRow);

}  // namespace orion::util
