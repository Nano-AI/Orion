// Black and white: scene-linear luminance, brightened or darkened per hue band.
//
// research/bw-mix.md. The baseline is the working space's own luminance, so
// every slider at zero is exactly Rec.2020 Y. A band's slider moves the gray of
// what was that colour by up to kBwStops either way, weighted by the mixer's
// own band membership (hueBandWeight) and by how coloured the pixel was.

#ifndef ORION_BW_OPS_SLANG
#define ORION_BW_OPS_SLANG

#include "tone_ops.slang"
#include "hsl_ops.slang"

/// A slider at +1 or -1 on a fully saturated colour of one band only: two
/// stops. Orion's own constant, research/UNSOURCED.md §33.
static const float kBwStops = 2.0f;

/// `mix` is eight entries in -1..1, in the mixer's band order. R = G = B out.
float3 applyBlackWhite(float3 c, float mix[8])
{
    const float y = max(luminance2020(c), 0.0f);
    const float peak = max(c.r, max(c.g, c.b));
    const float low = min(c.r, min(c.g, c.b));
    if (peak < 1e-6f) return float3(y);

    // (max - min) / max, the chroma vibrance reads: scale-invariant, so a
    // highlight above 1 is weighted like the same colour below it, and zero on
    // the gray axis, so a neutral and its noise keep their luminance exactly.
    const float chroma = saturate((peak - low) / peak);
    if (chroma < 1e-6f) return float3(y);

    const float hue = rgbToHsl(c / max(peak, 1.0f)).x;
    float sum = 0.0f, total = 0.0f;
    [unroll]
    for (uint i = 0; i < 8; ++i)
    {
        const float w = hueBandWeight(hue, i);
        sum   += mix[i] * w;
        total += w;
    }
    if (total < 1e-6f) return float3(y);

    return float3(y * exp2(kBwStops * chroma * sum / total));
}

#endif
