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
 *  mask is worth the trouble: there is nothing to resample, only a centre and
 *  an angle to move. research/masking.md §3.
 */

#pragma once

#include <algorithm>
#include <cmath>

namespace orion::pipe::mask {

/// Where the mask lives as far as the interface is concerned: normalized
/// against the cropped, rotated picture on screen.
struct Placement {
    float centreX = 0.5f, centreY = 0.5f;
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
/// The pivot is the crop's centre, in the same normalized rotated space, and it
/// is *passed* rather than derived. Deriving it from the crop origin and size is
/// what once made the preview turn about the frame centre and the committed
/// render about the crop centre, so an off-centre crop delivered a different
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
    float x = c.x + p.centreX * c.w;
    float y = c.y + p.centreY * c.h;

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
    out.centreX = x;
    out.centreY = y;

    // The angle turns with it. Anticlockwise here, because this undoes the
    // rotation the viewer sees — and the straighten is a rotation too, so it
    // enters the angle directly whatever the aspect does to the position.
    constexpr float kHalfPi = 1.57079632679489662f;
    out.angle = p.angle + straightenRad - float(k) * kHalfPi;
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

/// Radial semi-axes, which unlike a gradient's length do have an axis each —
/// and swap when the picture turns onto its side.
[[nodiscard]] inline void radiusToFrame(float rx, float ry, const Crop& c, int turns,
                                        float& outX, float& outY) noexcept {
    const int k = ((turns % 4) + 4) % 4;
    const float a = rx * c.w;
    const float b = ry * c.h;
    if (k % 2 == 0) { outX = a; outY = b; } else { outX = b; outY = a; }
}

}  // namespace orion::pipe::mask
