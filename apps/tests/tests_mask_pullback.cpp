// The display map, and the migration that retired it from the render path.
//
// ⚠ **Split from `tests_mask_geom.cpp` 2026-08-07 at 1,173 lines** — over
// `CLAUDE.md`'s 1,000-line ceiling, which is a hard constraint. The cut is at a
// seam and not at a line number, and the seam was decision #138.
//
// The seam moved again when masks moved to **frame storage**. `displayMatrix`
// was the render path from #138 until then — each pixel carried back through
// one 3 × 3 and the mask evaluated as drawn — and three tests here graded that
// kernel spelling (`testRampIsTheExactPullBack`, `testRampDenominatorIsTheMatrix`,
// `testRadialIsTheExactPullBack`). They died with it: the kernel now reads
// stored frame coordinates and applies no geometry at all, which is what
// anchors a mask to its subject through a later crop.
//
// What lives here now: `testDisplayMatrixMatchesFromFrame`, because the matrix
// is still the one frame ↔ display derivation — serving the overlay, the
// gestures and the `maskcheck` oracle instead of the kernel — and
// `testPlaceToFrame`, which grades the **migration**: a display-space-era
// sidecar's numbers converted once, at load, into the space the kernel runs in.
// `tests_mask_geom.cpp` keeps grading the forward transport those conversions
// are built from.

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
    // OLD: t as the drawn ramp measures it — carry the frame pixel out to the
    // displayed picture through `displayMatrix` and project onto the ramp
    // there, which is the definition of what the display-space numbers meant.
    // NEW: t evaluated directly in frame space from the converted ones.
    // Exact under turns and uniform crops, like the radial.
    {
        struct LCase { const char* name; mg::Crop crop; int turns; double hi; };
        const LCase lcases[] = {
            {"neutral",             {0,0,1,1},                 0, 1e-3},
            {"uniform crop + turn", {0.15f,0.20f,0.5f,0.5f},   3, 1e-3},
            {"one turn",            {0,0,1,1},                 1, 1e-3},
        };
        const float lcx = 0.55f, lcy = 0.40f, lang = 0.7f, llen = 0.35f;
        const float ux = std::cos(lang) * llen, uy = std::sin(lang) * llen;
        const float zx = lcx - ux * 0.5f, zy = lcy - uy * 0.5f;
        const float uu = ux * ux + uy * uy;

        for (const LCase& k : lcases) {
            const persp::Matrix3 m = mg::displayMatrix(
                k.crop, k.turns, 0.0f, k.crop.x + k.crop.w * 0.5f,
                k.crop.y + k.crop.h * 0.5f, W, H, nullptr);

            mg::PlacedShape s{};
            s.centerX = lcx; s.centerY = lcy; s.angle = lang; s.length = llen;
            const mg::PlacedShape f = mg::placeToFrame(s, 1, k.crop, k.turns,
                                                       0.0f, W, H, nullptr);
            const auto newRamp = mg::ramp(f.centerX, f.centerY, f.angle,
                                          f.length);

            double worst = 0.0;
            for (int iy = 0; iy < 21; ++iy) {
                for (int ix = 0; ix < 21; ++ix) {
                    const float qx = (float(ix) + 0.5f) / 21.0f;
                    const float qy = (float(iy) + 0.5f) / 21.0f;
                    const double den = double(m.m[6]) * qx + double(m.m[7]) * qy
                                     + double(m.m[8]);
                    const double dx = (double(m.m[0]) * qx + double(m.m[1]) * qy
                                       + double(m.m[2])) / den;
                    const double dy = (double(m.m[3]) * qx + double(m.m[4]) * qy
                                       + double(m.m[5])) / den;
                    const double tOld = ((dx - zx) * ux + (dy - zy) * uy) / uu;
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
