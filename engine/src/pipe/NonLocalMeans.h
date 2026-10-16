/*  Non-local means — the constants, and the reference the kernel is checked
 *  against.
 *
 *  Buades, Coll & Morel, CVPR 2005, and their IPOL 2011 article
 *  (doi:10.5201/ipol.2011.bcm_nlm) for the pixelwise form and the parameter
 *  table. research/high-iso.md §4. Implemented from the equations; the IPOL
 *  code is GPL and was not read.
 *
 *  `reference()` is the published pixelwise algorithm written out literally
 *  on a luminance plane, in double precision, with the per-pixel σ the kernel
 *  uses. It is what `testNlmGpu` compares the shader against, so that "the
 *  kernel implements the paper" is a measured claim.
 */

#pragma once

#include "pipe/Pyramid.h"

#include <algorithm>
#include <cmath>

namespace orion::pipe::nlm {

/// IPOL Table 1, the row for moderate noise on a grey image: patch 5x5 and a
/// 21x21 search window. A high-ISO frame is the only reason this runs.
inline constexpr int kRadius = 10;
inline constexpr int kPatch  = 2;

/// The smaller window the bench measures beside it (research/high-iso.md §4):
/// 11x11 search, 3x3 patch, the table's row for light noise.
inline constexpr int kSmallRadius = 5;
inline constexpr int kSmallPatch  = 1;

/// The slider is `k` in `h = k·σ`. The table's 0.40 sits mid-slider.
inline constexpr float kMaxK = 1.0f;

using pyr::Plane;

/// The pixelwise algorithm on one plane. `noiseA`/`noiseB` are the luma model
/// after white balance, `k` the filtering multiple. Edge handling is clamp,
/// matching the kernel's tile load.
[[nodiscard]] inline Plane reference(const Plane& img, int radius, int patch,
                                     float noiseA, float noiseB, float k) {
    Plane out = img;
    const double n = double((2 * patch + 1) * (2 * patch + 1));
    for (int y = 0; y < img.height; ++y) {
        for (int x = 0; x < img.width; ++x) {
            double mean = 0.0;
            for (int my = -1; my <= 1; ++my)
                for (int mx = -1; mx <= 1; ++mx) mean += img.at(x + mx, y + my);
            mean /= 9.0;
            const double sigma2 = std::max(double(noiseA) * std::max(mean, 0.0) + double(noiseB), 1e-12);
            const double h = double(k) * std::sqrt(sigma2);
            const double y0 = img.at(x, y);
            if (h <= 0.0) continue;

            double sum = 0.0, wsum = 0.0, wmax = 0.0;
            for (int oy = -radius; oy <= radius; ++oy) {
                for (int ox = -radius; ox <= radius; ++ox) {
                    if (ox == 0 && oy == 0) continue;
                    double d2 = 0.0;
                    for (int py = -patch; py <= patch; ++py) {
                        for (int px = -patch; px <= patch; ++px) {
                            const double d = double(img.at(x + px, y + py)) -
                                             double(img.at(x + ox + px, y + oy + py));
                            d2 += d * d;
                        }
                    }
                    const double w = std::exp(-std::max(d2 / n - 2.0 * sigma2, 0.0) / (h * h));
                    sum += w * img.at(x + ox, y + oy);
                    wsum += w;
                    wmax = std::max(wmax, w);
                }
            }
            sum += wmax * y0;
            wsum += wmax;
            out.v[static_cast<std::size_t>(y) * img.width + x] =
                float(wsum > 0.0 ? sum / wsum : y0);
        }
    }
    return out;
}

}  // namespace orion::pipe::nlm
