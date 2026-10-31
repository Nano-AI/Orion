/*  Star registration — see StarAlign.h for the sources. With merge/Align.cpp
 *  and util/ImageWriterPortable.cpp, one of the three translation units that
 *  include OpenCV.
 *
 *  Detection [BA96][S87]: luma -> Gaussian blur sigma 1 -> background median
 *  and 1.4826*MAD on a sparse grid -> smoothed > L + 5 sigma (and mask) ->
 *  8-connected components of area 2-400 -> flux-weighted centroid of the
 *  *unsmoothed* luma above L. A component whose brightest pixel carries over
 *  half its flux is a hot pixel, not a star (sharpness, [S87]).
 *
 *  Matching [G86][V95][BCS20]: each star and its 4 nearest neighbours give
 *  C(5,3) = 10 triangles; a triangle's shape is (L2/L1, L1/L0) with sides
 *  L2 >= L1 >= L0, and its vertices are ordered by the side they face, so two
 *  triangles of one shape pair their stars. Every vertex pair of every matched
 *  triangle is a vote; RANSAC [FB81] keeps the votes one homography explains.
 *  A homography, not a similarity: a tripod camera sees the sky turn as a
 *  pure 3D rotation, which images as H = K·R·K⁻¹ [HZ04] §8.4.2 — at 14 mm
 *  over 200 s the best similarity misses by ~15 px median, ~43 px max.
 *  Lens distortion is the remaining ceiling: no homography models it.
 *
 *  The constants (5 sigma, area 2-400, peak share 0.5, 60 stars, radius 0.1,
 *  2 px, 8 inliers)
 *  are research/UNSOURCED.md §32's.
 */

#include "stack/StarAlign.h"

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
// findHomography: geometry on OpenCV 5, calib3d on 4 (#376).
#if __has_include(<opencv2/geometry.hpp>)
#include <opencv2/geometry.hpp>
#else
#include <opencv2/calib3d.hpp>
#endif

#include <algorithm>
#include <array>
#include <cmath>
#include <set>
#include <utility>

