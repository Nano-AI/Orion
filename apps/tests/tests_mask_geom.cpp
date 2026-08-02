// Where a mask sits: dabs under a transform, the frame transform both ways, and
// what a homography does to a placement and to its extent.
//
// Split out of main.cpp 2026-07-31; see harness.h.

#include "harness.h"
#include "pipe/Perspective.h"

namespace {
namespace persp = orion::pipe::persp;
}  // namespace

void testBrushDabsFollowTheFrame() {
    section("Brush dabs under crop and rotation");

    namespace mg = orion::pipe::mask;

    // An off-center asymmetric stroke: a symmetric one survives a flip, and a
    // centerd one survives a rotation, so neither would catch a mirrored axis.
    const float stroke[][2] = {{0.20f, 0.30f}, {0.35f, 0.32f}, {0.55f, 0.41f}};

    struct Case { const char* name; mg::Crop crop; int turns; float straighten; };
    const Case cases[] = {
        {"no crop, no turn",   mg::Crop{},                      0, 0.0f},
        {"one quarter turn",   mg::Crop{},                      1, 0.0f},
        {"three quarter turns",mg::Crop{},                      3, 0.0f},
        {"a tight crop",       mg::Crop{0.1f, 0.2f, 0.5f, 0.6f},0, 0.0f},
        {"crop and a turn",    mg::Crop{0.1f, 0.2f, 0.5f, 0.6f},1, 0.0f},
        {"and a straighten",   mg::Crop{0.1f, 0.2f, 0.5f, 0.6f},1, 0.05f},
    };

    for (const auto& c : cases) {
        const bool swaps = (c.turns % 2) != 0;
        const float rotW = swaps ? 4024.0f : 6024.0f;
        const float rotH = swaps ? 6024.0f : 4024.0f;
        const float pivotX = c.crop.x + c.crop.w * 0.5f;
        const float pivotY = c.crop.y + c.crop.h * 0.5f;

        double worst = 0.0;
        for (const auto& dab : stroke) {
            const auto asDab = mg::toFrame({dab[0], dab[1], 0.0f}, c.crop, c.turns,
                                           c.straighten, pivotX, pivotY, rotW, rotH);
            // The same point, placed as a gradient center. One transform or two.
            const auto asCenter = mg::toFrame({dab[0], dab[1], 0.7f}, c.crop, c.turns,
                                              c.straighten, pivotX, pivotY, rotW, rotH);
            worst = std::max(worst,
                             std::max(std::abs(double(asDab.centerX - asCenter.centerX)),
                                      std::abs(double(asDab.centerY - asCenter.centerY))));
        }
        report(worst < 1e-6,
               std::string("a dab lands where a gradient center does — ") + c.name,
               "worst " + std::to_string(worst));
    }

    // And the property that says the transform is the right one rather than
    // merely consistent: put a stroke where the subject appears, carry it
    // forward through the same rotation, and it must land back where it was put.
    for (int turns = 0; turns < 4; ++turns) {
        const bool swaps = (turns % 2) != 0;
        const float rotW = swaps ? 4024.0f : 6024.0f;
        const float rotH = swaps ? 6024.0f : 4024.0f;
        double worst = 0.0;
        for (const auto& dab : stroke) {
            const auto f = mg::toFrame({dab[0], dab[1], 0.0f}, mg::Crop{}, turns,
                                       0.0f, 0.5f, 0.5f, rotW, rotH);
            // Undo it by turning the other way the same number of times.
            float x = f.centerX, y = f.centerY;
            for (int i = 0; i < ((4 - turns) % 4); ++i) {
                const float nx = y, ny = 1.0f - x;
                x = nx; y = ny;
            }
            worst = std::max(worst, std::max(std::abs(double(x - dab[0])),
                                             std::abs(double(y - dab[1]))));
        }
        report(worst < 1e-6,
               "a stroke turned forward and back lands where it was painted, "
                   + std::to_string(turns) + " turns",
               "worst " + std::to_string(worst));
    }
}

