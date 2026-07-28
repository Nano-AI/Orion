// À-trous wavelet denoise, blur pass.
//
// Reference: research/deep-research-2026-07-27.md §2 (Starck et al., 2007).
//
// The starlet transform's scaling filter is the 5x5 separable B-spline
//
//     h = [1/16, 1/4, 3/8, 1/4, 1/16]
//
// applied "with holes": at scale j the taps are spaced 2^j apart rather than
// the kernel being resampled. That is what makes the transform
// translation-invariant, and translation invariance is what stops shrinkage
// producing ringing around edges.

#include "cfa.slang"

struct AtrousParams
{
    uint2 size;
    int   step;      // 2^j
    int   _pad;
};

[shader("compute")]
[numthreads(16, 16, 1)]
void atrousBlur(uint3 tid : SV_DispatchThreadID,
                uniform Texture2D<float4>   src,
                uniform RWTexture2D<float4> dst,
                uniform ConstantBuffer<AtrousParams> p)
{
    if (tid.x >= p.size.x || tid.y >= p.size.y) return;

    const float w[5] = { 0.0625f, 0.25f, 0.375f, 0.25f, 0.0625f };
    const int2 c = int2(tid.xy);

    float3 sum = float3(0.0f, 0.0f, 0.0f);

    [unroll]
    for (int j = 0; j < 5; ++j)
    {
        [unroll]
        for (int i = 0; i < 5; ++i)
        {
            const int2 q = clampToImage(
                c + int2((i - 2) * p.step, (j - 2) * p.step), p.size);
            sum += src[q].rgb * (w[i] * w[j]);
        }
    }

    dst[tid.xy] = float4(sum, 1.0f);
}
