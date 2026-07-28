/*  RAW decode — LibRaw wrapper producing an untouched CFA mosaic.
 *
 *  We deliberately do NOT let LibRaw demosaic, white-balance, or tone-map.
 *  It unpacks the sensor samples and hands us the metadata; everything after
 *  that is Orion's pipeline on the GPU. See planning/ARCHITECTURE.md.
 */

#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace orion::raw {

/// Colour filter array layout, normalised so (0,0) is the top-left of the
/// visible area. Values are LibRaw's channel indices.
enum class Channel : std::uint8_t { R = 0, G = 1, B = 2, G2 = 3 };

struct BayerImage {
    std::uint32_t width  = 0;
    std::uint32_t height = 0;

    /// One sensor sample per pixel, visible area only, row-major.
    std::vector<std::uint16_t> samples;

    /// LibRaw 'filters' bitmask, already shifted for the visible-area origin.
    std::uint32_t filters = 0;

    /// Per-channel black points (R, G, B, G2) and the shared saturation point.
    std::array<std::uint16_t, 4> black{};
    std::uint16_t                white = 0;

    /// As-shot white balance multipliers, camera-native order.
    std::array<float, 4> camMul{};

    /// Camera RGB -> CIE XYZ (D65). Row-major 3x3.
    std::array<float, 9> camToXyz{};

    /// LibRaw's flip: 0 none, 3 = 180 degrees, 5 = 90 CCW, 6 = 90 CW.
    /// A portrait frame shot on a landscape sensor arrives as landscape
    /// pixels plus this flag — ignoring it is why portraits show sideways.
    int flip = 0;

    std::string camera;

    /// Which CFA channel sits at (x, y). Mirrors LibRaw's FC macro.
    [[nodiscard]] Channel channelAt(std::uint32_t x, std::uint32_t y) const noexcept {
        const unsigned shift = (((y << 1) & 14) | (x & 1)) << 1;
        return static_cast<Channel>((filters >> shift) & 3);
    }

    /// Human-readable pattern of the top-left 2x2, e.g. "RGGB".
    [[nodiscard]] std::string patternString() const;

    [[nodiscard]] std::size_t pixelCount() const noexcept {
        return static_cast<std::size_t>(width) * height;
    }
};

/// Decodes the CFA mosaic and metadata from a raw file.
/// Throws std::runtime_error with LibRaw's message on failure.
BayerImage decodeBayer(const std::string& path);

}  // namespace orion::raw