void testMaskGeometry() {
    section("Mask geometry");

    namespace mg = orion::pipe::mask;
    const mg::Crop none{};

    // Nothing applied is nothing changed.
    {
        const auto p = mg::toFrame({0.3f, 0.7f, 0.5f}, none, 0);
        report(std::abs(p.centerX - 0.3f) < 1e-6f && std::abs(p.centerY - 0.7f) < 1e-6f &&
               std::abs(p.angle - 0.5f) < 1e-6f,
               "an unturned, uncropped frame leaves the placement alone", "");
    }

    // **The invariant that matters.** A quarter turn clockwise sends a frame
    // point (x, y) to (1 - y, x) on screen. So placing a mask where the subject
    // appears, then turning that placement forward again, must land back where
    // it was put — otherwise the mask slides off the subject the moment the
    // photograph is rotated, which is the whole reason this file exists.
    {
        const auto forward = [](float& x, float& y) {
            const float nx = 1.0f - y, ny = x;
            x = nx; y = ny;
        };

        double worst = 0.0;
        for (int turns = 0; turns < 4; ++turns) {
            for (const auto& pt : {std::pair{0.25f, 0.50f}, std::pair{0.10f, 0.90f},
                                   std::pair{0.75f, 0.30f}}) {
                const auto placed = mg::toFrame({pt.first, pt.second, 0.0f}, none, turns);
                float x = placed.centerX, y = placed.centerY;
                for (int i = 0; i < turns; ++i) forward(x, y);
                worst = std::max(worst, std::max(std::abs(double(x) - pt.first),
                                                 std::abs(double(y) - pt.second)));
            }
        }
        report(worst < 1e-6, "a placement survives being turned back through the rotation",
               "worst " + std::to_string(worst));
    }

    // Four turns is no turn.
    {
        const auto p = mg::toFrame({0.2f, 0.8f, 0.0f}, none, 4);
        report(std::abs(p.centerX - 0.2f) < 1e-6f && std::abs(p.centerY - 0.8f) < 1e-6f,
               "four quarter turns is the identity", "");
    }

    // The angle turns with the picture, or a linear gradient placed across the
    // frame would run down it after a rotation.
    {
        const auto p = mg::toFrame({0.5f, 0.5f, 0.0f}, none, 1);
        constexpr float kHalfPi = 1.57079632679489662f;
        report(std::abs(p.angle + kHalfPi) < 1e-6f,
               "one quarter turn takes ninety degrees off the angle",
               std::to_string(p.angle));
    }

    // A crop magnifies. The center of a centerd crop is the center of the
    // frame, and its corner is the crop's corner — not the frame's.
    {
        const mg::Crop c{0.25f, 0.25f, 0.5f, 0.5f};
        const auto mid = mg::toFrame({0.5f, 0.5f, 0.0f}, c, 0);
        const auto corner = mg::toFrame({0.0f, 0.0f, 0.0f}, c, 0);
        report(std::abs(mid.centerX - 0.5f) < 1e-6f && std::abs(mid.centerY - 0.5f) < 1e-6f,
               "the middle of a centerd crop is the middle of the frame", "");
        report(std::abs(corner.centerX - 0.25f) < 1e-6f &&
               std::abs(corner.centerY - 0.25f) < 1e-6f,
               "and its corner is the crop's corner, not the frame's", "");
    }

    // A gradient spanning half the visible width spans half the *crop*, which
    // is a smaller slice of the whole frame. Without this the feather widens
    // every time the picture is cropped tighter.
    {
        const mg::Crop c{0.25f, 0.25f, 0.5f, 0.5f};
        report(std::abs(mg::lengthToFrame(0.5f, c) - 0.25f) < 1e-6f,
               "a length shrinks with the crop it was measured against",
               std::to_string(mg::lengthToFrame(0.5f, c)));
        report(std::abs(mg::lengthToFrame(0.5f, none) - 0.5f) < 1e-6f,
               "and is untouched without one", "");
    }

    // ── Straighten ────────────────────────────────────────────────────────
    {
        constexpr float kDeg = 0.10471975512f;   // six degrees

        // The pivot itself cannot move, whatever the angle or the aspect.
        {
            float x = 0.4f, y = 0.6f;
            mg::unstraighten(x, y, kDeg, 0.4f, 0.6f, 6024.0f, 4024.0f);
            report(std::abs(x - 0.4f) < 1e-6f && std::abs(y - 0.6f) < 1e-6f,
                   "the straighten pivot is a fixed point", "");
        }

        // Rotating by an angle and then by its negative is the identity, which
        // is what says the transform is a rotation and not a shear.
        {
            double worst = 0.0;
            for (const auto& pt : {std::pair{0.2f, 0.3f}, std::pair{0.9f, 0.1f}}) {
                float x = pt.first, y = pt.second;
                mg::unstraighten(x, y,  kDeg, 0.5f, 0.5f, 6024.0f, 4024.0f);
                mg::unstraighten(x, y, -kDeg, 0.5f, 0.5f, 6024.0f, 4024.0f);
                worst = std::max(worst, std::max(std::abs(double(x) - pt.first),
                                                 std::abs(double(y) - pt.second)));
            }
            report(worst < 1e-6, "and undoing it returns the point exactly",
                   "worst " + std::to_string(worst));
        }

        // ⚠️ The shader rotates in the rotated frame's *pixels*, so the aspect
        // is part of the transform. If this were done in normalized space the
        // two would agree, and a mask on a 3:2 frame would drift.
        {
            float sq = 0.2f, sqy = 0.3f;
            mg::unstraighten(sq, sqy, kDeg, 0.5f, 0.5f, 1.0f, 1.0f);
            float wide = 0.2f, widey = 0.3f;
            mg::unstraighten(wide, widey, kDeg, 0.5f, 0.5f, 6024.0f, 4024.0f);
            report(std::abs(sq - wide) > 1e-4f || std::abs(sqy - widey) > 1e-4f,
                   "and a non-square frame rotates differently, as the shader does",
                   std::to_string(sq - wide) + ", " + std::to_string(sqy - widey));
        }

        // A straighten is a rotation, so it enters the mask's angle directly.
        {
            const auto p = mg::toFrame({0.5f, 0.5f, 0.0f}, none, 0,
                                       kDeg, 0.5f, 0.5f, 6024.0f, 4024.0f);
            report(std::abs(p.angle - kDeg) < 1e-6f,
                   "and it turns the mask's own angle with it",
                   std::to_string(p.angle));
        }
    }

    // ⚠️ This block used to assert the opposite, and the assertion was the bug.
    //
    // "Semi-axes have an axis each, so they swap when the picture goes on its
    // side" sounds right and is wrong: `toFrame` has already turned the mask by
    // subtracting k·π/2 from its angle, and the semi-axes are measured along
    // the mask's own axes. Swapping them as well applies the turn twice, which
    // put every radial mask in the wrong place on any frame at an odd total
    // quarter turn — including a portrait file with the rotate control never
    // touched, since its EXIF turn counts.
    //
    // The test passed for a year because it checked the transform against the
    // belief that produced it, never against the render. `repro/mask-alignment.txt`
    // checks it against the render, which is why it caught this.
    {
        // No perspective, so the derivative is the identity and the answer is
        // the crop's alone.
        const orion::pipe::persp::Jacobian flat{};
        auto e = mg::radiusToFrame(0.4f, 0.1f, none, flat, 0.0f);
        report(std::abs(e.semiX - 0.4f) < 1e-6f && std::abs(e.semiY - 0.1f) < 1e-6f,
               "radial semi-axes do not swap on a quarter turn — the angle "
               "already carries it",
               std::to_string(e.semiX) + ", " + std::to_string(e.semiY));

        // The crop does scale them, and per axis, because a crop is the one
        // part of the transform that is not rigid.
        mg::Crop tight{0.25f, 0.25f, 0.5f, 0.25f};
        e = mg::radiusToFrame(0.4f, 0.1f, tight, flat, 0.0f);
        report(std::abs(e.semiX - 0.2f) < 1e-6f && std::abs(e.semiY - 0.025f) < 1e-6f,
               "and the crop scales each along its own axis",
               std::to_string(e.semiX) + ", " + std::to_string(e.semiY));

        // ⚠ And it is *bit*-identical without a perspective, at an angle that
        // would put a rounding error into every term of the eigen-decomposition
        // if the neutral case were solved rather than short-circuited. A radial
        // mask on an uncorrected photograph must render as it did before the
        // ellipse existed, and "within 1e-6" is not that claim.
        e = mg::radiusToFrame(0.4f, 0.1f, tight, flat, 0.7f);
        report(e.semiX == 0.4f * 0.5f && e.semiY == 0.1f * 0.25f &&
               e.angleDelta == 0.0f,
               "a neutral perspective moves neither semi-axis by one bit",
               std::to_string(e.semiX) + ", " + std::to_string(e.semiY) + ", " +
               std::to_string(e.angleDelta));
    }
}

