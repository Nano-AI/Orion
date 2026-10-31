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
                                detectStars(ref.rgb.data(), kW, kH), kW, kH);
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
                                detectStars(refHot.data(), kW, kH), kW, kH);
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
    const auto few = alignStars(four, refStars, kW, kH);
    report(!few.ok && persp::isIdentity(few.refToSource), "4 stars refuse, with identity");

    const auto other = stackfix::starField(32, 80, kW, kH, persp::identity(), kNoise, kBackground);
    const auto unrelated = alignStars(detectStars(other.rgb.data(), kW, kH), refStars, kW, kH);
    report(!unrelated.ok && persp::isIdentity(unrelated.refToSource),
           "an unrelated field refuses, with identity",
           "inliers " + std::to_string(unrelated.inliers));
}

namespace {

/// The sky's turn over `seconds` as a tripod camera sees it: a pure rotation about the celestial pole,
/// which images as H = K·R·K⁻¹ ([HZ04] §8.4.2). A W x H frame, focal length fPx in pixels, principal
/// point at the centre; the camera looks at azimuth azDeg (from north, toward east), altitude altDeg,
/// from latitude latDeg. Reference pixel -> frame pixel, as starField draws.
persp::Matrix3 skyTurn(double W, double H, double fPx, double seconds, double latDeg, double altDeg, double azDeg) {
    const double rad = 3.14159265358979 / 180.0;
    const double alt = altDeg * rad, az = azDeg * rad, lat = latDeg * rad;
    // World: x east, y north, z up. Camera rows: right, down, forward.
    const double fwd[3] = {std::sin(az) * std::cos(alt), std::cos(az) * std::cos(alt), std::sin(alt)};
    const double rn = std::hypot(fwd[0], fwd[1]);
    const double right[3] = {fwd[1] / rn, -fwd[0] / rn, 0.0};   // fwd x up
    const double down[3] = {fwd[1] * right[2] - fwd[2] * right[1], fwd[2] * right[0] - fwd[0] * right[2],
                            fwd[0] * right[1] - fwd[1] * right[0]};   // fwd x right
    const double* C[3] = {right, down, fwd};
    // Rodrigues about the pole, at the sidereal rate of 15.041"/s.
    const double a[3] = {0.0, std::cos(lat), std::sin(lat)};
    const double t = seconds * 15.041 / 3600.0 * rad, c = std::cos(t), s = std::sin(t);
    double Rw[3][3];
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) {
            const double cross = i == j ? 0.0 : ((i + 1) % 3 == j ? -a[3 - i - j] : a[3 - i - j]);
            Rw[i][j] = (i == j ? c : 0.0) + (1 - c) * a[i] * a[j] + s * cross;
        }
    double R[3][3] = {};   // C · Rw · Cᵀ, the turn in camera axes
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
            for (int k = 0; k < 3; ++k)
                for (int l = 0; l < 3; ++l) R[i][j] += C[i][k] * Rw[k][l] * C[j][l];
    const double K[3][3] = {{fPx, 0, W / 2}, {0, fPx, H / 2}, {0, 0, 1}};
    const double Ki[3][3] = {{1 / fPx, 0, -W / 2 / fPx}, {0, 1 / fPx, -H / 2 / fPx}, {0, 0, 1}};
    double KR[3][3] = {}, Hm[3][3] = {};
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
            for (int k = 0; k < 3; ++k) KR[i][j] += K[i][k] * R[k][j];
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
            for (int k = 0; k < 3; ++k) Hm[i][j] += KR[i][k] * Ki[k][j];
    persp::Matrix3 m;
    for (int k = 0; k < 9; ++k) m.m[k] = float(Hm[k / 3][k % 3] / Hm[2][2]);
    return m;
}

}  // namespace

