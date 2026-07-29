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

// ── Where the zones sit ────────────────────────────────────────────────────
//
// On log2 luminance relative to middle gray, not on linear luminance.
//
// The linear partition this replaced put its boundaries at Y = 0.0/0.5/1.0,
// which sounds like "shadows, midtones, highlights" and is not. Middle gray is
// Y = 0.18, so it weighed 0.70 shadows / 0.30 midtones / 0.00 highlights: the
// shadow wheel graded most of a normal photograph and the midtone weight peaked
// a stop and a half above middle gray. Measured on two frames lifted 5.5 EV,
// the highlight wheel moved mean chroma by -0.0000 and +0.0001. Inert.
//
// Gaussian bands normalised to a partition of unity, which is the same
// construction the tone controls use for exactly the same reason
// (ops/tone_ops.slang, deep-research-2026-07-27.md §3): summed weights of one
// everywhere are what make an untouched image come back unchanged, and a
// Gaussian has no corner where two zones meet. Centres one stop either side of
// the tone controls' own inner pair, and the same sigma, so the two sets of
// bands describe the same picture.
static const float kEvShadow    = -2.5f;
static const float kEvMidtone   =  0.0f;   // middle gray, where it belongs
static const float kEvHighlight =  2.5f;
static const float kZoneSigma   =  1.6f;

float zoneWeight(float ev, float centre)
{
    const float d = (ev - centre) / kZoneSigma;
    return exp(-0.5f * d * d);
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

    const float y = max(luma2020(c), 1e-6f);
    const float ev = log2(y / 0.18f);

    const float rawS = zoneWeight(ev, kEvShadow);
    const float rawM = zoneWeight(ev, kEvMidtone);
    const float rawH = zoneWeight(ev, kEvHighlight);
    const float total = rawS + rawM + rawH + 1e-6f;

    const float ws = rawS / total;
    const float wm = rawM / total;
    const float wh = rawH / total;

    const float3 offset = p.shadow.rgb * ws
                        + p.midtone.rgb * wm
                        + p.highlight.rgb * wh;

    const float slope = 1.0f + p.shadow.w * ws
                             + p.midtone.w * wm
                             + p.highlight.w * wh;

    // ── The offset scales with the pixel ───────────────────────────────────
    //
    // ASC CDL's offset is defined on code values — a bounded, display-referred
    // signal. Applied as a constant in unbounded scene-linear light it is not
    // scale-invariant: what it is worth *relative* to the pixel, which is what
    // survives a log-shaped display transform, falls as one over the level. So
    // a wheel had all its authority in the deep shadows and none in the
    // highlights, which is where the highlight wheel lives.
    //
    // Multiplying by the pixel's own luminance makes it a constant chromaticity
    // shift instead, at every exposure. It also removes the clamp problem: a
    // zero-sum offset always has a negative component, and as a constant it was
    // larger than a deep shadow, so two channels stuck at zero, the sum stopped
    // cancelling and the wheel *brightened* what it was tinting — measured at
    // +29% on a 0.0096-linear patch. Scaled by y it cannot exceed the pixel it
    // is tinting.
    //
    // Clamped at zero only. The pipeline is unbounded above this point, so a
    // ceiling here would clip highlights the display transform rolls off.
    dst[tid.xy] = float4(max(c * slope + offset * y, 0.0f), source.a);
}
