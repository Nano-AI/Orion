/*  Alignment — see Align.h. This is the one translation unit that includes
 *  OpenCV; keep it that way.
 *
 *  Pipeline: exposure-normalized log-luma proxies (~1500 px long edge) -> ORB ->
 *  Hamming brute-force with cross-check -> findHomography(RANSAC) -> ECC
 *  refinement when it converges -> plausibility gate -> conjugate the
 *  proxy-scale homography back to full-resolution texels.
 *
 *  The log domain is what makes a ±2 EV pair matchable: after dividing the
 *  exposure ratio out, log2 turns the residual multiplicative error into an
 *  additive offset that ORB's intensity comparisons shrug off, and it
 *  compresses the highlights one frame clips and the other does not.
 */

#include "merge/Align.h"

// OpenCV 5 module names: features2d became features, and calib3d split —
// findHomography lives in geometry. The 4.x-compat headers exist but the
// canonical names say what is actually linked.
#include <opencv2/core.hpp>
#include <opencv2/features.hpp>
#include <opencv2/geometry.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/video/tracking.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

namespace orion::merge {
namespace {

/// The proxy's target long edge. Feature matching wants a working image big
/// enough that ORB's pyramid and border margins leave real area (a fixed
/// divide-by-four left a 512-wide test frame with a 128-wide proxy, where
/// the default 31-px edge margin ate nearly everything), and small enough
/// that a 42 MP frame does not cost seconds — ~1500 px puts the a7riii at
/// scale 5 and leaves smaller inputs at scale 1.
constexpr float kProxyLongEdge = 1500.0f;

int proxyScaleFor(const Frame& f) {
    const float longest = float(std::max(f.width, f.height));
    return std::max(1, int(std::lround(longest / kProxyLongEdge)));
}

/// The log-luma window: -13.3 is just under the a7riii's ~13-stop floor,
/// +2 leaves room for exposure-normalized values above the reference clip.
constexpr float kLogFloor = -13.3f;
constexpr float kLogCeil  = 2.0f;

/// Quarter-res 8-bit proxy: block-averaged luma, divided by the frame's
/// exposure ratio, mapped through log2 into the fixed window above.
cv::Mat proxyOf(const Frame& f, int scale) {
    const int pw = int(f.width) / scale;
    const int ph = int(f.height) / scale;
    cv::Mat proxy(ph, pw, CV_8UC1);

    const float invE = 1.0f / f.exposureRatio;
    const float toByte = 255.0f / (kLogCeil - kLogFloor);

    for (int py = 0; py < ph; ++py) {
        auto* row = proxy.ptr<std::uint8_t>(py);
        for (int px = 0; px < pw; ++px) {
            float sum = 0.0f;
            for (int dy = 0; dy < scale; ++dy) {
                const std::size_t base =
                    (std::size_t(py) * scale + std::size_t(dy)) * f.width
                    + std::size_t(px) * scale;
                for (int dx = 0; dx < scale; ++dx) {
                    const float* p = f.rgb + (base + std::size_t(dx)) * 3;
                    sum += p[0] + p[1] + p[2];
                }
            }
            const float y = sum / (3.0f * float(scale) * float(scale)) * invE;
            const float l = std::log2(std::max(y, 1e-4f));
            row[px] = std::uint8_t(std::clamp(
                (l - kLogFloor) * toByte, 0.0f, 255.0f));
        }
    }
    return proxy;
}

/// A handheld bracket moves the camera a little. An estimate that moves any
/// corner more than a quarter of the diagonal is a matching failure wearing
/// a homography, and warping by it would smear the picture — refuse it.
bool plausible(const persp::Matrix3& h, float width, float height) {
    const float diag = std::sqrt(width * width + height * height);
    const float limit = 0.25f * diag;
    const float corners[4][2] = {
        {0, 0}, {width - 1, 0}, {0, height - 1}, {width - 1, height - 1}};
    for (const auto& c : corners) {
        float x = c[0], y = c[1];
        persp::apply(h, x, y);
        if (!std::isfinite(x) || !std::isfinite(y)) return false;
        const float dx = x - c[0], dy = y - c[1];
        if (std::sqrt(dx * dx + dy * dy) > limit) return false;
    }
    return true;
}

persp::Matrix3 fromCv(const cv::Mat& h) {
    persp::Matrix3 out;
    for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 3; ++c) {
            out.m[r * 3 + c] = float(h.at<double>(r, c));
        }
    }
    return out;
}

bool debugAlign() {
    const char* v = std::getenv("ORION_DEBUG_ALIGN");
    return v != nullptr && *v == '1';
}

}  // namespace

