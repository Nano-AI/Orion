// Guided filter, step 1.5 — subsample before filtering.
//
// Reference: He & Sun (2015), "Fast Guided Filter", arXiv:1505.00996. The same
// authors as the original guided filter, and their own answer to its cost.
//
// The insight is that a and b — the per-pixel linear coefficients the filter
// solves for — vary slowly by construction: they are averages over a window of
// radius r. Sampling them at full resolution is therefore wasted work. Compute
// them on an image subsampled by s, with the radius divided by s, and upsample
// the coefficients bilinearly. Cost falls by roughly s², and He & Sun report
// the result is visually indistinguishable at s = 4.
//
// What this replaces: box blurs with a radius of 30 running over 24 megapixels,
// four of them. That is 61 taps a pixel a pass, and it took 90 ms — five times
// the budget for a slider you drag. The comment in box_blur.slang claimed the
// filter was O(1) in the radius; the loop in it was not.

struct GuideDownParams
{
    uint2 outSize;    // subsampled dimensions
    uint2 inSize;     // full dimensions
    int   scale;      // s
    int   _pad[3];
};

[shader("compute")]
[numthreads(16, 16, 1)]
void guideDown(uint3 tid : SV_DispatchThreadID,
               uniform Texture2D<float2>   src,
               uniform RWTexture2D<float2> dst,
               uniform ConstantBuffer<GuideDownParams> p)
{
    if (tid.x >= p.outSize.x || tid.y >= p.outSize.y) return;

    // A box average over the s x s block, not a single sample. Point-sampling
    // would alias the variance term, and the variance is what decides where the
    // filter treats an edge as an edge.
    const int s = max(p.scale, 1);
    const int2 origin = int2(tid.xy) * s;

    float2 sum = float2(0.0f, 0.0f);
    float count = 0.0f;

    for (int dy = 0; dy < s; ++dy)
    {
        for (int dx = 0; dx < s; ++dx)
        {
            const int2 q = origin + int2(dx, dy);
            if (q.x >= int(p.inSize.x) || q.y >= int(p.inSize.y)) continue;
            sum += src[q];
            count += 1.0f;
        }
    }

    dst[tid.xy] = sum / max(count, 1.0f);
}
