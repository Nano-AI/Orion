// Spot removal, step 2 — paste the source, corrected.
//
// research/spot-removal.md. Clone takes the source's pixels; heal takes the
// source's *detail* and the destination's tone, which is the same paste plus
// the constant `spot_measure.slang` worked out.
//
// ⚠ In scene-linear light, before the tone controls. A correction that is
// additive here is additive in the physical quantity the sensor measured; the
// same offset applied after AgX would be a different correction to a highlight
// than to a midtone, for reasons unrelated to the dust.

#include "ops/mask_ops.slang"

struct SpotApplyParams
{
    uint2 size;
    int   count;
    int   _pad;

    float4 spots[64];  // xy destination, zw source
    float4 shape[64];  // x radius, y feather, z heal (0 clone), w unused
};

[shader("compute")]
[numthreads(16, 16, 1)]
void spotApply(uint3 tid : SV_DispatchThreadID,
               uniform Texture2D<float4>   src,
               uniform Texture2D<float4>   corrections,  // N x 1 from step 1
               uniform RWTexture2D<float4> dst,
               uniform ConstantBuffer<SpotApplyParams> p)
{
    if (tid.x >= p.size.x || tid.y >= p.size.y) return;

    const float2 toPx = float2(p.size);
    const float2 q = (float2(tid.xy) + 0.5f) / toPx;

    float4 out = src[tid.xy];

    // Aspect-corrected, so a spot is a disc on the picture rather than an
    // ellipse on any frame that is not square. The radius is in normalized x
    // and the y term is scaled by the frame's aspect — the same fix the brush
    // nib needed after it shipped stretching under the Size slider.
    const float2 aspect = float2(1.0f, toPx.x / max(toPx.y, 1.0f));

    for (int i = 0; i < p.count; ++i)
    {
        const float2 dest   = p.spots[i].xy;
        const float2 source = p.spots[i].zw;
        const float  radius = max(p.shape[i].x, 1e-6f);

        // Reject on the bounding square before doing anything else. Most pixels
        // miss most spots, and a frame with sixty of them would otherwise pay a
        // length() for every one at every pixel.
        const float2 e = (q - dest) / aspect;
        if (abs(e.x) > radius || abs(e.y) > radius) continue;

        const float d = length(e) / radius;
        if (d >= 1.0f) continue;

        // Perlin's smootherstep, shared with the masks through ops/mask_ops —
        // C² so the rim does not leave a Mach band on the smooth backgrounds
        // this is used against, which is precisely where one would show.
        const float feather = clamp(p.shape[i].y, 0.0f, 0.999f);
        const float coverage =
            1.0f - maskFalloff((d - (1.0f - feather)) / max(feather, 1e-3f));
        if (coverage <= 0.0f) continue;

        // The source pixel for this destination pixel: the same offset from the
        // source centre as this pixel is from the destination centre.
        const float2 sq = source + (q - dest);
        if (sq.x < 0.0f || sq.x > 1.0f || sq.y < 0.0f || sq.y > 1.0f) continue;
        const int2 sp = int2(clamp(sq * toPx, float2(0.0f, 0.0f), toPx - 1.0f));

        float3 patch = src[sp].rgb;
        // Heal adds the tone difference measured around the rim; clone does
        // not. One flag, because the two operations differ by exactly this.
        if (p.shape[i].z != 0.0f) patch += corrections[uint2(i, 0)].rgb;

        // Later spots lay over earlier ones, which is what a photographer means
        // by placing a second spot over the first.
        out.rgb = lerp(out.rgb, patch, coverage);
    }

    dst[tid.xy] = out;
}
