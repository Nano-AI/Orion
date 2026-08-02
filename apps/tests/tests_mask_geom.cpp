// Where a mask sits: dabs under a transform, and the frame transform both ways.
//
// Split out of main.cpp 2026-07-31; see harness.h.

#include "harness.h"

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
