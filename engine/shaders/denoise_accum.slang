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
// ‖W_j‖₂ for the 5x5 starlet is [0.8907, 0.2007, 0.0855, 0.0412, 0.0202] —
// noise falls away fast at coarse scales, which is why a single threshold
// across scales over-smooths the fine ones and under-smooths the coarse.
//
// Shrinkage is the non-negative garrote. Soft thresholding biases every
// surviving coefficient down by τ and visibly dulls edges; hard thresholding
// leaves a discontinuity at τ that shows as speckle. The garrote is continuous
// like soft and asymptotically unbiased like hard.

struct ShrinkParams
{
    uint2 size;
    float noiseA;      // Poisson term
    float noiseB;      // Gaussian term
    float scaleNorm;   // ‖W_j‖₂ for this scale
    float strength;    // user control, scales β
    float chromaBoost; // chroma tolerates more smoothing than luma
    float _pad;
};

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

[shader("compute")]
[numthreads(16, 16, 1)]
void atrousShrink(uint3 tid : SV_DispatchThreadID,
                  uniform Texture2D<float4>   fine,     // c_j
                  uniform Texture2D<float4>   coarse,   // c_{j+1}
                  uniform Texture2D<float4>   accum,    // running result
                  uniform RWTexture2D<float4> dst,
                  uniform ConstantBuffer<ShrinkParams> p)
{
    if (tid.x >= p.size.x || tid.y >= p.size.y) return;

    const float3 cj = fine[tid.xy].rgb;
    const float3 cj1 = coarse[tid.xy].rgb;
    const float3 detail = cj - cj1;

    if (p.strength <= 0.0f)
    {
        dst[tid.xy] = float4(accum[tid.xy].rgb + detail, 1.0f);
        return;
    }

    // Noise level at this pixel's own brightness, at this scale.
    const float y = max(luminance2020(cj1), 0.0f);
    const float sigma = p.scaleNorm * sqrt(max(p.noiseA * y + p.noiseB, 0.0f));

    // Split into luma and chroma. Chroma noise is the ugly kind — coloured
    // blotches — and chroma detail is scarce, so it tolerates a much harder
    // threshold than luma without visible loss.
    const float dLuma = luminance2020(detail);
    const float3 dChroma = detail - dLuma;

    const float tauLuma = p.strength * sigma;
    const float tauChroma = p.strength * sigma * p.chromaBoost;

    const float keptLuma = garrote(dLuma, tauLuma);
    const float3 keptChroma = float3(garrote(dChroma.r, tauChroma),
                                     garrote(dChroma.g, tauChroma),
                                     garrote(dChroma.b, tauChroma));

    dst[tid.xy] = float4(accum[tid.xy].rgb + keptLuma + keptChroma, 1.0f);
}
