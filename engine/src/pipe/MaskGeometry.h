/*  Putting a mask where the photographer put it.
 *
 *  A mask is placed on the picture the user is looking at — which is rotated
 *  and cropped — but it is *applied* in `develop:linear`, which runs before the
 *  geometry node and therefore sees the whole frame, unturned. Handing the
 *  displayed coordinates straight to the shader means a mask slides off its
 *  subject the moment the photograph is rotated, and shrinks away from it under
 *  a crop.
 *
 *  This is the transform between the two, and it is the reason a *parametric*
 *  mask is worth the trouble: there is nothing to resample, only a center and
 *  an angle to move. research/masking.md §3.
 */

#pragma once

#include "pipe/Perspective.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace orion::pipe::mask {

/// Where the mask lives as far as the interface is concerned: normalized
/// against the cropped, rotated picture on screen.
struct Placement {
    float centerX = 0.5f, centerY = 0.5f;
    float angle   = 0.0f;      // radians, clockwise, as displayed
    /// What a length near this point is multiplied by on the way through.
    ///
    /// Only the perspective correction ever sets it to anything but 1: the
    /// crop's scale is the same everywhere and is handled by
    /// `lengthToFrame`/`radiusToFrame`, but a homography's is not, so it has to
    /// travel with the point it was measured at. research/perspective.md.
    ///
    /// ⚠ **Isotropic, and therefore not the whole story.** It is √|det J| — the
    /// geometric mean of the two axis scales — which is right for a length whose
    /// direction is not being tracked and wrong for a *shape*. A radial mask's
    /// semi-axes go through `jac` instead, and the difference between the two is
    /// what leaked coverage past the rim of a large mask under a strong
    /// keystone.
    float scale   = 1.0f;

    /// The map's derivative at this point, **in normalized coordinates** —
    /// W⁻¹·J·W for the frame's W = diag(width, height), since `persp::jacobian`
    /// is posed in texels and everything here is not.
    ///
    /// Exactly the identity when no perspective is in play, which is what lets
    /// every consumer short-circuit rather than compute a transform that is the
    /// identity in exact arithmetic and not in float.
    persp::Jacobian jac{};
};

/// The crop rectangle, normalized against the rotated frame — the same
/// convention `Adjustments::cropX..cropH` uses.
struct Crop {
    float x = 0.0f, y = 0.0f, w = 1.0f, h = 1.0f;
};

/// The straighten, exactly as `geometry.slang` applies it.
///
/// ⚠️ **The shader rotates in pixel coordinates of the rotated frame**, not in
/// normalized ones, so the frame's aspect is part of the transform: a rotation
/// applied to normalized coordinates of a 3:2 frame is a different rotation.
/// `frameW` and `frameH` are that frame's dimensions after any quarter turns —
/// only their ratio matters.
///
/// The pivot is the crop's center, in the same normalized rotated space, and it
/// is *passed* rather than derived. Deriving it from the crop origin and size is
/// what once made the preview turn about the frame center and the committed
/// render about the crop center, so an off-center crop delivered a different
/// picture than the box had shown.
inline void unstraighten(float& x, float& y, float radians,
                         float pivotX, float pivotY,
                         float frameW, float frameH) noexcept {
    if (std::fabs(radians) <= 1e-6f) return;

    const float w = std::max(frameW, 1e-6f);
    const float h = std::max(frameH, 1e-6f);

    // Into the shader's units, rotate the same way it does, and back out.
    const float dx = (x - pivotX) * w;
    const float dy = (y - pivotY) * h;

    const float c = std::cos(radians), s = std::sin(radians);
    const float rx = dx * c - dy * s;
    const float ry = dx * s + dy * c;

    x = pivotX + rx / w;
    y = pivotY + ry / h;
}