namespace orion::stack {
namespace {

constexpr int   kMaxStars    = 60;
constexpr int   kNeighbours  = 4;
constexpr float kMatchRadius = 0.1f;
constexpr int   kMinInliers  = 8;   // a homography has 8 degrees of freedom: margin over its 4 points
/// DAOPHOT's sharpness test [S87]: a component whose brightest unsmoothed
/// pixel carries more than this share of its flux is a hot pixel or a cosmic
/// ray, not a star. A sigma 1.2 star's peak carries ~0.11, a sigma 0.7 one
/// ~0.32, a lone spike ~1. research/UNSOURCED.md §32.
constexpr float kMaxPeakShare = 0.5f;

/// Median of v (reorders it).
float median(std::vector<float>& v) {
    auto mid = v.begin() + std::ptrdiff_t(v.size() / 2);
    std::nth_element(v.begin(), mid, v.end());
    return *mid;
}

struct Triangle {
    float a = 0, b = 0;   ///< L2/L1, L1/L0
    int v[3] = {};        ///< star indices, facing L0, L1, L2
};

std::vector<Triangle> triangles(const std::vector<Star>& s) {
    const int n = std::min(int(s.size()), kMaxStars);
    const auto dist = [&](int i, int j) { return std::hypot(s[i].x - s[j].x, s[i].y - s[j].y); };

    std::set<std::array<int, 3>> seen;
    std::vector<Triangle> out;
    for (int i = 0; i < n; ++i) {
        std::vector<int> near;
        for (int j = 0; j < n; ++j) if (j != i) near.push_back(j);
        const int k = std::min<int>(kNeighbours, int(near.size()));
        std::partial_sort(near.begin(), near.begin() + k, near.end(),
                          [&](int p, int q) { return dist(i, p) < dist(i, q); });
        near.resize(std::size_t(k));
        near.push_back(i);

        for (std::size_t p = 0; p < near.size(); ++p)
            for (std::size_t q = p + 1; q < near.size(); ++q)
                for (std::size_t r = q + 1; r < near.size(); ++r) {
                    std::array<int, 3> t{near[p], near[q], near[r]};
                    std::sort(t.begin(), t.end());
                    if (!seen.insert(t).second) continue;
                    // (length of the side facing vertex t[k], t[k]), shortest first.
                    std::array<std::pair<float, int>, 3> side{{{dist(t[1], t[2]), t[0]},
                                                               {dist(t[0], t[2]), t[1]},
                                                               {dist(t[0], t[1]), t[2]}}};
                    std::sort(side.begin(), side.end());
                    if (side[0].first <= 0.0f) continue;
                    out.push_back({side[2].first / side[1].first, side[1].first / side[0].first,
                                   {side[0].second, side[1].second, side[2].second}});
                }
    }
    return out;
}

}  // namespace

std::vector<Star> detectStars(const float* rgb, std::uint32_t width, std::uint32_t height,
                              const float* mask, int maxStars) {
    const int w = int(width), h = int(height);
    if (!rgb || w < 3 || h < 3 || maxStars <= 0) return {};

    cv::Mat luma(h, w, CV_32F);
    for (int y = 0; y < h; ++y) {
        auto* row = luma.ptr<float>(y);
        for (int x = 0; x < w; ++x) {
            const float* p = rgb + (std::size_t(y) * width + std::size_t(x)) * 3;
            row[x] = (p[0] + p[1] + p[2]) / 3.0f;
        }
    }
    cv::Mat smooth;
    cv::GaussianBlur(luma, smooth, cv::Size(0, 0), 1.0);

    // Background on a grid of ~50k samples [BA96].
    const int step = std::max(1, int(std::sqrt(double(w) * h / 50000.0)));
    std::vector<float> samples;
    for (int y = 0; y < h; y += step)
        for (int x = 0; x < w; x += step) samples.push_back(smooth.at<float>(y, x));
    const float level = median(samples);
    for (float& v : samples) v = std::fabs(v - level);
    // The floor only keeps a noiseless synthetic frame's float round-off out.
    const float sigma = std::max(1.4826f * median(samples), 1e-6f);
    const float threshold = level + 5.0f * sigma;

    cv::Mat binary(h, w, CV_8U);
    for (int y = 0; y < h; ++y) {
        const auto* s = smooth.ptr<float>(y);
        auto* b = binary.ptr<std::uint8_t>(y);
        for (int x = 0; x < w; ++x)
            b[x] = s[x] > threshold && (!mask || mask[std::size_t(y) * width + std::size_t(x)] >= 0.5f);
    }

    cv::Mat labels, stats, centroids;
    const int count = cv::connectedComponentsWithStats(binary, labels, stats, centroids, 8, CV_32S);

    std::vector<Star> stars;
    for (int l = 1; l < count; ++l) {
        const int area = stats.at<int>(l, cv::CC_STAT_AREA);
        if (area < 2 || area > 400) continue;
        const int x0 = stats.at<int>(l, cv::CC_STAT_LEFT), y0 = stats.at<int>(l, cv::CC_STAT_TOP);
        const int bw = stats.at<int>(l, cv::CC_STAT_WIDTH), bh = stats.at<int>(l, cv::CC_STAT_HEIGHT);
        double sum = 0, sx = 0, sy = 0, peak = 0;
        for (int y = y0; y < y0 + bh; ++y)
            for (int x = x0; x < x0 + bw; ++x) {
                if (labels.at<int>(y, x) != l) continue;
                const double v = luma.at<float>(y, x) - level;
                sum += v; sx += v * x; sy += v * y; peak = std::max(peak, v);
            }
        if (sum > 0 && peak <= kMaxPeakShare * sum) stars.push_back({float(sx / sum), float(sy / sum), float(sum)});
    }
    std::sort(stars.begin(), stars.end(), [](const Star& a, const Star& b) { return a.flux > b.flux; });
    if (int(stars.size()) > maxStars) stars.resize(std::size_t(maxStars));
    return stars;
}

merge::AlignResult alignStars(const std::vector<Star>& frame, const std::vector<Star>& reference) {
    merge::AlignResult result;   // identity, ok = false
    if (int(frame.size()) < kMinInliers || int(reference.size()) < kMinInliers) return result;

    const auto ft = triangles(frame), rt = triangles(reference);
    // Distinct votes: one (reference, frame) pair counts once however many
    // triangles cast it. ponytail: brute-force nearest shape, O(R*F) ~ 400²; a k-d tree if star counts grow.
    std::set<std::pair<int, int>> votes;
    for (const Triangle& r : rt) {
        const Triangle* best = nullptr;
        float bestD = kMatchRadius;
        for (const Triangle& f : ft) {
            const float d = std::hypot(r.a - f.a, r.b - f.b);
            if (d < bestD) { bestD = d; best = &f; }
        }
        if (best)
            for (int k = 0; k < 3; ++k) votes.insert({r.v[k], best->v[k]});
    }
    if (int(votes.size()) < kMinInliers) return result;

    std::vector<cv::Point2f> refPts, framePts;
    for (const auto& [r, f] : votes) {
        refPts.emplace_back(reference[std::size_t(r)].x, reference[std::size_t(r)].y);
        framePts.emplace_back(frame[std::size_t(f)].x, frame[std::size_t(f)].y);
    }
    cv::Mat inl;
    const cv::Mat a = cv::findHomography(refPts, framePts, cv::RANSAC, 2.0, inl);
    if (a.empty() || std::fabs(a.at<double>(2, 2)) < 1e-12) return result;

    // Inliers are counted in distinct stars on both sides: a collapsed fit
    // (scale ~0) sends many reference stars to one frame star and must not
    // count each of them.
    std::set<int> refUsed, frameUsed;
    int i = 0;
    for (const auto& [r, f] : votes) {
        if (inl.at<std::uint8_t>(i++)) { refUsed.insert(r); frameUsed.insert(f); }
    }
    const int inliers = int(std::min(refUsed.size(), frameUsed.size()));
    if (inliers < kMinInliers) return result;

    for (int k = 0; k < 9; ++k) result.refToSource.m[k] = float(a.at<double>(k / 3, k % 3) / a.at<double>(2, 2));
    result.inliers = inliers;
    result.ok = true;
    return result;
}

}  // namespace orion::stack
