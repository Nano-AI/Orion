#!/usr/bin/env python3
"""The statistics tools/bench.py judges with (decision #402).

    python3 tools/bench_stats.py        runs its own checks

A sample is one process run of one scenario, so samples are independent: no
state, cache or JIT carries from one to the next. Within a run a scenario may
take many readings (60 drag ticks); it reduces them to one number first (their
median), because readings inside one process share its state and are not
independent evidence.

**Summary**: the median of the runs, and the MAD (median absolute deviation)
scaled by 1.4826 so it estimates a standard deviation where the noise is normal.
Both survive outliers: a run that collided with a backup job moves the mean and
the standard deviation arbitrarily far, and the median and MAD not at all
until half the runs are bad.

**Comparison**: the Hodges-Lehmann shift, the median of every pairwise
difference `current - baseline`, with its distribution-free confidence interval
from the exact Mann-Whitney null distribution (Hodges and Lehmann 1963,
"Estimates of location based on rank tests", Ann. Math. Statist. 34(2);
Hollander, Wolfe and Chicken, *Nonparametric Statistical Methods*, 3rd ed.
2014, ss. 4.3-4.4). Nothing here assumes normal noise, and machine noise is not
normal: contention only ever adds time, so latency is skewed to the right.

**Verdict**: slower only when the whole interval is above zero AND the shift
is at least the practical minimum (a fraction of the baseline, with an absolute
floor per unit). The interval is the measured noise: a regression must be
outside it. The minimum is what keeps a real but meaningless 1% drift between
two sessions from blocking work. Faster is the mirror image; everything else is
"same", and "too few" when there are not enough runs for any interval at the
chosen confidence (two runs a side cannot reach 95%).

⚠ **The price is power.** Five runs a side need 23 of the 25 differences
positive; four need all 16. So one baseline run slowed by a neighbour hides a
real regression as surely as one current run cannot fake one. A shift at least
the minimum whose interval still crosses zero is therefore said, as "slower?"
(or "faster?"), and does not fail the run: it is the cue to measure again with
more repeats or `--ab`, which narrow the interval. Ties (counts, a timer's
quantum) are scored as if there were none, which only widens the interval.

This replaces the M0 gate's fixed 16 ms line (#116): one number from one run
against a constant flips with the machine's mood; a shift whose interval
excludes zero does not, because a single bad run moves one rank, not the
verdict.
"""

from __future__ import annotations

import math
from functools import lru_cache

MAD_TO_SIGMA = 1.4826


