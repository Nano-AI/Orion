// Split out of ViewportTests.swift 2026-07-31.

import CoreGraphics
import Foundation

// MARK: - Sky detection
//
// research/sky-detection.md. Shen & Wang (2013): sky is smooth, the ground is
// not, so the boundary is where the gradient first becomes large scanning down
// a column — and the problem is choosing how large. Pure arithmetic, so it is
// pinned here on synthetic frames whose answer is known exactly.

extension ViewportTests {

    /// Builds an RGB image row-major from a per-pixel closure.
    static func frame(_ w: Int, _ h: Int,
                      _ f: (Int, Int) -> (Float, Float, Float)) -> [Float] {
        var out = [Float](repeating: 0, count: w * h * 3)
        for y in 0..<h {
            for x in 0..<w {
                let (r, g, b) = f(x, y)
                let i = (y * w + x) * 3
                out[i] = r; out[i + 1] = g; out[i + 2] = b
            }
        }
        return out
    }

    /// A flat sky over textured ground, with the horizon at a known row.
    /// The fill is four-connected, and a one-pixel diagonal gap does not leak.
    ///
    /// ⚠ `UNSOURCED.md` §23 has recorded this as **untested** since the sky
    /// detector shipped: "the synthetic frames have no one-pixel diagonal gap,
    /// and the mutation that adds diagonal neighbours survives." It is the
    /// difference between a sky mask and a mask of the entire photograph, so it
    /// is worth a fixture that has one.
    ///
    /// The frame is a wall of hard gradient separating a calm sky from calm
    /// ground, breached by two calm pixels that touch **only at their corner**:
    ///
    /// ```
    ///   sky   sky   sky   sky      <- calm, seeded from the top row
    ///   wall  GAP   wall  wall     <- row m,   gap at x = k
    ///   wall  wall  GAP   wall     <- row m+1, gap at x = k+1
    ///   ground ground ground       <- calm, and must stay unfilled
    /// ```
    ///
    /// Four-connected, the fill reaches the first gap and stops: that pixel's
    /// four neighbours are wall, wall, sky and wall. Eight-connected, it steps
    /// diagonally into the second gap and floods the ground — which is exactly
    /// how a fill escapes through a gap in a branch and takes the whole frame.
    static func testSkyFillCannotSqueezeThroughADiagonal() {
        let w = 32, h = 24
        let m = 10, k = 12
        let wall: Float = 1.0, calm: Float = 0.0
        let threshold: Float = 0.5

        var grad = [Float](repeating: calm, count: w * h)
        for x in 0..<w {
            grad[m * w + x] = wall
            grad[(m + 1) * w + x] = wall
        }
        grad[m * w + k] = calm
        grad[(m + 1) * w + k + 1] = calm

        let filled = SkyDetector.fill(grad: grad, width: w, height: h,
                                      threshold: threshold)

        // The sky above is reached, or the fixture proves nothing.
        report(filled[(m / 2) * w + w / 2],
               "the calm region above the wall is filled")
        report(filled[m * w + k],
               "and the first gap, which is joined to it edgewise, is filled")

        // ⚠ The load-bearing pair. The second gap touches the first only at a
        // corner, so a four-connected fill cannot enter it — and everything
        // below depends on that one step not being taken.
        report(!filled[(m + 1) * w + k + 1],
               "the diagonally-touching gap is NOT filled")

        var ground = 0
        for y in (m + 2)..<h {
            for x in 0..<w where filled[y * w + x] { ground += 1 }
        }
        report(ground == 0,
               "and no ground pixel is filled — a diagonal step floods all of it",
               "\(ground) of \(w * (h - m - 2)) ground pixels filled")
    }

