// Capture sharpening — unsharp mask with detail masking.
//
// Sits immediately after the demosaic, which is where capture sharpening
// belongs: it is undoing the sensor's anti-alias filter and the interpolation's
// own softening, not adding a creative effect. Output sharpening for a specific
// print or screen size is a separate, later thing.
//
// Position matters for another reason. Because the graph only propagates dirt
// downstream, keeping sharpening upstream of the tone controls means dragging
// exposure never recomputes it.
//
// The masking control is what stops this being a noise amplifier: it suppresses
// sharpening wherever local contrast is low, which is exactly where noise lives
// and detail does not.

#include "cfa.slang"

struct SharpenParams
{
    float amount;    // 0..2
    float radius;    // pixels, 0.5..3
    float masking;   // 0..1, higher protects flat areas
    float _pad;
    uint2 size;
    uint2 _pad2;
};

float luma(float3 c)
{
    return dot(c, float3(0.2627f, 0.6780f, 0.0593f));
}

[shader("compute")]
[numthreads(16, 16, 1)]
void sharpen(uint3 tid : SV_DispatchThreadID,
             uniform Texture2D<float4>   src,
             uniform RWTexture2D<float4> dst,
             uniform ConstantBuffer<SharpenParams> p)
{
    if (tid.x >= p.size.x || tid.y >= p.size.y) return;

    const int2 c = int2(tid.xy);
    const float3 centre = src[tid.xy].rgb;

    if (p.amount <= 0.0f)
    {
        dst[tid.xy] = float4(centre, 1.0f);
        return;
    }

    // Radius scales the tap offsets rather than the tap count, so cost stays
    // constant. A 3x3 of scaled taps approximates a Gaussian closely enough for
    // capture sharpening, where radii are small by definition.
    const int r = max(1, int(round(p.radius)));

    float3 blur = float3(0.0f, 0.0f, 0.0f);
    float weight = 0.0f;

    [unroll]
    for (int dy = -1; dy <= 1; ++dy)
    {
        [unroll]
        for (int dx = -1; dx <= 1; ++dx)
        {
            // Binomial weights: 1 2 1 / 2 4 2 / 1 2 1.
            const float w = float((dx == 0 ? 2 : 1) * (dy == 0 ? 2 : 1));
            const int2 q = clampToImage(c + int2(dx * r, dy * r), p.size);
            blur += src[q].rgb * w;
            weight += w;
        }
    }
    blur /= weight;

    const float3 detail = centre - blur;

    // Detail mask: how much local contrast there is, normalised. Below the
    // threshold we are almost certainly looking at noise.
    const float localContrast = abs(luma(detail));
    const float threshold = p.masking * 0.02f;
    const float mask = (p.masking <= 0.0f)
        ? 1.0f
        : smoothstep(threshold * 0.25f, threshold, localContrast);

    dst[tid.xy] = float4(max(centre + detail * p.amount * mask, 0.0f), 1.0f);
}
