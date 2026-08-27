// The pull-back: the map a mask is evaluated *through*, rather than pushed by.
//
// ⚠ **Split from `tests_mask_geom.cpp` 2026-08-07 at 1,173 lines** — over
// `CLAUDE.md`'s 1,000-line ceiling, which is a hard constraint. The cut is at a
// seam and not at a line number, and the seam is decision #138.
//
// What stayed behind grades the **forward transport**: `toFrame`,
// `radiusToFrame`, `lengthAlong` — a mask pushed into frame coordinates through
// the map's derivative at one point. ⚠ Three of those have had **no product
// caller since #138** (#184); the checks on them are pointed at live Jacobian
// maths *through* a lens the renderer no longer uses.
//
// What is here grades what the renderer **does**: one 3 × 3 folding crop,
// straighten, quarter turns and the correction, with each pixel carried back
// through it and the mask evaluated as the photographer drew it. No derivative,
// so nothing here is first order about anything.

#include "harness.h"
#include "pipe/Perspective.h"

namespace {
namespace persp = orion::pipe::persp;
}  // namespace

void testDisplayMatrixMatchesFromFrame() {
    section("The frame-to-display map as one matrix");

    namespace mg = orion::pipe::mask;

    // ⚠ Two derivations of one map is the arrangement this file already warns
    // about — `mask::toFrame` exists precisely so the shader and the interface
    // cannot drift apart. `displayMatrix` is a second way to compute what
    // `fromFrame` computes step by step, and it is only safe because of this.
    //
    // Every case below turns on something a single-feature fixture would miss:
    // a crop that is off-centre AND not square, so a translation error and a
    // scale error cannot cancel; a straighten on a non-square frame, where the
    // rotation is not a rotation in normalized coordinates; and a keystone,
    // which is the only step that is not affine.
    struct Case {
        const char* name;
        mg::Crop crop;
        int turns;
        float straightenDeg;
        float vertical, horizontal, aspect;
    };
    const Case cases[] = {
        {"neutral",            {0.0f, 0.0f, 1.0f, 1.0f}, 0,  0.0f, 0,     0,     0},
        {"crop only",          {0.13f, 0.07f, 0.61f, 0.44f}, 0, 0.0f, 0,  0,     0},
        {"one turn",           {0.0f, 0.0f, 1.0f, 1.0f}, 1,  0.0f, 0,     0,     0},
        {"three turns",        {0.0f, 0.0f, 1.0f, 1.0f}, 3,  0.0f, 0,     0,     0},
        {"straighten",         {0.0f, 0.0f, 1.0f, 1.0f}, 0,  4.5f, 0,     0,     0},
        {"keystone",           {0.0f, 0.0f, 1.0f, 1.0f}, 0,  0.0f, 0.45f, 0.30f, 0},
        {"aspect",             {0.0f, 0.0f, 1.0f, 1.0f}, 0,  0.0f, 0,     0,     1.0f},
        {"crop + turn + keystone",
                               {0.13f, 0.07f, 0.61f, 0.44f}, 1, 0.0f, 0.45f, 0.30f, 0},
        {"all four at once",   {0.21f, 0.11f, 0.55f, 0.66f}, 2, -3.5f, 1.0f, -0.4f, 0.6f},
    };

    const float W = 6000.0f, H = 4000.0f;
    constexpr float kPi = 3.14159265358979324f;

    double worst = 0.0;
    for (const Case& k : cases) {
        const persp::Params pp{k.vertical, k.horizontal, k.aspect};
        const persp::Matrix3 h = persp::compose(pp, W, H);
        const persp::Matrix3 hInv = persp::inverse(h);
        const persp::Matrix3* hp = persp::isIdentity(h) ? nullptr : &hInv;

        const float rad = k.straightenDeg * kPi / 180.0f;
        const float pivotX = k.crop.x + k.crop.w * 0.5f;
        const float pivotY = k.crop.y + k.crop.h * 0.5f;

        const persp::Matrix3 m = mg::displayMatrix(k.crop, k.turns, rad,
                                                   pivotX, pivotY, W, H, hp);

        double caseWorst = 0.0;
        for (int iy = 0; iy <= 8; ++iy) {
            for (int ix = 0; ix <= 8; ++ix) {
                const float qx = 0.02f + 0.96f * float(ix) / 8.0f;
                const float qy = 0.02f + 0.96f * float(iy) / 8.0f;

                const auto step = mg::fromFrame({qx, qy, 0.0f}, k.crop, k.turns,
                                                rad, pivotX, pivotY, W, H, hp);

                const float wgt = m.m[6] * qx + m.m[7] * qy + m.m[8];
                const float mx = (m.m[0] * qx + m.m[1] * qy + m.m[2]) / wgt;
                const float my = (m.m[3] * qx + m.m[4] * qy + m.m[5]) / wgt;

                caseWorst = std::max(caseWorst,
                    double(std::max(std::fabs(mx - step.centerX),
                                    std::fabs(my - step.centerY))));
            }
        }
        report(caseWorst < 2e-5,
               std::string("the one matrix is the step-by-step map — ") + k.name,
               "worst " + std::to_string(caseWorst));
        worst = std::max(worst, caseWorst);
    }

    // ⚠ The neutral case must be *exactly* the identity, not merely close: a
    // photograph with no crop, no turn, no straighten and no correction has to
    // render bit-for-bit as it did before this function existed, and a matrix
    // that is the identity to six places is not the identity.
    const mg::Crop none{0.0f, 0.0f, 1.0f, 1.0f};
    const persp::Matrix3 id = mg::displayMatrix(none, 0, 0.0f, 0.5f, 0.5f, W, H, nullptr);
    report(persp::isIdentity(id),
           "and it is exactly the identity when nothing is set",
           "m0 " + std::to_string(id.m[0]) + " m8 " + std::to_string(id.m[8]));

    report(worst < 2e-5, "over every case together",
           "worst " + std::to_string(worst));
}