// The inverse geometry transform — `mask::fromFrame`.
//
// A spot is stored in FRAME coordinates, because dust is on the sensor and has
// to follow the subject through a crop and a turn. Drawing one therefore needs
// the transform the other way, and this is the only place in the program that
// does.
//
// ⚠ The risk is not the algebra, it is the ORDER. `toFrame` goes crop, then
// straighten, then turns; the inverse must go turns, then straighten, then
// crop. Applying the three in the forward order with negated angles is the
// mistake that looks right, and it is *exactly equivalent* whenever at most one
// of the three is doing anything — which is every case anybody checks by hand.
// So every case below turns on at least two at once.
void testMaskGeometryInverse() {
    section("Mask geometry, inverted");

    namespace mask = orion::pipe::mask;
    constexpr float kPi = 3.14159265358979324f;

    struct Case {
        const char* what;
        mask::Crop crop;
        int turns;
        float straightenDeg;
        float frameW, frameH;
    };
    const Case cases[] = {
        {"no geometry at all",        {0.0f, 0.0f, 1.0f, 1.0f}, 0,  0.0f, 6024, 4024},
        {"a crop alone",              {0.2f, 0.1f, 0.5f, 0.6f}, 0,  0.0f, 6024, 4024},
        {"one quarter turn alone",    {0.0f, 0.0f, 1.0f, 1.0f}, 1,  0.0f, 4024, 6024},
        {"a straighten alone",        {0.0f, 0.0f, 1.0f, 1.0f}, 0,  7.0f, 6024, 4024},
        // ⚠ The discriminating ones: two or three at once, where an inverse
        // that reuses the forward order stops agreeing.
        {"crop and a turn",           {0.2f, 0.1f, 0.5f, 0.6f}, 1,  0.0f, 4024, 6024},
        {"crop and a straighten",     {0.15f, 0.25f, 0.6f, 0.5f}, 0, 6.0f, 6024, 4024},
        {"a turn and a straighten",   {0.0f, 0.0f, 1.0f, 1.0f}, 3, -5.0f, 4024, 6024},
        {"all three, and an odd turn",{0.12f, 0.3f, 0.55f, 0.45f}, 1, 4.0f, 4024, 6024},
        {"all three, two turns",      {0.3f, 0.05f, 0.4f, 0.7f}, 2, -8.0f, 6024, 4024},
        {"a portrait frame's EXIF turn plus a crop",
                                      {0.05f, 0.4f, 0.9f, 0.5f}, 3,  3.0f, 4024, 6024},
    };

    const float points[][2] = {
        {0.5f, 0.5f}, {0.0f, 0.0f}, {1.0f, 1.0f}, {0.17f, 0.83f}, {0.92f, 0.08f},
    };

    for (const Case& k : cases) {
        const float rad = k.straightenDeg * kPi / 180.0f;
        const float px = k.crop.x + k.crop.w * 0.5f;
        const float py = k.crop.y + k.crop.h * 0.5f;

        double worst = 0.0, worstAngle = 0.0;
        for (const auto& pt : points) {
            const mask::Placement start{pt[0], pt[1], 0.7f};
            const auto framed = mask::toFrame(start, k.crop, k.turns, rad,
                                              px, py, k.frameW, k.frameH);
            const auto back = mask::fromFrame(framed, k.crop, k.turns, rad,
                                              px, py, k.frameW, k.frameH);
            worst = std::max(worst, double(std::abs(back.centerX - start.centerX)));
            worst = std::max(worst, double(std::abs(back.centerY - start.centerY)));
            worstAngle = std::max(worstAngle,
                                  double(std::abs(back.angle - start.angle)));
        }
        report(worst < 1e-5,
               std::string("a point survives the round trip: ") + k.what,
               std::to_string(worst));
        report(worstAngle < 1e-5,
               std::string("and so does its angle: ") + k.what,
               std::to_string(worstAngle));
    }

    // ⚠ And the round trip is not enough on its own. A pair of transforms that
    // are each wrong in mirrored ways round-trips perfectly — the same trap the
    // matte's `undoTurns` had, where the test passed under a consistent
    // reversal. So one case is pinned against a hand-computed answer.
    //
    // One clockwise quarter turn, no crop, no straighten. `toFrame` sends a
    // displayed point (x, y) to (y, 1 - x). So the frame point (0.25, 0.90)
    // came from the displayed point (1 - 0.90, 0.25) = (0.10, 0.25).
    {
        const auto out = mask::fromFrame({0.25f, 0.90f, 0.0f},
                                         {0.0f, 0.0f, 1.0f, 1.0f}, 1);
        report(std::abs(out.centerX - 0.10f) < 1e-5 &&
               std::abs(out.centerY - 0.25f) < 1e-5,
               "and one turn lands where the algebra says, not merely somewhere "
               "the forward transform agrees with",
               std::to_string(out.centerX) + ", " + std::to_string(out.centerY));
    }
}


