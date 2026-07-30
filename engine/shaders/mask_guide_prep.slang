// Guided feathering, step 1 — the four moments, gathered onto the small grid.
//
// He, Sun & Tang, "Guided Image Filtering" (ECCV 2010; TPAMI 35(6):1397-1409,
// 2013) devote a section to refining a rough mask into an alpha matte, and name
// Photoshop's "Refine Edge" as the comparable feature. research/masking.md §4.
//
// The self-guided chain that serves highlight and shadow recovery already
// computes mean(I) and mean(I*I). Feathering needs two more — mean(p) and
// mean(I*p), where p is the mask being refined — because the coefficient is a
// covariance rather than a variance:
//
//     a = cov(I, p) / (var(I) + eps)
//     b = mean(p) - a*mean(I)
//
// That is the whole difference between the two uses, and it is why §4 is worth
// doing at all: the filter is already here, the guide is already here, and only
// the *input* changes.
//
// All four moments travel in one RGBA target so the box passes that follow
// filter them together — the same trick guide_prep.slang uses for two.
//
// ⚠ This node does the subsample as well as the gather, unlike the self-guided
// chain which spends a separate node on it. At 24 MP a full-resolution RGBA32F
// moment texture is 384 MB, and it would be read exactly once. Gathering
// straight onto the small grid costs one node and one small texture instead.

struct MaskGuidePrepParams
{
    uint2 outSize;    // subsampled dimensions
    uint2 inSize;     // full dimensions
    int   scale;      // s
    int   _pad[3];
};

[shader("compute")]
[numthreads(16, 16, 1)]
void maskGuidePrep(uint3 tid : SV_DispatchThreadID,
                   uniform Texture2D<float2>   guide,   // (l, l*l) from guidePrep
                   uniform Texture2D<float>    mask,    // p, the rough coverage
                   uniform RWTexture2D<float4> dst,     // mean I, I*I, p, I*p
                   uniform ConstantBuffer<MaskGuidePrepParams> p)
{
    if (tid.x >= p.outSize.x || tid.y >= p.outSize.y) return;

    // A box average over the s x s block, not a point sample. Point-sampling
    // aliases the variance term, and the variance is what decides where the
    // filter treats an edge as an edge — the same reasoning as guide_down.slang.
    const int s = max(p.scale, 1);
    const int2 origin = int2(tid.xy) * s;

    float4 sum = float4(0.0f, 0.0f, 0.0f, 0.0f);
    float count = 0.0f;

    for (int dy = 0; dy < s; ++dy)
    {
        for (int dx = 0; dx < s; ++dx)
        {
            const int2 q = origin + int2(dx, dy);
            if (q.x >= int(p.inSize.x) || q.y >= int(p.inSize.y)) continue;

            const float l = guide[q].x;           // log2 luminance
            const float a = mask[q];              // coverage, 0..1
            sum += float4(l, l * l, a, l * a);
            count += 1.0f;
        }
    }

    dst[tid.xy] = sum / max(count, 1.0f);
}
