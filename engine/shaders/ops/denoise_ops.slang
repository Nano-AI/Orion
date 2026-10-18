// The starlet shrink's point-wise maths, shared by the kernels that need it.
//
// Split out of denoise_accum.slang (the ops/llf_ops.slang pattern) so the
// kernel stays under the 150-line ceiling as the chain grows: Texture's gain
// and the coarse chroma nodes read the same luminance and the same garrote.
// research/denoise-masked.md and research/high-iso.md.
//
// ⚠ Includes nothing: `textureFactor` uses `clampToImage` from cfa.slang, so
// an includer brings that in first.

#pragma once

float luminance2020(float3 c)
{
    return dot(c, float3(0.2627f, 0.6780f, 0.0593f));
}

float garrote(float w, float tau)
{
    const float a = abs(w);
    if (a <= tau || a < 1e-8f) return 0.0f;
    return w - (tau * tau) / w;
}

/// σ_n / max(σ_x, σ_n) at this pixel, from the luma detail energy over a 5x5
/// window spaced like the transform itself — the support this coefficient was
/// formed over. research/denoise-masked.md §1. In (0, 1], so it can only ever
/// lower the threshold.
float textureFactor(Texture2D<float4> fine, Texture2D<float4> coarse,
                    int2 c, int step, uint2 size, float sigma)
{
    float energy = 0.0f;
    [unroll]
    for (int j = 0; j < 5; ++j)
    {
        [unroll]
        for (int i = 0; i < 5; ++i)
        {
            const int2 q = clampToImage(c + int2((i - 2) * step, (j - 2) * step), size);
            const float d = luminance2020(fine[q].rgb - coarse[q].rgb);
            energy += d * d;
        }
    }
    energy *= 1.0f / 25.0f;
    const float sigmaX = sqrt(max(energy - sigma * sigma, 0.0f));
    return sigma / max(max(sigmaX, sigma), 1e-12f);
}

/// Texture: Starck, Murtagh, Candès & Donoho, IEEE TIP 12(6), 2003, eq. (1),
/// the noise-aware multiscale contrast gain. research/high-iso.md §2.
///
/// `w` is the coefficient as measured (the noise in it has deviation
/// `sigma`), `p` the signed degree of non-linearity, `m` the amplitude above
/// which nothing is boosted. c = 3, s = 0:
///
///     |w| <  cσ           1                  the noise is never boosted
///     cσ ≤ |w| < 2cσ      linear from 1 to (m/2cσ)^p
///     2cσ ≤ |w| < m       (m/|w|)^p
///     |w| ≥ m             1                  (m/|w|)^s, s = 0: edges pass
///
/// ⚠ One deviation from the printed equation: its middle branch ends at
/// (m/cσ)^p, which is not where the third begins, so the published gain jumps
/// down by 2^p at 2cσ and the transfer w·y(w) is not monotone there. The blend
/// here ends at the third branch's value instead, which is continuous.
float textureGain(float w, float sigma, float m, float p)
{
    const float a = abs(w);
    const float cs = 3.0f * sigma;
    if (p == 0.0f || a < cs || a >= m || m <= 2.0f * cs) return 1.0f;
    const float knee = pow(m / (2.0f * cs), p);
    const float y = (a < 2.0f * cs) ? lerp(1.0f, knee, (a - cs) / cs) : pow(m / a, p);
    return clamp(y, 0.25f, 4.0f);
}
