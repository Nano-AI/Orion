// The colour mixer — eight hue bands, each with hue, saturation and luminance.
//
// Lightroom's Color Mixer is the UX benchmark here, and the maths behind it is
// unglamorous: find the pixel's hue, weight each band by how close that hue is
// to the band's centre, and blend the offsets. The only subtlety is the
// weighting. Hard band edges produce visible seams where a gradient crosses
// from one band to the next, so the weights overlap smoothly and always sum
// to roughly one.

#ifndef ORION_HSL_OPS_SLANG
#define ORION_HSL_OPS_SLANG

// Band centres in degrees: red, orange, yellow, green, aqua, blue, purple,
// magenta. Matching Lightroom's naming means a user's existing instincts
// transfer.
static const float kBandCentres[8] = { 0.0f, 30.0f, 60.0f, 120.0f,
                                       180.0f, 240.0f, 285.0f, 320.0f };

float3 rgbToHsl(float3 c)
{
    const float maxc = max(c.r, max(c.g, c.b));
    const float minc = min(c.r, min(c.g, c.b));
    const float l = (maxc + minc) * 0.5f;
    const float d = maxc - minc;

    if (d < 1e-6f) return float3(0.0f, 0.0f, l);

    const float s = (l > 0.5f) ? d / max(2.0f - maxc - minc, 1e-6f)
                               : d / max(maxc + minc, 1e-6f);

    float h;
    if (maxc == c.r)      h = (c.g - c.b) / d + ((c.g < c.b) ? 6.0f : 0.0f);
    else if (maxc == c.g) h = (c.b - c.r) / d + 2.0f;
    else                  h = (c.r - c.g) / d + 4.0f;

    return float3(h * 60.0f, s, l);
}

float hueToChannel(float p, float q, float t)
{
    if (t < 0.0f) t += 1.0f;
    if (t > 1.0f) t -= 1.0f;
    if (t < 1.0f / 6.0f) return p + (q - p) * 6.0f * t;
    if (t < 1.0f / 2.0f) return q;
    if (t < 2.0f / 3.0f) return p + (q - p) * (2.0f / 3.0f - t) * 6.0f;
    return p;
}

float3 hslToRgb(float3 hsl)
{
    const float h = hsl.x / 360.0f;
    const float s = hsl.y;
    const float l = hsl.z;

    if (s < 1e-6f) return float3(l, l, l);

    const float q = (l < 0.5f) ? l * (1.0f + s) : l + s - l * s;
    const float p = 2.0f * l - q;

    return float3(hueToChannel(p, q, h + 1.0f / 3.0f),
                  hueToChannel(p, q, h),
                  hueToChannel(p, q, h - 1.0f / 3.0f));
}

// Shortest angular distance between two hues, in degrees.
float hueDistance(float a, float b)
{
    float d = abs(a - b);
    return min(d, 360.0f - d);
}

/// hueShift/satShift/lumShift are 8 entries each, in -1..1.
/// Applying this in display-adjacent HSL rather than scene-linear is
/// deliberate: users judge "make the blues deeper" by what they see, and a
/// linear-space saturation shift does not match that expectation.
float3 applyColorMixer(float3 c,
                       float hueShift[8], float satShift[8], float lumShift[8])
{
    // Skip entirely when nothing is set. HSL is a bounded model and this data
    // is not — every round trip through it costs a little precision, so don't
    // pay for one that changes nothing.
    float any = 0.0f;
    [unroll]
    for (uint k = 0; k < 8; ++k)
    {
        any += abs(hueShift[k]) + abs(satShift[k]) + abs(lumShift[k]);
    }
    if (any < 1e-6f) return c;

    // HSL is defined on 0..1, but scene-linear data is unbounded — a bright
    // specular can sit well above 1. Clamping into range would crush every
    // highlight to white and strip its colour, so normalise by the peak
    // channel instead and restore the magnitude afterwards. Hue and saturation
    // are scale-invariant, which is what makes this safe.
    const float peak = max(c.r, max(c.g, c.b));
    if (peak < 1e-6f) return c;

    const float scale = max(peak, 1.0f);
    float3 hsl = rgbToHsl(c / scale);

    // A near-grey pixel has no meaningful hue, so leave it alone rather than
    // letting numerical noise pick a band for it.
    if (hsl.y < 1e-3f) return c;

    float hue = 0.0f, sat = 0.0f, lum = 0.0f, total = 0.0f;

    [unroll]
    for (uint i = 0; i < 8; ++i)
    {
        // 60 degrees of falloff gives neighbouring bands real overlap, which is
        // what keeps a sky gradient from banding as it crosses aqua into blue.
        const float d = hueDistance(hsl.x, kBandCentres[i]);
        const float w = max(0.0f, 1.0f - d / 60.0f);
        const float w2 = w * w;

        hue   += hueShift[i] * w2;
        sat   += satShift[i] * w2;
        lum   += lumShift[i] * w2;
        total += w2;
    }

    if (total < 1e-6f) return c;

    hue /= total;
    sat /= total;
    lum /= total;

    hsl.x = fmod(hsl.x + hue * 30.0f + 360.0f, 360.0f);
    hsl.y = saturate(hsl.y * (1.0f + sat));
    hsl.z = saturate(hsl.z * (1.0f + lum * 0.5f));

    return hslToRgb(hsl) * scale;
}

#endif
