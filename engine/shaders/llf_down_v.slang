// Local Laplacian — the vertical half of a separable halving, on a packed stack.
//
// Pairs with llf_remap_h.slang. No remapping here: it happened on the way in,
// which is the entire point of splitting the two.

#include "ops/llf_ops.slang"

struct LlfDownVParams
{
    uint2 outSize;
    uint2 inSize;
};

[shader("compute")]
[numthreads(16, 16, 1)]
void llfDownV(uint3 tid : SV_DispatchThreadID,
              uniform Texture2D<float4>   src,
              uniform RWTexture2D<float4> dst,
              uniform ConstantBuffer<LlfDownVParams> p)
{
    if (tid.x >= p.outSize.x || tid.y >= p.outSize.y) return;

    const int x  = clamp(int(tid.x), 0, int(p.inSize.x) - 1);
    const int cy = int(tid.y) * 2;
    const int hi = int(p.inSize.y) - 1;

    float4 sum = float4(0.0f, 0.0f, 0.0f, 0.0f);
    for (int dy = -2; dy <= 2; ++dy)
    {
        sum += kBurt[dy + 2] * src[int2(x, clamp(cy + dy, 0, hi))];
    }

    dst[tid.xy] = sum;
}
