// Separable box blur over a two-channel target.
//
// The guided filter needs four box filters; packing two channels per pass and
// running horizontally then vertically turns that into four dispatches instead
// of eight. A box filter is also the reason the guided filter is O(1) per pixel
// regardless of radius — the same property that makes it practical here.

#include "cfa.slang"

struct BoxBlurParams
{
    uint2 size;
    int   radius;
    int   horizontal;   // 1 for the horizontal pass, 0 for vertical
};

[shader("compute")]
[numthreads(16, 16, 1)]
void boxBlur(uint3 tid : SV_DispatchThreadID,
             uniform Texture2D<float2>   src,
             uniform RWTexture2D<float2> dst,
             uniform ConstantBuffer<BoxBlurParams> p)
{
    if (tid.x >= p.size.x || tid.y >= p.size.y) return;

    const int2 centre = int2(tid.xy);
    const int2 step = (p.horizontal != 0) ? int2(1, 0) : int2(0, 1);

    float2 sum = float2(0.0f, 0.0f);
    float count = 0.0f;

    for (int i = -p.radius; i <= p.radius; ++i)
    {
        const int2 q = clampToImage(centre + step * i, p.size);
        sum += src[q];
        count += 1.0f;
    }

    dst[tid.xy] = sum / max(count, 1.0f);
}
