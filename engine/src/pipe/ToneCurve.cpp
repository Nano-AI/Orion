#include "pipe/ToneCurve.h"

#include <algorithm>
#include <cmath>

namespace orion::pipe {
namespace {

bool isIdentityChannel(const CurveChannel& c) {
    return c.size() == 2 &&
           std::abs(c[0].x) < 1e-6f && std::abs(c[0].y) < 1e-6f &&
           std::abs(c[1].x - 1.0f) < 1e-6f && std::abs(c[1].y - 1.0f) < 1e-6f;
}

/// Fritsch–Carlson tangents: zero the slope wherever the data changes
/// direction, which is what guarantees the spline stays monotone.
std::vector<float> tangentsFor(const std::vector<CurvePoint>& p) {
    const std::size_t n = p.size();
    std::vector<float> dx(n - 1), dy(n - 1), m(n - 1), t(n);

    for (std::size_t i = 0; i + 1 < n; ++i) {
        dx[i] = p[i + 1].x - p[i].x;
        dy[i] = p[i + 1].y - p[i].y;
        m[i]  = (dx[i] != 0.0f) ? dy[i] / dx[i] : 0.0f;
    }

    t[0] = m[0];
    for (std::size_t i = 1; i + 1 < n; ++i) {
        if (m[i - 1] * m[i] <= 0.0f) {
            t[i] = 0.0f;
        } else {
            const float w1 = 2.0f * dx[i] + dx[i - 1];
            const float w2 = dx[i] + 2.0f * dx[i - 1];
            t[i] = (w1 + w2) / (w1 / m[i - 1] + w2 / m[i]);
        }
    }
    t[n - 1] = m[n - 2];
    return t;
}

}  // namespace

bool ToneCurveSpec::isIdentity() const noexcept {
    return isIdentityChannel(master) && isIdentityChannel(red) &&
           isIdentityChannel(green) && isIdentityChannel(blue);
}

namespace {

/// The spline itself, on points that are already sorted and tangents that are
/// already fitted. Split out so the LUT builder can hoist both.
float evaluateSorted(const std::vector<CurvePoint>& p,
                     const std::vector<float>& t, float x) {
    if (x <= p.front().x) return p.front().y;
    if (x >= p.back().x)  return p.back().y;

    std::size_t i = 0;
    while (i + 2 < p.size() && x > p[i + 1].x) ++i;

    const float h = p[i + 1].x - p[i].x;
    if (h <= 0.0f) return p[i].y;

    const float s  = (x - p[i].x) / h;
    const float s2 = s * s;
    const float s3 = s2 * s;

    return (2.0f * s3 - 3.0f * s2 + 1.0f) * p[i].y
         + (s3 - 2.0f * s2 + s) * h * t[i]
         + (-2.0f * s3 + 3.0f * s2) * p[i + 1].y
         + (s3 - s2) * h * t[i + 1];
}

}  // namespace

float evaluateCurve(const CurveChannel& channel, float x) {
    if (channel.size() < 2) return x;

    // Callers may hand us points in any order; the spline needs them sorted.
    std::vector<CurvePoint> p(channel);
    std::sort(p.begin(), p.end(),
              [](const CurvePoint& a, const CurvePoint& b) { return a.x < b.x; });

    return evaluateSorted(p, tangentsFor(p), x);
}

std::vector<float> buildCurveLut(const ToneCurveSpec& spec) {
    std::vector<float> lut(static_cast<std::size_t>(kCurveResolution) * kCurveRows);

    const CurveChannel* rows[kCurveRows] = {
        &spec.master, &spec.red, &spec.green, &spec.blue
    };

    for (std::uint32_t row = 0; row < kCurveRows; ++row) {
        const CurveChannel& channel = *rows[row];

        // Sort and fit the tangents once per channel rather than once per
        // sample. `evaluateCurve` does both on every call — correct, and fine
        // for the handful of calls the UI makes, but this loop makes 1024 of
        // them per rebuild and the curve does not change between them.
        if (channel.size() < 2) {
            for (std::uint32_t i = 0; i < kCurveResolution; ++i) {
                const float x = static_cast<float>(i) / static_cast<float>(kCurveResolution - 1);
                lut[static_cast<std::size_t>(row) * kCurveResolution + i] =
                    std::clamp(x, 0.0f, 1.0f);
            }
            continue;
        }

        std::vector<CurvePoint> p(channel);
        std::sort(p.begin(), p.end(),
                  [](const CurvePoint& a, const CurvePoint& b) { return a.x < b.x; });
        const auto t = tangentsFor(p);

        for (std::uint32_t i = 0; i < kCurveResolution; ++i) {
            const float x = static_cast<float>(i) / static_cast<float>(kCurveResolution - 1);
            lut[static_cast<std::size_t>(row) * kCurveResolution + i] =
                std::clamp(evaluateSorted(p, t, x), 0.0f, 1.0f);
        }
    }
    return lut;
}

}  // namespace orion::pipe
