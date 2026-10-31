/*  tests_stack_stars — star detection and triangle-matched registration
 *  against a synthetic sky whose truth is known (stack_fixture.h).
 *
 *  The pure-noise frame is the detector's negative control; the 4-star frame
 *  (real stars of the reference, just too few) and the unrelated field are
 *  the aligner's: a registration that "succeeds" on them would smear a stack.
 */

#include "harness.h"
#include "stack_fixture.h"

namespace {

using orion::stack::Star;
using orion::stack::alignStars;
using orion::stack::detectStars;
namespace persp = orion::pipe::persp;

constexpr std::uint32_t kW = 640;
constexpr std::uint32_t kH = 480;
constexpr float kBackground = 0.02f;
constexpr float kNoise = 0.003f;

/// Detected stars within `tol` px of some true star.
int matched(const std::vector<Star>& got, const std::vector<Star>& truth, float tol) {
    int n = 0;
    for (const Star& s : got) {
        float best = 1e9f;
        for (const Star& t : truth) best = std::min(best, std::hypot(s.x - t.x, s.y - t.y));
        n += best < tol;
    }
    return n;
}

float worstCornerError(const persp::Matrix3& got, const persp::Matrix3& want) {
    const float corners[4][2] = {{0, 0}, {kW - 1, 0}, {0, kH - 1}, {kW - 1, kH - 1}};
    float worst = 0.0f;
    for (const auto& c : corners) {
        float gx = c[0], gy = c[1], wx = c[0], wy = c[1];
        persp::apply(got, gx, gy);
        persp::apply(want, wx, wy);
        worst = std::max(worst, std::hypot(gx - wx, gy - wy));
    }
    return worst;
}

}  // namespace

void testStarDetect() {
    section("stack stars: detection finds the true stars and nothing else");

    const auto field = stackfix::starField(5, 80, kW, kH, persp::identity(), kNoise, kBackground);
    const auto stars = detectStars(field.rgb.data(), kW, kH);
    const int hits = matched(stars, field.truth, 0.15f);
    std::printf("    measured %d of %zu detected within 0.15 px of a true star\n", hits, stars.size());
    report(stars.size() == 60, "the 60 brightest come back", std::to_string(stars.size()));
    report(hits >= 55, "at least 55 of them land within 0.15 px of a true star",
           std::to_string(hits) + " of " + std::to_string(stars.size()));
    bool sorted = true;
    for (std::size_t i = 1; i < stars.size(); ++i) sorted &= stars[i - 1].flux >= stars[i].flux;
    report(sorted, "brightest first");

    const auto blank = stackfix::starField(6, 0, kW, kH, persp::identity(), kNoise, kBackground);
    const auto none = detectStars(blank.rgb.data(), kW, kH);
    std::printf("    measured %zu stars in pure noise\n", none.size());
    report(none.size() < 3, "a pure-noise frame yields fewer than 3 stars",
           std::to_string(none.size()));

    std::vector<float> mask(std::size_t(kW) * kH, 1.0f);
    std::fill(mask.begin() + std::ptrdiff_t(kW) * (kH / 2), mask.end(), 0.0f);
    const auto sky = detectStars(field.rgb.data(), kW, kH, mask.data());
    bool top = !sky.empty();
    for (const Star& s : sky) top &= s.y < float(kH / 2);
    report(top && sky.size() > 10, "a mask over the bottom half keeps only top-half stars",
           std::to_string(sky.size()) + " stars");
}

void testStarAlignRecovers() {
    section("stack stars: triangle matching recovers the sky's rotation");

    const auto truth = stackfix::rotation(1.7f, 900.0f, -300.0f, 12.5f, -7.25f);
    const auto ref = stackfix::starField(21, 80, kW, kH, persp::identity(), kNoise, kBackground, 101);
    const auto moved = stackfix::starField(21, 80, kW, kH, truth, kNoise, kBackground, 202);

    const auto got = alignStars(detectStars(moved.rgb.data(), kW, kH),
                                detectStars(ref.rgb.data(), kW, kH));
    report(got.ok, "a rotated sky registers", "inliers " + std::to_string(got.inliers));
    const float err = worstCornerError(got.refToSource, truth);
    std::printf("    measured max corner error %.4f px, %d inliers\n", err, got.inliers);
    report(got.ok && err < 0.3f, "the four corners land within 0.3 px",
           std::to_string(err) + " px");

    // Hot pixels sit still on the sensor while the sky turns: if they were
    // taken for stars, their identical triangles would vote for identity.
    // A spike's flux is about its amplitude, a star's about 9x its peak, so
    // against this fixture's bright stars (peak 0.05-0.8) spikes of 8 are
    // what outrank them all — a real sky's mostly faint stars against
    // spikes of 1. Measured: without the sharpness test this fails.
    auto refHot = ref.rgb, movedHot = moved.rgb;
    stackfix::addSpikes(refHot, kW, kH, 77, 40, 8.0f);
    stackfix::addSpikes(movedHot, kW, kH, 77, 40, 8.0f);
    const auto hot = alignStars(detectStars(movedHot.data(), kW, kH),
                                detectStars(refHot.data(), kW, kH));
    const float hotErr = worstCornerError(hot.refToSource, truth);
    std::printf("    with 40 hot pixels: max corner error %.4f px, %d inliers\n", hotErr, hot.inliers);
    report(hot.ok && hotErr < 0.3f, "40 fixed hot pixels outranking every star do not pull it to identity",
           std::to_string(hotErr) + " px");

    auto spikes = stackfix::starField(41, 0, kW, kH, persp::identity(), kNoise, kBackground).rgb;
    stackfix::addSpikes(spikes, kW, kH, 77, 40, 1.0f);
    const auto spikeStars = detectStars(spikes.data(), kW, kH);
    report(spikeStars.size() < 3, "hot pixels alone are not stars",
           std::to_string(spikeStars.size()));
}

void testStarAlignRefuses() {
    section("stack stars: too few or unrelated stars are a refusal");

    const auto ref = stackfix::starField(31, 80, kW, kH, persp::identity(), kNoise, kBackground);
    const auto refStars = detectStars(ref.rgb.data(), kW, kH);

    const std::vector<Star> four(refStars.begin(),
                                refStars.begin() + std::ptrdiff_t(std::min<std::size_t>(4, refStars.size())));
    const auto few = alignStars(four, refStars);
    report(!few.ok && persp::isIdentity(few.refToSource), "4 stars refuse, with identity");

    const auto other = stackfix::starField(32, 80, kW, kH, persp::identity(), kNoise, kBackground);
    const auto unrelated = alignStars(detectStars(other.rgb.data(), kW, kH), refStars);
    report(!unrelated.ok && persp::isIdentity(unrelated.refToSource),
           "an unrelated field refuses, with identity",
           "inliers " + std::to_string(unrelated.inliers));
}
