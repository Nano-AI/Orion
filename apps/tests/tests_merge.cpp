/*  tests_merge — the HDR merge core against a synthetic bracket whose true
 *  radiance is known.
 *
 *  The fixture is a scene X in reference exposure units, photographed three
 *  times through the merge's own forward model: y = clamp(X·E + noise, 0, 1)
 *  with Var = a·y + b. Because the model is shared, every deviation the
 *  checks see is the estimator's, not the fixture's.
 *
 *  House idioms: the zero-noise stack is the exact control (an estimator
 *  that flattens or rescales cannot pass it), and the huge-ghost-threshold
 *  run is the positive control for deghosting — it proves the contamination
 *  the deghost test claims to remove is actually detectable when let in.
 */

#include "harness.h"
#include "merge/Merge.h"

namespace {

using orion::merge::Frame;

constexpr std::uint32_t kW = 160;
constexpr std::uint32_t kH = 120;

// The scene, in reference exposure units. Bands by x, blocks by y.
float radiance(std::uint32_t x, std::uint32_t y) {
    if (x >= 120 && y < 40)  return 6.0f;   // beyond every frame's reach
    if (x >= 120 && y >= 80) return 0.3f;   // the ghost block's true value
    if (x < 40)   return 0.4f;              // well-exposed in the reference
    if (x < 80)   return 0.02f;             // deep shadow
    return 2.0f;                            // highlight: only the short frame sees it
}

struct Stack {
    std::vector<std::vector<float>> pixels;   // per frame, W*H*3
    std::vector<Frame> frames;
};

/// exposures[] are E_i; shift shifts the *scene* right by `shiftX` texels in
/// that frame (and sets the matching dest->source homography). ghostInFrame
/// paints a moving subject into one frame's ghost block.
Stack makeStack(const std::vector<float>& exposures, int reference,
                float noiseA, float noiseB, std::uint32_t seed,
                int shiftFrame = -1, float shiftX = 0.0f,
                int ghostFrame = -1) {
    Stack st;
    st.pixels.resize(exposures.size());
    std::mt19937 rng(seed);
    std::normal_distribution<float> unit(0.0f, 1.0f);

    for (std::size_t i = 0; i < exposures.size(); ++i) {
        auto& px = st.pixels[i];
        px.resize(std::size_t(kW) * kH * 3);
        const float e = exposures[i];
        const float dx = (int(i) == shiftFrame) ? shiftX : 0.0f;

        for (std::uint32_t y = 0; y < kH; ++y) {
            for (std::uint32_t x = 0; x < kW; ++x) {
                // The scene point this frame's pixel (x, y) depicts sits at
                // (x - dx) in reference coordinates.
                const auto sx = std::uint32_t(std::clamp(
                    int(std::lround(float(x) - dx)), 0, int(kW) - 1));
                float X = radiance(sx, y);
                if (int(i) == ghostFrame && sx >= 120 && y >= 80) {
                    // The subject moved between frames. Darker on purpose: a
                    // brighter ghost would saturate the long frame and be
                    // rejected by w_sat, which would make the deghost test
                    // pass for the wrong reason.
                    X = 0.05f;
                }
                for (int c = 0; c < 3; ++c) {
                    float v = X * e;
                    if (noiseA > 0.0f || noiseB > 0.0f) {
                        const float var =
                            std::max(noiseA * std::min(v, 1.0f) + noiseB, 0.0f);
                        v += unit(rng) * std::sqrt(var);
                    }
                    px[(std::size_t(y) * kW + x) * 3 + std::size_t(c)] =
                        std::clamp(v, 0.0f, 1.0f);
                }
            }
        }

        Frame f;
        f.width = kW; f.height = kH;
        f.rgb = px.data();
        f.exposureRatio = e;
        f.noiseA = noiseA; f.noiseB = noiseB;
        if (int(i) == shiftFrame) {
            // Reference (x, y) -> this frame (x + dx, y).
            f.refToSource.m[2] = dx;
        }
        st.frames.push_back(f);
    }
    (void)reference;
    return st;
}

float meanOver(const std::vector<float>& rgb,
               std::uint32_t x0, std::uint32_t x1,
               std::uint32_t y0, std::uint32_t y1) {
    double sum = 0.0;
    std::size_t n = 0;
    for (std::uint32_t y = y0; y < y1; ++y) {
        for (std::uint32_t x = x0; x < x1; ++x) {
            for (int c = 0; c < 3; ++c) {
                sum += rgb[(std::size_t(y) * kW + x) * 3 + std::size_t(c)];
            }
            n += 3;
        }
    }
    return float(sum / double(n));
}

float varianceAgainst(const std::vector<float>& rgb, float truth,
                      std::uint32_t x0, std::uint32_t x1,
                      std::uint32_t y0, std::uint32_t y1) {
    double sum = 0.0;
    std::size_t n = 0;
    for (std::uint32_t y = y0; y < y1; ++y) {
        for (std::uint32_t x = x0; x < x1; ++x) {
            for (int c = 0; c < 3; ++c) {
                const double d =
                    rgb[(std::size_t(y) * kW + x) * 3 + std::size_t(c)] - truth;
                sum += d * d;
            }
            n += 3;
        }
    }
    return float(sum / double(n));
}

}  // namespace

