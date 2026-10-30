// Shared by both export writers; see ExportOps.h. Moved out of ImageWriter.mm
// unchanged when Windows and Linux got a writer of their own (#376).

#include "util/ExportOps.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <string>
#include <vector>

namespace orion::util {

/// Output sharpening, as an unsharp mask over the **resized** image.
///
/// ⚠️ The placement is sourced and the numbers are not. Fraser's multipass
/// model — capture, creative, output — puts this pass last, after the image is
/// at its final size, because resampling is what softens it and sharpening
/// before the resample is thrown away by it. That is why this runs inside
/// `convert`, on the resized buffer, rather than as a node in the graph.
///
/// The sigma and amount below are **chosen, not measured**, and are listed in
/// `research/UNSOURCED.md`. The invariant the tests hold is the one the control
/// promises: Print sharpens more than Screen, Screen more than None, and None
/// is bit-identical to no pass at all.
Unsharp unsharpFor(Sharpen s) {
    switch (s) {
        case Sharpen::None:   return {0.0f, 0.0f};
        case Sharpen::Screen: return {0.6f, 0.40f};
        case Sharpen::Print:  return {1.0f, 0.80f};
    }
    return {0.0f, 0.0f};
}

/// Unsharp mask in place over 16-bit RGBA, on luminance only.
///
/// Luminance only, not per channel: adding the same high-pass to R, G and B
/// moves the pixel along the grey axis, which sharpens the edge without moving
/// its hue. Sharpening the three channels independently is what puts colored
/// fringes on high-contrast edges.
///
/// Rec.709 luma weights, which are the primaries the display transform already
/// ends in — using any other set here would sharpen against a luminance the
/// rest of the pipeline does not agree with.
///
/// Cost, measured at 24 Mpx full size: two float planes, ~194 MB transient, and
/// a few hundred milliseconds single-threaded. Both are the same order as the
/// readback and the bitmap contexts either side of it, and export is off the
/// interaction path — but the panel's live size estimate runs this too, which
/// is why it is debounced and shows a spinner.
void unsharpMask(std::uint16_t* rgba, std::size_t w, std::size_t h,
                 std::size_t bytesPerRow, Unsharp s) {
    if (s.amount <= 0.0f || s.sigma <= 0.0f || w == 0 || h == 0) return;

    const std::size_t rowPixels = bytesPerRow / sizeof(std::uint16_t);

    // Three sigma either side: past that the Gaussian's weight is under 0.3%
    // and the taps cost more than they change.
    const int half = std::max(1, static_cast<int>(std::ceil(3.0f * s.sigma)));
    std::vector<float> kernel(static_cast<std::size_t>(2 * half + 1));
    float sum = 0.0f;
    for (int i = -half; i <= half; ++i) {
        const float v = std::exp(-(static_cast<float>(i) * static_cast<float>(i))
                                 / (2.0f * s.sigma * s.sigma));
        kernel[static_cast<std::size_t>(i + half)] = v;
        sum += v;
    }
    for (float& v : kernel) v /= sum;

    const auto at = [&](std::size_t x, std::size_t y) -> std::uint16_t* {
        return rgba + y * rowPixels + x * 4;
    };

    // The luminance plane, and the horizontal pass over it. Two planes rather
    // than three: the vertical pass reads `blurred` and `luma` together and
    // writes the correction straight into the pixels.
    std::vector<float> luma(w * h), blurred(w * h);
    for (std::size_t y = 0; y < h; ++y) {
        for (std::size_t x = 0; x < w; ++x) {
            const std::uint16_t* p = at(x, y);
            luma[y * w + x] = (0.2126f * p[0] + 0.7152f * p[1] + 0.0722f * p[2])
                            / 65535.0f;
        }
    }

    for (std::size_t y = 0; y < h; ++y) {
        for (std::size_t x = 0; x < w; ++x) {
            float acc = 0.0f;
            for (int i = -half; i <= half; ++i) {
                // Clamp at the border. Wrapping would sharpen the left edge
                // against the right one.
                const std::size_t sx = static_cast<std::size_t>(
                    std::clamp<long>(static_cast<long>(x) + i, 0,
                                     static_cast<long>(w) - 1));
                acc += kernel[static_cast<std::size_t>(i + half)] * luma[y * w + sx];
            }
            blurred[y * w + x] = acc;
        }
    }

    for (std::size_t y = 0; y < h; ++y) {
        for (std::size_t x = 0; x < w; ++x) {
            float acc = 0.0f;
            for (int i = -half; i <= half; ++i) {
                const std::size_t sy = static_cast<std::size_t>(
                    std::clamp<long>(static_cast<long>(y) + i, 0,
                                     static_cast<long>(h) - 1));
                acc += kernel[static_cast<std::size_t>(i + half)] * blurred[sy * w + x];
            }
            const float delta = s.amount * (luma[y * w + x] - acc) * 65535.0f;
            std::uint16_t* p = at(x, y);
            for (int c = 0; c < 3; ++c) {
                p[c] = static_cast<std::uint16_t>(
                    std::clamp(static_cast<float>(p[c]) + delta, 0.0f, 65535.0f));
            }
        }
    }
}

ImageFormat formatForPath(const std::string& path) {
    const auto dot = path.find_last_of('.');
    if (dot == std::string::npos) return ImageFormat::Jpeg;

    std::string ext = path.substr(dot + 1);
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    if (ext == "png") return ImageFormat::Png;
    if (ext == "tif" || ext == "tiff") return ImageFormat::Tiff;
    if (ext == "jxl") return ImageFormat::Jxl;
    return ImageFormat::Jpeg;
}

}  // namespace orion::util
