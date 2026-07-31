import Foundation

/// Sky detection from a single image, with no model.
///
/// `research/sky-detection.md` has the citation and the failure list;
/// `DECISIONS.md` #78 has why this is not a segmentation network. The short
/// version: no Apple API produces a sky matte from an imported RAW, and no
/// model is known whose architecture, weights *and* training data all carry a
/// clean redistribution grant.
///
/// **Shen & Wang (2013).** Sky is smooth and the ground is not, so the boundary
/// is where the gradient first becomes large scanning down a column — and the
/// whole problem is choosing *how* large. For each candidate threshold, take the
/// resulting border, and score the partition by how internally uniform the two
/// halves are. Keep the best.
///
/// ⚠ **It never asks what sky looks like.** No hue prior, no blue. It asks which
/// horizontal cut makes both halves most self-consistent, which is why it works
/// on an overcast sky as well as a blue one.
///
/// Pure arithmetic, no AppKit and no facade call, so `orion-viewport-tests` can
/// pin it without a GPU — the same split `MatteGeometry` and `BatchExport` use.
enum SkyDetector {

    /// What the detector concluded.
    enum Outcome: Equatable {
        /// Coverage, row-major, one float per pixel.
        case found(alpha: [Float], coverage: Double)
        /// ⚠ A real answer, not an error. A frame with no sky must say so —
        /// returning "everything" is indistinguishable from the feature being
        /// broken, which is the failure the person matte had before it learned
        /// to report an empty result.
        case noSky(reason: String)
    }

    /// How many thresholds the search tries. Orion's own number —
    /// `UNSOURCED.md`. The paper searches a fixed absolute range, which does not
    /// transfer: it assumes 8-bit camera JPEGs and this runs on an AgX-mapped
    /// render whose gradient scale is different.
    static let searchSteps = 24

    /// Coverage outside this band is reported as no sky.
    static let minCoverage = 0.02
    static let maxCoverage = 0.90

    /// How much calmer than the ground the sky has to be, as a ratio of mean
    /// gradient magnitude. The method's whole premise, turned into a check —
    /// see `detect`. Orion's own number.
    static let smoothnessRatio = 0.5

