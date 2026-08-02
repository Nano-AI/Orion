#pragma once

/*  Perspective correction — the homography, and the zoom that keeps the frame
 *  full. research/perspective.md.
 *
 *  Hartley & Zisserman, *Multiple View Geometry in Computer Vision*, 2nd ed.,
 *  CUP 2004: §2.3 for the plane projectivity and its eight degrees of freedom,
 *  §4.1 for the Direct Linear Transformation, §4.1.2 for the inhomogeneous
 *  solve used here, §4.4.4 for the normalization this problem happens to be
 *  posed in already. Implemented from those descriptions; no GPL source was
 *  consulted.
 *
 *  Everything is posed as a map from the DESTINATION to the SOURCE, because a
 *  warp is evaluated backwards — every output pixel is written exactly once,
 *  which forward mapping cannot promise.
 */

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace orion::pipe::persp {

/// Row-major 3x3 acting on the column vector (x, y, 1).
///
/// ⚠ The default is the identity and it is *exactly* the identity. Several
/// things below depend on that: a control at zero must leave the picture
/// byte-for-byte alone, and `geometry.slang` must be able to take the same
/// branch it took before this existed.
struct Matrix3 {
    float m[9] = {1.0f, 0.0f, 0.0f,
                  0.0f, 1.0f, 0.0f,
                  0.0f, 0.0f, 1.0f};
};

[[nodiscard]] inline Matrix3 identity() noexcept { return Matrix3{}; }

[[nodiscard]] inline bool isIdentity(const Matrix3& h) noexcept {
    const Matrix3 i{};
    for (int k = 0; k < 9; ++k) {
        if (h.m[k] != i.m[k]) return false;
    }
    return true;
}

/// The three controls. All zero is neutral, and neutral is a distinct case
/// throughout rather than a value that happens to come out close to one.
struct Params {
    float vertical   = 0.0f;   ///< -1..1, converging verticals
    float horizontal = 0.0f;   ///< -1..1, converging horizontals
    float aspect     = 0.0f;   ///< -1..1, the squeeze a correction leaves
};

[[nodiscard]] inline bool isNeutral(const Params& p) noexcept {
    return p.vertical == 0.0f && p.horizontal == 0.0f && p.aspect == 0.0f;
}

/// How far a corner travels at full slider, as a fraction of the half-width.
///
/// A range rather than a constant of nature, and it is in
/// `research/UNSOURCED.md` §24 as one. What it costs is auto-scale: more
/// correction is more zoom, and more zoom magnifies the source.
inline constexpr float kCornerTravel = 0.35f;

[[nodiscard]] inline Matrix3 multiply(const Matrix3& a, const Matrix3& b) noexcept {
    Matrix3 r{};
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            r.m[i * 3 + j] = a.m[i * 3 + 0] * b.m[0 * 3 + j] +
                             a.m[i * 3 + 1] * b.m[1 * 3 + j] +
                             a.m[i * 3 + 2] * b.m[2 * 3 + j];
        }
    }
    return r;
}

/// A point through the map.
///
/// ⚠ Written as a division rather than a multiply by a reciprocal because
/// `geometry.slang` divides, and the two have to agree to the last bit or the
/// host's auto-scale is right for a transform nobody performs — the same trap
/// `pipe/LensGeometry.h` names for its polynomial.
inline void apply(const Matrix3& h, float& x, float& y) noexcept {
    const float u = h.m[0] * x + h.m[1] * y + h.m[2];
    const float v = h.m[3] * x + h.m[4] * y + h.m[5];
    const float w = h.m[6] * x + h.m[7] * y + h.m[8];
    x = u / w;
    y = v / w;
}

/// The map's derivative at a point, as [[a, b], [c, d]].
///
/// A homography is not conformal, so a direction or a length has no single
/// answer over the whole frame — it has an exact one at a point. This is what
/// carries a gradient mask's angle (exact: H takes lines to lines, and the
/// image line's direction is J applied to the original direction) and, less
/// exactly, its extent.
struct Jacobian { float a = 1.0f, b = 0.0f, c = 0.0f, d = 1.0f; };