    /// `largestVariance` is the exact largest eigenvalue, checked against an
    /// independent solver.
    ///
    /// ⚠ Recorded as untested in `UNSOURCED.md` §23, and writing the test is
    /// what showed the code was wrong. It used to return the largest **diagonal
    /// entry** of the covariance, with a comment claiming that "orders
    /// candidates the same way in every case measured" — true only because
    /// every case measured had the same covariance *shape*. Against populations
    /// wide in different channels it reorders a pair in 21.
    ///
    /// It is Smith's closed form now (CACM 1961). The oracle here is a Jacobi
    /// rotation — a different algorithm, iterative where the product's is
    /// closed-form, so agreement is evidence rather than a tautology.
    static func testSkyEigenvalueProxyOrdersTheSameWay() {
        // Jacobi for a 3x3 symmetric matrix. Enough sweeps to converge well
        // past the precision this comparison needs.
        func largestEigenvalue(_ mIn: [Double]) -> Double {
            var a = mIn
            for _ in 0..<64 {
                var p = 0, q = 1
                var off = 0.0
                for i in 0..<3 {
                    for j in 0..<3 where i != j {
                        if abs(a[i * 3 + j]) > off { off = abs(a[i * 3 + j]); p = i; q = j }
                    }
                }
                if off < 1e-14 { break }
                let app = a[p * 3 + p], aqq = a[q * 3 + q], apq = a[p * 3 + q]
                let theta = 0.5 * atan2(2 * apq, app - aqq)
                let c = cos(theta), s = sin(theta)
                var r = [Double](repeating: 0, count: 9)
                for i in 0..<3 { r[i * 3 + i] = 1 }
                r[p * 3 + p] = c; r[q * 3 + q] = c
                r[p * 3 + q] = -s; r[q * 3 + p] = s
                // a := Rᵀ a R
                var t = [Double](repeating: 0, count: 9)
                for i in 0..<3 { for j in 0..<3 {
                    var v = 0.0
                    for l in 0..<3 { v += r[l * 3 + i] * a[l * 3 + j] }
                    t[i * 3 + j] = v
                } }
                for i in 0..<3 { for j in 0..<3 {
                    var v = 0.0
                    for l in 0..<3 { v += t[i * 3 + l] * r[l * 3 + j] }
                    a[i * 3 + j] = v
                } }
            }
            return max(a[0], max(a[4], a[8]))
        }

        // Populations with correlated channels *and differing covariance
        // shapes*.
        //
        // ⚠ The first draft of this fixture varied only an overall spread, so
        // every channel's variance scaled together — and then the largest and
        // the *smallest* diagonal entry order the candidates identically. The
        // mutation replacing `max` with `min` survived it. A fixture where one
        // population is wide in red and the next is wide in blue is what makes
        // "largest" load-bearing rather than incidental.
        func population(_ seed: Int, _ spread: (Double, Double, Double))
            -> SkyDetector.Stats {
            var s = SkyDetector.Stats()
            var state = UInt64(seed &* 2_654_435_761 &+ 1)
            for _ in 0..<400 {
                state = state &* 6_364_136_223_846_793_005 &+ 1_442_695_040_888_963_407
                let u = Double((state >> 33) % 10_000) / 10_000.0
                state = state &* 6_364_136_223_846_793_005 &+ 1_442_695_040_888_963_407
                let v = Double((state >> 33) % 10_000) / 10_000.0
                // Correlated, so the covariance is genuinely off-diagonal and
                // the proxy is a strict lower bound rather than the exact value.
                s.add((0.20 + spread.0 * u,
                       0.15 + spread.1 * (0.8 * u + 0.2 * v),
                       0.10 + spread.2 * v))
            }
            return s
        }

        let shapes: [(Double, Double, Double)] = [
            (0.40, 0.05, 0.05),   // wide in red only
            (0.05, 0.05, 0.40),   // wide in blue only
            (0.05, 0.35, 0.05),   // wide in green only
            (0.30, 0.30, 0.02),   // flat-ish, red and green
            (0.02, 0.30, 0.30),   // flat-ish, green and blue
            (0.22, 0.20, 0.24),   // near-isotropic
            (0.45, 0.10, 0.02),   // strongly anisotropic
        ]

        var worst = 0.0
        var offDiagonal = false
        var diagonalWouldReorder = 0
        var exacts: [Double] = []
        for (i, shape) in shapes.enumerated() {
            let p = population(i * 17 + 3, shape)
            let got = p.largestVariance()
            let want = largestEigenvalue(p.covariance())
            worst = max(worst, abs(got - want) / max(want, 1e-12))
            exacts.append(want)

            // ⚠ The guard on the fixture. If the covariances were diagonal the
            // agreement above would be trivially true of the old code as well,
            // and this test would pin nothing.
            let c = p.covariance()
            if want > max(c[0], max(c[4], c[8])) * 1.02 { offDiagonal = true }
        }
        report(worst < 1e-9,
               "the closed form matches an independent Jacobi solve on every shape",
               String(format: "worst relative error %.3g", worst))
        report(offDiagonal,
               "and the covariances are genuinely off-diagonal, so that is not trivially true")

        // ⚠ What the old code got wrong, kept as a check rather than only as a
        // note: the largest diagonal entry reorders candidates against the true
        // eigenvalue. If this ever stops being true the fixture has gone bland
        // and stopped discriminating.
        for a in 0..<(shapes.count - 1) {
            for b in (a + 1)..<shapes.count {
                let ca = population(a * 17 + 3, shapes[a]).covariance()
                let cb = population(b * 17 + 3, shapes[b]).covariance()
                let diagA = max(ca[0], max(ca[4], ca[8]))
                let diagB = max(cb[0], max(cb[4], cb[8]))
                if (diagA < diagB) != (exacts[a] < exacts[b]) { diagonalWouldReorder += 1 }
            }
        }
        report(diagonalWouldReorder > 0,
               "and the fixture is sharp enough to see the diagonal shortcut reorder them",
               "the old proxy reorders \(diagonalWouldReorder) pairs")
    }

