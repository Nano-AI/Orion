import Foundation

/// The curve the panel draws, evaluated the way the engine evaluates it.
///
/// A deliberate second implementation of `pipe/ToneCurve.cpp`: the panel has to
/// draw the line before any render comes back, and reading it out of a GPU LUT
/// on every frame of a drag would be absurd. Two implementations of one spline
/// is a real risk, so `ViewportTests.testCurveMatchesTheEngine` pins this
/// against values taken from the C++ rather than trusting them to agree.
///
/// Fritsch–Carlson monotone cubic Hermite. Monotone matters: a plain
/// Catmull-Rom puts a wiggle between control points that nobody asked for, and
/// in an image that reads as banding or a tonal reversal.
enum CurveMath {

    static func evaluate(_ channel: [CurvePoint], at x: Float) -> Float {
        guard channel.count >= 2 else { return x }

        let p = channel.sorted { $0.x < $1.x }
        if x <= p[0].x { return p[0].y }
        if x >= p[p.count - 1].x { return p[p.count - 1].y }

        let t = tangents(p)

        var i = 0
        while i + 2 < p.count && x > p[i + 1].x { i += 1 }

        let h = p[i + 1].x - p[i].x
        guard h > 0 else { return p[i].y }

        let s = (x - p[i].x) / h
        let s2 = s * s
        let s3 = s2 * s

        return (2 * s3 - 3 * s2 + 1) * p[i].y
             + (s3 - 2 * s2 + s) * h * t[i]
             + (-2 * s3 + 3 * s2) * p[i + 1].y
             + (s3 - s2) * h * t[i + 1]
    }

    /// Zero the slope wherever the data changes direction — that is the whole
    /// trick that keeps the spline monotone.
    private static func tangents(_ p: [CurvePoint]) -> [Float] {
        let n = p.count
        var dx = [Float](repeating: 0, count: n - 1)
        var m = [Float](repeating: 0, count: n - 1)
        var t = [Float](repeating: 0, count: n)

        for i in 0..<(n - 1) {
            dx[i] = p[i + 1].x - p[i].x
            let dy = p[i + 1].y - p[i].y
            m[i] = dx[i] != 0 ? dy / dx[i] : 0
        }

        t[0] = m[0]
        for i in 1..<(n - 1) {
            if m[i - 1] * m[i] <= 0 {
                t[i] = 0
            } else {
                let w1 = 2 * dx[i] + dx[i - 1]
                let w2 = dx[i] + 2 * dx[i - 1]
                t[i] = (w1 + w2) / (w1 / m[i - 1] + w2 / m[i])
            }
        }
        t[n - 1] = m[n - 2]
        return t
    }
}