[[nodiscard]] inline Jacobian jacobian(const Matrix3& h, float x, float y) noexcept {
    const float u = h.m[0] * x + h.m[1] * y + h.m[2];
    const float v = h.m[3] * x + h.m[4] * y + h.m[5];
    const float w = h.m[6] * x + h.m[7] * y + h.m[8];
    const float iw2 = 1.0f / (w * w);
    return { (h.m[0] * w - u * h.m[6]) * iw2,
             (h.m[1] * w - u * h.m[7]) * iw2,
             (h.m[3] * w - v * h.m[6]) * iw2,
             (h.m[4] * w - v * h.m[7]) * iw2 };
}

/// The isotropic part of the local scale — the geometric mean of the two axis
/// scales, which is the same compromise `mask::lengthToFrame` already makes for
/// a non-square crop. Exactly 1 wherever the map is the identity.
[[nodiscard]] inline float areaScale(const Matrix3& h, float x, float y) noexcept {
    const Jacobian j = jacobian(h, x, y);
    const float det = std::fabs(j.a * j.d - j.b * j.c);
    return det > 0.0f ? std::sqrt(det) : 1.0f;
}

/// The homography through four point correspondences — H&Z §4.1, the minimal
/// case, solved inhomogeneously with h33 fixed at 1 (§4.1.2).
///
/// Each correspondence (x, y) -> (X, Y) contributes two rows:
///
///     [x  y  1  0  0  0  -xX  -yX | X]
///     [0  0  0  x  y  1  -xY  -yY | Y]
///
/// Eight rows, eight unknowns, Gauss-Jordan with partial pivoting. Returns
/// false — and leaves `out` untouched — when a pivot is degenerate, which is
/// what "no three of the four are collinear" fails as.
///
/// Double precision inside deliberately: the frame is only 8 rows, the solve
/// runs once per geometry change, and a float pivot on a near-identity system
/// is a place to lose digits for nothing.
[[nodiscard]] inline bool fourPoint(const float from[4][2], const float to[4][2],
                                    Matrix3& out) noexcept {
    double a[8][9] = {};
    for (int i = 0; i < 4; ++i) {
        const double x = from[i][0], y = from[i][1];
        const double X = to[i][0],   Y = to[i][1];

        double* r0 = a[std::size_t(i) * 2 + 0];
        r0[0] = x; r0[1] = y; r0[2] = 1.0;
        r0[6] = -x * X; r0[7] = -y * X; r0[8] = X;

        double* r1 = a[std::size_t(i) * 2 + 1];
        r1[3] = x; r1[4] = y; r1[5] = 1.0;
        r1[6] = -x * Y; r1[7] = -y * Y; r1[8] = Y;
    }

    for (int c = 0; c < 8; ++c) {
        int pivot = c;
        for (int r = c + 1; r < 8; ++r) {
            if (std::fabs(a[r][c]) > std::fabs(a[pivot][c])) pivot = r;
        }
        if (std::fabs(a[pivot][c]) < 1e-12) return false;
        if (pivot != c) {
            for (int k = c; k < 9; ++k) std::swap(a[c][k], a[pivot][k]);
        }
        for (int r = 0; r < 8; ++r) {
            if (r == c) continue;
            const double f = a[r][c] / a[c][c];
            if (f == 0.0) continue;
            for (int k = c; k < 9; ++k) a[r][k] -= f * a[c][k];
        }
    }

    for (int i = 0; i < 8; ++i) out.m[i] = static_cast<float>(a[i][8] / a[i][i]);
    out.m[8] = 1.0f;
    return true;
}

