// Per-pixel denoise strengths for the masked chain.
//
// Reference: research/denoise-masked.md §2.1. A wavelet shrink's parameter is
// its threshold, already a per-pixel quantity, so a layer's coverage can scale
// it the way it scales every pointwise local control (research/masking.md §2b):
//
//     β(p) = Σ_L  layerDenoise[L] · coverage_L(p)
//
// Reads the refined folds, not the components, so a guided-feathered layer
// denoises where the photographer sees its coverage.

#include "ops/coverage.slang"

struct DenoiseFieldParams
{
    uint2 size;
    int   layerMask[8];    // which fold ends each layer, as develop:linear
    int   layerCount;
    int   _pad;
    float layerLuma[8];    // β_luma per layer, 0..4
    float layerChroma[8];  // β_chroma per layer, 0..4
};

[shader("compute")]
[numthreads(16, 16, 1)]
void denoiseField(uint3 tid : SV_DispatchThreadID,
                  uniform Texture2D<float>    mask0,
                  uniform Texture2D<float>    mask1,
                  uniform Texture2D<float>    mask2,
                  uniform Texture2D<float>    mask3,
                  uniform Texture2D<float>    mask4,
                  uniform Texture2D<float>    mask5,
                  uniform Texture2D<float>    mask6,
                  uniform Texture2D<float>    mask7,
                  uniform RWTexture2D<float2> dst,
                  uniform ConstantBuffer<DenoiseFieldParams> p)
{
    if (tid.x >= p.size.x || tid.y >= p.size.y) return;

    float2 beta = float2(0.0f, 0.0f);
    for (int L = 0; L < p.layerCount; ++L)
    {
        const float a = saturate(coverage(p.layerMask[L], tid.xy,
                                          mask0, mask1, mask2, mask3,
                                          mask4, mask5, mask6, mask7));
        beta += a * float2(p.layerLuma[L], p.layerChroma[L]);
    }

    // The sliders' range. Overlapping layers add, and four is already a
    // threshold that clears every scale it touches.
    dst[tid.xy] = float2(clamp(beta.x, 0.0f, 4.0f), clamp(beta.y, 0.0f, 4.0f));
}
