namespace OrionShell;

/// A port of app/CurveMath.swift: the curve the panel draws, evaluated the way
/// pipe/ToneCurve.cpp does — Fritsch–Carlson monotone cubic Hermite, so no
/// wiggle appears between points that nobody asked for.
static class CurveMath {
    public static readonly (float X, float Y)[] Identity = [(0, 0), (1, 1)];

    public static float Evaluate(IReadOnlyList<(float X, float Y)> channel, float x) {
        if (channel.Count < 2) return x;
        var p = channel.OrderBy(q => q.X).ToArray();
        if (x <= p[0].X) return p[0].Y;
        if (x >= p[^1].X) return p[^1].Y;
        var t = Tangents(p);
        int i = 0;
        while (i + 2 < p.Length && x > p[i + 1].X) i++;
        float h = p[i + 1].X - p[i].X;
        if (h <= 0) return p[i].Y;
        float s = (x - p[i].X) / h, s2 = s * s, s3 = s2 * s;
        return (2 * s3 - 3 * s2 + 1) * p[i].Y + (s3 - 2 * s2 + s) * h * t[i]
             + (-2 * s3 + 3 * s2) * p[i + 1].Y + (s3 - s2) * h * t[i + 1];
    }

    /// Zero the slope wherever the data changes direction: that keeps it monotone.
    static float[] Tangents((float X, float Y)[] p) {
        int n = p.Length;
        var dx = new float[n - 1];
        var m = new float[n - 1];
        var t = new float[n];
        for (int i = 0; i < n - 1; i++) {
            dx[i] = p[i + 1].X - p[i].X;
            m[i] = dx[i] != 0 ? (p[i + 1].Y - p[i].Y) / dx[i] : 0;
        }
        t[0] = m[0];
        for (int i = 1; i < n - 1; i++) {
            if (m[i - 1] * m[i] <= 0) { t[i] = 0; continue; }
            float w1 = 2 * dx[i] + dx[i - 1], w2 = dx[i] + 2 * dx[i - 1];
            t[i] = (w1 + w2) / (w1 / m[i - 1] + w2 / m[i]);
        }
        t[n - 1] = m[n - 2];
        return t;
    }

    /// Where a new point goes and at what x, strictly between its neighbours,
    /// or null when there is no room: the engine treats a repeated x as a
    /// malformed curve and silently falls back to the identity.
    public static (int Index, float X)? Insertion(float fraction, IReadOnlyList<(float X, float Y)> points) {
        if (points.Count < 2) return null;
        float raw = Math.Clamp(fraction, 0, 1);
        int first = points.Select((q, i) => (q, i)).FirstOrDefault(e => e.q.X > raw, (default, points.Count)).Item2;
        int at = Math.Clamp(first, 1, points.Count - 1);
        float lower = points[at - 1].X, upper = points[at].X;
        if (upper - lower <= 0.02f) return null;
        return (at, Math.Clamp(raw, lower + 0.01f, upper - 0.01f));
    }
}