void testRampIsTheExactPullBack() {
    section("A gradient's ramp, pulled back exactly");

    namespace mg = orion::pipe::mask;

    // ⚠ **This exists because `repro/perspective-carries-the-mask.txt` §4c
    // cannot see the denominator.** `maskcheck` asserts two things: cells the
    // overlay draws *clear* come back bit-identical, and cells it draws
    // *covered* moved. Neither says anything about the falloff band in between,
    // and dropping the projective divide — replacing the exact ratio with its
    // affine part — moves values almost entirely inside that band. That
    // mutation was green on all 32 scenario checks and all 821 in this binary
    // until this check existed.
    //
    // So this asserts the algebra directly: the two rows the kernel evaluates
    // must agree with carrying the point out to the displayed picture and
    // measuring the ramp there, which is the definition of what they mean.
    constexpr float kPi = 3.14159265358979324f;
    const float W = 6000.0f, H = 4000.0f;

    struct Case { const char* name; mg::Crop crop; int turns;
                  float straightenDeg, vertical, horizontal, aspect; };
    const Case cases[] = {
        {"neutral",        {0,0,1,1}, 0,  0.0f, 0,     0,     0},
        {"aspect",         {0,0,1,1}, 0,  0.0f, 0,     0,     1.0f},
        {"keystone",       {0,0,1,1}, 0,  0.0f, 0.45f, 0.30f, 0},
        {"keystone strong",{0,0,1,1}, 0,  0.0f, 1.0f,  0,     0},
        {"squeeze + keystone", {0,0,1,1}, 0, 0.0f, 0.45f, 0.30f, 0.6f},
        {"crop + turn + keystone", {0.13f,0.07f,0.61f,0.44f}, 1, 0.0f, 0.45f, 0.30f, 0},
        {"all four",       {0.21f,0.11f,0.55f,0.66f}, 2, -3.5f, 1.0f, -0.4f, 0.6f},
    };

    const float cx = 0.30f, cy = 0.25f, ang = 0.6f, len = 0.20f;

    for (const Case& k : cases) {
        const persp::Params pp{k.vertical, k.horizontal, k.aspect};
        const persp::Matrix3 h = persp::compose(pp, W, H);
        const persp::Matrix3 hInv = persp::inverse(h);
        const persp::Matrix3* hp = persp::isIdentity(h) ? nullptr : &hInv;

        const float rad = k.straightenDeg * kPi / 180.0f;
        const float pivotX = k.crop.x + k.crop.w * 0.5f;
        const float pivotY = k.crop.y + k.crop.h * 0.5f;

        const persp::Matrix3 m = mg::displayMatrix(k.crop, k.turns, rad,
                                                   pivotX, pivotY, W, H, hp);
        const auto r = mg::ramp(cx, cy, ang, len, m);

        const float ux = std::cos(ang) * len, uy = std::sin(ang) * len;
        const float zx = cx - ux * 0.5f, zy = cy - uy * 0.5f;
        const float uu = ux * ux + uy * uy;

        double worst = 0.0;
        for (int iy = 0; iy <= 12; ++iy) {
            for (int ix = 0; ix <= 12; ++ix) {
                const float qx = 0.02f + 0.96f * float(ix) / 12.0f;
                const float qy = 0.02f + 0.96f * float(iy) / 12.0f;

                const auto d = mg::fromFrame({qx, qy, 0.0f}, k.crop, k.turns,
                                             rad, pivotX, pivotY, W, H, hp);
                const double tExact =
                    ((d.centerX - zx) * ux + (d.centerY - zy) * uy) / uu;

                const double num = r.num[0] * qx + r.num[1] * qy + r.num[2];
                const double den = r.den[0] * qx + r.den[1] * qy + r.den[2];
                worst = std::max(worst, std::fabs(tExact - num / den));
            }
        }
        report(worst < 1e-4,
               std::string("the two rows are the ramp the photographer drew — ")
                   + k.name,
               "worst |dt| " + std::to_string(worst));
    }

    // ⚠ And the neutral case exactly, not merely closely: with no correction the
    // denominator has to be (0, 0, 1) so the ratio collapses to the affine ramp
    // it replaced, and a photograph with the sliders at rest renders as it did.
    const auto flat = mg::ramp(cx, cy, ang, len, persp::identity());
    report(flat.den[0] == 0.0f && flat.den[1] == 0.0f && flat.den[2] == 1.0f,
           "and its denominator is exactly 1 with no correction",
           std::to_string(flat.den[0]) + ", " + std::to_string(flat.den[1])
               + ", " + std::to_string(flat.den[2]));
}

