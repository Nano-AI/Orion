/*  tests_align — the ORB + RANSAC + ECC aligner against warps whose ground
 *  truth is known.
 *
 *  The fixture is a field of random sharp-edged rectangles — corners
 *  everywhere, which is what a feature detector eats — warped by a known
 *  homography built from the same persp:: conventions the aligner reports
 *  in. The identity pair is the exact control; the featureless pair is the
 *  negative control (an aligner that "succeeds" on a blank sky would smear
 *  real pictures); and the exposure-offset pair checks the log-luma
 *  normalization actually earns its keep.
 */

#include "harness.h"
#include "merge/Align.h"

namespace {

using orion::merge::Frame;
namespace persp = orion::pipe::persp;

constexpr std::uint32_t kW = 512;
constexpr std::uint32_t kH = 384;

// The virtual scene is larger than either frame and both frames sample it
// fully — a handheld camera's sensor is always covered; it is the *scene*
// that shifts underneath. A fixture that filled uncovered pixels with black
// would hand ECC a mismatched border no real bracket has, and it measurably
// biased the estimate by ~3 px before this was learned.
constexpr std::uint32_t kMargin = 64;
constexpr std::uint32_t kSW = kW + 2 * kMargin;
constexpr std::uint32_t kSH = kH + 2 * kMargin;

/// Random rectangles over a mid gray: dense corners, wide value range,
/// nothing periodic (a repeating pattern would give ORB aliasing to trip on).
/// Capped at 0.45 so the one-stop-brighter frame stays unclipped: features
/// that clip in one exposure genuinely look different, and real brackets
/// align on their unclipped structure.
std::vector<float> sceneTexture(std::uint32_t seed) {
    std::vector<float> rgb(std::size_t(kSW) * kSH * 3, 0.25f);
    std::mt19937 rng(seed);
    std::uniform_int_distribution<std::uint32_t> px(0, kSW - 1), py(0, kSH - 1);
    std::uniform_int_distribution<std::uint32_t> sz(6, 48);
    std::uniform_real_distribution<float> value(0.02f, 0.45f);

    for (int i = 0; i < 320; ++i) {
        const std::uint32_t x0 = px(rng), y0 = py(rng);
        const std::uint32_t w = sz(rng), h = sz(rng);
        const float v = value(rng);
        for (std::uint32_t y = y0; y < std::min(y0 + h, kSH); ++y) {
            for (std::uint32_t x = x0; x < std::min(x0 + w, kSW); ++x) {
                float* p = &rgb[(std::size_t(y) * kSW + x) * 3];
                p[0] = v; p[1] = v * 0.9f; p[2] = v * 0.8f;
            }
        }
    }
    return rgb;
}

/// One photograph of the scene: frame pixel p depicts scene point H⁻¹(p)
/// (in reference coordinates; the scene array carries a kMargin offset).
/// Bilinear, so no half-texel quantization is charged to the aligner.
std::vector<float> photograph(const std::vector<float>& scene,
                              const persp::Matrix3& h, float gain) {
    const persp::Matrix3 inv = persp::inverse(h);
    std::vector<float> out(std::size_t(kW) * kH * 3, 0.0f);
    for (std::uint32_t y = 0; y < kH; ++y) {
        for (std::uint32_t x = 0; x < kW; ++x) {
            float sx = float(x), sy = float(y);
            persp::apply(inv, sx, sy);
            sx += float(kMargin);
            sy += float(kMargin);
            const auto x0 = std::uint32_t(std::clamp(sx, 0.0f, float(kSW - 2)));
            const auto y0 = std::uint32_t(std::clamp(sy, 0.0f, float(kSH - 2)));
            const float fx = std::clamp(sx - float(x0), 0.0f, 1.0f);
            const float fy = std::clamp(sy - float(y0), 0.0f, 1.0f);
            float* d = &out[(std::size_t(y) * kW + x) * 3];
            for (int c = 0; c < 3; ++c) {
                const auto at = [&](std::uint32_t xx, std::uint32_t yy) {
                    return scene[(std::size_t(yy) * kSW + xx) * 3 + std::size_t(c)];
                };
                const float top = at(x0, y0) * (1 - fx) + at(x0 + 1, y0) * fx;
                const float bot = at(x0, y0 + 1) * (1 - fx) + at(x0 + 1, y0 + 1) * fx;
                d[c] = std::min((top + (bot - top) * fy) * gain, 1.0f);
            }
        }
    }
    return out;
}

float worstCornerError(const persp::Matrix3& got, const persp::Matrix3& want) {
    const float corners[4][2] = {
        {0, 0}, {kW - 1, 0}, {0, kH - 1}, {kW - 1, kH - 1}};
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

void testAlignRecoversHomography() {
    section("align: ORB + RANSAC recover a known handheld move");
    using orion::merge::align;

    const auto scene = sceneTexture(11);
    const auto refPixels = photograph(scene, persp::identity(), 1.0f);
    Frame ref;
    ref.width = kW; ref.height = kH;
    ref.rgb = refPixels.data();

    // A handheld move: ~0.6 degrees of roll and a 9-texel drift. Built with
    // the same Matrix3 the aligner answers in.
    persp::Matrix3 truth;
    const float th = 0.010f;
    truth.m[0] = std::cos(th); truth.m[1] = -std::sin(th); truth.m[2] = 9.0f;
    truth.m[3] = std::sin(th); truth.m[4] =  std::cos(th); truth.m[5] = -5.0f;

    // The moved frame is also one stop brighter — the aligner must see
    // through the exposure difference, not match on it.
    const auto framePixels = photograph(scene, truth, 2.0f);
    Frame moved;
    moved.width = kW; moved.height = kH;
    moved.rgb = framePixels.data();
    moved.exposureRatio = 2.0f;

    const auto got = align(moved, ref);
    report(got.ok, "a textured pair aligns");
    if (got.ok) {
        const float err = worstCornerError(got.refToSource, truth);
        report(err < 2.0f, "worst corner lands within two texels",
               "err " + std::to_string(err) + " px, inliers " +
                   std::to_string(got.inliers));
    }

    // ── Exact control: identical frames must come back as identity ────────
    Frame same;
    same.width = kW; same.height = kH;
    same.rgb = refPixels.data();
    const auto id = align(same, ref);
    report(id.ok, "an identical pair aligns");
    if (id.ok) {
        const float err = worstCornerError(id.refToSource, persp::identity());
        report(err < 0.5f, "and its homography is the identity to half a texel",
               "err " + std::to_string(err));
    }
}

void testAlignRefusesFeatureless() {
    section("align: a blank sky is a refusal, not a guess");
    using orion::merge::align;

    // Flat frames with a whisper of noise — nothing for FAST to corner on.
    std::vector<float> a(std::size_t(kW) * kH * 3, 0.4f);
    std::vector<float> b(std::size_t(kW) * kH * 3, 0.4f);
    std::mt19937 rng(3);
    std::uniform_real_distribution<float> n(-0.002f, 0.002f);
    for (auto& v : a) v += n(rng);
    for (auto& v : b) v += n(rng);

    Frame fa, fb;
    fa.width = fb.width = kW;
    fa.height = fb.height = kH;
    fa.rgb = a.data();
    fb.rgb = b.data();

    const auto got = align(fa, fb);
    report(!got.ok, "featureless frames refuse");
    report(persp::isIdentity(got.refToSource),
           "and the refusal carries an identity matrix, never garbage");
}
