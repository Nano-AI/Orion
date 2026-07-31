// The film grain plate: determinism, statistics, and the mip chain the preview
// depends on. research/film-grain.md, decision #81.
//
// Split out of main.cpp 2026-07-31; see harness.h.

#include "harness.h"
#include "pipe/GrainPlate.h"

namespace grain = orion::pipe::grain;

namespace {

/// Mean and standard deviation of one level of the stacked plate.
struct LevelStats {
    double mean = 0.0;
    double sd = 0.0;
};

LevelStats levelStats(const std::vector<float>& plate, int level) {
    const int n = grain::kPlateSize >> level;
    const std::size_t row = static_cast<std::size_t>(grain::levelOffset(level));
    double sum = 0.0;
    for (int y = 0; y < n; ++y)
        for (int x = 0; x < n; ++x)
            sum += plate[(row + y) * grain::kPlateSize + x];
    const double mean = sum / (double(n) * n);
    double var = 0.0;
    for (int y = 0; y < n; ++y) {
        for (int x = 0; x < n; ++x) {
            const double d = plate[(row + y) * grain::kPlateSize + x] - mean;
            var += d * d;
        }
    }
    return {mean, std::sqrt(var / (double(n) * n))};
}

}  // namespace

void testGrainPlate() {
    section("Film grain plate");

    const auto plate = grain::buildPlate();
    report(plate.size() == std::size_t(grain::kPlateSize) * grain::kPlateHeight,
           "the plate is the size the shader will index");

    // ⚠ The whole export-matches-preview argument rests on this: a level must
    // start exactly where the shader's closed form says it does. The shader
    // recomputes `levelOffset` from the same expression, so a mismatch here is
    // a level read from the wrong rows — plausible noise, wrong field.
    report(grain::levelOffset(0) == 0, "level 0 starts at row 0");
    report(grain::levelOffset(1) == grain::kPlateSize, "level 1 starts below it");
    report(grain::levelOffset(2) == grain::kPlateSize + grain::kPlateSize / 2,
           "and level 2 below that");
    report(grain::levelOffset(grain::kPlateLevels - 1) + 1 <= grain::kPlateHeight,
           "the whole chain fits in the texture",
           "needs " + std::to_string(grain::levelOffset(grain::kPlateLevels - 1) + 1));

    // Level 0 is the calibrated one: `amount` is defined against unit variance,
    // so if this drifts every Amount setting silently means something else.
    const auto l0 = levelStats(plate, 0);
    checkNear(l0.mean, 0.0, 1e-6, "level 0 is zero mean");
    checkNear(l0.sd, 1.0, 1e-5, "level 0 has unit standard deviation");

    // ⚠ **The falling standard deviation down the chain is the point, not a
    // defect.** A preview pixel covering sixteen frame pixels must show the
    // variance sixteen average to. If every level were renormalised to 1.0 the
    // 1/16 preview would look exactly as grainy as the full render — which is
    // the failure the plate exists to prevent, and it would pass any check that
    // only asserted "the levels are noise".
    const auto l1 = levelStats(plate, 1);
    const auto l2 = levelStats(plate, 2);
    report(l1.sd < l0.sd * 0.95, "level 1 is calmer than level 0",
           std::to_string(l1.sd) + " against " + std::to_string(l0.sd));
    report(l2.sd < l1.sd * 0.95, "and level 2 calmer than level 1",
           std::to_string(l2.sd) + " against " + std::to_string(l1.sd));

    // Each level is still centred, or averaging would introduce a brightness
    // shift that varies with zoom — grain that lightens the preview.
    checkNear(l1.mean, 0.0, 1e-5, "level 1 is still zero mean");
    checkNear(l2.mean, 0.0, 1e-5, "level 2 is still zero mean");

    // ⚠ Correlated, not white. This is what separates film grain from sensor
    // noise, and it is the reason the plate is blurred at all. A white field
    // has ~zero correlation between neighbours; a grain field does not.
    // Measured on level 0 against its own horizontal neighbour.
    {
        double num = 0.0, den = 0.0;
        const int n = grain::kPlateSize;
        for (int y = 0; y < n; y += 4) {
            for (int x = 0; x < n - 1; ++x) {
                const double a = plate[std::size_t(y) * n + x];
                const double b = plate[std::size_t(y) * n + x + 1];
                num += a * b;
                den += a * a;
            }
        }
        const double corr = den > 0 ? num / den : 0.0;
        report(corr > 0.3, "neighbouring texels are correlated — grain has a size",
               "correlation " + std::to_string(corr));
    }

    // ⚠ Determinism, which is the property that keeps export matching preview.
    // Two builds from one seed must agree **bit for bit**, not nearly: the
    // generator is written out precisely because `std::normal_distribution`
    // gives different fields on different standard libraries from the same
    // seed.
    {
        const auto again = grain::buildPlate();
        bool identical = again.size() == plate.size();
        std::size_t differing = 0;
        for (std::size_t i = 0; identical && i < plate.size(); ++i)
            if (again[i] != plate[i]) { ++differing; identical = false; }
        report(identical, "two builds from the same seed are bit-identical",
               identical ? "" : "first difference at texel " + std::to_string(differing));

        const auto other = grain::buildPlate(12345u);
        bool same = true;
        for (std::size_t i = 0; i < 4096 && same; ++i) same = (other[i] == plate[i]);
        report(!same, "and a different seed gives a different field");
    }
}
