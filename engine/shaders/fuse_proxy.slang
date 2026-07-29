// Exposure fusion, step 1 — scene-linear light into the domain the method is
// defined in.
//
// Simulated Exposure Fusion (Hessel & Morel, WACV 2020) works on an intensity
// in [0,1] taken from an ordinary photograph. Everything in it assumes that
// range: the restrained-range centres walk across it, well-exposedness is a
// Gaussian at 0.5, and the count of darkened images comes from the median.
// Orion carries unbounded scene-linear light, so something has to map one to
// the other.
//
// A logistic in log2 is that mapping, and the choice is not free — see
// research/exposure-fusion.md. Raw normalised log stretches the shadow axis, so
// the median falls, so the plan allocates nearly every simulated image to the
// brightened side, and the weights end up reading the sensor's own noise floor
// as underexposed content that wants lifting. A sigmoid compresses the shadows
// back and puts middle grey at 0.5, which is where the paper's constants were
// validated. AgX is itself a sigmoid in log2, so this is a cheap stand-in for
// the display transform rather than an unrelated curve.
//
// Also does the subsampling. Fusion is a large-scale tonal move — the fine
// detail rides through untouched because only a gain is applied at the end —
// so the pyramid runs on a quarter-resolution proxy and nothing is lost that
// this filter was ever going to affect.

struct FuseProxyParams
{
    uint2 outSize;
    uint2 inSize;
    int   scale;
    float slope;      // stops per unit of t, at middle grey
    float _pad[2];
};

float fuseLuma(float3 c)
{
    return max(dot(c, float3(0.2627f, 0.6780f, 0.0593f)), 1e-8f);
}

/// Middle grey to 0.5, monotone, bounded, and symmetric in stops.
float fuseForward(float y, float slope)
{
    const float ev = log2(y / 0.18f) / max(slope, 1e-4f);
    return saturate(0.5f + 0.5f * tanh(ev));
}

[shader("compute")]
[numthreads(16, 16, 1)]
void fuseProxy(uint3 tid : SV_DispatchThreadID,
               uniform Texture2D<float4>  src,
               uniform RWTexture2D<float> dst,
               uniform ConstantBuffer<FuseProxyParams> p)
{
    if (tid.x >= p.outSize.x || tid.y >= p.outSize.y) return;

    const int s = max(p.scale, 1);
    const int2 origin = int2(tid.xy) * s;

    // Average the proxy, not the light: a mean of scene-linear values inside
    // the block is dominated by its brightest pixel, which would put the block
    // in the wrong place on a curve whose whole job is to say where it sits.
    float sum = 0.0f;
    float count = 0.0f;
    for (int dy = 0; dy < s; ++dy)
    {
        const int y = origin.y + dy;
        if (y >= int(p.inSize.y)) continue;
        for (int dx = 0; dx < s; ++dx)
        {
            const int x = origin.x + dx;
            if (x >= int(p.inSize.x)) continue;
            sum += fuseForward(fuseLuma(src[int2(x, y)].rgb), p.slope);
            count += 1.0f;
        }
    }

    dst[tid.xy] = sum / max(count, 1.0f);
}