    static func testSkyFindsAHorizon() {
        let w = 64, h = 64, horizon = 24
        // ⚠ The ground is *noisy*, not merely darker. The energy is about how
        // uniform each half is, so a smooth dark ground would be as good a
        // "sky" as the sky — and a test with one would pass on a detector that
        // simply cut at the largest brightness step.
        let img = frame(w, h) { x, y in
            if y < horizon { return (0.55, 0.62, 0.80) }
            let n = Float((x &* 37 &+ y &* 61) % 23) / 23.0
            return (0.18 + n * 0.35, 0.16 + n * 0.30, 0.12 + n * 0.25)
        }

        switch SkyDetector.detect(rgb: img, width: w, height: h) {
        case .noSky(let why):
            report(false, "a flat sky over textured ground is found", why)
        case .found(let alpha, let coverage):
            let want = Double(horizon) / Double(h)
            report(abs(coverage - want) < 0.06,
                   "the horizon lands where it was drawn",
                   String(format: "%.3f against %.3f", coverage, want))
            // Two-sided: covered above, clear below.
            let above = alpha[(horizon / 2) * w + w / 2]
            let below = alpha[(horizon + (h - horizon) / 2) * w + w / 2]
            report(above > 0.5 && below < 0.5,
                   "with the sky covered and the ground not",
                   "above \(above), below \(below)")
        }
    }

    /// ⚠ **No hue prior.** The method scores how uniform each half is; it never
    /// asks whether the top is blue. A detector that had quietly grown a blue
    /// test would pass every other check here and fail on overcast — which is
    /// most of the photographs anyone reaches for this on.
    static func testSkyNeverAsksWhatSkyLooksLike() {
        let w = 64, h = 64, horizon = 30
        // A grey overcast sky. Nothing blue anywhere in the frame.
        let img = frame(w, h) { x, y in
            if y < horizon { return (0.78, 0.78, 0.77) }
            let n = Float((x &* 29 &+ y &* 53) % 19) / 19.0
            return (0.22 + n * 0.30, 0.20 + n * 0.26, 0.18 + n * 0.22)
        }
        switch SkyDetector.detect(rgb: img, width: w, height: h) {
        case .noSky(let why):
            report(false, "an overcast sky is found without a colour prior", why)
        case .found(_, let coverage):
            let want = Double(horizon) / Double(h)
            report(abs(coverage - want) < 0.08,
                   "an overcast sky is found without a colour prior",
                   String(format: "%.3f against %.3f", coverage, want))
        }
    }