void testRampDenominatorIsTheMatrix() {
    section("A ramp's denominator is the matrix's bottom row");

    namespace mg = orion::pipe::mask;

    // ⚠ **This is a check on a deletion.** `MaskComponent` carried the ramp's
    // denominator as its own three floats until 2026-08-02, and they were, field
    // for field, the bottom row of the frame-to-display matrix — `mask::ramp`
    // copied them out of it. The kernel now reads that row directly and the
    // duplicate is gone, which is only safe for as long as the two really are
    // the same three numbers. Nothing in the shipping path can notice if they
    // stop being: the host would upload a numerator built against one
    // denominator and the kernel would divide by another, which renders a
    // gradient that is merely in the wrong place.
    constexpr float kPi = 3.14159265358979324f;
    const float W = 6000.0f, H = 4000.0f;

    struct Case { const char* name; mg::Crop crop; int turns;
                  float straightenDeg, vertical, horizontal, aspect; };
    const Case cases[] = {
        {"neutral",   {0,0,1,1}, 0,  0.0f, 0,     0,     0},
        {"keystone",  {0,0,1,1}, 0,  0.0f, 0.45f, 0.30f, 0},
        {"all four",  {0.21f,0.11f,0.55f,0.66f}, 2, -3.5f, 1.0f, -0.4f, 0.6f},
    };

    bool same = true;
    double worst = 0.0;
    for (const Case& k : cases) {
        const persp::Params pp{k.vertical, k.horizontal, k.aspect};
        const persp::Matrix3 h = persp::compose(pp, W, H);
        const persp::Matrix3 hInv = persp::inverse(h);
        const persp::Matrix3* hp = persp::isIdentity(h) ? nullptr : &hInv;
        const float rad = k.straightenDeg * kPi / 180.0f;

        const persp::Matrix3 m = mg::displayMatrix(
            k.crop, k.turns, rad, k.crop.x + k.crop.w * 0.5f,
            k.crop.y + k.crop.h * 0.5f, W, H, hp);
        const auto r = mg::ramp(0.30f, 0.25f, 0.6f, 0.20f, m);

        // Bit-identical, not close: one is a copy of the other.
        for (int i = 0; i < 3; ++i) {
            if (r.den[i] != m.m[6 + i]) same = false;
            worst = std::max(worst, std::fabs(double(r.den[i]) - double(m.m[6 + i])));
        }
    }
    report(same, "the denominator the host derives is the row the kernel reads",
           "worst |d| " + std::to_string(worst));
}

