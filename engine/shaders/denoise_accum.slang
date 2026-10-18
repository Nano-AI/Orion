// À-trous wavelet denoise, shrinkage and accumulation.
//
// Reference: research/deep-research-2026-07-27.md §2.
//
// The detail coefficients at this scale are w_j = c_j − c_{j+1}. They are
// shrunk against a threshold derived from the sensor's own noise model rather
// than a fixed number, which is the whole point of a *profiled* denoiser: noise
// grows with signal, so a threshold that works in the shadows destroys texture
// in the highlights.
//
//     σ²(x) = a·x + b        Poisson shot noise + Gaussian read noise
//     σ_j   = ‖W_j‖₂ · sqrt(a·x + b)
//     τ_j   = β · σ_j
//
// ‖W_j‖₂ for the 5x5 starlet is [0.8907, 0.2007, 0.0855, 0.0412, 0.0204] —
// noise falls away fast at coarse scales, which is why a single threshold
// across scales over-smooths the fine ones and under-smooths the coarse.
//
// Shrinkage is the non-negative garrote. Soft thresholding biases every
// surviving coefficient down by τ and visibly dulls edges; hard thresholding
// leaves a discontinuity at τ that shows as speckle. The garrote is continuous
// like soft and asymptotically unbiased like hard.

#include "cfa.slang"
#include "ops/denoise_ops.slang"

// Two refinements, research/denoise-masked.md:
//
//   §0.1  luma and chroma take their strengths separately. A single strength
//         with chroma derived from it shrank luma at the Color value whenever
//         Luminance was zero.
//   §1    `detail` scales the luma threshold by a local BayesShrink factor
//         (Chang, Yu & Vetterli, IEEE TIP 9(9), 2000), so texture that stands
//         above the noise keeps more of itself. Zero skips it entirely.
//
// The masked chain after the profile runs this same kernel with `useField`: the
// two strengths are then per pixel, read from the coverage field.

struct ShrinkParams
{
    uint2 size;
    float noiseA;          // Poisson term
    float noiseB;          // Gaussian term
    float scaleNorm;       // ‖W_j‖₂ for this scale
    float strengthLuma;    // β for luma, in units of σ_j
    float strengthChroma;  // β for chroma
    float detail;          // 0..1, how much of the BayesShrink factor applies
    int   step;            // 2^j: the local energy window's spacing
    int   useField;        // 1: β per pixel from `field`, strengths ignored
    float texture;         // -1..1, Starck et al. 2003's gain on j = 0, 1
    int   _pad1;
};

[shader("compute")]
[numthreads(16, 16, 1)]
void atrousShrink(uint3 tid : SV_DispatchThreadID,
                  uniform Texture2D<float4>   fine,     // c_j
                  uniform Texture2D<float4>   coarse,   // c_{j+1}
                  uniform Texture2D<float4>   accum,    // running result
                  uniform Texture2D<float4>   field,    // β_luma, β_chroma (useField)
                  uniform RWTexture2D<float4> dst,
                  uniform ConstantBuffer<ShrinkParams> p)
{
    if (tid.x >= p.size.x || tid.y >= p.size.y) return;

    const float3 cj = fine[tid.xy].rgb;
    const float3 cj1 = coarse[tid.xy].rgb;
    const float3 detail = cj - cj1;

    float betaLuma = p.strengthLuma;
    float betaChroma = p.strengthChroma;
    if (p.useField != 0)
    {
        // ⚠ No early-out on the field: a zero there gives τ = 0, and the
        // garrote returns the coefficient unchanged, which is exact
        // reconstruction already.
        const float2 b = field[tid.xy].rg;
        betaLuma = b.x;
        betaChroma = b.y;
    }
    else if (betaLuma <= 0.0f && betaChroma <= 0.0f && p.texture == 0.0f)
    {
        dst[tid.xy] = float4(accum[tid.xy].rgb + detail, 1.0f);
        return;
    }

    // Noise level at this pixel's own brightness, at this scale.
    const float y = max(luminance2020(cj1), 0.0f);
    const float sigma = p.scaleNorm * sqrt(max(p.noiseA * y + p.noiseB, 0.0f));

    // Split into luma and chroma. Chroma noise is the ugly kind — colored
    // blotches — and chroma detail is scarce, so it tolerates a much harder
    // threshold than luma without visible loss.
    const float dLuma = luminance2020(detail);
    const float3 dChroma = detail - dLuma;

    float tauLuma = betaLuma * sigma;
    const float tauChroma = betaChroma * sigma;

    // Chroma keeps the fixed threshold: the control that protects texture
    // should not also protect blotches. research/denoise-masked.md §1.
    if (p.detail > 0.0f && tauLuma > 0.0f)
    {
        const float f = textureFactor(fine, coarse, int2(tid.xy), p.step, p.size, sigma);
        tauLuma *= lerp(1.0f, f, p.detail);
    }

    float keptLuma = garrote(dLuma, tauLuma);

    // Texture (research/high-iso.md §2): the two finest scales, the second at
    // half weight. The gain reads the coefficient as measured, because σ is
    // the deviation of *that*, and multiplies what the garrote kept.
    //
    // `m`, where the boost has faded to one, is 30σ or a quarter of the local
    // brightness, whichever is larger. 30σ alone made this a high-ISO-only
    // control: on a clean frame σ is so small that every real coefficient sat
    // above 30σ and passed untouched. A quarter of the local level is an edge
    // on any frame; texture is under it.
    const float level = (p.step == 1) ? 1.0f : (p.step == 2) ? 0.5f : 0.0f;
    const float m = max(30.0f * sigma, 0.25f * y);
    keptLuma *= textureGain(dLuma, sigma, m, p.texture * 0.5f * level);
    float3 keptChroma = float3(garrote(dChroma.r, tauChroma),
                               garrote(dChroma.g, tauChroma),
                               garrote(dChroma.b, tauChroma));
    // ⚠ Back onto the zero-luminance plane. The split puts dChroma there, but
    // three independent garrotes move it off, and that leak alone took 4% off
    // the luma residual at Color 2, Luminance 0 (tests_denoise.cpp). Without
    // this, Color still smooths luminance, which is §0.1's defect again.
    keptChroma -= luminance2020(keptChroma);

    dst[tid.xy] = float4(accum[tid.xy].rgb + keptLuma + keptChroma, 1.0f);
}