// Decimating a Bayer mosaic for the preview pipeline.
//
// ⚠ The failure this guards against is invisible in the obvious sense: sample a
// mosaic on a stride that is not a multiple of the 2x2 cell and the red samples
// land where the demosaic expects green. `filters` still says the pattern is
// intact, so nothing downstream complains — the picture simply comes out with
// its colors wrong, and it would be very easy to blame the demosaic.


// ── Under a perspective correction ───────────────────────────────────────────
//
// Moved here from tests_perspective.cpp (decision #129). Neither reads a GPU,
// a `coordinateFrame` or a `loadMatrix`: both are `mask::toFrame`, `fromFrame`
// and `radiusToFrame` on the host, which is this file's fixture, with a
// homography passed in as the fourth transform beside the crop, the turns and
// the straighten. The kernel-side perspective checks stay where they were.

/// A mask, a brush dab and a spot all follow the corrected picture.
///
/// The failure this stops is the one this project fears most: not a crash, a
/// *plausible* wrong answer. A mask that lands half a subject away still looks
/// like a mask.
void testPerspectiveMaskGeometry() {
    section("Perspective — masks and spots follow");

    namespace mask = orion::pipe::mask;

    const float frameW = 600.0f, frameH = 400.0f;
    const auto h = persp::compose({0.7f, -0.35f, 0.15f}, frameW, frameH);
    const auto inv = persp::inverse(h);
    report(!persp::isIdentity(h), "the fixture's correction is not the identity");

    const mask::Crop crop{0.15f, 0.10f, 0.60f, 0.70f};
    constexpr int turns = 1;
    const float straighten = 5.0f * 3.14159265358979324f / 180.0f;

    // 1. A neutral matrix changes nothing at all — the same guarantee the
    //    shader's flag gives, on this side of the boundary.
    {
        const mask::Placement p{0.37f, 0.62f, 0.4f};
        const auto with = mask::toFrame(p, crop, turns, straighten, 0.45f, 0.45f,
                                        frameW, frameH, nullptr);
        const auto persp0 = persp::identity();
        const auto also = mask::toFrame(p, crop, turns, straighten, 0.45f, 0.45f,
                                        frameW, frameH, &persp0);
        report(with.centerX == also.centerX && with.centerY == also.centerY &&
               with.angle == also.angle && with.scale == also.scale,
               "an identity matrix is exactly the same as no matrix");
    }

    // 2. It actually moves the mask. Without this the round trip below would
    //    pass on a transform that does nothing.
    {
        const mask::Placement p{0.30f, 0.25f, 0.0f};
        const auto plain = mask::toFrame(p, crop, turns, straighten, 0.45f, 0.45f,
                                         frameW, frameH, nullptr);
        const auto moved = mask::toFrame(p, crop, turns, straighten, 0.45f, 0.45f,
                                         frameW, frameH, &h);
        const double d = std::hypot(double(plain.centerX) - moved.centerX,
                                    double(plain.centerY) - moved.centerY);
        report(d > 0.01, "the correction moves the mask center",
               "by " + std::to_string(d) + " of the frame");
        report(std::abs(moved.scale - 1.0f) > 1e-3,
               "and rescales what a length near it means",
               "scale " + std::to_string(moved.scale));
    }

    // 3. `fromFrame` undoes `toFrame`, with the inverse matrix and in the
    //    reverse order. A spot is stored in frame coordinates and drawn through
    //    this; an inverse that is nearly right puts every handle off its dust.
    {
        double worst = 0.0;
        for (int i = 1; i < 6; ++i) {
            for (int j = 1; j < 6; ++j) {
                const mask::Placement p{0.15f * float(i), 0.15f * float(j), 0.3f};
                const auto f = mask::toFrame(p, crop, turns, straighten,
                                             0.45f, 0.45f, frameW, frameH, &h);
                const auto back = mask::fromFrame(f, crop, turns, straighten,
                                                  0.45f, 0.45f, frameW, frameH, &inv);
                worst = std::max(worst, std::abs(double(back.centerX) - p.centerX));
                worst = std::max(worst, std::abs(double(back.centerY) - p.centerY));
                worst = std::max(worst, std::abs(double(back.angle) - p.angle));
            }
        }
        report(worst < 2e-3, "toFrame and fromFrame round-trip under a correction",
               "worst " + std::to_string(worst));
    }

    // 4. A mask center goes where the *picture* went. This is the check that
    //    ties the two halves together: the geometry node maps a displayed point
    //    to a source texel, and `mask::toFrame` must agree with it, because the
    //    mask is applied to that source.
    //
    //    Composed by hand from the shader's own steps rather than by calling
    //    the shader, so the check is readable — and the shader is separately
    //    pinned against the host matrix in `testPerspectiveShaderMatchesHost`.
    {
        const mask::Placement p{0.62f, 0.30f, 0.0f};
        const auto got = mask::toFrame(p, crop, 0, 0.0f, 0.5f, 0.5f,
                                       frameW, frameH, &h);

        // The shader, with no turns and no straighten: crop, then H, in texels.
        const float u = crop.x + p.centerX * crop.w;
        const float v = crop.y + p.centerY * crop.h;
        float rx = u * frameW - 0.5f;
        float ry = v * frameH - 0.5f;
        persp::apply(h, rx, ry);

        checkNear(double(got.centerX) * frameW - 0.5, double(rx), 0.05,
                  "a mask center lands on the texel the geometry node fetched (x)");
        checkNear(double(got.centerY) * frameH - 0.5, double(ry), 0.05,
                  "a mask center lands on the texel the geometry node fetched (y)");
    }
}