void testRadialIsTheExactPullBack() {
    section("A radial mask, pulled back exactly");

    namespace mg = orion::pipe::mask;

    // The radial mask travelled to the kernel as an ellipse pushed *forward*
    // into frame coordinates — the centre through `toFrame`, the semi-axes and
    // the angle through the map's derivative at that centre. Exact to first
    // order, and incapable of being exact: a homography's derivative differs at
    // every point, so one 2x2 describes a large mask's rim only as well as the
    // map is close to linear across it.
    //
    // It now travels as the numbers the photographer set, and the kernel carries
    // each pixel back to meet them. This asserts that the kernel's arithmetic —
    // one 3x3 and a divide — is the same thing as walking `mask::fromFrame`'s
    // four steps, which is the definition of where a frame pixel lands on the
    // displayed picture. Decision #138.
    constexpr float kPi = 3.14159265358979324f;
    const float W = 6000.0f, H = 4000.0f;

    struct Case { const char* name; mg::Crop crop; int turns;
                  float straightenDeg, vertical, horizontal, aspect; };
    const Case cases[] = {
        {"neutral",        {0,0,1,1}, 0,  0.0f, 0,     0,     0},
        {"crop only",      {0.13f,0.07f,0.61f,0.44f}, 0, 0.0f, 0, 0, 0},
        {"one turn",       {0,0,1,1}, 1,  0.0f, 0,     0,     0},
        {"straighten",     {0,0,1,1}, 0, -3.5f, 0,     0,     0},
        {"aspect",         {0,0,1,1}, 0,  0.0f, 0,     0,     1.0f},
        {"keystone",       {0,0,1,1}, 0,  0.0f, 0.45f, 0.30f, 0},
        {"keystone strong",{0,0,1,1}, 0,  0.0f, 1.0f,  0,     0},
        {"crop + turn + keystone", {0.13f,0.07f,0.61f,0.44f}, 1, 0.0f, 0.45f, 0.30f, 0},
        {"all four",       {0.21f,0.11f,0.55f,0.66f}, 2, -3.5f, 1.0f, -0.4f, 0.6f},
    };

    // Deliberately large — a third of the frame across — because that is where
    // the curvature lives. At a tenth of the frame the first-order answer this
    // replaces is wrong over 0.25% of the picture and this test would be a
    // weaker statement about a better-behaved case.
    const float cx = 0.42f, cy = 0.48f, ang = 0.5f;
    const float rx = 0.34f, ry = 0.26f, feather = 0.30f;

    // The kernel's own falloff, on the kernel's own metric.
    const auto coverageAt = [&](float dx, float dy) {
        const float c = std::cos(ang), s = std::sin(ang);
        const float ex = dx - cx, ey = dy - cy;
        const float u = ( c * ex + s * ey) / rx;
        const float v = (-s * ex + c * ey) / ry;
        const float r = std::sqrt(u * u + v * v);
        const float t = std::clamp((r - (1.0f - feather)) / feather, 0.0f, 1.0f);
        return double(1.0f - t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f));
    };

    for (const Case& k : cases) {
        const persp::Params pp{k.vertical, k.horizontal, k.aspect};
        const persp::Matrix3 h = persp::compose(pp, W, H);
        const persp::Matrix3 hInv = persp::inverse(h);
        const persp::Matrix3* hp = persp::isIdentity(h) ? nullptr : &hInv;

        const float rad = k.straightenDeg * kPi / 180.0f;
        const float pivotX = k.crop.x + k.crop.w * 0.5f;
        const float pivotY = k.crop.y + k.crop.h * 0.5f;

        const persp::Matrix3 m = mg::displayMatrix(k.crop, k.turns, rad,
                                                   pivotX, pivotY, W, H, hp);

        double worst = 0.0;
        for (int iy = 0; iy < 21; ++iy) {
            for (int ix = 0; ix < 21; ++ix) {
                const float qx = (float(ix) + 0.5f) / 21.0f;
                const float qy = (float(iy) + 0.5f) / 21.0f;

                // What the kernel computes.
                const double den = double(m.m[6]) * qx + double(m.m[7]) * qy + double(m.m[8]);
                const double dx = (double(m.m[0]) * qx + double(m.m[1]) * qy + double(m.m[2])) / den;
                const double dy = (double(m.m[3]) * qx + double(m.m[4]) * qy + double(m.m[5])) / den;

                // What it is supposed to mean.
                const auto d = mg::fromFrame({qx, qy, 0.0f}, k.crop, k.turns, rad,
                                             pivotX, pivotY, W, H, hp);

                worst = std::max(worst,
                    std::fabs(coverageAt(float(dx), float(dy))
                              - coverageAt(d.centerX, d.centerY)));
            }
        }
        report(worst < 1e-4,
               std::string("the kernel's ellipse is the drawn ellipse — ") + k.name,
               "worst |dCoverage| " + std::to_string(worst));
    }

    // ── And what it bought, stated as a number ───────────────────────────────
    //
    // ⚠ **Without this the whole test above passes on the code it replaced.**
    // Everything so far compares the matrix against `fromFrame`, and both were
    // already correct; what was wrong was using a *derivative* instead of either.
    // So build the first-order ellipse the old host built and measure how far
    // apart the two answers are — near zero where the map is linear, and most of
    // a unit of coverage where it is not.
    //
    // `radiusToFrame` and `lengthAlong` are no longer on the render path. They
    // are kept, and tested above, as exactly this: the first-order answer, so
    // the improvement can be measured rather than asserted.
    struct Bought { const char* name; float vertical, horizontal, aspect; float lo, hi; };
    const Bought bought[] = {
        // An aspect squeeze is exactly linear, so it has no curvature and the
        // derivative was already exact. This row is the measurement checking
        // itself: if it ever reports a difference, the comparison is broken.
        {"aspect is linear, so nothing was bought", 0, 0, 1.0f, 0.0f, 1e-4f},
        {"a keystone at 0.45",  0.45f, 0.0f, 0.0f, 0.30f, 1.01f},
        {"a keystone at 1.00",  1.00f, 0.0f, 0.0f, 0.50f, 1.01f},
    };

    for (const Bought& b : bought) {
        const persp::Params pp{b.vertical, b.horizontal, b.aspect};
        const persp::Matrix3 h = persp::compose(pp, W, H);
        const persp::Matrix3 hInv = persp::inverse(h);
        const mg::Crop none{0, 0, 1, 1};

        const persp::Matrix3 m = mg::displayMatrix(none, 0, 0.0f, 0.5f, 0.5f,
                                                   W, H, &hInv);
        const auto placed = mg::toFrame({cx, cy, ang}, none, 0, 0.0f,
                                        0.5f, 0.5f, W, H, &h);
        const auto ext = mg::radiusToFrame(rx, ry, none, placed.jac, ang);
        const float fc = std::cos(placed.angle + ext.angleDelta);
        const float fs = std::sin(placed.angle + ext.angleDelta);

        double worst = 0.0;
        for (int iy = 0; iy < 121; ++iy) {
            for (int ix = 0; ix < 121; ++ix) {
                const float qx = (float(ix) + 0.5f) / 121.0f;
                const float qy = (float(iy) + 0.5f) / 121.0f;

                const double den = double(m.m[6]) * qx + double(m.m[7]) * qy + double(m.m[8]);
                const double dx = (double(m.m[0]) * qx + double(m.m[1]) * qy + double(m.m[2])) / den;
                const double dy = (double(m.m[3]) * qx + double(m.m[4]) * qy + double(m.m[5])) / den;
                const double now = coverageAt(float(dx), float(dy));

                const float ex = qx - placed.centerX, ey = qy - placed.centerY;
                const float u = ( fc * ex + fs * ey) / ext.semiX;
                const float v = (-fs * ex + fc * ey) / ext.semiY;
                const float r = std::sqrt(u * u + v * v);
                const float t = std::clamp((r - (1.0f - feather)) / feather, 0.0f, 1.0f);
                const double before = double(1.0f - t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f));

                worst = std::max(worst, std::fabs(now - before));
            }
        }
        report(worst >= double(b.lo) && worst <= double(b.hi),
               std::string("what the exact answer bought — ") + b.name,
               "worst |dCoverage| " + std::to_string(worst)
                   + " wanted [" + std::to_string(b.lo) + ", "
                   + std::to_string(b.hi) + "]");
    }
}

