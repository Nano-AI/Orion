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

#include <algorithm>
#include <cmath>

namespace orion::pipe::mask {

/// Where the mask lives as far as the interface is concerned: normalized
/// against the cropped, rotated picture on screen.
struct Placement {
    float centerX = 0.5f, centerY = 0.5f;
    float angle   = 0.0f;      // radians, clockwise, as displayed
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

/// Displayed coordinates to the frame the develop stage sees.
///
/// Two steps, in this order. The crop first, because the displayed picture *is*
/// the crop: a point halfway across the visible image is halfway across the
/// crop rectangle, not halfway across the frame. Then the quarter turns, since
/// the crop is expressed against the rotated frame.
///
/// `turns` is clockwise quarter turns, matching `DevelopPipeline::quarterTurns`.
[[nodiscard]] inline Placement toFrame(Placement p, const Crop& c, int turns,
                                       float straightenRad = 0.0f,
                                       float pivotX = 0.5f, float pivotY = 0.5f,
                                       float frameW = 1.0f, float frameH = 1.0f) noexcept {
    // Into the rotated frame.
    float x = c.x + p.centerX * c.w;
    float y = c.y + p.centerY * c.h;

    // Then the straighten, in the same place the shader applies it: after the
    // crop has put the point in the rotated frame, before the turns are undone.
    unstraighten(x, y, straightenRad, pivotX, pivotY, frameW, frameH);

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

    // The angle turns with it. Anticlockwise here, because this undoes the
    // rotation the viewer sees — and the straighten is a rotation too, so it
    // enters the angle directly whatever the aspect does to the position.
    constexpr float kHalfPi = 1.57079632679489662f;
    out.angle = p.angle + straightenRad - float(k) * kHalfPi;
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
/// opposite signs.** `toFrame` goes crop, then straighten, then turns; this
/// goes turns, then straighten, then crop. Applying the three in the forward
/// order with negated angles is the mistake that looks right — it is only
/// equivalent when at most one of them is doing anything, which is exactly the
/// case anybody tests by hand.
[[nodiscard]] inline Placement fromFrame(Placement p, const Crop& c, int turns,
                                         float straightenRad = 0.0f,
                                         float pivotX = 0.5f, float pivotY = 0.5f,
                                         float frameW = 1.0f, float frameH = 1.0f) noexcept {
    float x = p.centerX;
    float y = p.centerY;

    // Back into the rotated frame. `toFrame` sends (x, y) to (y, 1 - x) once
    // per turn, so the inverse is (x, y) -> (1 - y, x), applied as many times.
    const int k = ((turns % 4) + 4) % 4;
    for (int i = 0; i < k; ++i) {
        const float nx = 1.0f - y;
        const float ny = x;
        x = nx;
        y = ny;
    }

    // Then the straighten, the other way about the same pivot.
    unstraighten(x, y, -straightenRad, pivotX, pivotY, frameW, frameH);

    // And out of the crop, which is what the displayed picture *is*.
    Placement out{};
    out.centerX = (x - c.x) / std::max(c.w, 1e-6f);
    out.centerY = (y - c.y) / std::max(c.h, 1e-6f);

    constexpr float kHalfPi = 1.57079632679489662f;
    out.angle = p.angle - straightenRad + float(k) * kHalfPi;
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
[[nodiscard]] inline void radiusToFrame(float rx, float ry, const Crop& c,
                                        float& outX, float& outY) noexcept {
    outX = rx * c.w;
    outY = ry * c.h;
}

}  // namespace orion::pipe::mask