/// The perspective correction, exactly as `geometry.slang` applies it — the
/// *same matrix bytes*, converted into the same texel convention.
///
/// ⚠ **One derivation, two consumers.** The homography is composed once in
/// `persp::compose` and handed both to the shader and to this file. A second
/// derivation here — "the same map, but in normalized coordinates" — is how a
/// mask ends up a few percent off its subject on a corrected photograph, which
/// is a plausible wrong answer and therefore the worst kind.
///
/// The shader works in texels: `r = u·W − 0.5` for a normalized u. So does
/// this. `frameW` and `frameH` are the frame's dimensions after any quarter
/// turns, the same two `unstraighten` takes.
///
/// Fills `outScale` with the local isotropic scale at the point, `outJac` with
/// the whole derivative there, and rotates `angle` by it. The angle is
/// **exact** — a homography takes lines to lines, and the image line's
/// direction is the Jacobian applied to the original direction — while the
/// scale is isotropic and therefore first order, which
/// `research/perspective.md` states rather than implies. A shape that needs the
/// anisotropy takes `outJac`; `radiusToFrame` does.
inline void unperspective(float& x, float& y, float& angle, float& outScale,
                          persp::Jacobian& outJac, const persp::Matrix3* h,
                          float frameW, float frameH) noexcept {
    if (h == nullptr || persp::isIdentity(*h)) return;

    const float w = std::max(frameW, 2.0f);
    const float t = std::max(frameH, 2.0f);

    float px = x * w - 0.5f;
    float py = y * t - 0.5f;

    const persp::Jacobian j = persp::jacobian(*h, px, py);

    // Everything here is measured in *normalized* frame coordinates, so the
    // derivative has to be conjugated by the axis scales before it can turn an
    // angle or carry a shape: a direction (cos, sin) is (cos·W, sin·H) in
    // texels, and comes back divided by the same two. That conjugation is
    // W⁻¹·J·W, and it is done once here rather than at each use.
    const persp::Jacobian jn{j.a, j.b * t / w, j.c * w / t, j.d};

    const float cs = std::cos(angle), sn = std::sin(angle);
    const float tx = jn.a * cs + jn.b * sn;
    const float ty = jn.c * cs + jn.d * sn;
    if (tx != 0.0f || ty != 0.0f) angle = std::atan2(ty, tx);

    outScale *= persp::areaScale(*h, px, py);
    // Composed rather than assigned. Only one perspective ever applies today,
    // but a second call that silently discarded the first would be a bug whose
    // symptom is a mask the right size in the wrong place.
    outJac = persp::Jacobian{jn.a * outJac.a + jn.b * outJac.c,
                             jn.a * outJac.b + jn.b * outJac.d,
                             jn.c * outJac.a + jn.d * outJac.c,
                             jn.c * outJac.b + jn.d * outJac.d};

    persp::apply(*h, px, py);
    x = (px + 0.5f) / w;
    y = (py + 0.5f) / t;
}

/// Displayed coordinates to the frame the develop stage sees.
///
/// The steps, in this order. The crop first, because the displayed picture *is*
/// the crop: a point halfway across the visible image is halfway across the
/// crop rectangle, not halfway across the frame. Then the straighten and the
/// perspective, in the order `geometry.slang` applies them. Then the quarter
/// turns, since the crop is expressed against the rotated frame.
///
/// `turns` is clockwise quarter turns, matching `DevelopPipeline::quarterTurns`.
/// `perspective` is null when the control is neutral, which is the common case
/// and costs nothing.
[[nodiscard]] inline Placement toFrame(Placement p, const Crop& c, int turns,
                                       float straightenRad = 0.0f,
                                       float pivotX = 0.5f, float pivotY = 0.5f,
                                       float frameW = 1.0f, float frameH = 1.0f,
                                       const persp::Matrix3* perspective = nullptr) noexcept {
    // Into the rotated frame.
    float x = c.x + p.centerX * c.w;
    float y = c.y + p.centerY * c.h;

    // Then the straighten, in the same place the shader applies it: after the
    // crop has put the point in the rotated frame, before the turns are undone.
    unstraighten(x, y, straightenRad, pivotX, pivotY, frameW, frameH);

    // And the perspective, in the place the shader applies that: after the
    // straighten, before the turns.
    float angle = p.angle + straightenRad;
    float scale = p.scale;
    persp::Jacobian jac = p.jac;
    unperspective(x, y, angle, scale, jac, perspective, frameW, frameH);

    // Out of the rotation. A quarter turn clockwise sends a frame point (x, y)
    // to (1 - y, x) on screen, so coming back is the inverse of that, applied
    // once per turn.
    const int k = ((turns % 4) + 4) % 4;
    for (int i = 0; i < k; ++i) {
        const float nx = y;
        const float ny = 1.0f - x;
        x = nx;
        y = ny;
    }

    Placement out{};
    out.centerX = x;
    out.centerY = y;
    out.scale   = scale;
    // ⚠ Not turned. The quarter turns are rigid in normalized coordinates, so
    // they change no extent — and `radiusToFrame` returns its angle *relative*
    // to `out.angle`, which has already been turned. Turning the derivative as
    // well would apply the turn twice, which is decision #83's mistake in a new
    // costume.
    out.jac     = jac;

    // The angle turns with it. Anticlockwise here, because this undoes the
    // rotation the viewer sees — and the straighten is a rotation too, so it
    // enters the angle directly whatever the aspect does to the position. The
    // perspective has already entered it, above, through its derivative.
    constexpr float kHalfPi = 1.57079632679489662f;
    out.angle = angle - float(k) * kHalfPi;
    return out;
}

