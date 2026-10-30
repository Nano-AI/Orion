namespace OrionShell.Masks;

/// Sky detection from one picture, with no model: a port of app/SkyDetector.swift,
/// which cites the method: Shen and Wang 2013 (research/sky-detection.md), a
/// gradient threshold, a fill from the top edge, and the colour-energy score
/// that picks the threshold. Same constants, same refusals, same sentences.
static class SkyDetector {
    const int SearchSteps = 24;
    const double MinCoverage = 0.02, MaxCoverage = 0.90, SmoothnessRatio = 0.5;

    /// `rgb`: width * height * 3 display-referred floats in 0…1. A 0/1 matte, or the reason there is none.
    public static (float[]? Alpha, string? NoSky) Detect(float[] rgb, int width, int height) {
        if (width <= 2 || height <= 2 || rgb.Length < width * height * 3) return (null, "the picture is too small to look at");
        int n = width * height;
        var grad = Gradient(rgb, width, height);
        var sorted = (float[])grad.Clone();
        Array.Sort(sorted);
        float lo = sorted[(int)((n - 1) * 0.05)], hi = sorted[(int)((n - 1) * 0.95)];
        if (!(hi > lo)) return (null, "the picture has no edges to find a horizon in");

        double bestScore = double.NegativeInfinity;
        bool[]? best = null;
        for (int step = 0; step < SearchSteps; step++) {
            var region = Fill(grad, width, height, lo + (hi - lo) * step / (SearchSteps - 1));
            double cov = (double)region.Count(b => b) / n;
            if (cov < MinCoverage || cov > MaxCoverage) continue;
            double score = Energy(rgb, region);
            if (score > bestScore) (bestScore, best) = (score, region);
        }
        if (best == null) return (null, "no region connected to the top edge divides this picture into a sky and a ground");

        // The region checks of the Swift (coverage, then calm against the whole picture).
        double skySum = 0, allSum = 0;
        int lit = 0;
        var alpha = new float[n];
        for (int i = 0; i < n; i++) {
            allSum += grad[i];
            if (!best[i]) continue;
            alpha[i] = 1;
            lit++;
            skySum += grad[i];
        }
        double coverage = (double)lit / n;
        if (coverage < MinCoverage) return (null, "no region connected to the top edge looks like sky");
        if (coverage > MaxCoverage) return (null, "almost the whole frame scored as sky, which is what this looks like when there is no horizon");
        double overall = allSum / n;
        if (lit > 0 && overall > 1e-9 && skySum / lit > overall * SmoothnessRatio)
            return (null, "the calmest region joined to the top edge is no calmer than the picture as a whole");
        return (alpha, null);
    }

    /// Sobel magnitude of Rec.2020 luma, edges clamped.
    static float[] Gradient(float[] rgb, int w, int h) {
        var y = new float[w * h];
        for (int i = 0; i < y.Length; i++) y[i] = 0.2627f * rgb[i * 3] + 0.6780f * rgb[i * 3 + 1] + 0.0593f * rgb[i * 3 + 2];
        float At(int c, int r) => y[Math.Clamp(r, 0, h - 1) * w + Math.Clamp(c, 0, w - 1)];
        var g = new float[w * h];
        for (int r = 0; r < h; r++)
            for (int c = 0; c < w; c++) {
                float gx = -At(c - 1, r - 1) - 2 * At(c - 1, r) - At(c - 1, r + 1) + At(c + 1, r - 1) + 2 * At(c + 1, r) + At(c + 1, r + 1);
                float gy = -At(c - 1, r - 1) - 2 * At(c, r - 1) - At(c + 1, r - 1) + At(c - 1, r + 1) + 2 * At(c, r + 1) + At(c + 1, r + 1);
                g[r * w + c] = MathF.Sqrt(gx * gx + gy * gy);
            }
        return g;
    }

    /// The 4-connected region at or under `threshold` that touches the top row.
    static bool[] Fill(float[] grad, int w, int h, float threshold) {
        var inSky = new bool[w * h];
        var queue = new Queue<int>();
        void Visit(int i) { if (!inSky[i] && grad[i] <= threshold) { inSky[i] = true; queue.Enqueue(i); } }
        for (int x = 0; x < w; x++) Visit(x);
        while (queue.TryDequeue(out int i)) {
            int x = i % w, y = i / w;
            if (x > 0) Visit(i - 1);
            if (x < w - 1) Visit(i + 1);
            if (y > 0) Visit(i - w);
            if (y < h - 1) Visit(i + w);
        }
        return inSky;
    }

    /// The paper's energy: the inverse of how spread the sky and ground colours are, sky weighted by γ = 2.
    static double Energy(float[] rgb, bool[] inSky) {
        Stats sky = new(), ground = new();
        for (int i = 0; i < inSky.Length; i++) (inSky[i] ? sky : ground).Add(rgb[i * 3], rgb[i * 3 + 1], rgb[i * 3 + 2]);
        if (sky.N <= 1 || ground.N <= 1) return double.NegativeInfinity;
        const double gamma = 2;
        double denom = gamma * sky.Determinant() + ground.Determinant() + gamma * sky.LargestVariance() + ground.LargestVariance();
        return denom > 1e-12 ? 1 / denom : double.PositiveInfinity;
    }

    sealed class Stats {
        public int N;
        readonly double[] _sum = new double[3], _sq = new double[9];

        public void Add(double r, double g, double b) {
            N++;
            double[] v = [r, g, b];
            for (int a = 0; a < 3; a++) {
                _sum[a] += v[a];
                for (int c = 0; c < 3; c++) _sq[a * 3 + c] += v[a] * v[c];
            }
        }

        double[] Covariance() {
            var c = new double[9];
            if (N <= 1) return c;
            for (int a = 0; a < 3; a++)
                for (int b = 0; b < 3; b++) c[a * 3 + b] = _sq[a * 3 + b] / N - _sum[a] / N * (_sum[b] / N);
            return c;
        }

        static double Det(double[] c) =>
            c[0] * (c[4] * c[8] - c[5] * c[7]) - c[1] * (c[3] * c[8] - c[5] * c[6]) + c[2] * (c[3] * c[7] - c[4] * c[6]);

        public double Determinant() => Det(Covariance());

        /// The largest eigenvalue of the symmetric 3×3 covariance, in closed form (O. K. Smith, CACM 4(4), 1961).
        public double LargestVariance() {
            var c = Covariance();
            double p1 = c[1] * c[1] + c[2] * c[2] + c[5] * c[5];
            if (p1 <= 0) return Math.Max(c[0], Math.Max(c[4], c[8]));
            double q = (c[0] + c[4] + c[8]) / 3, d0 = c[0] - q, d4 = c[4] - q, d8 = c[8] - q;
            double p = Math.Sqrt((d0 * d0 + d4 * d4 + d8 * d8 + 2 * p1) / 6);
            if (!(p > 0)) return q;
            double[] b = [d0 / p, c[1] / p, c[2] / p, c[3] / p, d4 / p, c[5] / p, c[6] / p, c[7] / p, d8 / p];
            return q + 2 * p * Math.Cos(Math.Acos(Math.Clamp(Det(b) / 2, -1, 1)) / 3);
        }
    }
}