    /// `rgb` is row-major, three floats per pixel, 0..1.
    static func detect(rgb: [Float], width: Int, height: Int) -> Outcome {
        guard width > 2, height > 2, rgb.count >= width * height * 3 else {
            return .noSky(reason: "the picture is too small to look at")
        }

        let grad = gradientMagnitude(rgb: rgb, width: width, height: height)

        // ⚠ Percentiles rather than the paper's absolute range, so the search is
        // relative to this frame's own contrast. A fixed range tuned on 8-bit
        // JPEGs finds nothing on a flat render and everything on a punchy one.
        let sorted = grad.sorted()
        let lo = sorted[Int(Double(sorted.count - 1) * 0.05)]
        let hi = sorted[Int(Double(sorted.count - 1) * 0.95)]
        guard hi > lo else {
            return .noSky(reason: "the picture has no edges to find a horizon in")
        }

        // ⚠ **A candidate that is not a plausible partition is not a
        // candidate.** The paper's energy assumes the sky is internally
        // uniform, and a *real* sky is not — it runs light at the horizon and
        // deep at the zenith, so its covariance is far from zero. A one-row
        // sky, by contrast, is perfectly uniform and therefore always scores
        // best. Measured on the daylight frame before this guard: 673 of 684
        // columns cut inside the top eighth, and the detector reported no sky
        // on every photograph tried.
        //
        // So the coverage bounds are applied **during** the search rather than
        // after it. They were already the definition of an answer worth
        // returning; using them to choose is the same statement made earlier.
        var bestScore = -Double.infinity
        var bestBorder: [Int] = []
        for step in 0..<searchSteps {
            let t = lo + (hi - lo) * Float(step) / Float(searchSteps - 1)
            let border = smoothed(borderFor(grad: grad, width: width,
                                            height: height, threshold: t),
                                  width: width)
            let cov = Double(border.reduce(0) { $0 + min(max($1, 0), height) })
                    / Double(width * height)
            if cov < minCoverage || cov > maxCoverage { continue }
            let score = energy(rgb: rgb, width: width, height: height, border: border)
            if score > bestScore { bestScore = score; bestBorder = border }
        }
        guard !bestBorder.isEmpty else {
            return .noSky(reason: "no horizon divides this picture into a sky "
                                + "and a ground")
        }

        var alpha = [Float](repeating: 0, count: width * height)
        var lit = 0
        for x in 0..<width {
            let b = bestBorder[x]
            if b <= 0 { continue }
            for y in 0..<b { alpha[y * width + x] = 1 }
            lit += b
        }
        let coverage = Double(lit) / Double(width * height)

        // ⚠ Both ends. Too little is no sky; too much is a frame the energy gave
        // up on and cut at the bottom, which would cover the whole photograph.
        if coverage < minCoverage {
            return .noSky(reason: "no region connected to the top edge looks like sky")
        }
        if coverage > maxCoverage {
            return .noSky(reason: "almost the whole frame scored as sky, which is "
                                + "what this looks like when there is no horizon")
        }

        // ⚠ **The assumption, checked — on the gradient, not on colour.**
        //
        // The method rests on sky being smoother than the ground, and nothing
        // above verifies it: on a frame of pure texture the search still returns
        // whichever cut scores best, and a thin band across the top passes the
        // coverage floor while being nothing at all.
        //
        // The first version of this check compared the two regions' colour
        // *covariance* and rejected genuine skies — measured on a flat sky over
        // noisy ground, the sky's covariance came out **larger** than the
        // ground's. Colour spread is not smoothness: a sky with a gentle
        // top-to-bottom gradient has a wide colour distribution and no edges in
        // it at all, which is exactly the thing being looked for.
        //
        // Mean gradient magnitude is the quantity the premise is actually about,
        // and it is already computed.
        var skySum = 0.0, groundSum = 0.0
        var skyN = 0, groundN = 0
        for x in 0..<width {
            let b = min(max(bestBorder[x], 0), height)
            for yy in 0..<height {
                let v = Double(grad[yy * width + x])
                if yy < b { skySum += v; skyN += 1 } else { groundSum += v; groundN += 1 }
            }
        }
        if skyN > 0 && groundN > 0 {
            let calm = skySum / Double(skyN)
            let rough = groundSum / Double(groundN)
            if rough > 1e-9 && calm > rough * smoothnessRatio {
                return .noSky(reason: "nothing at the top of the frame is calmer "
                                    + "than what is under it")
            }
        }
        return .found(alpha: alpha, coverage: coverage)
    }

    // MARK: The pieces

    /// Sobel magnitude over the luminance. Rec.2020 weights, matching the rest
    /// of the program — a second definition of brightness is a bug waiting.
    static func gradientMagnitude(rgb: [Float], width: Int, height: Int) -> [Float] {
        var y = [Float](repeating: 0, count: width * height)
        for i in 0..<(width * height) {
            y[i] = 0.2627 * rgb[i * 3] + 0.6780 * rgb[i * 3 + 1] + 0.0593 * rgb[i * 3 + 2]
        }
        // ⚠ **Clamped at the edges, not left at zero.** Leaving the border row
        // at zero makes the very top of every frame look perfectly smooth — so
        // a thin band across the top of a frame of pure texture passed the
        // smoothness check, because the zero row dragged its mean down. The
        // whole method reads downward from the top edge, which is precisely
        // where that artefact sits.
        let at = { (col: Int, row: Int) -> Float in
            y[min(max(row, 0), height - 1) * width + min(max(col, 0), width - 1)]
        }
        var g = [Float](repeating: 0, count: width * height)
        for row in 0..<height {
            for col in 0..<width {
                let gx = -at(col - 1, row - 1) - 2 * at(col - 1, row) - at(col - 1, row + 1)
                       +  at(col + 1, row - 1) + 2 * at(col + 1, row) + at(col + 1, row + 1)
                let gy = -at(col - 1, row - 1) - 2 * at(col, row - 1) - at(col + 1, row - 1)
                       +  at(col - 1, row + 1) + 2 * at(col, row + 1) + at(col + 1, row + 1)
                g[row * width + col] = (gx * gx + gy * gy).squareRoot()
            }
        }
        return g
    }

