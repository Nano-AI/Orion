// Scene-linear tone and color operations.
//
// One function per adjustment, so each stays readable and testable on its own,
// but they are *called* from a single fused kernel rather than each getting its
// own pass. At 24 MP an rgba16f texture is 194 MB; five separate pointwise
// passes move 1.9 GB per slider tick and the pipeline becomes bandwidth-bound
// long before it becomes compute-bound. Fusing pointwise work is not a hack —
// these operations share a pixel and have no spatial dependency on each other.

#ifndef ORION_TONE_OPS_SLANG
#define ORION_TONE_OPS_SLANG

// Rec.2020 luminance — the working space's own weights, not sRGB's.
float luminance2020(float3 c)
{
    return dot(c, float3(0.2627f, 0.6780f, 0.0593f));
}

// ── Exposure ───────────────────────────────────────────────────────────────
// A pure multiply, which is the entire argument for a scene-referred pipeline.
// On display-referred data the same slider needs a curve, behaves differently
// in shadows than highlights, and shifts hue as it goes.
float3 applyExposure(float3 c, float ev)
{
    return c * exp2(ev);
}

// ── Highlights / shadows / whites / blacks ─────────────────────────────────
//
// Mask geometry and gain mapping follow research/deep-research-2026-07-27.md §3.
//
// Four Gaussian masks centered on the exposure bands photographers reason in:
//
//     Blacks     −5.5 EV      Shadows    −2.5 EV
//     Highlights +2.5 EV      Whites     +5.5 EV
//
// ★ The masks are normalized to a **partition of unity** — they sum to 1 at
// every exposure — and the four exposure offsets are summed into a *single*
// exponent. Both matter. Multiplying independent masks, which is what this did
// before, double-counts wherever two bands overlap and produces a brightness
// spike there. Summing exponents under normalized weights cannot.
//
// Highlights and shadows read the guided-filter estimate of the neighborhood;
// whites and blacks read the pixel. That split is confirmed correct by the
// research: the first pair adjusts the base layer so detail survives, the
// second sets the display endpoints and must stay global, or extreme contrast
// boundaries — a dark window frame against bright sky — grow halos.

static const float kEvBlacks     = -5.5f;
static const float kEvShadows    = -2.5f;
static const float kEvHighlights =  2.5f;
static const float kEvWhites     =  5.5f;

// Wide enough that adjacent bands overlap smoothly rather than stepping.
static const float kBandSigma = 1.6f;

// A slider at full travel is worth two stops.
static const float kEvPerUnit = 2.0f;

float bandWeight(float ev, float center)
{
    const float d = (ev - center) / kBandSigma;
    return exp(-0.5f * d * d);
}

float3 applyTone(float3 c, float guideEv,
                 float highlights, float shadows, float whites, float blacks)
{
    const float y = max(luminance2020(c), 1e-6f);

    // EV relative to middle gray, which is where the band centers are defined.
    const float ev      = log2(y / 0.18f);
    const float evLocal = guideEv - log2(0.18f);

    // Local bands read the neighborhood; endpoint bands read the pixel.
    const float wShadows    = bandWeight(evLocal, kEvShadows);
    const float wHighlights = bandWeight(evLocal, kEvHighlights);
    const float wBlacks     = bandWeight(ev, kEvBlacks);
    const float wWhites     = bandWeight(ev, kEvWhites);

    // Partition of unity: normalize so the weights sum to 1.
    const float total = wBlacks + wShadows + wHighlights + wWhites + 1e-6f;

    const float deltaEv =
          (wBlacks     / total) * blacks     * kEvPerUnit
        + (wShadows    / total) * shadows    * kEvPerUnit
        + (wHighlights / total) * highlights * kEvPerUnit
        + (wWhites     / total) * whites     * kEvPerUnit;

    // One exponent, not four multiplied gains.
    return max(c * exp2(deltaEv), 0.0f);
}

// ── Vibrance / saturation ──────────────────────────────────────────────────
// Saturation scales every color's distance from gray equally. Vibrance
// weights that by how unsaturated a color already is, so muted tones move and
// vivid ones barely do — which is why it flatters skin and skies where a flat
// saturation boost turns them lurid.
float3 applyColor(float3 c, float vibrance, float saturation)
{
    const float y = luminance2020(c);

    const float maxc = max(c.r, max(c.g, c.b));
    const float minc = min(c.r, min(c.g, c.b));
    const float chroma = (maxc > 1e-6f) ? ((maxc - minc) / maxc) : 0.0f;

    const float amount = 1.0f + saturation + vibrance * (1.0f - saturate(chroma));

    return max(y + (c - y) * amount, 0.0f);
}

#endif
