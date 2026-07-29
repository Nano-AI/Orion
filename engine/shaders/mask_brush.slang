// Brush masks — a stroke as a list of dab centres.
//
// research/masking.md §1 and §3. The stroke is stored parametrically in
// normalized coordinates and rasterized on demand, which is what makes it a few
// kilobytes instead of the 24–120 MB a raster mask costs at 24–60 MP — and what
// lets it survive a crop or a rotation exactly, since each centre goes through
// the same transform the image does rather than being resampled.
//
// **One radius for the whole mask, not one per dab.** That is the research's
// shape and it is also what keeps a stroke cheap to store and to reason about.
//
// Output is R16Float. Not decoration: a low-flow brush is *meant* to be built
// up from many overlapping dabs, and eight bits bands visibly once a few dozen
// of them stack. Same class of error as the 8-bit resize that quietly cost the
// export its depth.

struct BrushParams
{
    uint2  size;
    int    count;        // dabs this pass
    int    accumulate;   // 0 starts from empty, 1 builds on `src`

    float  radius;       // normalized, one per mask
    float  flow;         // 0..1, how much one dab lays down
    float  hardness;     // 0 soft, 1 hard-edged
    float  _pad;

    float2 dabs[256];
};

/// Perlin's smootherstep, the same falloff the gradient masks use.
///
/// Shared on purpose: a feather that behaved differently under a brush than
/// under a gradient would be two things to fix and two things to explain.
/// Perlin, "Improving Noise", ACM TOG 21(3), 2002.
float brushFalloff(float t)
{
    const float x = saturate(t);
    return x * x * x * (x * (x * 6.0f - 15.0f) + 10.0f);
}

/// One dab's coverage at normalized distance `d` from its centre, where 1 is
/// the rim.
///
/// `hardness` moves where the falloff starts, exactly as the radial gradient's
/// feather does — hardness 0 ramps from the centre, hardness 1 is a disc with
/// only the last sliver softened. The sliver is deliberate: a truly hard circle
/// aliases into a visible staircase at any radius, and the mask is multiplied
/// into a parameter where that staircase becomes a banded edit.
float dabCoverage(float d, float hardness)
{
    const float h = clamp(hardness, 0.0f, 0.98f);
    return 1.0f - brushFalloff((d - h) / max(1.0f - h, 1e-3f));
}

[shader("compute")]
[numthreads(16, 16, 1)]
void maskBrush(uint3 tid : SV_DispatchThreadID,
               uniform Texture2D<float> src,
               uniform RWTexture2D<float> dst,
               uniform ConstantBuffer<BrushParams> p)
{
    if (tid.x >= p.size.x || tid.y >= p.size.y) return;

    // Pixel centres, in the normalized coordinates the mask is stored in, so a
    // stroke survives a resize and an export matches the preview it was drawn
    // on. The same convention mask_gradient.slang uses.
    const float2 q = (float2(tid.xy) + 0.5f) / float2(p.size);

    float alpha = (p.accumulate != 0) ? src[tid.xy] : 0.0f;

    const float r = max(p.radius, 1e-6f);
    const float flow = saturate(p.flow);

    for (int i = 0; i < p.count; ++i)
    {
        const float2 e = q - p.dabs[i];

        // Reject on the square first. A dab covers a disc, so most pixels miss
        // most dabs, and this turns the common case into two compares instead
        // of a square root.
        if (abs(e.x) > r || abs(e.y) > r) continue;

        const float d = length(e) / r;

        // Speed, not correctness: `brushFalloff` saturates, so a pixel past the
        // rim already gets zero coverage and adds nothing. Deleting this line
        // changes no output — checked by mutation, and the suite correctly does
        // not flinch. It is here because the square reject above admits the
        // corners of the bounding box, which are 1.41 radii out.
        if (d >= 1.0f) continue;

        // Source-over, not addition. Two dabs at full flow must give full
        // coverage and not two — adding would let a slow hand over the same
        // spot drive the alpha past 1 and clip, which reads as the brush
        // getting stronger the longer you hover.
        alpha += flow * dabCoverage(d, p.hardness) * (1.0f - alpha);
    }

    dst[tid.xy] = saturate(alpha);
}
