// Guided feathering, step 3 — reconstruct the refined coverage.
//
// q = mean(a)*I + mean(b), evaluated at full resolution against the guide the
// photograph produced. He, Sun & Tang (ECCV 2010 / TPAMI 2013);
// research/masking.md §4.
//
// The coefficients arrive on the subsampled grid and are lifted bilinearly, per
// He & Sun (2015), "Fast Guided Filter" (arXiv:1505.00996): a and b are already
// averages over a window, so they vary slowly and sampling them densely is
// wasted work. Same lift develop_linear.slang uses for the self-guided chain.
//
// ⚠ **The output is clamped to [0,1] and the raw mask is blended back in.**
// Neither is decoration:
//
//   - q is a *linear function of the guide* and nothing constrains it to the
//     range coverage is defined on. Over a specular highlight inside an
//     otherwise-covered region it overshoots past 1, and past 1 the adjustment
//     is applied harder than the photographer asked for — a mask that is more
//     than fully on. Below 0 it inverts the edit's sign. Both are visible as a
//     bright or dark rim exactly where the filter was supposed to help.
//
//   - `strength` blends between the mask as placed and the refined one, so the
//     control degrades continuously to the old behavior at zero rather than
//     switching between two masks. At zero the node is disabled outright and
//     costs nothing but its texture.

struct MaskGuideApplyParams
{
    uint2 size;        // full resolution
    uint2 coeffSize;   // subsampled grid the coefficients live on
    float strength;    // 0 keeps the placed mask, 1 takes the refined one
    float _pad[3];
};

/// Bilinear lift of the coefficients from their subsampled grid. Transcribed
/// from develop_linear.slang's `sampleAbBilinear` — same lift, same half-pixel
/// convention, and it has to stay the same or the two guided chains would
/// disagree about where a coefficient sits.
float2 liftAb(Texture2D<float2> ab, uint2 at, uint2 full, uint2 small)
{
    if (small.x == 0u || small.y == 0u) return ab[at];
    if (small.x == full.x && small.y == full.y) return ab[at];

    // Pixel centers on both grids, so the lift has no half-pixel drift.
    const float2 scale = float2(small) / float2(full);
    const float2 q = (float2(at) + 0.5f) * scale - 0.5f;

    const float2 maxCoord = float2(small) - 1.0f;
    const float2 clamped = clamp(q, float2(0.0f, 0.0f), maxCoord);

    const int2 i0 = int2(floor(clamped));
    const int2 i1 = min(i0 + int2(1, 1), int2(maxCoord));
    const float2 f = clamped - float2(i0);

    const float2 top = lerp(ab[int2(i0.x, i0.y)], ab[int2(i1.x, i0.y)], f.x);
    const float2 bot = lerp(ab[int2(i0.x, i1.y)], ab[int2(i1.x, i1.y)], f.x);
    return lerp(top, bot, f.y);
}

[shader("compute")]
[numthreads(16, 16, 1)]
void maskGuideApply(uint3 tid : SV_DispatchThreadID,
                    uniform Texture2D<float>   mask,   // p, as placed
                    uniform Texture2D<float2>  ab,     // a, b on the small grid
                    uniform Texture2D<float2>  guide,  // (l, l*l) at full res
                    uniform RWTexture2D<float> dst,
                    uniform ConstantBuffer<MaskGuideApplyParams> p)
{
    if (tid.x >= p.size.x || tid.y >= p.size.y) return;

    const float2 c = liftAb(ab, tid.xy, p.size, p.coeffSize);
    const float  l = guide[tid.xy].x;

    const float refined = clamp(c.x * l + c.y, 0.0f, 1.0f);
    const float placed  = mask[tid.xy];

    dst[tid.xy] = lerp(placed, refined, saturate(p.strength));
}
