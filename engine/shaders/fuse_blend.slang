// Exposure fusion, steps 3 and 4 — blend one pyramid level and collapse into it.
//
// Mertens et al. section 3.2:
//
//     L{R}^l = sum_k G{W_k}^l * L{I_k}^l
//
// each level of the output Laplacian pyramid is the weighted average of the
// inputs' Laplacian coefficients, weighted by the *Gaussian* pyramid of the
// weight at the same level. The collapse is folded in here, so the output
// pyramid is never written out: out_l = blended_l + expand(out_{l+1}).
//
// Why a pyramid at all, in their words: blending intensities directly leaves
// seams wherever the weights vary quickly, and pre-smoothing the weights trades
// the seams for halos. "Multiresolution blending is quite effective at avoiding
// seams, because it blends image features instead of intensities."

#include "ops/llf_ops.slang"

struct FuseBlendParams
{
    uint2 size;
    uint2 coarseSize;
    int   images;
    int   residual;    // nonzero at the coarsest level: no Laplacian, no collapse
    float _pad[2];
};

float fusePick(int slot, float4 a, float4 b)
{
    const float4 v = (slot < 4) ? a : b;
    const int k = slot & 3;
    if (k == 0) return v.x;
    if (k == 1) return v.y;
    if (k == 2) return v.z;
    return v.w;
}

[shader("compute")]
[numthreads(16, 16, 1)]
void fuseBlend(uint3 tid : SV_DispatchThreadID,
               uniform Texture2D<float4>  imageLo,     // simulated images, this level
               uniform Texture2D<float4>  imageHi,
               uniform Texture2D<float4>  weightLo,    // weights, this level
               uniform Texture2D<float4>  weightHi,
               uniform Texture2D<float4>  coarseLo,    // simulated images, level + 1
               uniform Texture2D<float4>  coarseHi,
               uniform Texture2D<float>   coarseOut,   // blended, level + 1
               uniform RWTexture2D<float> dst,
               uniform ConstantBuffer<FuseBlendParams> p)
{
    if (tid.x >= p.size.x || tid.y >= p.size.y) return;

    const float4 iLo = imageLo[tid.xy];
    const float4 iHi = imageHi[tid.xy];
    const float4 wLo = weightLo[tid.xy];
    const float4 wHi = weightHi[tid.xy];

    // The coarsest level carries the residual whole — that is what makes this a
    // reconstruction and not just a high-pass sum.
    if (p.residual != 0)
    {
        float sum = 0.0f;
        for (int s = 0; s < 8; ++s)
        {
            if (s >= p.images) break;
            sum += fusePick(s, wLo, wHi) * fusePick(s, iLo, iHi);
        }
        dst[tid.xy] = sum;
        return;
    }

    int3 cx, cy; float3 wx, wy;
    llfExpand1D(int(tid.x), cx, wx);
    llfExpand1D(int(tid.y), cy, wy);

    const int2 hi = int2(p.coarseSize) - 1;

    float4 upLo = float4(0.0f, 0.0f, 0.0f, 0.0f);
    float4 upHi = float4(0.0f, 0.0f, 0.0f, 0.0f);
    float  upOut = 0.0f;

    for (int b = 0; b < 3; ++b)
    {
        const int sy = clamp(cy[b], 0, hi.y);
        for (int a = 0; a < 3; ++a)
        {
            const int sx = clamp(cx[a], 0, hi.x);
            const float w = wx[a] * wy[b];
            if (w == 0.0f) continue;

            const int2 q = int2(sx, sy);
            upLo  += w * coarseLo[q];
            upHi  += w * coarseHi[q];
            upOut += w * coarseOut[q];
        }
    }

    float sum = 0.0f;
    for (int s = 0; s < 8; ++s)
    {
        if (s >= p.images) break;
        const float laplacian = fusePick(s, iLo, iHi) - fusePick(s, upLo, upHi);
        sum += fusePick(s, wLo, wHi) * laplacian;
    }

    dst[tid.xy] = sum + upOut;
}
