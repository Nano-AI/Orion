// Local Laplacian, the finest level — where the remapped image is recomputed
// rather than read.
//
// Level zero of a pyramid is the image itself, so G_0[r_j(I)] is just r_j(I) —
// a point-wise function of the luminance plane. Storing eight full-resolution
// remapped images to read each one once would be 1.5 GB of traffic to avoid an
// arithmetic expression, so this kernel evaluates the same llfRemap the
// pyramids were built with. Sharing that one function is what makes "the same"
// checkable rather than hoped for.

#include "ops/llf_ops.slang"

struct LlfCollapse0Params
{
    uint2 size;
    uint2 coarseSize;
    float gammaStep;
    int   gammaCount;
    float sigmaR;
    float alpha;
    float noiseLo;
    float noiseHi;
    float _pad[2];
};

[shader("compute")]
[numthreads(16, 16, 1)]
void llfCollapse0(uint3 tid : SV_DispatchThreadID,
                  uniform Texture2D<float>   luma,      // g and i, both
                  uniform Texture2D<float4>  coarse0,   // gammas 0-3, level 1
                  uniform Texture2D<float4>  coarse1,   // gammas 4-7
                  uniform Texture2D<float4>  coarse2,   // gammas 8-11
                  uniform Texture2D<float4>  coarse3,   // gammas 12-15
                  uniform Texture2D<float>   coarseOut, // collapsed, level 1
                  uniform RWTexture2D<float> dst,
                  uniform ConstantBuffer<LlfCollapse0Params> p)
{
    if (tid.x >= p.size.x || tid.y >= p.size.y) return;

    int3 cx, cy; float3 wx, wy;
    llfExpand1D(int(tid.x), cx, wx);
    llfExpand1D(int(tid.y), cy, wy);

    const int2 hi = int2(p.coarseSize) - 1;

    float4 up0 = float4(0.0f, 0.0f, 0.0f, 0.0f);
    float4 up1 = float4(0.0f, 0.0f, 0.0f, 0.0f);
    float4 up2 = float4(0.0f, 0.0f, 0.0f, 0.0f);
    float4 up3 = float4(0.0f, 0.0f, 0.0f, 0.0f);
    float  upOut = 0.0f;

    for (int b = 0; b < 3; ++b)
    {
        const int sy = clamp(cy[b], 0, hi.y);
        for (int a = 0; a < 3; ++a)
        {
            const int   sx = clamp(cx[a], 0, hi.x);
            const float w  = wx[a] * wy[b];
            if (w == 0.0f) continue;

            const int2 q = int2(sx, sy);
            up0   += w * coarse0[q];
            up1   += w * coarse1[q];
            up2   += w * coarse2[q];
            up3   += w * coarse3[q];
            upOut += w * coarseOut[q];
        }
    }

    // At level zero the pixel's own value is both the image sample i and the
    // Gaussian coefficient g. That is not a shortcut — G_0 is the image.
    const float v = luma[tid.xy];

    int   j;
    float frac;
    llfBracket(v, p.gammaStep, p.gammaCount, j, frac);

    const float ga = float(j)     * p.gammaStep;
    const float gb = float(j + 1) * p.gammaStep;

    const float l0 = llfRemap(v, ga, p.sigmaR, p.alpha, p.noiseLo, p.noiseHi)
                   - llfPick(j,     up0, up1, up2, up3);
    const float l1 = llfRemap(v, gb, p.sigmaR, p.alpha, p.noiseLo, p.noiseHi)
                   - llfPick(j + 1, up0, up1, up2, up3);

    dst[tid.xy] = lerp(l0, l1, frac) + upOut;
}
