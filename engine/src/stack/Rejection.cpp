#include "stack/Rejection.h"

#include <cmath>
#include <utility>

namespace orion::stack {
namespace {

constexpr int    kMinSurvivors = 3;
constexpr int    kMaxWinsorIter = 50;
constexpr double kWinsorC = 1.5;
constexpr double kWinsorFactor = 1.134;   // 1/sqrt(E[min(|z|,1.5)^2]), research/stacking.md §2
constexpr double kWinsorTol = 5e-4;
constexpr double kClipSigmas = 3.0;

// Every method works on v sorted ascending, weights riding along; a pass only
// removes entries and keeps the order, so the survivors are always a sorted
// prefix and the median is an index.
// ponytail: insertion sort is O(n^2); fine for stacks of tens of frames, switch
// to a pair sort if a stack ever holds hundreds.
void sortPairs(float* v, float* w, int n) noexcept {
    for (int i = 1; i < n; ++i)
        for (int j = i; j > 0 && v[j - 1] > v[j]; --j) {
            std::swap(v[j - 1], v[j]);
            std::swap(w[j - 1], w[j]);
        }
}

double median(const float* v, int n) noexcept {
    return n % 2 ? v[n / 2] : 0.5 * (double(v[n / 2 - 1]) + v[n / 2]);
}

double stdev(const float* v, int n) noexcept {
    double m = 0, ss = 0;
    for (int i = 0; i < n; ++i) m += v[i];
    m /= n;
    for (int i = 0; i < n; ++i) ss += (v[i] - m) * (v[i] - m);
    return std::sqrt(ss / n);
}

/// Huber's winsorized scale of v about its median m, to convergence.
double winsorSigma(const float* v, int n, double m) noexcept {
    double s = stdev(v, n);
    for (int it = 0; it < kMaxWinsorIter; ++it) {
        const double lo = m - kWinsorC * s, hi = m + kWinsorC * s;
        double mean = 0, ss = 0;
        for (int i = 0; i < n; ++i) mean += std::fmin(std::fmax(v[i], lo), hi);
        mean /= n;
        for (int i = 0; i < n; ++i) {
            const double d = std::fmin(std::fmax(v[i], lo), hi) - mean;
            ss += d * d;
        }
        const double next = kWinsorFactor * std::sqrt(ss / n);
        const bool done = std::fabs(next - s) <= kWinsorTol * s;
        s = next;
        if (done) break;
    }
    return s;
}

/// Keep entries with keep[i]; returns the new count, or n when nothing would
/// go or when fewer than kMinSurvivors would stay (the pass is not applied).
template <class Keep>
int compact(float* v, float* w, int n, Keep keep) noexcept {
    int k = 0;
    for (int i = 0; i < n; ++i) k += keep(i);
    if (k == n || k < kMinSurvivors) return n;
    k = 0;
    for (int i = 0; i < n; ++i)
        if (keep(i)) { v[k] = v[i]; w[k] = w[i]; ++k; }
    return k;
}

// Every pass takes `sigma`, the winsorized sigma' fixed on the full sample (unused by the others).
int percentilePass(float* v, float* w, int n, double) noexcept {
    const double m = median(v, n), lo = m - 0.2 * std::fabs(m), hi = m + 0.1 * std::fabs(m);
    return compact(v, w, n, [&](int i) { return v[i] >= lo && v[i] <= hi; });
}

int sigmaPass(float* v, float* w, int n, double) noexcept {
    const double m = median(v, n), s = kClipSigmas * stdev(v, n);
    return compact(v, w, n, [&](int i) { return v[i] >= m - s && v[i] <= m + s; });
}

int winsorPass(float* v, float* w, int n, double sigma) noexcept {
    const double m = median(v, n), s = kClipSigmas * sigma;
    return compact(v, w, n, [&](int i) { return v[i] >= m - s && v[i] <= m + s; });
}

int linearFitPass(float* v, float* w, int n, double) noexcept {
    // Least-squares line of value on rank 0..n-1, then mean |residual| as sigma.
    const double xm = 0.5 * (n - 1);
    double ym = 0, sxy = 0, sxx = 0;
    for (int i = 0; i < n; ++i) ym += v[i];
    ym /= n;
    for (int i = 0; i < n; ++i) { sxy += (i - xm) * (v[i] - ym); sxx += (i - xm) * (i - xm); }
    const double slope = sxy / sxx;
    auto resid = [&](int i) { return v[i] - (ym + slope * (i - xm)); };
    double sigma = 0;
    for (int i = 0; i < n; ++i) sigma += std::fabs(resid(i));
    sigma /= n;
    return compact(v, w, n, [&](int i) { return std::fabs(resid(i)) <= kClipSigmas * sigma; });
}

using Pass = int (*)(float*, float*, int, double) noexcept;

Pass passFor(Rejection r) noexcept {
    switch (r) {
        case Rejection::Percentile: return percentilePass;
        case Rejection::Sigma:      return sigmaPass;
        case Rejection::Winsorized: return winsorPass;
        case Rejection::LinearFit:  return linearFitPass;
        default:                    return nullptr;
    }
}

}  // namespace

Rejection autoRejection(int frames) noexcept {
    if (frames < 3)  return Rejection::None;
    if (frames <= 6) return Rejection::Percentile;
    // Sigma is manual only: 3σ clipping cannot reject a lone outlier below ~10
    // frames (σ = d·√(n−1)/n), where winsorizing can. research/stacking.md §2.
    return frames <= 20 ? Rejection::Winsorized : Rejection::LinearFit;
}

float combine(float* v, float* w, int n, Rejection r) noexcept {
    if (n < 1) return std::nanf("");
    if (r == Rejection::Auto) r = autoRejection(n);
    if (auto pass = passFor(r); pass && n >= kMinSurvivors) {
        sortPairs(v, w, n);
        // Winsorized sigma' once, on the full sample; later rounds only re-centre the median.
        // Re-estimated from survivors already cut at 3 sigma' it shrinks every round (the
        // 1.134 assumes an uncut Gaussian) until a skewed clean sample is eaten to 5 of 10.
        const double s = r == Rejection::Winsorized ? winsorSigma(v, n, median(v, n)) : 0.0;
        // Percentile is one pass; the others repeat until a pass removes nothing.
        for (int k = pass(v, w, n, s); k != n; k = pass(v, w, n, s)) {
            n = k;
            if (r == Rejection::Percentile) break;
        }
    }
    double sw = 0, swv = 0;
    for (int i = 0; i < n; ++i) { sw += w[i]; swv += double(w[i]) * v[i]; }
    return static_cast<float>(swv / sw);
}

}  // namespace orion::stack
