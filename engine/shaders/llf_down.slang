// Local Laplacian, step 2 — the input's own Gaussian pyramid.
//
// Burt & Adelson (1983), the pyramid Paris et al. §5.1 says it uses. This is
// the pyramid whose coefficients supply g: for every output coefficient at
// (x, y, level), g is this pyramid's value there, and it is what decides which
// two of the precomputed remappings get interpolated (Aubry et al., step 4i).

#include "ops/llf_ops.slang"

struct LlfDownParams
{
    uint2 outSize;
    uint2 inSize;
};

[shader("compute")]
[numthreads(16, 16, 1)]
void llfDown(uint3 tid : SV_DispatchThreadID,
             uniform Texture2D<float>   src,
             uniform RWTexture2D<float> dst,
             uniform ConstantBuffer<LlfDownParams> p)
{
    if (tid.x >= p.outSize.x || tid.y >= p.outSize.y) return;

    const int2 c = int2(tid.xy) * 2;
    const int2 hi = int2(p.inSize) - 1;

    float sum = 0.0f;
    for (int dy = -2; dy <= 2; ++dy)
    {
        const int sy = clamp(c.y + dy, 0, hi.y);
        for (int dx = -2; dx <= 2; ++dx)
        {
            const int sx = clamp(c.x + dx, 0, hi.x);
            sum += kBurt[dy + 2] * kBurt[dx + 2] * src[int2(sx, sy)];
        }
    }

    dst[tid.xy] = sum;
}