/// The inverse of `toFrame`: a point in the frame, as a point on the displayed
/// picture.
///
/// Needed the moment anything stored in *frame* coordinates has to be drawn —
/// which is spots, and only spots. A mask is stored in displayed coordinates
/// and needs no inverse to draw; dust is on the sensor, so it is stored where
/// the sensor put it and has to be carried back out to be shown.
///
/// ⚠ **The order is the reverse of `toFrame`'s, not the same order with
/// opposite signs.** `toFrame` goes crop, straighten, perspective, turns; this
/// goes turns, perspective, straighten, crop. Applying them in the forward
/// order with negated angles is the mistake that looks right — it is only
/// equivalent when at most one of them is doing anything, which is exactly the
/// case anybody tests by hand.
///
/// `perspectiveInverse` is H⁻¹, not H: this direction undoes the correction.
/// `persp::inverse` is the only place that inversion is written.
[[nodiscard]] inline Placement fromFrame(Placement p, const Crop& c, int turns,
                                         float straightenRad = 0.0f,
                                         float pivotX = 0.5f, float pivotY = 0.5f,
                                         float frameW = 1.0f, float frameH = 1.0f,
                                         const persp::Matrix3* perspectiveInverse = nullptr) noexcept {
    float x = p.centerX;
    float y = p.centerY;

    // Back into the rotated frame. `toFrame` sends (x, y) to (y, 1 - x) once
    // per turn, so the inverse is (x, y) -> (1 - y, x), applied as many times.
    const int k = ((turns % 4) + 4) % 4;
    constexpr float kHalfPi = 1.57079632679489662f;
    for (int i = 0; i < k; ++i) {
        const float nx = 1.0f - y;
        const float ny = x;
        x = nx;
        y = ny;
    }

    // Then the perspective, undone, before the straighten is.
    float angle = p.angle + float(k) * kHalfPi;
    float scale = p.scale;
    persp::Jacobian jac = p.jac;
    unperspective(x, y, angle, scale, jac, perspectiveInverse, frameW, frameH);

    // Then the straighten, the other way about the same pivot.
    unstraighten(x, y, -straightenRad, pivotX, pivotY, frameW, frameH);

    // And out of the crop, which is what the displayed picture *is*.
    Placement out{};
    out.centerX = (x - c.x) / std::max(c.w, 1e-6f);
    out.centerY = (y - c.y) / std::max(c.h, 1e-6f);
    out.scale   = scale;
    out.jac     = jac;
    out.angle   = angle - straightenRad;
    return out;
}

/// A length on screen, as a length in the frame.
///
/// A crop magnifies: a gradient spanning half the visible width spans half of
/// the *crop*, which is a smaller fraction of the whole frame. Without this a
/// mask's feather widens every time the picture is cropped tighter.
///
/// Quarter turns do not change a length, only which axis it lies along, and a
/// gradient's length is measured along its own direction — so the turn does not
/// enter here.
[[nodiscard]] inline float lengthToFrame(float length, const Crop& c) noexcept {
    // The crop is not square in general, so a diagonal length has no single
    // scale. The geometric mean is the honest compromise and is exact whenever
    // the crop preserves the aspect, which is the overwhelmingly common case.
    return length * std::sqrt(std::max(c.w, 1e-6f) * std::max(c.h, 1e-6f));
}

