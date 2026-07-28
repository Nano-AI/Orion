// Scene-linear tone and colour operations.
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
// Highlights and shadows are *local*: their masks are built from a guided-filter
// estimate of the neighbourhood's tone rather than from the pixel's own value.
// This is what Lightroom and darktable do and a pointwise curve cannot: recovery
// follows the structure of the scene, so a bright window against a dark wall is
// pulled down as a region instead of every bright pixel everywhere being pulled
// down individually.
//
// Whites and blacks stay pointwise on purpose. They set the endpoints of the
// range, and an endpoint that moved around with local content would be
// unpredictable to use.
//
// `guideEv` is the filtered neighbourhood luminance in log2 exposure, already
// scaled for the current exposure setting.
float3 applyTone(float3 c, float guideEv,
                 float highlights, float shadows, float whites, float blacks)
{
    const float y  = max(luminance2020(c), 1e-6f);
    const float ev = log2(y);

    // Middle grey sits near -2.5 EV. Wide, overlapping knees keep transitions
    // invisible. Highlights and shadows read the neighbourhood; whites and
    // blacks read the pixel.
    const float hiMask    = smoothstep(-4.0f,  1.0f, guideEv);
    const float loMask    = 1.0f - smoothstep(-7.0f, -1.5f, guideEv);
    const float whiteMask = smoothstep(-2.0f,  2.5f, ev);
    const float blackMask = 1.0f - smoothstep(-9.0f, -3.5f, ev);

    float gain = 1.0f;
    gain *= exp2(highlights * hiMask    * 1.5f);
    gain *= exp2(shadows    * loMask    * 1.5f);
    gain *= exp2(whites     * whiteMask * 1.0f);

    c *= gain;

    // Blacks is an offset, not a gain: crushing the floor should move the black
    // point rather than rescale everything beneath it.
    c += blacks * blackMask * 0.05f;

    return max(c, 0.0f);
}

// ── Vibrance / saturation ──────────────────────────────────────────────────
// Saturation scales every colour's distance from grey equally. Vibrance
// weights that by how unsaturated a colour already is, so muted tones move and
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