/// A mask's **extent**: the ellipse the map's derivative makes of it.
///
/// The centre and a gradient's direction were exact from the day the
/// correction shipped; the size was √|det J| — one isotropic number standing in
/// for a general 2×2. That is right only where the homography happens to be
/// conformal, and it leaked coverage past the rim of a large mask under a
/// strong keystone while staying perfect at its centre.
///
/// ⚠ **Five of the checks below fail on the isotropic version**, which is what
/// makes them checks rather than descriptions: both axes of check 1, the turn in
/// check 2, the rim invariant in check 3, and the two-to-one ratio in check 6.
/// The mutation is one line: return `{ax·s, ay·s, 0}` for `s = √|det J|` instead
/// of the eigen-decomposition. Named cases and what they do to it are on each
/// block.
///
/// ⚠ **The rest are deliberately blind to it and say so where they sit** —
/// check 4 preserves area, check 5 is the neutral control, and 6b is aimed at a
/// different mutation entirely (the conjugation, which lives in `unperspective`
/// and not here). This paragraph used to read "every check below", which was
/// wrong about four of them before 6b existed and is the same over-claim #130
/// went looking for: measured, not asserted.
void testPerspectiveMaskExtent() {
    section("Perspective — a mask's extent");

    namespace mask = orion::pipe::mask;

    const mask::Crop none{};

    // 1. **The aspect squeeze, where the old answer is not merely imprecise but
    //    empty.** Aspect is diag(1/g, g): exactly linear, exactly
    //    area-preserving, so √|det J| is exactly **1** and the isotropic version
    //    moved no semi-axis at all while the picture under the mask was
    //    stretched by g each way. Nothing about it is second order.
    {
        const float g = std::exp2(0.5f);          // full aspect travel
        const orion::pipe::persp::Jacobian squeeze{1.0f / g, 0.0f, 0.0f, g};

        const auto e = mask::radiusToFrame(0.20f, 0.20f, none, squeeze, 0.0f);
        checkNear(double(e.semiX), 0.20 / double(g), 1e-6,
                  "an axis-aligned mask stretches by the squeeze, not by its "
                  "square root (x)");
        checkNear(double(e.semiY), 0.20 * double(g), 1e-6,
                  "and the other axis the other way (y)");
        report(std::abs(e.angleDelta) < 1e-6f,
               "an axis-aligned mask under an axis-aligned squeeze does not turn",
               std::to_string(e.angleDelta));

        // ⚠ And the determinant really is 1, so the isotropic version returns
        // the mask untouched. Stated as a check so the mutation above cannot be
        // waved away as "close enough".
        const float det = squeeze.a * squeeze.d - squeeze.b * squeeze.c;
        report(std::abs(det - 1.0f) < 1e-6f,
               "the squeeze's determinant is 1, so √|det J| is blind to it",
               std::to_string(det));
    }

    // 2. **A mask at an angle turns.** Under an anisotropic map the image of an
    //    ellipse is an ellipse whose axes are *not* the images of the original
    //    axes, so the returned angle has to move — and the isotropic version
    //    returns a delta of exactly zero here, every time.
    {
        const float g = std::exp2(0.5f);
        const orion::pipe::persp::Jacobian squeeze{1.0f / g, 0.0f, 0.0f, g};
        const float angle = 0.6f;
        const auto e = mask::radiusToFrame(0.24f, 0.14f, none, squeeze, angle);
        report(std::abs(e.angleDelta) > 0.05f,
               "a mask at an angle comes out turned by an anisotropic map",
               "delta " + std::to_string(e.angleDelta));
    }

    // 3. **The invariant, and the one that would catch an algebra slip:** every
    //    point of the source ellipse's boundary, carried through J, lies on the
    //    boundary of the ellipse that comes back. That is the definition of the
    //    image, checked against the answer rather than re-derived from it.
    //
    //    Run over a shear as well as a squeeze, because a symmetric J and a
    //    diagonal one both hide a transposed term.
    {
        const orion::pipe::persp::Jacobian maps[] = {
            {1.3f, 0.0f, 0.0f, 0.8f},        // a squeeze
            {1.0f, 0.35f, 0.0f, 1.0f},       // a shear, one way
            {1.0f, 0.0f, -0.4f, 1.0f},       // and the other — B·Bᵀ vs Bᵀ·B
            {1.15f, 0.22f, -0.3f, 0.9f},     // and a general one
        };
        const mask::Crop cropped{0.1f, 0.2f, 0.55f, 0.7f};

        double worst = 0.0;
        for (const auto& j : maps) {
            for (float angle : {0.0f, 0.4f, 1.1f, -0.9f}) {
                for (auto rr : {std::pair{0.30f, 0.18f}, std::pair{0.12f, 0.12f},
                                std::pair{0.05f, 0.31f}}) {
                    const auto e = mask::radiusToFrame(rr.first, rr.second,
                                                       cropped, j, angle);
                    const float ax = rr.first * cropped.w;
                    const float ay = rr.second * cropped.h;
                    const float ca = std::cos(angle), sa = std::sin(angle);
                    // ⚠ The delta is relative to `Placement::angle` — the
                    // *image* of the mask's own direction — and not to the
                    // angle that went in. That is what keeps the quarter turns
                    // out of `radiusToFrame`, and it is the one thing about its
                    // contract a caller can get wrong while still compiling.
                    const float phi = std::atan2(j.c * ca + j.d * sa,
                                                 j.a * ca + j.b * sa);
                    const float cb = std::cos(phi + e.angleDelta);
                    const float sb = std::sin(phi + e.angleDelta);

                    for (int k = 0; k < 32; ++k) {
                        const float t = 6.28318530717958648f * float(k) / 32.0f;
                        // A point on the source ellipse's rim.
                        const float px = ax * std::cos(t) * ca - ay * std::sin(t) * sa;
                        const float py = ax * std::cos(t) * sa + ay * std::sin(t) * ca;
                        // Through the map.
                        const float qx = j.a * px + j.b * py;
                        const float qy = j.c * px + j.d * py;
                        // And into the returned ellipse's own frame, exactly as
                        // `mask_component.slang` forms it.
                        const double u = double( cb * qx + sb * qy) / double(e.semiX);
                        const double v = double(-sb * qx + cb * qy) / double(e.semiY);
                        worst = std::max(worst, std::abs(u * u + v * v - 1.0));
                    }
                }
            }
        }
        report(worst < 1e-4,
               "every point of the mask's rim, mapped, lands on the rim of the "
               "ellipse that comes back",
               "worst |u²+v²−1| " + std::to_string(worst));
    }

    // 4. **Area is preserved to |det J|**, which the isotropic version also gets
    //    right — deliberately. It is here so a future rewrite that gets the
    //    shape right and the size wrong is caught by something, and it is *not*
    //    sufficient on its own: check 1 is the one with teeth.
    {
        const orion::pipe::persp::Jacobian j{1.15f, 0.22f, -0.3f, 0.9f};
        const auto e = mask::radiusToFrame(0.30f, 0.18f, none, j, 0.7f);
        const double det = std::abs(double(j.a) * j.d - double(j.b) * j.c);
        checkNear(double(e.semiX) * e.semiY, 0.30 * 0.18 * det, 1e-6,
                  "the ellipse's area is the mask's times |det J|");
    }

    // 5. **A neutral control is bit-identical**, not close. Every uncorrected
    //    photograph in the library renders through this line.
    {
        const orion::pipe::persp::Jacobian flat{};
        const mask::Crop cropped{0.1f, 0.2f, 0.55f, 0.7f};
        bool exact = true;
        for (float angle : {0.0f, 0.4f, 1.1f, -0.9f}) {
            const auto e = mask::radiusToFrame(0.30f, 0.18f, cropped, flat, angle);
            exact = exact && e.semiX == 0.30f * cropped.w &&
                    e.semiY == 0.18f * cropped.h && e.angleDelta == 0.0f;
        }
        report(exact, "with no correction the crop's answer comes back to the bit");
    }

    // 6. **The whole path, through `toFrame`.** Checks 1–5 hand the derivative
    //    in by hand; this one asks whether the pipeline's own `Placement`
    //    carries it, in normalized coordinates and not in texels. Getting the
    //    conjugation W⁻¹JW wrong on a 3:2 frame is a plausible wrong answer of
    //    exactly the kind this file exists for: it is invisible on the square
    //    fixtures and wrong on every photograph.
    //
    // ⚠ **Two fixtures, and the second one is here because the first cannot
    //    keep that promise.** A pure aspect squeeze has a *diagonal* Jacobian,
    //    so b = c = 0 and the conjugation multiplies two zeros: deleting it
    //    outright left every check in this file green (#129). The squeeze block
    //    stays, because what it does assert — that a shape and not just an area
    //    survives the trip — it asserts well. The conjugation needs an
    //    off-diagonal derivative, which means a keystone, and that is the block
    //    after it.
    {
        const float frameW = 600.0f, frameH = 400.0f;
        const auto h = persp::compose({0.0f, 0.0f, 1.0f}, frameW, frameH);
        report(!persp::isIdentity(h), "a pure aspect squeeze is not the identity");

        const mask::Placement p{0.5f, 0.5f, 0.0f};
        const auto placed = mask::toFrame(p, none, 0, 0.0f, 0.5f, 0.5f,
                                          frameW, frameH, &h);

        const auto e = mask::radiusToFrame(0.20f, 0.20f, none, placed.jac, 0.0f);
        const double ratio = double(e.semiY) / double(e.semiX);
        // Two full travels of g = 2^½ apart, one per axis. The isotropic
        // version returns this ratio as exactly 1, whatever the frame.
        checkNear(ratio, 2.0, 2e-2,
                  "a round mask comes out of the pipeline's own placement "
                  "stretched two to one under a full aspect squeeze");

        // ⚠ And `Placement::scale` is not wrong so much as *blind*: it is the
        // geometric mean of the two, so it carries the area exactly and the
        // shape not at all. Written as a check because it is the reason the old
        // code looked right — a mask under a pure squeeze covered the correct
        // number of pixels, in the wrong ones.
        checkNear(double(e.semiX) * e.semiY, 0.04 * double(placed.scale) * placed.scale,
                  1e-6, "and the isotropic scale it replaced carries their area");

        // ⚠ And the reason the block above cannot be the whole check, stated as
        // a check so a future fixture cannot quietly go back to being blind: the
        // squeeze's derivative is diagonal, and every one of the conjugation's
        // two corrected terms is multiplied by a zero here.
        report(placed.jac.b == 0.0f && placed.jac.c == 0.0f,
               "the squeeze's derivative is diagonal, so the conjugation is a "
               "no-op on it",
               "b " + std::to_string(placed.jac.b) + ", c " +
                   std::to_string(placed.jac.c));
    }

    // 6b. **The keystone: the fixture that can see the conjugation.**
    //
    //     A converging-verticals correction is not axis-aligned anywhere but the
    //     centre line, so its derivative has real off-diagonal terms — and the
    //     conjugation scales those two by W/H and H/W, which on 3:2 is 1.5 and
    //     0.667. That is the mistake the block above describes and cannot catch.
    //
    //     The answer is checked against **where the pipeline itself puts the
    //     mask's neighbours**: a central difference of `toFrame`'s own centres.
    //     That is an independent derivation rather than the same algebra twice,
    //     because a *position* never passes through the conjugated matrix — it
    //     goes into texels, through `persp::apply`, and back out divided by the
    //     frame. So the two agree only if `Placement::jac` really is the
    //     derivative of the map the placement performs, expressed in the
    //     normalized coordinates its own documentation claims.
    {
        const float frameW = 600.0f, frameH = 400.0f;
        const auto h = persp::compose({0.8f, 0.6f, 0.0f}, frameW, frameH);
        report(!persp::isIdentity(h), "a two-way keystone is not the identity");

        // Off the centre line in both axes, and at four mask angles: b and c
        // vanish on the axes of symmetry, which is how a fixture ends up blind.
        struct Spot { float x, y, angle; };
        const Spot spots[] = {{0.30f, 0.25f,  0.0f}, {0.30f, 0.25f,  0.7f},
                              {0.45f, 0.60f, -1.1f}, {0.20f, 0.45f,  0.4f}};

        constexpr float kPi = 3.14159265358979324f;
        constexpr float kStep = 2e-3f;   // ~1 texel; the map is smooth over it
        double worstJac = 0.0, worstAxis = 0.0;
        double leastOffDiagonal = 1e9;

        for (const auto& s : spots) {
            const auto placed = mask::toFrame({s.x, s.y, s.angle}, none, 0, 0.0f,
                                              0.5f, 0.5f, frameW, frameH, &h);
            leastOffDiagonal = std::min(leastOffDiagonal,
                                        double(std::min(std::fabs(placed.jac.b),
                                                        std::fabs(placed.jac.c))));

            const auto centre = [&](float dx, float dy) {
                const auto o = mask::toFrame({s.x + dx, s.y + dy, 0.0f}, none, 0,
                                             0.0f, 0.5f, 0.5f, frameW, frameH, &h);
                return std::pair<float, float>{o.centerX, o.centerY};
            };
            const auto px = centre(kStep, 0.0f), mx = centre(-kStep, 0.0f);
            const auto py = centre(0.0f, kStep), my = centre(0.0f, -kStep);
            const persp::Jacobian fd{(px.first  - mx.first ) / (2.0f * kStep),
                                     (py.first  - my.first ) / (2.0f * kStep),
                                     (px.second - mx.second) / (2.0f * kStep),
                                     (py.second - my.second) / (2.0f * kStep)};

            worstJac = std::max(worstJac,
                                std::max(std::max(std::fabs(double(placed.jac.a - fd.a)),
                                                  std::fabs(double(placed.jac.b - fd.b))),
                                         std::max(std::fabs(double(placed.jac.c - fd.c)),
                                                  std::fabs(double(placed.jac.d - fd.d)))));

            // And what the difference does to the mask the kernel is actually
            // handed: `DevelopMask.cpp` sends the ellipse out at
            // `placed.angle + ext.angleDelta`, with `ext` taken at the mask's
            // *source* angle. Formed the same way here from each derivative.
            const float cs = std::cos(s.angle), sn = std::sin(s.angle);
            const auto got  = mask::radiusToFrame(0.20f, 0.14f, none, placed.jac, s.angle);
            const auto want = mask::radiusToFrame(0.20f, 0.14f, none, fd, s.angle);
            const float fdAngle = std::atan2(fd.c * cs + fd.d * sn,
                                             fd.a * cs + fd.b * sn);
            float d = (placed.angle + got.angleDelta) - (fdAngle + want.angleDelta);
            d -= kPi * std::round(d / kPi);      // an axis, not a direction
            worstAxis = std::max(worstAxis, std::fabs(double(d)));
        }

        std::printf("  keystone: least |off-diagonal| %.4f, worst dJ %.2e, "
                    "worst axis %.2e rad\n",
                    leastOffDiagonal, worstJac, worstAxis);

        // The fixture's own premise, asserted rather than assumed. Without this
        // the two checks below pass on a fixture that has quietly become
        // diagonal again, which is exactly how the squeeze block got away with
        // naming a mutation it could not see.
        report(leastOffDiagonal > 0.02,
               "the keystone's derivative is off-diagonal at every spot, which "
               "the squeeze's is nowhere",
               "least " + std::to_string(leastOffDiagonal));

        report(worstJac < 1e-3,
               "the placement's derivative is the one the pipeline's own "
               "neighbouring centres trace out, in normalized coordinates",
               "worst " + std::to_string(worstJac));

        report(worstAxis < 5e-3,
               "so the ellipse handed to the kernel points where those "
               "neighbours say it points",
               "worst " + std::to_string(worstAxis) + " rad");
    }
}
