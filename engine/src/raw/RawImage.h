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

/// Color filter array layout, normalized so (0,0) is the top-left of the
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

    /// Per-channel black points from LibRaw's global black, its four-channel
    /// trim and its 2D pattern.
    ///
    /// **The 2x2 case is exact, the rest is an average.** A camera that carries
    /// a per-pixel black pattern usually carries a 2x2 one, which lines up with
    /// the CFA cell by cell — so each channel gets its own value rather than the
    /// mean of four. Larger patterns (X-Trans is 6x6) are averaged, and X-Trans
    /// is rejected at decode anyway. Averaging a 2x2 pattern with real spread
    /// leaves a color cast in the deep shadows that no control can remove.
    ///
    /// `cblack` is LibRaw's `imgdata.color.cblack`: [0..3] per channel,
    /// [4] pattern rows, [5] pattern columns, [6...] the pattern itself in
    /// row-major order.
    static std::array<std::uint16_t, 4> blackLevels(unsigned black,
                                                    const unsigned* cblack,
                                                    std::uint32_t filters) noexcept;

    /// Human-readable pattern of the top-left 2x2, e.g. "RGGB".
    [[nodiscard]] std::string patternString() const;

    [[nodiscard]] std::size_t pixelCount() const noexcept {
        return static_cast<std::size_t>(width) * height;
    }
};

/// Basic facts about a raw file, read without decoding the sensor data.
struct RawInfo {
    std::uint32_t width = 0, height = 0;
    std::string   camera;
    std::string   lens;
    float         isoSpeed = 0;
    float         shutter = 0;
    float         aperture = 0;
    float         focalLength = 0;
    std::int64_t  timestamp = 0;
};

/// Metadata only. Orders of magnitude cheaper than decodeBayer — this is what a
/// folder scan uses, so opening a directory of 500 frames stays instant.
RawInfo readInfo(const std::string& path);

/// The JPEG preview the camera embedded. Every raw file carries one, and it is
/// what makes a filmstrip viable: decoding 500 mosaics to build thumbnails
/// would take minutes, extracting 500 JPEGs takes about a second.
/// Returns an empty vector when the file has no usable preview.
std::vector<std::uint8_t> extractThumbnail(const std::string& path);

/// Decodes the CFA mosaic and metadata from a raw file.
/// Throws std::runtime_error with LibRaw's message on failure.
BayerImage decodeBayer(const std::string& path);

/// A smaller mosaic, for a preview that has to be rendered while a slider moves.
///
/// research: none needed — this is decimation, not a filter. What it has to get
/// right is the one thing decimating a *mosaic* can get wrong.
///
/// ⚠ **The CFA phase must survive.** A Bayer mosaic is only meaningful with its
/// pattern: sample it on any stride that is not a multiple of the 2x2 cell and
/// the red pixels land where the demosaic expects green. The result does not
/// look soft, it looks like a color-swapped nightmare — and `filters` would
/// still say the pattern was intact, so nothing downstream would notice.
///
/// So the output is built cell by cell: output cell (I, J) averages the input
/// cells in the `scale/2` square starting at (I*scale/2, J*scale/2), channel by
/// channel. Averaging rather than point-sampling because a mosaic point-sampled
/// at stride four moires badly on fabric and foliage, and a preview that
/// shimmers while a slider moves is worse than one that is soft.
///
/// `scale` must be even and at least 2; anything else returns the image
/// unchanged rather than guessing.
[[nodiscard]] BayerImage decimate(const BayerImage& image, int scale);

}  // namespace orion::raw