void testStarAlignWideField() {
    section("stack stars: a 14 mm sky turn registers as a homography");

    // Full frame at half resolution, 3000 x 2000 at 12 um: 14 mm is 1167 px, the same field of view as
    // 6000 x 4000 at 2333 px for a quarter of the memory. 200 s of sidereal turn, latitude 45 deg,
    // looking south at 20 deg altitude. At full resolution the best similarity misses this by ~15 px
    // median, ~43 px max, and the 2 px RANSAC gate refused the old similarity fit outright.
    constexpr std::uint32_t W = 3000, H = 2000;
    const auto truth = skyTurn(W, H, 14.0 / 0.012, 200.0, 45.0, 20.0, 180.0);
    const auto refStars =
        detectStars(stackfix::starField(51, 400, W, H, persp::identity(), kNoise, kBackground, 301).rgb.data(), W, H);
    const auto moved = stackfix::starField(51, 400, W, H, truth, kNoise, kBackground, 302);
    const auto got = alignStars(detectStars(moved.rgb.data(), W, H), refStars, W, H);

    // Every reference star that lands in the frame, through the fit vs through the truth.
    std::vector<float> err;
    for (const Star& s : moved.reference) {
        float tx = s.x, ty = s.y, gx = s.x, gy = s.y;
        persp::apply(truth, tx, ty);
        if (tx < 0 || ty < 0 || tx > float(W - 1) || ty > float(H - 1)) continue;
        persp::apply(got.refToSource, gx, gy);
        err.push_back(std::hypot(gx - tx, gy - ty));
    }
    std::sort(err.begin(), err.end());
    const float med = err.empty() ? 1e9f : err[err.size() / 2], worst = err.empty() ? 1e9f : err.back();
    std::printf("    measured median %.4f px, max %.4f px over %zu stars, %d inliers (ok %d)\n",
                med, worst, err.size(), got.inliers, int(got.ok));
    report(got.ok, "a 14 mm, 200 s sky turn registers", "inliers " + std::to_string(got.inliers));
    report(got.ok && med < 0.02f && worst < 0.1f, "median under 0.02 px, max under 0.1 px",
           std::to_string(med) + " / " + std::to_string(worst) + " px");
}

void testStarAlignRefusesFolds() {
    section("stack stars: a homography that folds or explodes the frame is a refusal");

    CHECK(orion::stack::wellConditioned(persp::identity(), kW, kH));
    CHECK(orion::stack::wellConditioned(skyTurn(kW, kH, 14.0 / 0.006 * kW / 6000.0, 200.0, 45.0, 20.0, 180.0), kW, kH));
    persp::Matrix3 far, behind, mirror;
    far.m[6] = 1.5f / float(kW);       // w = 2.5 at the right-hand corners
    behind.m[6] = -1.5f / float(kW);   // w = -0.5 there: those corners map from behind the camera
    mirror.m[0] = -1.0f;               // x -> kW - 1 - x: Jacobian determinant -1
    mirror.m[2] = float(kW - 1);
    report(!orion::stack::wellConditioned(far, kW, kH), "w over 2 at a corner refuses");
    report(!orion::stack::wellConditioned(behind, kW, kH), "w under 0.5 at a corner refuses");
    report(!orion::stack::wellConditioned(mirror, kW, kH), "a mirror (Jacobian determinant < 0) refuses");

    // End to end: a mirrored field has the same triangles (side ratios survive a reflection), and a
    // homography can fit a reflection exactly. It must not register.
    const auto ref = stackfix::starField(31, 80, kW, kH, persp::identity(), kNoise, kBackground);
    const auto refStars = detectStars(ref.rgb.data(), kW, kH);
    std::vector<Star> flipped = refStars;
    for (Star& s : flipped) s.x = float(kW - 1) - s.x;
    const auto got = alignStars(flipped, refStars, kW, kH);
    std::printf("    mirrored field: ok %d, %d inliers\n", int(got.ok), got.inliers);
    report(!got.ok && persp::isIdentity(got.refToSource), "a mirrored field refuses, with identity",
           "inliers " + std::to_string(got.inliers));
    report(got.inliers >= 8, "the refusal still reports the fit's inliers", std::to_string(got.inliers));
}
