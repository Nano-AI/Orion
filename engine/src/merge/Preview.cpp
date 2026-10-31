/*  Preview — see the header. The body is HdrMerge's, moved out unchanged. */

#include "merge/Preview.h"

#include "raw/RawImage.h"
#include "util/ImageWriter.h"

#include <algorithm>
#include <cmath>

namespace orion::merge {
namespace {

bool invert3(const float m[9], float out[9]) {
    const float det = m[0] * (m[4] * m[8] - m[5] * m[7])
                    - m[1] * (m[3] * m[8] - m[5] * m[6])
                    + m[2] * (m[3] * m[7] - m[4] * m[6]);
    if (std::abs(det) < 1e-12f) return false;
    const float k = 1.0f / det;
    out[0] = (m[4] * m[8] - m[5] * m[7]) * k;
    out[1] = (m[2] * m[7] - m[1] * m[8]) * k;
    out[2] = (m[1] * m[5] - m[2] * m[4]) * k;
    out[3] = (m[5] * m[6] - m[3] * m[8]) * k;
    out[4] = (m[0] * m[8] - m[2] * m[6]) * k;
    out[5] = (m[2] * m[3] - m[0] * m[5]) * k;
    out[6] = (m[3] * m[7] - m[4] * m[6]) * k;
    out[7] = (m[1] * m[6] - m[0] * m[7]) * k;
    out[8] = (m[0] * m[4] - m[1] * m[3]) * k;
    return true;
}

}  // namespace

Preview buildPreview(const std::vector<float>& rgb, std::uint32_t width, std::uint32_t height,
                     float gainEv, const std::array<float, 4>& camMul,
                     const std::array<float, 9>& camToXyzIn, int flip) {
    Preview p;

    // Camera -> sRGB: XYZ->sRGB times the inverse of the file's XYZ->camera,
    // rows normalized so a white-balanced neutral stays neutral — the same
    // dcraw convention the develop pipeline applies for its working space.
    constexpr float kXyzToSrgb[9] = {
         3.2404542f, -1.5371385f, -0.4985314f,
        -0.9692660f,  1.8760108f,  0.0415560f,
         0.0556434f, -0.2040259f,  1.0572252f,
    };
    float camToXyz[9], m[9];
    float xyzToCam[9];
    std::copy(camToXyzIn.begin(), camToXyzIn.end(), xyzToCam);
    if (!invert3(xyzToCam, camToXyz)) return p;
    for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 3; ++c) {
            m[r * 3 + c] = kXyzToSrgb[r * 3 + 0] * camToXyz[0 * 3 + c]
                         + kXyzToSrgb[r * 3 + 1] * camToXyz[1 * 3 + c]
                         + kXyzToSrgb[r * 3 + 2] * camToXyz[2 * 3 + c];
        }
    }
    for (int r = 0; r < 3; ++r) {
        const float sum = m[r * 3] + m[r * 3 + 1] + m[r * 3 + 2];
        if (std::abs(sum) > 1e-9f) {
            for (int c = 0; c < 3; ++c) m[r * 3 + c] /= sum;
        }
    }

    const float g = camMul[1] != 0.0f ? camMul[1] : 1.0f;
    const float wb[3] = {
        camMul[0] != 0.0f ? camMul[0] / g : 1.0f, 1.0f,
        camMul[2] != 0.0f ? camMul[2] / g : 1.0f};

    // Downsample to ~1024 on the long edge, in sensor orientation first.
    const std::uint32_t longEdge = std::max(width, height);
    const std::uint32_t step = std::max(1u, (longEdge + 1023) / 1024);
    const std::uint32_t sw = width / step;
    const std::uint32_t sh = height / step;
    if (sw == 0 || sh == 0) return p;

    // The stored data is radiance / H; the preview shows it at intended
    // brightness, exactly as BaselineExposure instructs a renderer to.
    const float gain = std::exp2(gainEv);

    std::vector<float> small(std::size_t(sw) * sh * 3);
    for (std::uint32_t y = 0; y < sh; ++y) {
        for (std::uint32_t x = 0; x < sw; ++x) {
            float acc[3] = {0, 0, 0};
            for (std::uint32_t dy = 0; dy < step; ++dy) {
                const std::size_t row =
                    (std::size_t(y) * step + dy) * width;
                for (std::uint32_t dx = 0; dx < step; ++dx) {
                    const float* px =
                        rgb.data() + (row + std::size_t(x) * step + dx) * 3;
                    acc[0] += px[0]; acc[1] += px[1]; acc[2] += px[2];
                }
            }
            const float inv = gain / float(step * step);
            float cam[3] = {acc[0] * inv * wb[0], acc[1] * inv * wb[1],
                            acc[2] * inv * wb[2]};
            // The common neutral ceiling, linearize.slang's argument in
            // miniature: a pixel clipped in every frame arrives with its
            // channels cut at different levels, and unclamped it renders in
            // the white-balance gains' color. The ceiling after the gains is
            // gain·min(wb) = gain, since wb is normalized to green.
            for (float& c : cam) c = std::min(c, gain);
            float* out = small.data() + (std::size_t(y) * sw + x) * 3;
            for (int c = 0; c < 3; ++c) {
                const float lin = m[c * 3 + 0] * cam[0] + m[c * 3 + 1] * cam[1]
                                + m[c * 3 + 2] * cam[2];
                // A camera-JPEG-ish shoulder: the recovered headroom (up to
                // 2^headroom above white) rolls smoothly toward paper white,
                // and the mids land where a preview is expected to sit —
                // bare gamma rendered a stop and a half under the develop
                // path.
                out[c] = 1.0f - std::exp(-3.0f * std::max(lin, 0.0f));
            }
        }
    }

    // Bake the rotation into the pixels. Thumbnail consumers pass the JPEG
    // stream around without the TIFF tag it sat under, so an upright
    // preview is the only kind that is upright everywhere.
    const int quarters = raw::quarterTurnsFor(flip);
    const bool swaps = (quarters % 2) != 0;
    p.width  = swaps ? sh : sw;
    p.height = swaps ? sw : sh;

    std::vector<std::uint16_t> rgba(std::size_t(p.width) * p.height * 4);
    for (std::uint32_t dy = 0; dy < p.height; ++dy) {
        for (std::uint32_t dx = 0; dx < p.width; ++dx) {
            std::uint32_t sx = dx, sy = dy;
            switch (quarters) {
                case 1: sx = dy;            sy = sh - 1 - dx; break;
                case 2: sx = sw - 1 - dx;   sy = sh - 1 - dy; break;
                case 3: sx = sw - 1 - dy;   sy = dx;          break;
                default: break;
            }
            const float* px = small.data() + (std::size_t(sy) * sw + sx) * 3;
            std::uint16_t* out =
                rgba.data() + (std::size_t(dy) * p.width + dx) * 4;
            for (int c = 0; c < 3; ++c) {
                // sRGB gamma over the toned light.
                const float v = std::clamp(px[c], 0.0f, 1.0f);
                const float e = v <= 0.0031308f
                    ? v * 12.92f
                    : 1.055f * std::pow(v, 1.0f / 2.4f) - 0.055f;
                out[std::size_t(c)] = std::uint16_t(e * 65535.0f + 0.5f);
            }
            out[3] = 65535;
        }
    }

    p.jpeg = util::encodeJpeg(rgba.data(), p.width, p.height,
                              std::size_t(p.width) * 4 * sizeof(std::uint16_t),
                              0.85f);
    return p;
}

}  // namespace orion::merge
