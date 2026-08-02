// The creative vignette — a deliberate darkening toward the corners of the
// *composition*.
//
// ⚠ This is not the lens vignette **correction** in `lens.slang`. That one
// divides out a falloff a real lens measured, from a lensfun profile, before
// the demosaic; this one puts a falloff in on purpose, after the grade, and the
// two must never be wired to each other. See research/vignette.md.
//
// ── The curve ──────────────────────────────────────────────────────────────
//
// The natural illumination falloff of a lens obeys the cos^4 law: off-axis
// illuminance at field angle theta is proportional to cos^4(theta).
//
//   M. Reiss, "The cos^4 Law of Illumination", J. Opt. Soc. Am. 35(4),
//   283-288 (1945).
//   R. Kingslake, "Optics in Photography", SPIE Press (1992), p. 121.
//
// ⚠ Reiss's own paper, and every optics text since, is clear that cos^4 is a
// first-order model and that real lenses depart from it — pupil aberration and
// mechanical vignetting both break it. That is a reason to refuse it for a
// *correction*, which is why the correction uses lensfun's measured polynomial
// instead, and no reason at all to refuse it here: this control is imitating the
// look, and cos^4 is the shape the look is imitating.
//
// Writing tan(theta) = r * tan(thetaMax) puts the corner of the composition at
// r = 1 and the axis at r = 0, and cos^2 = 1/(1 + tan^2) turns the whole thing
// into two multiplies with no trigonometry in the kernel:
//
//   V(r) = cos^4(theta) = 1 / (1 + (r*T)^2)^2 ,   T = tan(thetaMax)
//
// `thetaMax` is the half-diagonal field angle of the lens being imitated — 45
// degrees is roughly a 21 mm lens on full frame, 23 degrees roughly a 50 mm —
// so the second control is a real optical quantity rather than a shaped number.

/// The falloff, normalized so it is 0 on the axis and exactly 1 at the corner
/// of the composition. `r` is distance from the composition's center in units
/// of its own half-diagonal; `tanTheta` is tan of the half-diagonal field angle.
///
/// ⚠ Deliberately **not** clamped above r = 1. Pixels outside the crop exist
/// while the crop tool is open, and cos^4 saturates on its own — as r grows V
/// goes to zero and this goes to 1/(1 - V(1)), a finite ceiling — so the field
/// stays smooth instead of gaining a visible ring at the edge of the rectangle.
float vignetteFalloff(float r, float tanTheta)
{
    const float a = 1.0f + r * r * tanTheta * tanTheta;
    const float b = 1.0f + tanTheta * tanTheta;
    const float v = 1.0f / (a * a);          // cos^4 here
    const float corner = 1.0f / (b * b);     // cos^4 at r = 1
    return (1.0f - v) / max(1.0f - corner, 1e-6f);
}

/// Distance from the composition's center, in units of its half-diagonal.
///
/// `center` is normalized in the *frame* this kernel is rendering, and `radius`
/// is the composition's half-diagonal in units of the frame's height — both
/// computed on the host from the same crop rectangle the geometry node uses, so
/// the circle follows a crop, a straighten and a quarter turn without this
/// kernel knowing any of them happened.
///
/// The x axis is scaled by the frame's aspect so the falloff is a circle in
/// pixels rather than an ellipse. That is not a style choice: a lens's
/// iso-illuminance contours are circles about the optical axis, and the frame
/// is a rectangle cut out of one.
float vignetteRadius(uint2 tid, uint2 size, float2 center, float radius)
{
    const float2 uv = (float2(tid) + 0.5f) / float2(size);
    const float aspect = float(size.x) / max(float(size.y), 1.0f);
    const float2 d = (uv - center) * float2(aspect, 1.0f);
    return length(d) / max(radius, 1e-6f);
}

/// The gain a pixel gets: `amount` stops at the corner, nothing on the axis,
/// cos^4 in between.
///
/// An exposure change in scene-linear light, not a blend toward black. That is
/// what a lens does, and it is what lets the display transform roll a bright
/// corner off rather than crushing it — the same reason the pipeline is
/// scene-referred at all (decision #6).
float vignetteGain(float r, float tanTheta, float amountEv)
{
    return exp2(amountEv * vignetteFalloff(r, tanTheta));
}
