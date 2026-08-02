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

    /// How much calmer than the **whole frame** the sky has to be, as a ratio
    /// of mean gradient magnitude. The method's premise, turned into a check —
    /// see `detect` for why it is not compared against the ground. Orion's own
    /// number.
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
        var best: [Bool] = []
        for step in 0..<searchSteps {
            let t = lo + (hi - lo) * Float(step) / Float(searchSteps - 1)
            let region = fill(grad: grad, width: width, height: height, threshold: t)
            let cov = Double(region.lazy.filter { $0 }.count) / Double(width * height)
            // ⚠ A candidate that is not a plausible partition is not a
            // candidate. The energy prefers the *smallest* uniform region, so
            // without this the search settles on a sliver every time.
            if cov < minCoverage || cov > maxCoverage { continue }
            let score = energy(rgb: rgb, width: width, height: height, inSky: region)
            if score > bestScore { bestScore = score; best = region }
        }
        guard !best.isEmpty else {
            return .noSky(reason: "no region connected to the top edge divides "
                                + "this picture into a sky and a ground")
        }

        var alpha = [Float](repeating: 0, count: width * height)
        var lit = 0
        for i in 0..<(width * height) where best[i] { alpha[i] = 1; lit += 1 }
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

        // ⚠ **The assumption, checked — on the gradient, not on color.**
        //
        // The method rests on sky being smoother than the ground, and nothing
        // above verifies it: on a frame of pure texture the search still returns
        // whichever cut scores best, and a thin band across the top passes the
        // coverage floor while being nothing at all.
        //
        // The first version of this check compared the two regions' color
        // *covariance* and rejected genuine skies — measured on a flat sky over
        // noisy ground, the sky's covariance came out **larger** than the
        // ground's. Color spread is not smoothness: a sky with a gentle
        // top-to-bottom gradient has a wide color distribution and no edges in
        // it at all, which is exactly the thing being looked for.
        //
        // Mean gradient magnitude is the quantity the premise is actually about,
        // and it is already computed.
        // ⚠ **Against the whole frame, not against the ground.** Comparing the
        // two halves is circular: the fill *defines* them by gradient, so the
        // unfilled part is rougher by construction and the check can never
        // fail. On a frame of pure texture the region grew to 81% and the
        // comparison happily passed it.
        //
        // The frame's own mean is not circular in the same way. A real sky is
        // far calmer than the picture containing it; a region that merely
        // flooded across noise has the picture's own roughness in it.
        var skySum = 0.0, allSum = 0.0
        var skyN = 0
        for i in 0..<(width * height) {
            let v = Double(grad[i])
            allSum += v
            if best[i] { skySum += v; skyN += 1 }
        }
        if skyN > 0 {
            let calm = skySum / Double(skyN)
            let overall = allSum / Double(width * height)
            if overall > 1e-9 && calm > overall * smoothnessRatio {
                return .noSky(reason: "the calmest region joined to the top edge "
                                    + "is no calmer than the picture as a whole")
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
        // where that artifact sits.
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

    /// The sky region: a flood fill from the top edge over pixels calm enough
    /// to belong to it.
    ///
    /// ⚠ **This replaced a per-column border, and the difference is the whole
    /// story of the first attempt.** Taking the first row in each column whose
    /// gradient exceeds a threshold assumes the sky is a *function of x* — one
    /// row per column — and on a frame with a tower's lattice or an irregular
    /// treeline the column answers are unrelated to each other. It reported
    /// 18.2% coverage on the daylight frame, which read as a perfectly
    /// reasonable amount of sky, and drew as **vertical stripes**. A median
    /// filter across columns reduced the comb and did not touch the cause.
    ///
    /// A fill is 2D and connected, so it can go around the tower, stop at the
    /// treeline, and cannot produce a stripe: every pixel it takes is joined to
    /// the top edge by a path of calm pixels.
    ///
    /// Four-connected rather than eight: a diagonal step lets the region squeeze
    /// through a one-pixel gap in a branch, which is how a fill escapes into the
    /// ground and takes the whole frame.
    static func fill(grad: [Float], width: Int, height: Int,
                     threshold: Float) -> [Bool] {
        var inSky = [Bool](repeating: false, count: width * height)
        var queue: [Int] = []
        queue.reserveCapacity(width * 4)

        // Seeds: the top row, wherever it is calm enough to be sky at all.
        for x in 0..<width where grad[x] <= threshold {
            inSky[x] = true
            queue.append(x)
        }

        var head = 0
        while head < queue.count {
            let i = queue[head]; head += 1
            let x = i % width, y = i / width
            // Four-connected.
            if x > 0            { visit(i - 1, &inSky, &queue, grad, threshold) }
            if x < width - 1    { visit(i + 1, &inSky, &queue, grad, threshold) }
            if y > 0            { visit(i - width, &inSky, &queue, grad, threshold) }
            if y < height - 1   { visit(i + width, &inSky, &queue, grad, threshold) }
        }
        return inSky
    }

    private static func visit(_ i: Int, _ inSky: inout [Bool], _ queue: inout [Int],
                              _ grad: [Float], _ threshold: Float) {
        guard !inSky[i], grad[i] <= threshold else { return }
        inSky[i] = true
        queue.append(i)
    }

    /// The paper's energy: `1 / (γ·det(Σs) + det(Σg) + γ·λs₁² + λg₁²)`, with
    /// `γ = 2`. Maximising it minimizes the spread *within* each region, so the
    /// best border is the one that makes both halves internally uniform.
    ///
    /// ⚠ The largest eigenvalue is approximated by the largest **diagonal**
    /// entry of the covariance. A 3×3 symmetric eigenvalue solve per threshold
    /// per frame is real work for a term that only breaks ties, and the
    /// diagonal is a lower bound that orders candidates the same way in every
    /// case measured. Recorded in `UNSOURCED.md` as a departure.
    static func energy(rgb: [Float], width: Int, height: Int,
                       inSky: [Bool]) -> Double {
        var sky = Stats(), ground = Stats()
        for i in 0..<(width * height) {
            let p = (Double(rgb[i * 3]), Double(rgb[i * 3 + 1]), Double(rgb[i * 3 + 2]))
            if inSky[i] { sky.add(p) } else { ground.add(p) }
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

        /// The largest eigenvalue of the covariance — exactly, in closed form.
        ///
        /// > Oliver K. Smith, **"Eigenvalues of a symmetric 3 × 3 matrix"**,
        /// > *Communications of the ACM* 4(4), p. 168, April 1961.
        /// > [doi:10.1145/355578.366316](https://doi.org/10.1145/355578.366316)
        ///
        /// ⚠ **This used to be the largest diagonal entry, and that was wrong in
        /// both of its justifications.** The diagonal is a lower bound, equal to
        /// the largest eigenvalue only when the covariance is already diagonal,
        /// and the comment claimed it "orders candidates the same way in every
        /// case measured" — true only because every case measured had the same
        /// covariance *shape*. Given populations that are wide in different
        /// channels, the proxy reorders a pair against the exact value; measured
        /// 2026-07-31, 1 pair in 21.
        ///
        /// The second justification was that an exact solve is "real work". It
        /// is not: this runs once per candidate threshold per region, so 48
        /// times in a whole detection, against a Sobel over every pixel. The
        /// cost it was avoiding was never on the table.
        ///
        /// Smith's method is a closed form — no iteration, no convergence
        /// criterion, and therefore no way for it to be slow or to not
        /// terminate.
        func largestVariance() -> Double {
            let c = covariance()
            let p1 = c[1] * c[1] + c[2] * c[2] + c[5] * c[5]
            // Already diagonal: the eigenvalues are the diagonal entries.
            if p1 <= 0 { return max(c[0], max(c[4], c[8])) }

            let q = (c[0] + c[4] + c[8]) / 3
            let d0 = c[0] - q, d4 = c[4] - q, d8 = c[8] - q
            let p2 = d0 * d0 + d4 * d4 + d8 * d8 + 2 * p1
            let p = (p2 / 6).squareRoot()
            guard p > 0 else { return q }

            // det((A - qI) / p) / 2
            let b = [d0 / p, c[1] / p, c[2] / p,
                     c[3] / p, d4 / p, c[5] / p,
                     c[6] / p, c[7] / p, d8 / p]
            let det = b[0] * (b[4] * b[8] - b[5] * b[7])
                    - b[1] * (b[3] * b[8] - b[5] * b[6])
                    + b[2] * (b[3] * b[7] - b[4] * b[6])
            // ⚠ Clamped before `acos`. Rounding can push this a hair outside
            // [-1, 1] on a near-degenerate covariance, and `acos` of that is
            // NaN — which would propagate into the energy and make the search
            // pick whichever candidate happened to compare false against a NaN.
            let r = min(max(det / 2, -1), 1)
            let phi = Foundation.acos(r) / 3
            return q + 2 * p * Foundation.cos(phi)
        }
    }
}