def median(xs: list[float]) -> float:
    s = sorted(xs)
    n = len(s)
    if n == 0:
        return math.nan
    return s[n // 2] if n % 2 else (s[n // 2 - 1] + s[n // 2]) / 2


def mad(xs: list[float]) -> float:
    """Scaled median absolute deviation: a standard deviation for normal noise, robust to outliers."""
    if len(xs) < 2:
        return 0.0
    m = median(xs)
    return MAD_TO_SIGMA * median([abs(x - m) for x in xs])


def summarise(xs: list[float]) -> dict:
    return {"n": len(xs), "median": median(xs), "mad": mad(xs),
            "min": min(xs) if xs else math.nan, "max": max(xs) if xs else math.nan}


@lru_cache(maxsize=None)
def _ways(u: int, m: int, n: int) -> int:
    """Arrangements of m baseline and n current runs with Mann-Whitney U = u."""
    if u < 0:
        return 0
    if m == 0 or n == 0:
        return 1 if u == 0 else 0
    # The largest of all m+n is either a current run (it beats all m baseline runs) or a baseline run.
    return _ways(u - m, m, n - 1) + _ways(u, m - 1, n)


def u_cdf(k: int, m: int, n: int) -> float:
    """P(U <= k) under the null hypothesis of no shift, exactly."""
    total = math.comb(m + n, m)
    return sum(_ways(u, m, n) for u in range(k + 1)) / total


def shift(baseline: list[float], current: list[float], confidence: float = 0.95) -> dict:
    """Hodges-Lehmann shift of current over baseline, and its exact interval.

    The interval is [d(k), d(N+1-k)] over the N = m*n sorted differences, for
    the largest k with P(U <= k-1) <= (1-confidence)/2; its achieved confidence
    is 1 - 2 P(U <= k-1), which is at least `confidence`. No such k means too
    few runs: the interval is unbounded.
    """
    m, n = len(baseline), len(current)
    d = sorted(c - b for b in baseline for c in current)
    est = median(d)
    alpha = (1 - confidence) / 2
    k = 0
    while k + 1 <= len(d) // 2 and u_cdf(k, m, n) <= alpha:
        k += 1
    if k == 0:
        return {"shift": est, "lo": -math.inf, "hi": math.inf, "confidence": 0.0}
    return {"shift": est, "lo": d[k - 1], "hi": d[len(d) - k],
            "confidence": 1 - 2 * u_cdf(k - 1, m, n)}


# What lower-is-better means for each unit, and the smallest change worth a
# verdict whatever the fraction says: a millisecond read on a timer that jitters
# by tens of microseconds, a sixteenth of a GiB on a card measured by a sampler,
# one whole event for a count.
FLOORS = {"_ms": 0.5, "_mib": 16.0, "_count": 1.0}


def unit(metric: str) -> str | None:
    """The suffix that says a metric is judged (lower is better), or None for a reading only."""
    return next((u for u in FLOORS if metric.endswith(u)), None)


def verdict(metric: str, baseline: list[float], current: list[float],
            confidence: float = 0.95, min_effect: float = 0.05) -> dict:
    u = unit(metric)
    out = {"baseline": median(baseline), "current": median(current)}
    if u is None or not baseline or not current:
        return out | {"verdict": "info"}
    s = shift(baseline, current, confidence)
    need = max(min_effect * abs(out["baseline"]), FLOORS[u])
    out |= s | {"needed": need}
    if s["confidence"] == 0.0:
        out["verdict"] = "too few"
    elif s["lo"] > 0 and s["shift"] >= need:
        out["verdict"] = "SLOWER"
    elif s["hi"] < 0 and -s["shift"] >= need:
        out["verdict"] = "faster"
    elif abs(s["shift"]) >= need:
        out["verdict"] = "slower?" if s["shift"] > 0 else "faster?"
    else:
        out["verdict"] = "same"
    return out


def _checks() -> int:
    bad = 0

    def check(ok: bool, what: str) -> None:
        nonlocal bad
        print(("ok    " if ok else "FAIL  ") + what)
        bad += not ok

    # The null distribution: symmetric, sums to one, and the textbook tail.
    check(abs(u_cdf(25, 5, 5) - 1) < 1e-12, "U's distribution sums to one (m = n = 5)")
    # Counted by hand: U = 0, 1, 2 are 1, 1 and 2 of the C(10, 5) = 252 orderings,
    # so 2 is the two-sided 5% critical value for five runs a side.
    check(abs(u_cdf(2, 5, 5) - 4 / 252) < 1e-12 and abs(u_cdf(3, 5, 5) - 7 / 252) < 1e-12,
          "P(U <= 2) = 4/252 and P(U <= 3) = 7/252 for m = n = 5")
    check(all(abs(u_cdf(k, 4, 6) - (1 - u_cdf(23 - k, 4, 6))) < 1e-12 for k in range(24)), "U is symmetric about mn/2")
    # Five runs a side: the 95% interval is [d(3), d(23)] of 25 differences, at 1 - 8/252.
    s = shift([10, 11, 12, 13, 14], [20, 21, 22, 23, 24])
    check(s["shift"] == 10 and (s["lo"], s["hi"]) == (7, 13) and abs(s["confidence"] - (1 - 8 / 252)) < 1e-12,
          f"a clear shift of 10, interval [d(3), d(23)]: {s}")
    check(verdict("x_ms", [10, 11, 12, 13, 14], [20, 21, 22, 23, 24])["verdict"] == "SLOWER", "a clear slowdown is called")
    check(verdict("x_ms", [20, 21, 22, 23, 24], [10, 11, 12, 13, 14])["verdict"] == "faster", "a clear speedup is called")
    check(verdict("x_ms", [10, 11, 12, 13, 14], [10.5, 11, 12.2, 13, 14])["verdict"] == "same", "overlapping runs are the same")
    # One wild run in five (a backup job, a GPU neighbour) does not flip the verdict.
    check(verdict("x_ms", [10, 10.2, 10.1, 9.9, 10], [10.1, 10, 9.9, 10.2, 31])["verdict"] == "same",
          "one contaminated run cannot make a regression")
    # Consistent but tiny: outside the noise, under the practical minimum.
    check(verdict("x_ms", [100, 100.1, 100.2, 100.1, 100], [101, 101.1, 101.2, 101.1, 101])["verdict"] == "same",
          "a 1% shift outside the noise is under the 5% minimum")
    check(verdict("x_ms", [10, 11], [20, 21])["verdict"] == "too few", "two runs a side cannot reach 95%")
    # One slow baseline run hides a real 20% slowdown from the interval; the point estimate still says so.
    check(verdict("x_ms", [10, 10.1, 9.9, 10, 30], [12, 12.1, 11.9, 12, 12.2])["verdict"] == "slower?",
          "a big shift the interval cannot confirm is flagged, not passed")
    check(verdict("photos", [3, 3], [3, 3])["verdict"] == "info", "a metric with no unit is a reading, never judged")
    check(abs(mad([1, 2, 3, 4, 100]) - MAD_TO_SIGMA) < 1e-12, "the MAD ignores the outlier")
    return 1 if bad else 0


if __name__ == "__main__":
    raise SystemExit(_checks())
