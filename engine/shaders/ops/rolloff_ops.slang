// Four published soft-clip operators for the display transform's contrast
// stage (develop_display.slang, `DisplayParams::rollOff`), so the hard
// `saturate` defect measured in decision #222 can be judged blind against
// real alternatives instead of this session choosing one (#223).
//
// Included after develop_display.slang declares `kPivotNorm`, `kRollOffLo`,
// `kRollOffHi`, `kRollOffXLo` and `kRollOffXHi` -- these functions read those
// as globals, the same convention `agxNormalize`/`agxCurve` already use for
// `kPivotNorm`/`kBlackStops`/`kWhiteStops`.
//
// Every operator is the identity at `y == kPivotNorm` (gray must not move,
// #222's trap 2), and every one but Reinhard is exactly the identity across
// [kRollOffLo, kRollOffHi] so contrast's midtone slope survives unclamped.

#pragma once

/// ACES Reference Gamut Compression parametric curve, mirrored about the
/// thresholds. Academy of Motion Picture Arts and Sciences, "Reference Gamut
/// Compression Specification", ACES 1.3.
/// https://docs.acescentral.com/rgc/specification/
///
/// `s = 1 - t` rather than the spec's `l`-derived `s` (that derivation puts
/// the asymptote above 1, correct for a gamut distance and wrong for a value
/// that must stay under 1); `p = 2`, so the root is a `sqrt` and
/// `f'(u) = (1 + (u/s)^2)^(-3/2)`, positive everywhere -- monotonic by
/// construction, not by clamping.
float acesRgcHi(float y, float t)
{
    const float s = 1.0f - t;
    const float u = y - t;
    return t + u / sqrt(1.0f + (u / s) * (u / s));
}

float acesRgc(float y)
{
    if (y >= kRollOffHi) return acesRgcHi(y, kRollOffHi);
    if (y <= kRollOffLo) return 1.0f - acesRgcHi(1.0f - y, 1.0f - kRollOffLo);
    return y;
}

/// The general two-point cubic Hermite basis, value and (pre-scaled) tangent
/// at each end. Standard construction; see e.g. Wikipedia, "Cubic Hermite
/// spline". `hm0`/`hm1` are the tangents already multiplied by the segment
/// width, which is what turns this into ITU-R BT.2390's specific formula
/// below.
float hermite01(float s, float p0, float hm0, float p1, float hm1)
{
    const float s2 = s * s, s3 = s2 * s;
    const float h00 = 2.0f * s3 - 3.0f * s2 + 1.0f;
    const float h10 = s3 - 2.0f * s2 + s;
    const float h01 = -2.0f * s3 + 3.0f * s2;
    const float h11 = s3 - s2;
    return h00 * p0 + h10 * hm0 + h01 * p1 + h11 * hm1;
}

/// ITU-R BT.2390-11 (2023), "High dynamic range television for production and
/// international programme exchange", Annex 5's EETF knee: a cubic Hermite
/// from the knee (value KS, tangent 1, matching identity) to the display
/// maximum (value 1, tangent 0). Mirrored for the low side the same way ACES
/// is above.
///
/// ⚠ The standard's own formula assumes its input already lives in [KS, 1] --
/// true for a PQ signal, false here once contrast > 1 pushes `y` past 1. Used
/// as published, that mismatch would re-hard-clip anything past `y = 1`
/// exactly the defect this exists to remove. So the far anchor is this
/// pixel's *actual* reachable extreme (`yMin`/`yMax`, from `x` in [0,1]
/// through the live contrast) rather than a literal 0/1, which keeps the
/// spline's domain matched to its input and the curve genuinely injective
/// short of that true extreme.
float bt2390(float y, float contrast)
{
    const float yMin = (0.0f - kPivotNorm) * contrast + kPivotNorm;
    const float yMax = (1.0f - kPivotNorm) * contrast + kPivotNorm;
    if (y >= kRollOffHi)
    {
        const float h = max(yMax - kRollOffHi, 1e-6f);
        const float s = saturate((y - kRollOffHi) / h);
        return hermite01(s, kRollOffHi, h, 1.0f, 0.0f);
    }
    if (y <= kRollOffLo)
    {
        const float h = max(kRollOffLo - yMin, 1e-6f);
        const float s = saturate((kRollOffLo - y) / h);
        return hermite01(s, kRollOffLo, -h, 0.0f, 0.0f);
    }
    return y;
}

