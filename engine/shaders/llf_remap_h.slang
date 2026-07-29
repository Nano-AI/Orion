// Local Laplacian — the remapping and the horizontal half of the halving.
//
// The Burt kernel is separable, and this is the half that matters. The fused
// 5x5 version evaluates the remapping at all twenty-five taps for each of four
// gammas — a hundred per output pixel — and the per-node profile put those four
// nodes at 47% of a clarity drag. Split, the horizontal pass evaluates five
// taps by four gammas and the vertical pass evaluates none: the remapping has
// already happened.
//
// The cost is an intermediate at half width and full height. Whether that trade
// is worth it is a measurement, not an argument — see research/local-laplacian.md.

#include "ops/llf_ops.slang"

struct LlfRemapHParams
{
    uint2 outSize;     // (width + 1) / 2, height
    uint2 inSize;
    float gamma0;
    float gammaStep;
    float sigmaR;
    float alpha;
    float noiseLo;
    float noiseHi;
    float _pad[2];
};

[shader("compute")]
[numthreads(16, 16, 1)]
void llfRemapH(uint3 tid : SV_DispatchThreadID,
               uniform Texture2D<float>    src,
               uniform RWTexture2D<float4> dst,
               uniform ConstantBuffer<LlfRemapHParams> p)
{
    if (tid.x >= p.outSize.x || tid.y >= p.outSize.y) return;

    const int cx = int(tid.x) * 2;
    const int y  = clamp(int(tid.y), 0, int(p.inSize.y) - 1);
    const int hi = int(p.inSize.x) - 1;

    float4 sum = float4(0.0f, 0.0f, 0.0f, 0.0f);
    for (int dx = -2; dx <= 2; ++dx)
    {
        const float w = kBurt[dx + 2];
        const float i = src[int2(clamp(cx + dx, 0, hi), y)];

        for (int k = 0; k < 4; ++k)
        {
            const float g = p.gamma0 + float(k) * p.gammaStep;
            sum[k] += w * llfRemap(i, g, p.sigmaR, p.alpha, p.noiseLo, p.noiseHi);
        }
    }

    dst[tid.xy] = sum;
}
