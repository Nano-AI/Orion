// Three-way color grading — shadows, midtones, highlights.
//
// Reference: ASC CDL v1.2 (2009), the slope/offset/power transfer function,
// applied once per tonal zone and blended by a smooth partition of luminance.
// Set out in research/color-grading.md, with the control surface described in
// Van Hurkman, Color Correction Handbook, 2nd ed., ch. 4.
//
//     out = in x slope + offset
//
// Power is left at 1: a per-zone gamma on top of a per-zone slope is two
// controls for one perceptual axis, and the tone curve upstream already owns
// contrast.
//
// This runs in scene-linear light, before the display transform. Grading after
// a filmic shoulder — which is where a colour balance control sits in a
// display-referred editor — means the same offset does something different to a
// highlight than to a midtone for reasons unrelated to the zone the user chose.

struct GradeParams
{
    uint2 size;
    float _pad0, _pad1;

    // Offsets are already zero-sum, computed on the host from each wheel's
    // angle and radius. Zero-sum is what makes a wheel a colour control and not
    // a brightness one; the .w of each carries that zone's slope.
    float4 shadow;      // rgb offset, w = slope
    float4 midtone;
    float4 highlight;
};

/// Rec.2020 luma. The working space is Rec.2020, so using Rec.709 coefficients
/// here would weight green wrongly and drag the zone boundaries with it.
float luma2020(float3 c)
{
    return dot(c, float3(0.2627f, 0.6780f, 0.0593f));
}

[shader("compute")]
[numthreads(16, 16, 1)]
void colorGrade(uint3 tid : SV_DispatchThreadID,
                uniform Texture2D<float4>   src,
                uniform RWTexture2D<float4> dst,
                uniform ConstantBuffer<GradeParams> p)
{
    if (tid.x >= p.size.x || tid.y >= p.size.y) return;

    const float4 source = src[tid.xy];
    const float3 c = source.rgb;

    const float y = luma2020(c);

    // A smooth partition rather than a linear ramp: smoothstep's derivative is
    // zero at both ends, so a correction fades in without a visible edge where
    // two zones meet. The three weights sum to 1 everywhere by construction,
    // which is what makes an untouched image come back unchanged.
    const float ws = 1.0f - smoothstep(0.0f, 0.5f, y);
    const float wh = smoothstep(0.5f, 1.0f, y);
    const float wm = 1.0f - ws - wh;

    const float3 offset = p.shadow.rgb * ws
                        + p.midtone.rgb * wm
                        + p.highlight.rgb * wh;

    const float slope = 1.0f + p.shadow.w * ws
                             + p.midtone.w * wm
                             + p.highlight.w * wh;

    // Clamped at zero only. The pipeline is scene-linear and unbounded above
    // this point, so a ceiling here would clip highlights the display transform
    // is about to roll off properly.
    dst[tid.xy] = float4(max(c * slope + offset, 0.0f), source.a);
}
