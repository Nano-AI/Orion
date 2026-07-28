// RCD pass 3 — red and blue, producing full camera-space RGB.
//
// Chroma varies slowly compared with luminance, so red and blue are
// interpolated in the colour-difference domain (C − G) rather than directly.
// Interpolating the difference and adding green back preserves the edge detail
// green already resolved, and is why this costs so little for how good it looks.

#include "cfa.slang"

struct RBParams
{
    uint2 size;
    uint  filters;
};

// Mean colour difference (sample − green) over the 3x3 neighbourhood, counting
// only sites that carry `want`. On a Bayer grid that is 2 or 4 taps depending
// on where the centre sits, so the divisor has to be accumulated, not assumed.
float gatherDiff(Texture2D<float> cfa,
                 Texture2D<float> green,
                 int2 centre, uint2 size, uint filters, uint want)
{
    float sum = 0.0f;
    float count = 0.0f;

    [unroll]
    for (int dy = -1; dy <= 1; ++dy)
    {
        [unroll]
        for (int dx = -1; dx <= 1; ++dx)
        {
            const int2 q = clampToImage(centre + int2(dx, dy), size);
            if (cfaChannelRGB(uint2(q), filters) == want)
            {
                sum += cfa[q] - green[q];
                count += 1.0f;
            }
        }
    }

    return (count > 0.0f) ? (sum / count) : 0.0f;
}

[shader("compute")]
[numthreads(16, 16, 1)]
void rcdRedBlue(uint3 tid : SV_DispatchThreadID,
                uniform Texture2D<float>    cfa,
                uniform Texture2D<float>    green,
                uniform RWTexture2D<float4> rgb,
                uniform ConstantBuffer<RBParams> p)
{
    if (tid.x >= p.size.x || tid.y >= p.size.y) return;

    const int2  c  = int2(tid.xy);
    const uint2 sz = p.size;
    const float g  = green[tid.xy];

    const uint here = cfaChannelRGB(tid.xy, p.filters);

    float r, b;
    if (here == 0)          // red site — red measured, blue interpolated
    {
        r = cfa[tid.xy];
        b = g + gatherDiff(cfa, green, c, sz, p.filters, 2);
    }
    else if (here == 2)     // blue site
    {
        b = cfa[tid.xy];
        r = g + gatherDiff(cfa, green, c, sz, p.filters, 0);
    }
    else                    // green site — both interpolated
    {
        r = g + gatherDiff(cfa, green, c, sz, p.filters, 0);
        b = g + gatherDiff(cfa, green, c, sz, p.filters, 2);
    }

    rgb[tid.xy] = float4(max(r, 0.0f), max(g, 0.0f), max(b, 0.0f), 1.0f);
}
