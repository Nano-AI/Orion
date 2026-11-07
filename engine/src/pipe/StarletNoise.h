/*  The starlet's noise at each scale, after the demosaic.
 *
 *  For white noise of deviation σ a starlet scale carries σ·‖W_j‖₂. The
 *  demosaic's output is not white: RCD interpolates three quarters of R and B
 *  and half of G, which low-passes the noise and correlates the channels, so
 *  the fine scales carry less than ‖W_j‖₂ says and the coarse ones more, and
 *  chroma far more than luma. Starck & Murtagh's answer for noise that is not
 *  white is σ_j^e measured by transforming simulated noise; here the
 *  simulation is a flat mosaic with Gaussian noise at several mixes of the
 *  three CFA classes, through RCD, with each scale's deviation read as a MAD
 *  (Donoho & Johnstone 1994).
 *  research/high-iso.md §7.
 *
 *  ⚠ Coupled to the demosaic: a change to `rcd_*.slang` re-measures the two
 *  tables (`testStarletNoiseGpu` fails when they drift).
 */

#pragma once

#include <array>
#include <cstddef>

namespace orion::pipe::starlet {

inline constexpr int kScales = 6;

/// ‖W_j‖₂ of the 5x5 B3 starlet, the norms for white noise.
/// `testDenoiseSmoothnessGpu` recomputes them from the taps.
inline constexpr float kWhiteNorm[kScales] = {0.8907f, 0.2007f, 0.0855f,
                                              0.0412f, 0.0204f, 0.0102f};

/// var(w_j) per unit variance of the R, G and B samples, after RCD, for
/// luminance (Rec.2020 weights) and for one chroma channel (a channel minus
/// luminance, the mean over the three). Least squares over eleven mixes of
/// per-class deviation, ratios 1 to 3, on a flat 0.1 with deviation 0.01;
/// every mix within 4% in deviation at the scales a chain shrinks (luma's
/// j = 5 B term, -2e-6, is held at zero). Level and amplitude do not move
/// them: 0.02 to 0.25 and 0.005 to 0.02 agree to 2%.
inline constexpr float kLuma[kScales][3] = {
    {2.532e-2f, 2.018e-1f, 1.324e-2f},
    {6.908e-3f, 3.981e-2f, 8.592e-4f},
    {1.757e-3f, 7.097e-3f, 1.599e-4f},
    {4.184e-4f, 1.604e-3f, 6.160e-5f},
    {1.067e-4f, 3.896e-4f, 1.681e-5f},
    {3.038e-5f, 1.037e-4f, 0.0f}};
inline constexpr float kChroma[kScales][3] = {
    {2.334e-2f, 1.291e-2f, 2.621e-2f},
    {1.669e-2f, 1.269e-2f, 2.136e-2f},
    {5.349e-3f, 4.718e-3f, 7.035e-3f},
    {1.584e-3f, 1.291e-3f, 1.845e-3f},
    {4.208e-4f, 2.810e-4f, 4.861e-4f},
    {1.073e-4f, 1.030e-4f, 9.950e-5f}};

/// Variance at scale j as `A·Y + B` in the luminance `Y` of the coarse level.
struct ScaleNoise {
    float lumaA = 0.0f, lumaB = 0.0f;
    float chromaA = 0.0f, chromaB = 0.0f;
};

/// From the mosaic's fit `var = a·x + b` and the multipliers `m` linearize
/// applies. A sample of class c on a grey of luminance Y read Y/m_c, so after
/// the multiplier its variance is m_c·a·Y + m_c²·b.
[[nodiscard]] constexpr ScaleNoise scaleNoise(float a, float b, const std::array<float, 3>& m,
                                              int j) noexcept {
    ScaleNoise s;
    for (int c = 0; c < 3; ++c) {
        const float va = m[std::size_t(c)] * a;
        const float vb = m[std::size_t(c)] * m[std::size_t(c)] * b;
        s.lumaA += kLuma[j][c] * va;
        s.lumaB += kLuma[j][c] * vb;
        s.chromaA += kChroma[j][c] * va;
        s.chromaB += kChroma[j][c] * vb;
    }
    return s;
}

}  // namespace orion::pipe::starlet