    /// ⚠ A frame with no sky must say so. Returning "everything" is
    /// indistinguishable from the feature being broken — the failure the person
    /// matte had before it learned to report an empty result.
    static func testSkyRefusesAFrameWithNone() {
        let w = 48, h = 48
        // Texture everywhere, no smooth region touching the top.
        let noise = frame(w, h) { x, y in
            let n = Float((x &* 41 &+ y &* 67) % 17) / 17.0
            return (0.15 + n * 0.6, 0.14 + n * 0.55, 0.13 + n * 0.5)
        }
        if case .found(_, let c) = SkyDetector.detect(rgb: noise, width: w, height: h) {
            report(false, "a frame of pure texture reports no sky",
                   String(format: "covered %.3f", c))
        } else {
            report(true, "a frame of pure texture reports no sky")
        }

        // And a completely flat frame: no edges at all, so no horizon exists.
        let flat = frame(w, h) { _, _ in (0.5, 0.5, 0.5) }
        switch SkyDetector.detect(rgb: flat, width: w, height: h) {
        case .noSky:
            report(true, "and so does a frame with no edges in it")
        case .found(_, let c):
            report(false, "and so does a frame with no edges in it",
                   String(format: "covered %.3f", c))
        }
    }
}

extension ViewportTests {

    /// ⚠ **The energy, and the sky-to-the-bottom rule, both actually exercised.**
    ///
    /// The first three checks could not see either. Their sky is *perfectly*
    /// flat, so its gradient is exactly zero and every candidate threshold finds
    /// the same first exceedance — the horizon — whatever the energy says. Two
    /// mutations survived on that: scoring the sky alone and ignoring the
    /// ground, and treating a column with no edge as having no sky.
    ///
    /// This frame has **mild grain in the sky**, as a real one does, so
    /// different thresholds give genuinely different borders and the energy has
    /// to choose. And its ground stops two thirds of the way across, so the
    /// remaining columns have no strong edge anywhere and must be sky all the
    /// way down.
    static func testSkyEnergyPicksTheBorder() {
        let w = 64, h = 64, horizon = 34
        let img = frame(w, h) { x, y in
            let grain = Float((x &* 13 &+ y &* 7) % 5) / 5.0 * 0.02
            if y >= horizon && x < (w * 2) / 3 {
                let n = Float((x &* 43 &+ y &* 71) % 21) / 21.0
                return (0.20 + n * 0.40, 0.18 + n * 0.34, 0.15 + n * 0.28)
            }
            return (0.52 + grain, 0.60 + grain, 0.79 + grain)
        }

        switch SkyDetector.detect(rgb: img, width: w, height: h) {
        case .noSky(let why):
            report(false, "a grainy sky over a partial horizon is found", why)
        case .found(let alpha, let coverage):
            // Two thirds of the columns end at the horizon; the last third is
            // sky to the bottom. Expected coverage is the weighted mix.
            let want = (2.0 / 3.0) * (Double(horizon) / Double(h)) + (1.0 / 3.0)
            report(abs(coverage - want) < 0.08,
                   "the energy chooses the horizon even when thresholds disagree",
                   String(format: "%.3f against %.3f", coverage, want))

            // ⚠ A column with no edge in it is sky all the way down — the
            // paper's rule, and what makes a frame of nothing but sky come out
            // covered rather than empty.
            let openColumn = w - 4
            report(alpha[(h - 2) * w + openColumn] > 0.5,
                   "and a column with no edge in it is sky to the bottom",
                   "\(alpha[(h - 2) * w + openColumn])")

            // ...while a column that does have one still stops at it.
            report(alpha[(h - 2) * w + 4] < 0.5,
                   "while a column that has one still stops there",
                   "\(alpha[(h - 2) * w + 4])")
        }
    }
}
