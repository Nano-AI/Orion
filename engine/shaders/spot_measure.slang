// Spot removal, step 1 — the boundary difference, one number per spot.
//
// research/spot-removal.md §3. Healing takes the source's detail and the
// destination's tone, and the tone difference is what this measures: the mean
// of (destination − source) around the rim of the disc.
//
// That mean is the **zeroth-order term** of Farbman et al.'s mean-value
// interpolant (SIGGRAPH 2009), which is itself the closed form of Pérez et
// al.'s Poisson solve (SIGGRAPH 2003). The three agree exactly when the
// boundary difference is constant, and on the smooth backgrounds sensor dust is
// visible against it very nearly is. ⚠ It is a truncation, and the bounded
// failure it buys is written down in UNSOURCED.md §21 rather than left to be
// discovered.
//
// One thread per spot, writing an N x 1 texture. Doing it here rather than
// inside the apply pass is the difference between sampling the rim once per
// spot and once per pixel of every spot.

struct SpotMeasureParams
{
    uint2 size;        // the image these coordinates are normalized against
    int   count;       // live spots
    int   samples;     // rim samples per spot

    // Destination centre, then source centre, both normalized. Radius is in
    // normalized *x*, converted to pixels against the frame's width so a spot
    // is round rather than an ellipse on a non-square frame — the same trap the
    // brush nib fell into.
    float4 spots[64];  // xy destination, zw source
    float4 shape[64];  // x radius, y feather, z heal (0 clone), w unused
};

[shader("compute")]
[numthreads(64, 1, 1)]
void spotMeasure(uint3 tid : SV_DispatchThreadID,
                 uniform Texture2D<float4>   src,
                 uniform RWTexture2D<float4> dst,   // N x 1, the corrections
                 uniform ConstantBuffer<SpotMeasureParams> p)
{
    const int i = int(tid.x);
    if (i >= p.count) return;

    const float2 dest   = p.spots[i].xy;
    const float2 source = p.spots[i].zw;
    const float  radius = max(p.shape[i].x, 1e-6f);

    // Sample just outside the rim on the destination side and at the matching
    // place on the source side. Just outside, because the pixels *inside* the
    // destination disc are the ones being replaced — measuring the difference
    // there would be measuring the dust.
    const float ring = radius * 1.05f;
    const float2 toPx = float2(p.size);
    const float2 aspect = float2(1.0f, toPx.x / max(toPx.y, 1.0f));

    const int n = clamp(p.samples, 4, 64);
    float3 sum = float3(0.0f, 0.0f, 0.0f);
    float taken = 0.0f;

    for (int k = 0; k < n; ++k)
    {
        const float t = 6.28318530718f * (float(k) + 0.5f) / float(n);
        const float2 offset = float2(cos(t), sin(t)) * ring * aspect;

        const float2 dq = dest + offset;
        const float2 sq = source + offset;

        // A rim point off the frame contributes nothing rather than clamping to
        // an edge pixel, which would drag the mean toward whatever happens to
        // be at the border.
        if (dq.x < 0.0f || dq.x > 1.0f || dq.y < 0.0f || dq.y > 1.0f) continue;
        if (sq.x < 0.0f || sq.x > 1.0f || sq.y < 0.0f || sq.y > 1.0f) continue;

        const int2 dp = int2(clamp(dq * toPx, float2(0.0f, 0.0f), toPx - 1.0f));
        const int2 sp = int2(clamp(sq * toPx, float2(0.0f, 0.0f), toPx - 1.0f));

        sum += src[dp].rgb - src[sp].rgb;
        taken += 1.0f;
    }

    // No usable rim — a spot dragged mostly off the frame. Zero is the clone
    // answer, which is the safe one: it pastes the source unmodified rather
    // than shifting it by a number derived from two or three stray pixels.
    const float3 correction = (taken > 0.0f) ? sum / taken : float3(0.0f, 0.0f, 0.0f);

    dst[uint2(tid.x, 0)] = float4(correction, taken);
}