/// The correction itself, in centered normalized coordinates of the rotated
/// frame: the frame is (-1, -1) to (+1, +1) whatever its pixel dimensions.
///
/// Vertical travel k fills the destination's top row from a *narrower* strip of
/// the source and its bottom row from a wider one, so the top is magnified and
/// the bottom shrunk — which is what pulls a building's converging verticals
/// apart until they are parallel. Horizontal is the same sentence with the axes
/// exchanged. research/perspective.md.
///
/// ⚠ **Neutral is short-circuited rather than solved.** Gaussian elimination
/// promises an accurate answer, not a bit-exact one, and a control sitting at
/// zero must not move the picture by a rounding error.
///
/// ⚠ Aspect is *not* a correspondence. It is an area-preserving squeeze
/// premultiplied into the same matrix, and at zero its factor is exactly 1.
[[nodiscard]] inline Matrix3 keystone(const Params& p) noexcept {
    if (isNeutral(p)) return identity();

    const float kv = std::clamp(p.vertical, -1.0f, 1.0f) * kCornerTravel;
    const float kh = std::clamp(p.horizontal, -1.0f, 1.0f) * kCornerTravel;

    float from[4][2] = {{-1.0f, -1.0f}, {1.0f, -1.0f},
                        {-1.0f,  1.0f}, {1.0f,  1.0f}};
    float to[4][2] = {};
    for (int i = 0; i < 4; ++i) {
        const float sx = from[i][0], sy = from[i][1];
        to[i][0] = sx * (1.0f + kv * sy);
        to[i][1] = sy * (1.0f + kh * sx);
    }

    Matrix3 h{};
    // A matrix that cannot be computed is a control that does nothing, rather
    // than a frame of garbage.
    if (!fourPoint(from, to, h)) return identity();

    if (p.aspect != 0.0f) {
        const float g = std::exp2(std::clamp(p.aspect, -1.0f, 1.0f) * 0.5f);
        const Matrix3 squeeze{{1.0f / g, 0.0f, 0.0f,
                               0.0f,     g,    0.0f,
                               0.0f,     0.0f, 1.0f}};
        h = multiply(h, squeeze);
    }
    return h;
}

/// The largest zoom that keeps every output pixel reading real data.
///
/// The same job decision #35 gave `lens::autoScale`, and cheaper for two
/// reasons that are worth writing down rather than leaving as a coincidence:
///
/// - **Four corners bound the rectangle.** A homography takes lines to lines,
///   so the image of the destination rectangle's boundary is the quadrilateral
///   through the four mapped corners, and a convex quad sits inside a convex
///   rectangle exactly when its vertices do. The lens correction walks 64
///   points an edge because its polynomial *bends* the edges. This does not.
/// - **Bisection is exact here, not approximate.** The image of the segment
///   {s·p : s in [0,1]} is a straight segment from H(0) to H(p), and a segment
///   leaving a convex region crosses its boundary once — so `fits` is true on
///   [0, s*] and false above it. The one way that argument fails is w changing
///   sign inside the rectangle, sending the segment through infinity; w is
///   affine in (x, y), so w > 0 at the four corners implies w > 0 on their
///   convex hull, and the corners are checked.
///
/// ⚠ Computed against the **whole frame**, never against the crop, and that is
/// what makes it compose. `CanvasLayout.constrainedCrop` already keeps the crop
/// inside the turned frame; if H maps the frame into the frame then it maps
/// anything already inside it into the frame too. Neither guarantee has to know
/// about the other, and this never needs recomputing when the rectangle moves.
///
/// Returns exactly 1.0f when nothing overreaches, so a neutral control pays no
/// zoom at all.
[[nodiscard]] inline float autoScale(const Matrix3& h) noexcept {
    // A ten-thousandth of the half-frame is far below a texel and is what keeps
    // an unreaching transform at exactly 1.0 rather than a hair under it.
    constexpr float kSlack = 1e-4f;
    static constexpr float kCorners[4][2] = {{-1.0f, -1.0f}, {1.0f, -1.0f},
                                             {-1.0f,  1.0f}, {1.0f,  1.0f}};

    const auto fits = [&](float s) noexcept {
        for (const auto& c : kCorners) {
            float x = c[0] * s, y = c[1] * s;
            const float w = h.m[6] * x + h.m[7] * y + h.m[8];
            if (!(w > 0.0f)) return false;
            apply(h, x, y);
            if (x < -1.0f - kSlack || x > 1.0f + kSlack ||
                y < -1.0f - kSlack || y > 1.0f + kSlack) {
                return false;
            }
        }
        return true;
    };

    if (fits(1.0f)) return 1.0f;

    float lo = 0.05f, hi = 1.0f;
    for (int i = 0; i < 24; ++i) {
        const float mid = 0.5f * (lo + hi);
        (fits(mid) ? lo : hi) = mid;
    }
    return lo;
}