AlignResult align(const Frame& frame, const Frame& reference) {
    AlignResult result;
    if (frame.rgb == nullptr || reference.rgb == nullptr ||
        frame.width < 64 || frame.height < 64) {
        return result;
    }

    // One scale for both proxies — the conjugation below assumes it, and a
    // bracket's frames share a sensor anyway.
    const int scale = proxyScaleFor(reference);
    const cv::Mat refProxy   = proxyOf(reference, scale);
    const cv::Mat frameProxy = proxyOf(frame, scale);

    // ORB over the proxies. 4000 features is generous for a quarter-res
    // frame; the cost is milliseconds against the decode that preceded it.
    auto orb = cv::ORB::create(4000);
    std::vector<cv::KeyPoint> refKeys, frameKeys;
    cv::Mat refDesc, frameDesc;
    orb->detectAndCompute(refProxy, cv::noArray(), refKeys, refDesc);
    orb->detectAndCompute(frameProxy, cv::noArray(), frameKeys, frameDesc);
    if (debugAlign()) {
        std::fprintf(stderr, "orion align: features ref=%d frame=%d\n",
                     refDesc.rows, frameDesc.rows);
    }
    if (refDesc.rows < 12 || frameDesc.rows < 12) return result;

    // Cross-checked Hamming matching: a pair counts only when each is the
    // other's best. Filters most of the junk before RANSAC ever votes.
    cv::BFMatcher matcher(cv::NORM_HAMMING, /*crossCheck=*/true);
    std::vector<cv::DMatch> matches;
    matcher.match(refDesc, frameDesc, matches);
    if (debugAlign()) {
        std::fprintf(stderr, "orion align: matches=%zu\n", matches.size());
    }
    if (matches.size() < 12) return result;

    std::vector<cv::Point2f> refPts, framePts;
    refPts.reserve(matches.size());
    framePts.reserve(matches.size());
    for (const auto& m : matches) {
        refPts.push_back(refKeys[std::size_t(m.queryIdx)].pt);
        framePts.push_back(frameKeys[std::size_t(m.trainIdx)].pt);
    }

    // reference -> frame, which is exactly the dest->source direction the
    // warp evaluates. RANSAC at 3 proxy pixels ≈ 12 full-res pixels of
    // tolerance before a match is an outlier.
    cv::Mat inlierMask;
    cv::Mat h = cv::findHomography(refPts, framePts, cv::RANSAC, 3.0, inlierMask);
    if (h.empty()) {
        if (debugAlign()) std::fprintf(stderr, "orion align: no homography\n");
        return result;
    }
    const int inliers = cv::countNonZero(inlierMask);
    if (debugAlign()) {
        std::fprintf(stderr, "orion align: inliers=%d\n", inliers);
    }
    if (inliers < 12) return result;

    // ECC refinement on the proxies, seeded by the RANSAC estimate. It
    // throws when it fails to converge; the RANSAC answer stands then.
    try {
        cv::Mat refF, frameF, hF;
        refProxy.convertTo(refF, CV_32F, 1.0 / 255.0);
        frameProxy.convertTo(frameF, CV_32F, 1.0 / 255.0);
        h.convertTo(hF, CV_32F);
        const cv::TermCriteria term(
            cv::TermCriteria::COUNT + cv::TermCriteria::EPS, 100, 1e-6);
        // ECC warps the second image onto the first; with (ref, frame) and
        // our ref->frame map as the seed, the refined map keeps direction.
        const double ecc =
            cv::findTransformECC(refF, frameF, hF, cv::MOTION_HOMOGRAPHY, term);
        hF.convertTo(h, CV_64F);
        if (debugAlign()) std::fprintf(stderr, "orion align: ecc=%.5f\n", ecc);
    } catch (const cv::Exception& e) {
        // keep the RANSAC estimate
        if (debugAlign()) std::fprintf(stderr, "orion align: ecc failed: %s\n", e.what());
    }

    // Conjugate proxy coordinates to full-resolution texels:
    // H_full = S · H_proxy · S⁻¹ with S = diag(s, s, 1).
    persp::Matrix3 full = fromCv(h);
    const float s = float(scale);
    full.m[2] *= s;                       // translation scales up
    full.m[5] *= s;
    full.m[6] /= s;                       // projective terms scale down
    full.m[7] /= s;

    if (!plausible(full, float(frame.width), float(frame.height))) {
        if (debugAlign()) {
            std::fprintf(stderr, "orion align: implausible warp refused\n");
        }
        return result;
    }

    result.refToSource = full;
    result.inliers = inliers;
    result.ok = true;
    return result;
}

}  // namespace orion::merge