    /// The first row in each column whose gradient exceeds `threshold`.
    ///
    /// ⚠ A column that never exceeds it is sky **all the way down**, which is
    /// the paper's rule and the reason an all-sky frame comes out covered rather
    /// than empty. Treating it as "no sky here" instead would make a photograph
    /// of nothing but sky return nothing.
    static func borderFor(grad: [Float], width: Int, height: Int,
                          threshold: Float) -> [Int] {
        var border = [Int](repeating: height, count: width)
        for x in 0..<width {
            for yy in 0..<height where grad[yy * width + x] > threshold {
                border[x] = yy
                break
            }
        }
        return border
    }

    /// ⚠ **A horizon is smooth in x, and nothing above says so.**
    ///
    /// Per-column first-exceedance is a comb on a real photograph: grain, and
    /// fine structure like a tower's lattice, make adjacent columns stop at
    /// wildly different rows. Measured on the daylight frame, the raw border
    /// gave 18.2% coverage that *looked* plausible as a number and was vertical
    /// stripes when drawn — which is why the overlay is the check that matters
    /// and a coverage figure is not.
    ///
    /// A median over a window is the standard repair and it is the right one
    /// here: it removes the spikes without rounding off a genuine step, which a
    /// mean would. The window is a fraction of the width so it does not need
    /// retuning per resolution. Orion's own number — `UNSOURCED.md`.
    static func smoothed(_ border: [Int], width: Int) -> [Int] {
        let half = max(2, width / 40)
        var out = border
        for x in 0..<width {
            let lo = max(0, x - half), hi = min(width - 1, x + half)
            var window = Array(border[lo...hi])
            window.sort()
            out[x] = window[window.count / 2]
        }
        return out
    }

    /// The paper's energy: `1 / (γ·det(Σs) + det(Σg) + γ·λs₁² + λg₁²)`, with
    /// `γ = 2`. Maximising it minimises the spread *within* each region, so the
    /// best border is the one that makes both halves internally uniform.
    ///
    /// ⚠ The largest eigenvalue is approximated by the largest **diagonal**
    /// entry of the covariance. A 3×3 symmetric eigenvalue solve per threshold
    /// per frame is real work for a term that only breaks ties, and the
    /// diagonal is a lower bound that orders candidates the same way in every
    /// case measured. Recorded in `UNSOURCED.md` as a departure.
    static func energy(rgb: [Float], width: Int, height: Int,
                       border: [Int]) -> Double {
        var sky = Stats(), ground = Stats()
        for x in 0..<width {
            let b = min(max(border[x], 0), height)
            for yy in 0..<height {
                let i = (yy * width + x) * 3
                let p = (Double(rgb[i]), Double(rgb[i + 1]), Double(rgb[i + 2]))
                if yy < b { sky.add(p) } else { ground.add(p) }
            }
        }
        // A partition with nothing on one side is not a partition.
        guard sky.n > 1, ground.n > 1 else { return -.infinity }

        let gamma = 2.0
        let denom = gamma * sky.determinant() + ground.determinant()
                  + gamma * sky.largestVariance() + ground.largestVariance()
        return denom > 1e-12 ? 1.0 / denom : .infinity
    }

    /// Running mean and covariance of an RGB population.
    struct Stats {
        var n = 0
        private var sum = (0.0, 0.0, 0.0)
        private var sq = [Double](repeating: 0, count: 9)

        mutating func add(_ p: (Double, Double, Double)) {
            n += 1
            sum.0 += p.0; sum.1 += p.1; sum.2 += p.2
            let v = [p.0, p.1, p.2]
            for a in 0..<3 { for b in 0..<3 { sq[a * 3 + b] += v[a] * v[b] } }
        }

        func covariance() -> [Double] {
            guard n > 1 else { return [Double](repeating: 0, count: 9) }
            let m = [sum.0 / Double(n), sum.1 / Double(n), sum.2 / Double(n)]
            var c = [Double](repeating: 0, count: 9)
            for a in 0..<3 {
                for b in 0..<3 { c[a * 3 + b] = sq[a * 3 + b] / Double(n) - m[a] * m[b] }
            }
            return c
        }

        func determinant() -> Double {
            let c = covariance()
            return c[0] * (c[4] * c[8] - c[5] * c[7])
                 - c[1] * (c[3] * c[8] - c[5] * c[6])
                 + c[2] * (c[3] * c[7] - c[4] * c[6])
        }

        func largestVariance() -> Double {
            let c = covariance()
            return max(c[0], max(c[4], c[8]))
        }
    }
}
