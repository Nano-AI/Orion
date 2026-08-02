// Local Laplacian, step 3 — four remapped images, one dispatch, one level down.
//
// This is Aubry et al.'s substitution: rather than remapping the image once per
// output *pixel* (Paris et al. Algorithm 1, about a minute per megapixel), remap
// it once per *intensity* gamma_j and interpolate. Step 3 of their algorithm.
//
// Two things are fused here on purpose. The remapped full-resolution image is
// never stored — it is a point-wise function of the luminance plane, so level
// zero of each remapped pyramid is recomputed in llf_collapse0 for the cost of
// an ALU op rather than a 194 MB round trip. And four gammas are produced at
// once into one RGBA target, so the full-resolution luminance is read once for
// four of them instead of four times.
//
// The 5x5 tap footprint at half resolution reads each source pixel about six
// times, but those reads are neighbors: they hit cache, not DRAM.

#include "ops/llf_ops.slang"

struct LlfRemapParams
{
    uint2 outSize;
    uint2 inSize;
    float gamma0;      // the first of this texture's four gammas
    float gammaStep;
    float sigmaR;
    float alpha;
    float noiseLo;     // Paris et al. §5.2, tau's smoothstep bounds
    float noiseHi;
    float _pad[2];
};

[shader("compute")]
[numthreads(16, 16, 1)]
void llfRemapDown(uint3 tid : SV_DispatchThreadID,
                  uniform Texture2D<float>    src,
                  uniform RWTexture2D<float4> dst,
                  uniform ConstantBuffer<LlfRemapParams> p)
{
    if (tid.x >= p.outSize.x || tid.y >= p.outSize.y) return;

    const int2 c  = int2(tid.xy) * 2;
    const int2 hi = int2(p.inSize) - 1;

    float4 sum = float4(0.0f, 0.0f, 0.0f, 0.0f);
    for (int dy = -2; dy <= 2; ++dy)
    {
        const int sy = clamp(c.y + dy, 0, hi.y);
        for (int dx = -2; dx <= 2; ++dx)
        {
            const int sx = clamp(c.x + dx, 0, hi.x);
            const float w = kBurt[dy + 2] * kBurt[dx + 2];
            const float i = src[int2(sx, sy)];

            // One fetch, four remappings. The pyramid must be built from the
            // remapped image, so the remap happens before the blur and not
            // after it.
            for (int k = 0; k < 4; ++k)
            {
                const float g = p.gamma0 + float(k) * p.gammaStep;
                sum[k] += w * llfRemap(i, g, p.sigmaR, p.alpha,
                                       p.noiseLo, p.noiseHi);
            }
        }
    }

    dst[tid.xy] = sum;
}
