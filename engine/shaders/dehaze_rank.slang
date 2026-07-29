// Dehaze, steps 2 and 3 — a separable rank filter.
//
// The patch operation in Eq. (5) is a 15 x 15 minimum, and a minimum over a box
// is separable exactly the way a sum is: the minimum of row minima is the
// minimum of the block. Thirty taps instead of two hundred and twenty-five, and
// the result is not an approximation of the square patch, it *is* the square
// patch.
//
// The same kernel does the maximum, which the guided-filter paper (TPAMI 35,
// 2013, section 5) calls for before refining the transmission: "we first apply
// a max filter to counteract the morphological effects of the min filter". A
// min filter dilates dark regions by the patch radius, so the transmission map
// it produces is systematically wide around every dark object; the max filter
// of the same size puts the boundary back where the picture has it.

struct DehazeRankParams
{
    uint2 size;
    int   radius;
    int   horizontal;   // nonzero for the x pass
    int   maximum;      // nonzero for max, zero for min
    int   _pad[3];
};

[shader("compute")]
[numthreads(16, 16, 1)]
void dehazeRank(uint3 tid : SV_DispatchThreadID,
                uniform Texture2D<float>   src,
                uniform RWTexture2D<float> dst,
                uniform ConstantBuffer<DehazeRankParams> p)
{
    if (tid.x >= p.size.x || tid.y >= p.size.y) return;

    const int2 hi = int2(p.size) - 1;
    const int2 step = (p.horizontal != 0) ? int2(1, 0) : int2(0, 1);

    float acc = src[tid.xy];
    for (int d = -p.radius; d <= p.radius; ++d)
    {
        const int2 q = clamp(int2(tid.xy) + step * d, int2(0, 0), hi);
        const float v = src[q];
        acc = (p.maximum != 0) ? max(acc, v) : min(acc, v);
    }

    dst[tid.xy] = acc;
}