void testMergeExposureMaths() {
    section("merge: exposure ratios");
    using namespace orion::merge;

    // t·ISO/N²: a quarter of the shutter is a quarter of the light.
    const float a = lightGathered(1.0f / 100.0f, 100.0f, 8.0f);
    const float b = lightGathered(1.0f / 400.0f, 100.0f, 8.0f);
    checkNear(a / b, 4.0, 1e-5, "shutter ratio carries through");
    CHECK(lightGathered(0.0f, 100.0f, 8.0f) == 0.0f);

    // EXIF stands inside a sixth of a stop; the pixels win outside it.
    checkNear(resolveExposureRatio(2.0f, 0.0f), 2.0, 0.0, "no measurement: EXIF stands");
    checkNear(resolveExposureRatio(2.0f, 2.05f), 2.0, 0.0, "small disagreement: EXIF stands");
    checkNear(resolveExposureRatio(2.0f, 2.5f), 2.5, 0.0, "large disagreement: pixels win");

    // The measured ratio, from a synthetic pair a true factor 2 apart.
    auto st = makeStack({2.0f, 1.0f}, 1, 0.0f, 0.0f, 7);
    const float measured =
        measuredExposureRatio(st.frames[0], st.frames[1]);
    checkNear(measured, 2.0, 0.02, "median-of-ratios finds the true factor");
}

void testMergeReconstruction() {
    section("merge: reconstruction against known radiance");
    using orion::merge::merge;

    const std::vector<float> ev = {4.0f, 1.0f, 0.25f};
    const float H = 4.0f;   // 1 / min(E)

    // ── The exact control: no noise, consistent frames ────────────────────
    {
        auto st = makeStack(ev, 1, 0.0f, 0.0f, 1);
        const auto out = merge(st.frames, 1);
        checkNear(out.headroomEv, 2.0, 1e-6, "headroom is the short frame's two stops");

        checkNear(meanOver(out.rgb, 8, 32, 8, 112), 0.4f / H, 1e-4,
                  "well-exposed band recovers exactly");
        checkNear(meanOver(out.rgb, 48, 72, 8, 112), 0.02f / H, 1e-4,
                  "shadow band recovers exactly");
        checkNear(meanOver(out.rgb, 88, 112, 8, 112), 2.0f / H, 1e-3,
                  "highlight the reference clipped is recovered from the short frame");
        checkNear(meanOver(out.rgb, 124, 156, 4, 36), 1.0, 1e-6,
                  "clipped-everywhere block falls back to the shortest frame's bound");
        report(meanOver(out.rgb, 8, 32, 8, 112) != meanOver(out.rgb, 48, 72, 8, 112),
               "bands stay distinct — nothing flattened");
    }

    // ── Noise: the merge must beat the reference alone in the shadows ─────
    {
        const float a = 4e-5f, b = 1e-6f;
        auto st = makeStack(ev, 1, a, b, 42);
        const auto out = merge(st.frames, 1);

        const float truth = 0.02f / H;
        const float vMerged = varianceAgainst(out.rgb, truth, 48, 72, 8, 112);

        // The reference alone, run through the same estimator: same scaling,
        // no help from the long exposure.
        std::vector<Frame> refOnly = {st.frames[1], st.frames[2]};
        // Keep the short frame so H — and therefore the output scale — is
        // identical; in the shadows its weight is negligible.
        const auto solo = merge(refOnly, 0);
        const float vSolo = varianceAgainst(solo.rgb, truth, 48, 72, 8, 112);

        report(vMerged < vSolo * 0.5f,
               "the long exposure at least halves shadow variance",
               "merged " + std::to_string(vMerged) + " vs reference " +
                   std::to_string(vSolo));
        checkNear(meanOver(out.rgb, 88, 112, 8, 112), 2.0f / H, 5e-3,
                  "highlights stay calibrated under noise");
    }
}

void testMergeDeghostAndCoverage() {
    section("merge: deghosting and non-overlap");
    using orion::merge::merge;

    const std::vector<float> ev = {4.0f, 1.0f, 0.25f};
    const float H = 4.0f;
    const float a = 4e-5f, b = 1e-6f;

    // A subject that moved in the long frame must not reach the output...
    {
        auto st = makeStack(ev, 1, a, b, 42, -1, 0.0f, /*ghostFrame=*/0);
        const auto out = merge(st.frames, 1);
        checkNear(meanOver(out.rgb, 124, 156, 84, 116), 0.3f / H, 2e-3,
                  "ghost block holds the reference's value");

        // ...and the positive control: with the gate effectively off, the
        // same contamination lands. A deghost test that cannot fail when
        // ghosting is let in would prove nothing by passing above.
        orion::merge::Options open;
        open.ghostSigmas = 1e9f;
        const auto polluted = merge(st.frames, 1, open);
        report(std::abs(meanOver(polluted.rgb, 124, 156, 84, 116) - 0.3f / H) > 0.03f,
               "with the gate open the ghost demonstrably lands");
    }

    // A frame that does not cover the border contributes nothing there —
    // and never black. Zero-noise, so the check is exact.
    {
        auto st = makeStack(ev, 1, 0.0f, 0.0f, 3, /*shiftFrame=*/0, /*shiftX=*/12.0f);
        const auto out = merge(st.frames, 1);
        // dest->source adds +12, so the long frame runs out on the RIGHT
        // edge: reference x >= 148 maps past its last column. That strip is
        // highlight (X = 2), which the short frame still covers — the value
        // must be exact, with no darkening from the absent frame.
        checkNear(meanOver(out.rgb, 150, 160, 48, 72), 2.0f / H, 1e-3,
                  "uncovered border pixels carry the frames that do cover them");
        checkNear(meanOver(out.rgb, 40, 72, 48, 72), 0.02f / H, 1e-4,
                  "covered interior agrees through the warp");
    }
}