/// Centered normalized to texel coordinates of the rotated frame — the space
/// `geometry.slang` works in, and the one `mask::toFrame` converts into.
///
///     x_texel = x_norm · (W/2) + (W-1)/2
///
/// which is the shader's own `u·W − 0.5` written for a [-1, 1] input.
///
/// ⚠ The neutral case returns the identity rather than computing T·I·T⁻¹.
/// That product is the identity in exact arithmetic and is not in float, and
/// the whole point of the neutral case is that a zeroed control cannot move a
/// pixel.
[[nodiscard]] inline Matrix3 inTexels(const Matrix3& h, float width,
                                      float height) noexcept {
    if (isIdentity(h)) return identity();

    const float ax = std::max(width,  2.0f) * 0.5f;
    const float ay = std::max(height, 2.0f) * 0.5f;
    const float bx = (std::max(width,  2.0f) - 1.0f) * 0.5f;
    const float by = (std::max(height, 2.0f) - 1.0f) * 0.5f;

    const Matrix3 t{{ax, 0.0f, bx,
                     0.0f, ay,  by,
                     0.0f, 0.0f, 1.0f}};
    const Matrix3 tInverse{{1.0f / ax, 0.0f,      -bx / ax,
                            0.0f,      1.0f / ay, -by / ay,
                            0.0f,      0.0f,      1.0f}};
    return multiply(t, multiply(h, tInverse));
}

/// The inverse map, by the adjugate. Scale is irrelevant to a homography, so
/// the determinant is divided out only to keep h33 near 1 and the numbers
/// readable; nothing downstream depends on the normalization.
[[nodiscard]] inline Matrix3 inverse(const Matrix3& h) noexcept {
    if (isIdentity(h)) return identity();

    const float a = h.m[0], b = h.m[1], c = h.m[2];
    const float d = h.m[3], e = h.m[4], f = h.m[5];
    const float g = h.m[6], i = h.m[7], j = h.m[8];

    Matrix3 out{};
    out.m[0] = e * j - f * i;
    out.m[1] = c * i - b * j;
    out.m[2] = b * f - c * e;
    out.m[3] = f * g - d * j;
    out.m[4] = a * j - c * g;
    out.m[5] = c * d - a * f;
    out.m[6] = d * i - e * g;
    out.m[7] = b * g - a * i;
    out.m[8] = a * e - b * d;

    const float det = a * out.m[0] + b * out.m[3] + c * out.m[6];
    if (std::fabs(det) < 1e-20f) return identity();
    for (int k = 0; k < 9; ++k) out.m[k] /= det;
    return out;
}

/// The whole correction as one matrix in the rotated frame's texel
/// coordinates: keystone, aspect, the auto-scale zoom and both coordinate
/// conversions, multiplied out here so the shader is handed a matrix and never
/// a chain.
///
/// `width` and `height` are the frame's dimensions **after** any quarter turns,
/// which is the space the geometry node's straighten already works in.
[[nodiscard]] inline Matrix3 compose(const Params& p, float width,
                                     float height) noexcept {
    if (isNeutral(p)) return identity();

    const Matrix3 k = keystone(p);
    if (isIdentity(k)) return identity();

    const float s = autoScale(k);
    Matrix3 centered = k;
    if (s != 1.0f) {
        const Matrix3 zoom{{s, 0.0f, 0.0f, 0.0f, s, 0.0f, 0.0f, 0.0f, 1.0f}};
        centered = multiply(k, zoom);
    }
    return inTexels(centered, width, height);
}

}  // namespace orion::pipe::persp
