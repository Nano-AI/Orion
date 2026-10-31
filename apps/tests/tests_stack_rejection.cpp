/*  tests_stack_rejection — the per-pixel outlier rejection every stack mode
 *  calls, against values with a known answer: 1.0 plus seeded Gaussian noise,
 *  with at most one planted outlier (a satellite trail high, a dead pixel low).
 *
 *  The fixture's noise is a seeded mt19937, so a failure is reproducible. The
 *  no-outlier run is the control: a rejector that eats good data moves the mean.
 */

#include "harness.h"
#include "stack/Rejection.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <random>
#include <vector>

namespace {

using orion::stack::Rejection;
using orion::stack::combine;

constexpr float kSigma = 0.01f;

/// n values 1 + N(0, kSigma); `planted` replaces value 2 when it is not NaN.
std::vector<float> noisy(int n, unsigned seed, float planted = NAN) {
    std::mt19937 rng(seed);
    std::normal_distribution<float> g(0.0f, kSigma);
    std::vector<float> v(static_cast<std::size_t>(n));
    for (float& x : v) x = 1.0f + g(rng);
    if (!std::isnan(planted)) v[2] = planted;
    return v;
}

float run(std::vector<float> v, Rejection r) {
    std::vector<float> w(v.size(), 1.0f);
    return combine(v.data(), w.data(), static_cast<int>(v.size()), r);
}

float plainMean(const std::vector<float>& v) {
    double s = 0;
    for (float x : v) s += x;
    return static_cast<float>(s / static_cast<double>(v.size()));
}

}  // namespace

