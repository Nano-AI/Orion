// Dehaze, step 5 — the four moments the guided filter needs, subsampled.
//
// The transmission from Eq. (12) is blocky: it inherits the patch size of the
// minimum filter, so it steps where the picture does not. The authors' first
// answer was Levin's matting Laplacian; their second, in the guided filter
// papers, was to "simply filter the raw transmission map under the guidance of
// the hazy image" — visually similar, and 40 ms against 10 seconds.
//
// This is the *cross*-guided form: the guide is the image and the input is the
// transmission, where the instance already in this pipeline is self-guided. So
// four moments rather than two, packed into one target so the box blurs that
// follow filter them together.
//
// Guidance is normalized log2 luminance over the same window clarity uses, not
// scene-linear luminance. A local linear model fitted over a range spanning
// several orders of magnitude is decided entirely by its brightest pixels — and
// it puts epsilon on a scale comparable to the 1e-3 the paper reports for
// images bounded at one.

struct DehazePrepParams
{
    uint2 outSize;
    uint2 inSize;
    int   scale;
    float omega;      // Eq. (12); the paper fixes it at 0.95
    float lo;         // log2 window floor
    float invRange;
};

float dehazeGuide(float3 c, float lo, float invRange)
{
    const float y = max(dot(c, float3(0.2627f, 0.6780f, 0.0593f)), 1e-8f);
    return saturate((log2(y) - lo) * invRange);
}

[shader("compute")]
[numthreads(16, 16, 1)]
void dehazePrep(uint3 tid : SV_DispatchThreadID,
                uniform Texture2D<float4>   image,
                uniform Texture2D<float>    minmap,
                uniform RWTexture2D<float4> dst,
                uniform ConstantBuffer<DehazePrepParams> p)
{
    if (tid.x >= p.outSize.x || tid.y >= p.outSize.y) return;

    const int s = max(p.scale, 1);
    const int2 origin = int2(tid.xy) * s;

    float4 sum = float4(0.0f, 0.0f, 0.0f, 0.0f);
    float count = 0.0f;

    for (int dy = 0; dy < s; ++dy)
    {
        const int y = origin.y + dy;
        if (y >= int(p.inSize.y)) continue;
        for (int dx = 0; dx < s; ++dx)
        {
            const int x = origin.x + dx;
            if (x >= int(p.inSize.x)) continue;

            const float g = dehazeGuide(image[int2(x, y)].rgb, p.lo, p.invRange);
            // Eq. (12). omega = 0 gives t = 1 everywhere, and Eq. (16) is then
            // the identity — which is what makes the slider's zero exact.
            const float t = 1.0f - p.omega * minmap[int2(x, y)];

            sum += float4(g, g * g, t, g * t);
            count += 1.0f;
        }
    }

    dst[tid.xy] = sum / max(count, 1.0f);
}
