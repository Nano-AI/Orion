// Local Laplacian, step 3 continued — the remapped pyramids, coarser levels.
//
// Identical to llf_down.slang but four gammas wide. Separate kernel rather than
// a generic one because the host binds concrete texture formats: R16Float for
// the input's own pyramid, RGBA16Float for a packed stack of four remappings.

#include "ops/llf_ops.slang"

struct LlfDownParams4
{
    uint2 outSize;
    uint2 inSize;
};

[shader("compute")]
[numthreads(16, 16, 1)]
void llfDownPacked(uint3 tid : SV_DispatchThreadID,
                   uniform Texture2D<float4>   src,
                   uniform RWTexture2D<float4> dst,
                   uniform ConstantBuffer<LlfDownParams4> p)
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
            sum += (kBurt[dy + 2] * kBurt[dx + 2]) * src[int2(sx, sy)];
        }
    }

    dst[tid.xy] = sum;
}