void testStackRejection() {
    section("stack: automatic method by frame count");
    using orion::stack::autoRejection;
    CHECK(autoRejection(2)  == Rejection::None);
    CHECK(autoRejection(3)  == Rejection::None);
    CHECK(autoRejection(4)  == Rejection::Winsorized);
    CHECK(autoRejection(6)  == Rejection::Winsorized);
    CHECK(autoRejection(7)  == Rejection::Winsorized);
    CHECK(autoRejection(9)  == Rejection::Winsorized);
    CHECK(autoRejection(10) == Rejection::Winsorized);
    CHECK(autoRejection(20) == Rejection::Winsorized);
    CHECK(autoRejection(21) == Rejection::LinearFit);

    section("stack: the weighted mean, and fewer than 3 values");
    {
        float v[] = {1, 2, 3, 4}, w[] = {1, 1, 1, 1};
        checkNear(combine(v, w, 4, Rejection::None), 2.5, 1e-6, "none, equal weights");
        float v2[] = {1, 2, 3, 4}, w2[] = {1, 1, 1, 5};
        checkNear(combine(v2, w2, 4, Rejection::None), 3.25, 1e-6, "none, weight 5 on the last");
        float v3[] = {1, 3}, w3[] = {1, 1};
        checkNear(combine(v3, w3, 2, Rejection::Sigma), 2.0, 1e-6, "n=2 degrades to the mean");
        float v4[] = {7}, w4[] = {2};
        checkNear(combine(v4, w4, 1, Rejection::LinearFit), 7.0, 1e-6, "n=1 is the value");
        float v5[] = {1, 5, 100}, w5[] = {1, 1, 1};
        checkNear(combine(v5, w5, 3, Rejection::Percentile), 35.3333333, 1e-4,
                  "a pass that would leave under 3 is not applied");
        float v6[] = {1}, w6[] = {1};
        CHECK(std::isnan(combine(v6, w6, 0, Rejection::None)));
    }

    section("stack: weights follow their values through sorting and rejection");
    for (Rejection r : {Rejection::Sigma, Rejection::Winsorized}) {
        // Unsorted, distinct weights; the 10.0 carries weight 100 and must be dropped.
        float v[] = {1.02f, 10.0f, 0.98f, 1.00f, 1.01f, 0.99f, 1.03f, 0.97f};
        float w[] = {1, 100, 2, 3, 1, 4, 1, 2};
        // Survivors: sum(w*v) = 13.92 over sum(w) = 14.
        checkNear(combine(v, w, 8, r), 13.92 / 14.0, 1e-5,
                  r == Rejection::Sigma ? "sigma keeps weights paired" : "winsorized keeps weights paired");
    }

    section("stack: one satellite among seven frames");
    checkNear(run(noisy(7, 7, 10.0f), Rejection::Winsorized), 1.0, 0.01, "winsorized drops a lone outlier at n=7");
    checkNear(run(noisy(7, 7, 10.0f), Rejection::Auto), 1.0, 0.01, "auto drops it too");

    section("stack: a satellite on dark sky among four to six frames");
    {
        // Sky 0.02 with noise as large as the sky itself (a dark sky's sigma/mean ~ 1), a trail 0.5
        // above it in one frame. Percentile's window is 20 % / 10 % of the median — narrower than
        // the noise — so it rejects nothing it should and keeps the trail; winsorized does not.
        for (int n = 4; n <= 6; ++n) {
            int autoKept = 0, pctKept = 0;
            for (unsigned seed = 1; seed <= 1000; ++seed) {
                std::mt19937 rng(seed);
                std::normal_distribution<float> g(0.02f, 0.02f);
                std::vector<float> v(std::size_t(n), 0.0f);
                for (float& x : v) x = g(rng);
                v[0] = 0.52f;
                autoKept += run(v, Rejection::Auto) - 0.02f > 0.05f;
                pctKept += run(v, Rejection::Percentile) - 0.02f > 0.05f;
            }
            std::printf("    n=%d: trail kept by auto (winsorized) %d, by percentile %d, of 1000\n", n, autoKept, pctKept);
            report(autoKept == 0, "auto rejects a dark-sky trail at n=" + std::to_string(n), std::to_string(autoKept));
        }
    }

    struct Case { Rejection r; int n; const char* name; };
    const Case cases[] = {{Rejection::Percentile, 5, "percentile"},
                          {Rejection::Sigma, 8, "sigma"},
                          {Rejection::Winsorized, 16, "winsorized"},
                          {Rejection::LinearFit, 24, "linear fit"}};

    section("stack: a satellite and a dead pixel are rejected");
    for (const Case& c : cases) {
        checkNear(run(noisy(c.n, 7, 10.0f), c.r), 1.0, 0.01, std::string(c.name) + " drops a bright outlier");
        checkNear(run(noisy(c.n, 7, -5.0f), c.r), 1.0, 0.01, std::string(c.name) + " drops a dark outlier");
        // Control: with rejection off the same outlier drags the mean far away.
        CHECK(std::fabs(run(noisy(c.n, 7, 10.0f), Rejection::None) - 1.0f) > 0.2f);
    }

    section("stack: winsorized does not erode clean data");
    {
        // Survivors are a contiguous run of the sorted values (the window is symmetric
        // about the median), compacted to the front of v: the count is the prefix whose
        // mean, summed as combine sums it, is the result.
        int fewest = 10;
        for (unsigned seed = 1; seed <= 1000; ++seed) {
            auto v = noisy(10, seed);
            std::vector<float> w(v.size(), 1.0f);
            const float r = combine(v.data(), w.data(), 10, Rejection::Winsorized);
            double s = 0;
            int k = 10;
            for (int i = 0; i < 10; ++i) {
                s += v[std::size_t(i)];
                if (float(s / (i + 1)) == r) { k = i + 1; break; }
            }
            fewest = std::min(fewest, k);
        }
        std::printf("    fewest survivors of 10 clean values over 1000 seeds: %d\n", fewest);
        CHECK(fewest >= 7);
    }

    section("stack: clean data is left alone");
    for (const Case& c : cases) {
        const auto v = noisy(16, 11);
        checkNear(run(v, c.r), plainMean(v), 0.005, std::string(c.name) + " on clean data");
    }
}
