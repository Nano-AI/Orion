/*  tests_stack_split — the shell's upright sky matte carried onto the sensor
 *  frame. The matte is drawn on the picture as displayed (the EXIF turn
 *  applied); the stacker works on the unturned sensor frame, so the matte has
 *  to be turned back. Convention (shell/Slices/Masks/SubjectMatte.cs UndoTurns):
 *  a clockwise turn sent (x, y) to (h-1-y, x), h the sensor height.
 */

#include "harness.h"
#include "stack/SkySplit.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace {

constexpr std::uint32_t kW = 600, kH = 400;   // the sensor frame, landscape

/// Mean of the sensor frame over a column (all rows) or a row (all columns).
float colMean(const std::vector<float>& m, std::uint32_t x) {
    double s = 0; for (std::uint32_t y = 0; y < kH; ++y) s += m[std::size_t(y) * kW + x];
    return float(s / kH);
}
float rowMean(const std::vector<float>& m, std::uint32_t y) {
    double s = 0; for (std::uint32_t x = 0; x < kW; ++x) s += m[std::size_t(y) * kW + x];
    return float(s / kW);
}

/// A matte whose top third (rows) is 1.
std::vector<float> topThird(int mw, int mh) {
    std::vector<float> a(std::size_t(mw) * mh, 0.0f);
    for (int y = 0; y < mh / 3; ++y) std::fill_n(a.begin() + std::ptrdiff_t(y) * mw, mw, 1.0f);
    return a;
}

}  // namespace

void testSkySplitOrientation() {
    using orion::stack::skyMatteToFrame;
    section("stack split: the upright sky matte lands on the sensor frame");
    const std::uint32_t r1 = 6, r3 = 18;   // 1% and 3% of the long edge

    // flip 6: one clockwise turn. An upright 40x60 portrait for the 600x400 frame.
    // The upright top is sensor column 0: a clockwise turn sent (x, y) to (h-1-y, x),
    // so upright row y' = sensor x. The sky is the sensor's left third (x < 200).
    {
        const int turns = orion::raw::quarterTurnsFor(6);
        CHECK(turns == 1);
        const auto up = topThird(40, 60);
        const auto m = skyMatteToFrame(up.data(), 40, 60, kW, kH, 6);
        CHECK(m.size() == std::size_t(kW) * kH);
        CHECK(colMean(m, 20) > 0.95f && colMean(m, 200 - r3 - 1) > 0.95f);
        CHECK(colMean(m, 200 + r3 + 1) < 0.05f && colMean(m, 580) < 0.05f);
        // No row-direction structure: a row is uniform in this orientation.
        CHECK(std::abs(rowMean(m, 5) - rowMean(m, 395)) < 0.02f);
        // Feathered: a mid value within 1% of the seam, none farther than 3%.
        bool mid = false, farMid = false;
        for (std::uint32_t x = 0; x < kW; ++x) {
            const float v = m[std::size_t(kH / 2) * kW + x];
            const std::uint32_t d = x > 200 ? x - 200 : 200 - x;
            if (v > 0.1f && v < 0.9f) { if (d <= r1) mid = true; if (d > r3) farMid = true; }
        }
        CHECK(mid);
        CHECK(!farMid);
    }

    // flip 3 (two turns): an upright 60x40... landscape; the top becomes the sensor bottom.
    {
        const auto up = topThird(60, 40);   // 600x400 sensor has aspect 3:2 = 60x40
        const auto m = skyMatteToFrame(up.data(), 60, 40, kW, kH, 3);
        CHECK(rowMean(m, 395) > 0.95f && rowMean(m, 5) < 0.05f);
    }

    // flip 5 (three clockwise turns): upright row y' = sensor (w-1-x): sky on the right.
    {
        const auto up = topThird(40, 60);
        const auto m = skyMatteToFrame(up.data(), 40, 60, kW, kH, 5);
        CHECK(colMean(m, 580) > 0.95f && colMean(m, 20) < 0.05f);
    }

    // flip 0: a plain resample, top stays top.
    {
        const auto up = topThird(60, 40);
        const auto m = skyMatteToFrame(up.data(), 60, 40, kW, kH, 0);
        CHECK(rowMean(m, 5) > 0.95f && rowMean(m, 395) < 0.05f);
        CHECK(std::abs(colMean(m, 300) - 1.0f / 3.0f) < 0.03f);   // a third of each column
    }
}