void testPlaceToFrame() {
    section("A legacy display-space mask, converted to frame space");

    namespace mg = orion::pipe::mask;
    constexpr float kPi = 3.14159265358979324f;
    const float W = 6000.0f, H = 4000.0f;

    // `placeToFrame` is the migration function: a sidecar written before masks
    // were stored in frame coordinates holds display-space numbers plus the
    // geometry they were relative to, and this converts them once, at load.
    //
    // The oracle is the renderer's own two spellings. OLD: the exact pull-back
    // the kernel shipped with #138 — carry the frame pixel out through
    // `displayMatrix` and evaluate the ellipse as drawn. NEW: evaluate the
    // converted ellipse at the frame pixel directly. The conversion takes the
    // whole map's derivative at the centre, so it is **exact** for every
    // affine geometry — crops of any aspect, quarter turns, straightens, the
    // aspect correction — and first order under a keystone, where an ellipse's
    // image is not an ellipse and no stored ellipse can be exact. Both tiers
    // are asserted, the keystone's as a band, so the residual is stated
    // rather than hidden.
    //
    // ⚠ The first cut of `placeToFrame` reassembled the pre-#137 forward
    // transport instead — `radiusToFrame`'s own per-axis crop scaling plus
    // straighten-as-pure-rotation — and this test caught both: 0.86 of
    // coverage on an angled ellipse under an anisotropic crop, 0.17 under a
    // 4.5° straighten on a 3:2 frame. Those rows are the exact tier below
    // precisely because they failed the band that tolerated them.
    const float cx = 0.42f, cy = 0.48f, ang = 0.5f;
    const float rx = 0.18f, ry = 0.13f, feather = 0.30f;

    const auto smoother = [](float t) {
        t = std::clamp(t, 0.0f, 1.0f);
        return double(1.0f - t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f));
    };
    const auto radialAt = [&](float px, float py, float mcx, float mcy,
                              float mang, float mrx, float mry) {
        const float c = std::cos(mang), s = std::sin(mang);
        const float ex = px - mcx, ey = py - mcy;
        const float u = ( c * ex + s * ey) / mrx;
        const float v = (-s * ex + c * ey) / mry;
        const float r = std::sqrt(u * u + v * v);
        return smoother((r - (1.0f - feather)) / feather);
    };

    struct Case { const char* name; mg::Crop crop; int turns;
                  float straightenDeg, vertical, horizontal, aspect;
                  double lo, hi; };
    const Case cases[] = {
        // Exact tier: the map is affine here, so the derivative is the map
        // and the residual is numerical noise alone — the Jacobian comes out
        // of float central differences, whose ~5e-6 relative error reaches
        // coverage through the falloff's slope as a few 1e-4. The keystone
        // band below sits three orders above this tier, so the two cannot be
        // confused.
        {"neutral",         {0,0,1,1},                 0, 0.0f, 0, 0, 0, 0.0, 1e-3},
        {"uniform crop",    {0.15f,0.20f,0.55f,0.55f}, 0, 0.0f, 0, 0, 0, 0.0, 1e-3},
        {"one turn",        {0,0,1,1},                 1, 0.0f, 0, 0, 0, 0.0, 1e-3},
        {"three turns",     {0,0,1,1},                 3, 0.0f, 0, 0, 0, 0.0, 1e-3},
        {"uniform crop + turn", {0.15f,0.20f,0.5f,0.5f}, 2, 0.0f, 0, 0, 0, 0.0, 1e-3},
        // Affine, so still exact — these are the rows the first cut failed.
        {"anisotropic crop", {0.10f,0.05f,0.75f,0.50f}, 0, 0.0f, 0, 0, 0, 0.0, 1e-3},
        {"straighten",       {0,0,1,1},                 0, 4.5f, 0, 0, 0, 0.0, 1e-3},
        {"aspect squeeze",   {0,0,1,1},                 0, 0.0f, 0, 0, 1.0f, 0.0, 1e-3},
        // First order: a keystone's image of an ellipse is not an ellipse.
        // The lower bound keeps the comparison honest — a zero here would
        // mean the two spellings stopped disagreeing and the test is
        // comparing something to itself.
        {"keystone 0.45",    {0,0,1,1},                 0, 0.0f, 0.45f, 0.30f, 0, 1e-3, 0.35},
    };

    for (const Case& k : cases) {
        const persp::Params pp{k.vertical, k.horizontal, k.aspect};
        const persp::Matrix3 h = persp::compose(pp, W, H);
        const persp::Matrix3 hInv = persp::inverse(h);
        const bool neutralH = persp::isIdentity(h);
        const persp::Matrix3* hFwd = neutralH ? nullptr : &h;
        const persp::Matrix3* hInvP = neutralH ? nullptr : &hInv;

        const float rad = k.straightenDeg * kPi / 180.0f;
        const float pivotX = k.crop.x + k.crop.w * 0.5f;
        const float pivotY = k.crop.y + k.crop.h * 0.5f;

        // What the renderer did with the legacy numbers: exact pull-back.
        const persp::Matrix3 m = mg::displayMatrix(k.crop, k.turns, rad,
                                                   pivotX, pivotY, W, H, hInvP);

        // What it does with the converted ones: direct frame evaluation.
        mg::PlacedShape s{};
        s.centerX = cx; s.centerY = cy; s.angle = ang;
        s.semiX = rx; s.semiY = ry;
        const mg::PlacedShape f = mg::placeToFrame(s, 2, k.crop, k.turns, rad,
                                                   W, H, hFwd);

        double worst = 0.0;
        for (int iy = 0; iy < 41; ++iy) {
            for (int ix = 0; ix < 41; ++ix) {
                const float qx = (float(ix) + 0.5f) / 41.0f;
                const float qy = (float(iy) + 0.5f) / 41.0f;

                const double den = double(m.m[6]) * qx + double(m.m[7]) * qy + double(m.m[8]);
                const double dx = (double(m.m[0]) * qx + double(m.m[1]) * qy + double(m.m[2])) / den;
                const double dy = (double(m.m[3]) * qx + double(m.m[4]) * qy + double(m.m[5])) / den;

                const double before = radialAt(float(dx), float(dy),
                                               cx, cy, ang, rx, ry);
                const double after  = radialAt(qx, qy, f.centerX, f.centerY,
                                               f.angle, f.semiX, f.semiY);
                worst = std::max(worst, std::fabs(before - after));
            }
        }
        report(worst >= k.lo && worst <= k.hi,
               std::string("the converted mask covers what the drawn one did — ")
                   + k.name,
               "worst |dCoverage| " + std::to_string(worst)
                   + " wanted [" + std::to_string(k.lo) + ", "
                   + std::to_string(k.hi) + "]");
    }

    // ── The identity conversion is the identity, bitwise ────────────────────
    //
    // A photograph with no crop, no turn, no straighten and no correction has
    // its numbers pass through untouched: the migration of a sidecar whose
    // geometry was never moved must not perturb a single float.
    {
        mg::PlacedShape s{};
        s.centerX = 0.31f; s.centerY = 0.67f; s.angle = 1.2f;
        s.length = 0.44f; s.semiX = 0.21f; s.semiY = 0.09f;
        s.brushRadius = 0.07f;
        const mg::Crop none{0, 0, 1, 1};
        for (int kind : {1, 2, 3}) {
            const mg::PlacedShape out =
                mg::placeToFrame(s, kind, none, 0, 0.0f, W, H, nullptr);
            report(out.centerX == s.centerX && out.centerY == s.centerY &&
                   out.angle == s.angle && out.length == s.length &&
                   out.semiX == s.semiX && out.semiY == s.semiY &&
                   out.brushRadius == s.brushRadius,
                   "neutral geometry converts kind " + std::to_string(kind)
                       + " to itself, bit for bit");
        }
    }

    // ── The nib rule, stated as the equality it preserves ───────────────────
    //
    // The legacy nib was `brushRadius · min(W·cropW, H·cropH)` pixels; the
    // frame-space nib is `brushRadius' · min(W, H)`. The conversion exists to
    // make those the same number, so the painted stroke's dabs do not change
    // size at the instant of migration.
    {
        const mg::Crop c{0.1f, 0.2f, 0.6f, 0.5f};
        mg::PlacedShape s{};
        s.brushRadius = 0.08f;
        const mg::PlacedShape out = mg::placeToFrame(s, 3, c, 1, 0.0f, W, H, nullptr);
        const float legacyPx = s.brushRadius * std::min(W * c.w, H * c.h);
        const float framePx  = out.brushRadius * std::min(W, H);
        report(std::fabs(legacyPx - framePx) < 1e-3f,
               "the nib's frame-pixel radius survives the conversion exactly",
               std::to_string(legacyPx) + " vs " + std::to_string(framePx));
    }

    // ── And the linear ramp, through the same two spellings ─────────────────
    //
    // OLD: t as the exact pull-back `mask::ramp` builds from the display-space
    // numbers. NEW: t evaluated directly in frame space from the converted
    // ones. Exact under turns and uniform crops, like the radial.
    {
        struct LCase { const char* name; mg::Crop crop; int turns; double hi; };
        const LCase lcases[] = {
            {"neutral",             {0,0,1,1},                 0, 1e-3},
            {"uniform crop + turn", {0.15f,0.20f,0.5f,0.5f},   3, 1e-3},
            {"one turn",            {0,0,1,1},                 1, 1e-3},
        };
        const float lcx = 0.55f, lcy = 0.40f, lang = 0.7f, llen = 0.35f;

        for (const LCase& k : lcases) {
            const persp::Matrix3 m = mg::displayMatrix(
                k.crop, k.turns, 0.0f, k.crop.x + k.crop.w * 0.5f,
                k.crop.y + k.crop.h * 0.5f, W, H, nullptr);
            const auto oldRamp = mg::ramp(lcx, lcy, lang, llen, m);

            mg::PlacedShape s{};
            s.centerX = lcx; s.centerY = lcy; s.angle = lang; s.length = llen;
            const mg::PlacedShape f = mg::placeToFrame(s, 1, k.crop, k.turns,
                                                       0.0f, W, H, nullptr);
            const persp::Matrix3 id{};
            const auto newRamp = mg::ramp(f.centerX, f.centerY, f.angle,
                                          f.length, id);

            double worst = 0.0;
            for (int iy = 0; iy < 21; ++iy) {
                for (int ix = 0; ix < 21; ++ix) {
                    const float qx = (float(ix) + 0.5f) / 21.0f;
                    const float qy = (float(iy) + 0.5f) / 21.0f;
                    const double tOld =
                        (double(oldRamp.num[0]) * qx + double(oldRamp.num[1]) * qy
                         + double(oldRamp.num[2]))
                        / (double(oldRamp.den[0]) * qx + double(oldRamp.den[1]) * qy
                           + double(oldRamp.den[2]));
                    const double tNew =
                        double(newRamp.num[0]) * qx + double(newRamp.num[1]) * qy
                        + double(newRamp.num[2]);
                    worst = std::max(worst,
                                     std::fabs(smoother(float(tOld))
                                               - smoother(float(tNew))));
                }
            }
            report(worst <= k.hi,
                   std::string("the converted ramp is the drawn ramp — ") + k.name,
                   "worst |dCoverage| " + std::to_string(worst));
        }
    }
}