/// What `radiusToFrame` answers with: an ellipse in the frame, and how far its
/// axis has turned away from `Placement::angle`.
struct Extent {
    float semiX = 0.0f;       ///< the frame ellipse's semi-axis along its angle
    float semiY = 0.0f;       ///< and the one across it
    float angleDelta = 0.0f;  ///< add to `Placement::angle`
};

/// Radial semi-axes, scaled by the crop the same way a gradient's length is.
///
/// ⚠️ **The quarter turns do not enter here, and getting that wrong is what put
/// every radial mask in the wrong place on a turned frame.** The reasoning that
/// they should was: a semi-axis has an axis of its own, unlike a length, so it
/// must swap when the picture goes on its side. What that misses is that
/// `toFrame` has *already* turned the mask — it subtracts k·π/2 from the angle,
/// and the semi-axes are measured along the mask's own axes, not the frame's.
/// Rotating the axes and then swapping the extents applies the turn twice.
///
/// The algebra, for one turn, no crop. `toFrame` sends a displaced point
/// e' = (x, y) in displayed coordinates to e = (y, −x) in the frame, and sets
/// φ = θ − π/2, so cos φ = sin θ and sin φ = −cos θ. The shader then forms
///
///     u = ( cos φ·eₓ + sin φ·e_y)/Rₓ = ( cos θ·e'ₓ + sin θ·e'_y)/Rₓ
///     v = (−sin φ·eₓ + cos φ·e_y)/R_y = (−sin θ·e'ₓ + cos θ·e'_y)/R_y
///
/// which is exactly what the interface computes with rx and ry. The two agree
/// if and only if Rₓ = rx and R_y = ry. A quarter turn in normalized
/// coordinates is rigid — it maps the unit square onto itself — so there is no
/// length change for it to contribute either.
///
/// Measured, on both a landscape and a portrait frame, at all four turns:
/// `repro/mask-alignment.txt`. With the swap, a radial mask leaked coverage
/// into 14 of the 207 cells the interface draws clear, up to 0.34 in luma.
///
/// `turns` is gone from the signature rather than ignored: a parameter a caller
/// still passes is a parameter the next reader assumes is used.
///
/// ## And the perspective, which is where the per-axis story stops being enough
///
/// A crop scales each axis on its own, so it can be applied to each semi-axis
/// on its own. A homography cannot: its derivative J at the mask's centre is a
/// general 2×2, and a general 2×2 takes a circle to an **ellipse at an angle of
/// its own**. What shipped with decision #100 was √|det J| — one isotropic
/// number standing in for two, exact only where the map happens to be conformal.
/// That is what leaked coverage past the rim of a large mask under a strong
/// keystone, and never at its centre. `research/perspective.md` has the table.
///
/// The exact image, to first order in the mask's size:
///
/// - The ellipse with semi-axes (aₓ, a_y) at angle φ is the image of the unit
///   disc under **A = R(φ)·diag(aₓ, a_y)**.
/// - Its image under J is the image of the unit disc under **B = J·A**, whose
///   semi-axis lengths are B's singular values along its left singular vectors
///   — Golub & Van Loan, *Matrix Computations*, 4th ed., JHU Press 2013,
///   §2.4.1: the image of the unit sphere under a matrix is a hyperellipse
///   whose semiaxes are σᵢ·uᵢ.
/// - Those are read off **S = B·Bᵀ**, symmetric positive semi-definite: its
///   eigenvalues are σ², its eigenvectors the axes. (Note BᵀB would give the
///   *pre*-image's axes, which is the mistake that looks identical.)
/// - The 2×2 symmetric eigenproblem is closed form — Golub & Van Loan §8.5.2,
///   the symmetric Schur decomposition Jacobi's method is built on. For
///   S = [[p, q], [q, r]]:
///
///       λ± = (p + r)/2 ± √( ((p − r)/2)² + q² ),   ψ = ½·atan2(2q, p − r)
///
/// ⚠ **This is the image under the map's *derivative*, which is not the image
/// under the map.** A projective map takes a conic to a conic but not an
/// ellipse to a similar ellipse, so what is removed here is the whole
/// first-order error — all of the anisotropy — and what remains is second
/// order, the map's curvature across the mask. Said plainly because the version
/// this replaces was also described as exact by somebody who had only checked
/// the middle of the frame.
///
/// ⚠ **Smith's closed form (CACM 1961) was *not* reused, though the repository
/// has it.** `SkyDetector.Stats.largestVariance()` is Swift, it is the 3×3
/// case, and it returns the largest eigenvalue and no eigenvector — this needs
/// both eigenvalues and an axis, in C++, on the other side of the facade.
/// Smith's construction reduces to the quadratic formula above at n = 2, so
/// what is written here is that reduction and not a second derivation.
///
/// The returned angle is a **delta**, to be added to `Placement::angle`. Two
/// reasons, and both are load-bearing: it keeps the quarter turns out of this
/// function (they are already in `Placement::angle`, decision #83), and it is
/// **exactly zero** at a neutral control, so a photograph with the perspective
/// slider at rest renders bit-for-bit as it did before any of this existed.
[[nodiscard]] inline Extent radiusToFrame(float rx, float ry, const Crop& c,
                                          const persp::Jacobian& j,
                                          float angle) noexcept {
    const float ax = rx * c.w;
    const float ay = ry * c.h;

    // ⚠ Neutral is short-circuited rather than solved, for the reason
    // `persp::inTexels` gives: the algebra below returns (ax, ay, 0) at the
    // identity in exact arithmetic and not in float.
    if (j.a == 1.0f && j.b == 0.0f && j.c == 0.0f && j.d == 1.0f) {
        return {ax, ay, 0.0f};
    }

    constexpr float kPi = 3.14159265358979324f;
    const float cs = std::cos(angle), sn = std::sin(angle);

    // B = J·R(angle)·diag(ax, ay), column by column.
    const float b00 = (j.a * cs + j.b * sn) * ax;
    const float b10 = (j.c * cs + j.d * sn) * ax;
    const float b01 = (-j.a * sn + j.b * cs) * ay;
    const float b11 = (-j.c * sn + j.d * cs) * ay;

    // S = B·Bᵀ.
    const float p = b00 * b00 + b01 * b01;
    const float q = b00 * b10 + b01 * b11;
    const float r = b10 * b10 + b11 * b11;

    const float mid  = 0.5f * (p + r);
    const float rad  = std::sqrt(0.25f * (p - r) * (p - r) + q * q);
    // Clamped at zero: S is positive semi-definite in exact arithmetic, and a
    // degenerate mask can put the smaller root a rounding error below it.
    float s1 = std::sqrt(std::max(mid + rad, 0.0f));
    float s2 = std::sqrt(std::max(mid - rad, 0.0f));
    float psi = 0.5f * std::atan2(2.0f * q, p - r);

    // Which principal axis to call "x". Either naming describes the *same*
    // ellipse — the kernel is symmetric under (semi.x ↔ semi.y, angle + π/2),
    // which holds for the superellipse exponent too since |u|ⁿ + |v|ⁿ is
    // symmetric in the two — so this is about continuity, not correctness:
    // keeping the first axis next to the one the mask itself calls x stops the
    // returned angle jumping ninety degrees as the two radii pass each other.
    //
    // The image of the mask's own x direction, which is what `unperspective`
    // put into `Placement::angle`:
    const float tx = j.a * cs + j.b * sn;
    const float ty = j.c * cs + j.d * sn;
    const float phi = (tx != 0.0f || ty != 0.0f) ? std::atan2(ty, tx) : angle;

    // ψ names an axis and not a direction, so fold the difference into
    // [−π/2, π/2] before choosing.
    float delta = psi - phi;
    delta -= kPi * std::round(delta / kPi);
    if (std::fabs(delta) > 0.25f * kPi) {
        std::swap(s1, s2);
        delta -= (delta > 0.0f ? 0.5f * kPi : -0.5f * kPi);
    }
    return {s1, s2, delta};
}

}  // namespace orion::pipe::mask