/// Reinhard, Stark, Shirley & Ferwerda, "Photographic Tone Reproduction for
/// Digital Images", ACM TOG 21(3), 2002, the global operator `L / (1 + L)`
/// (their eq. 3), applied about the pivot instead of 0 so gray is its fixed
/// point. ⚠ Deliberately has **no identity zone** -- compression starts the
/// instant `y` leaves the pivot, so midtone contrast is perturbed everywhere,
/// not just in the tails. That is the point of including it, not a bug to
/// chase: it is the comparison's "no latitude at all" arm.
float reinhard(float y)
{
    if (y >= kPivotNorm)
    {
        const float s = 1.0f - kPivotNorm;
        const float u = y - kPivotNorm;
        return kPivotNorm + s * u / (s + u);
    }
    const float s = kPivotNorm;
    const float u = kPivotNorm - y;
    return kPivotNorm - s * u / (s + u);
}

/// darktable filmic rgb's construction, from Aurélien Pierre's published
/// description -- eng.aurelienpierre.com, "Filmic, darktable and the quest of
/// the HDR tone mapping", Nov. 2018 -- **not** from the module's (GPL) source,
/// which CLAUDE.md excludes even as a check on a constant. The article poses
/// a middle segment linear in the log-encoded axis with slope = contrast,
/// joined to toe/shoulder quartics by value + first + second derivative
/// continuity at the latitude bounds, and zero tangent at the black/white
/// extremes. That is five conditions on five quartic coefficients, an exactly
/// determined linear system; the coefficients below are this session's own
/// solution of it, not a transcription of anything.
///
/// ⚠ Unlike the other three, this works on `x`, the pre-contrast axis, not on
/// the contrast-scaled `y` -- structurally the point of the construction: the
/// toe/shoulder *replace* the linear extrapolation rather than compress its
/// output, so they never see a value outside [0,1] to begin with. The
/// article's display-space grey anchor `G_d = 0.18^(1/gamma)` is replaced
/// with `kPivotNorm`: in Orion's pipeline gray must stay fixed on this axis
/// by construction (`agxCurve` downstream is what maps it to display gray),
/// where Pierre's own pipeline gamma-encodes directly to display here.
float filmicToe(float x, float contrast)
{
    const float td = contrast * (kRollOffXLo - kPivotNorm) + kPivotNorm;
    const float u  = saturate(x / max(kRollOffXLo, 1e-6f));
    const float u2 = u * u;
    const float a = 3.0f * td - 2.0f * contrast * kRollOffXLo;
    const float b = -8.0f * td + 5.0f * contrast * kRollOffXLo;
    const float c = 6.0f * td - 3.0f * contrast * kRollOffXLo;
    return a * u2 * u2 + b * u2 * u + c * u2;
}

float filmicShoulder(float x, float contrast)
{
    const float sd = contrast * (kRollOffXHi - kPivotNorm) + kPivotNorm;
    const float w  = max(1.0f - kRollOffXHi, 1e-6f);
    const float v  = saturate((x - kRollOffXHi) / w);
    const float v2 = v * v;
    const float m4 = 3.0f * sd + 2.0f * contrast * w - 3.0f;
    const float m3 = 4.0f - 4.0f * sd - 3.0f * contrast * w;
    const float m1 = contrast * w;
    return m4 * v2 * v2 + m3 * v2 * v + m1 * v + sd;
}

float filmicRgb(float x, float contrast)
{
    if (x <= kRollOffXLo) return filmicToe(x, contrast);
    if (x >= kRollOffXHi) return filmicShoulder(x, contrast);
    return contrast * (x - kPivotNorm) + kPivotNorm;
}
