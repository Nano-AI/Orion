// Three-way color grading — shadows, midtones, highlights — and the creative
// vignette, which rides along in the same pass.
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
// a filmic shoulder — which is where a color balance control sits in a
// display-referred editor — means the same offset does something different to a
// highlight than to a midtone for reasons unrelated to the zone the user chose.
//
// The creative vignette is here rather than in a node of its own because it is
// pointwise and wants the same light, and another full-resolution round trip is
// ~194 MB at 24 MP for six lines of arithmetic — the trade the creative LUT lost
// inside develop_display.slang. research/vignette.md, decision #103.

#include "ops/vignette_ops.slang"

struct GradeParams
{
    uint2 size;
    float _pad0, _pad1;

    // Offsets are already zero-sum, computed on the host from each wheel's
    // angle and radius. Zero-sum is what makes a wheel a color control and not
    // a brightness one; the .w of each carries that zone's slope.
    float4 shadow;      // rgb offset, w = slope
    float4 midtone;
    float4 highlight;

    // The creative vignette. Centre normalized in this frame and radius the
    // composition's half-diagonal in units of the frame's height — both from
    // the crop rectangle, on the host, so this kernel never learns a crop
    // happened. Amount is the exposure change at the corner **in stops**.
    float2 vignetteCenter;
    float  vignetteRadius;
    float  vignetteAmount;
    float  vignetteTanTheta;   // tan of the half-diagonal field angle

    /// Balance, -1..+1. Slides the three zone centres along the same log2 axis
    /// they are defined on. Zero is the identity, exactly. See below.
    float  balance;
    float  _pad3, _pad4;
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
// Gaussian bands normalized to a partition of unity, which is the same
// construction the tone controls use for exactly the same reason
// (ops/tone_ops.slang, deep-research-2026-07-27.md §3): summed weights of one
// everywhere are what make an untouched image come back unchanged, and a
// Gaussian has no corner where two zones meet. Centers one stop either side of
// the tone controls' own inner pair, and the same sigma, so the two sets of
// bands describe the same picture.
static const float kEvShadow    = -2.5f;
static const float kEvMidtone   =  0.0f;   // middle gray, where it belongs
static const float kEvHighlight =  2.5f;
static const float kZoneSigma   =  1.6f;

// ── Balance: the one thing split toning had that the wheels did not ────────
//
// Decision #101 refused to build split toning — three wheels already are its
// two tints plus a midtone it never reached — and named Balance as the single
// genuine remainder. The centres above are fixed, so nothing decides how much
// of a photograph counts as shadow.
//
// Reference behaviour: Adobe's Balance, on Split Toning (Camera Raw 4, 2007)
// and carried into the Color Grading panel that replaced it (Camera Raw 13.0 /
// Lightroom Classic 10.0, October 2020). Adobe's runs -100..+100 and, in their
// words, balances "the effect between the Highlight and Shadow sliders";
// positive favours the highlight tint. This is that control on Orion's axis.
//
// **A rigid shift of all three centres**, not a re-spacing. Sliding the whole
// partition along log2 luminance is what moves a crossover, and it is the one
// form that cannot go wrong: the zones keep their 2.5 EV spacing so they stay
// ordered and none can pass another, and a partition of unity translated along
// its own axis is still a partition of unity. Re-spacing the centres instead —
// pulling shadow and highlight toward one end — can collide them, and two
// Gaussians at the same centre are one zone with two wheels fighting over it.
//
// Sign: **positive is toward the highlights**, which moves the centres *down*
// so that more of the picture falls inside the highlight band. Same direction
// as Adobe's slider.
//
// kBalanceEv is chosen, not published — research/UNSOURCED.md §26. It is half
// the zone spacing, which is the largest travel where every crossover stays
// strictly between the two centres it separates: at full deflection the
// shadow/midtone crossover lands exactly on a neighbouring centre and never
// past it.
static const float kBalanceEv   =  1.25f;

float zoneWeight(float ev, float center)
{
    const float d = (ev - center) / kZoneSigma;
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

    // ⚠ At balance 0 this is `-0.0f`, and `x + (-0.0f) == x` bit for bit for
    // every finite x — so Balance centred leaves every existing render, bench
    // pin and baseline exactly where it was rather than rebasing them by a ulp.
    const float shift = -p.balance * kBalanceEv;

    const float rawS = zoneWeight(ev, kEvShadow + shift);
    const float rawM = zoneWeight(ev, kEvMidtone + shift);
    const float rawH = zoneWeight(ev, kEvHighlight + shift);
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
    float3 result = max(c * slope + offset * y, 0.0f);

    // ⚠ **The vignette goes after the grade, not before it.** Physically a
    // lens's falloff comes first, but the zones are Gaussians on luminance — so
    // a vignette applied first would slide every corner pixel into the shadow
    // band and silently change what all three wheels do, which is the
    // interaction #41 took out of the wheels themselves.
    if (p.vignetteAmount != 0.0f)
    {
        const float r = vignetteRadius(tid.xy, p.size, p.vignetteCenter,
                                       p.vignetteRadius);
        result *= vignetteGain(r, p.vignetteTanTheta, p.vignetteAmount);
    }

    dst[tid.xy] = float4(result, source.a);
}
