// The tone curve's own maths, against the engine's.
//
// Split out of ViewportTests.swift 2026-07-31.

import CoreGraphics
import Foundation

extension ViewportTests {

    /// The panel's spline must be the engine's spline.
    ///
    /// The panel evaluates the curve itself, because it has to draw the line
    /// during a drag rather than wait for a render. Two implementations of one
    /// spline is a real risk of them quietly diverging, so these are values
    /// taken from `pipe/ToneCurve.cpp` — if the two ever disagree, the line you
    /// drew and the picture you got would stop matching, which is the kind of
    /// bug that wastes an evening.
    static func testCurveMatchesTheEngine() {
        let s = [CurvePoint(x: 0, y: 0), CurvePoint(x: 0.25, y: 0.14),
                 CurvePoint(x: 0.62, y: 0.78), CurvePoint(x: 1, y: 1)]
        let sWant: [Float] = [0, 0.0497819521, 0.1037092581, 0.1974559724, 0.3822722733, 0.5956153274, 0.7598698139, 0.8408008814, 0.8986957073, 0.9477383494, 1]

        for (i, want) in sWant.enumerated() {
            let x = Float(i) / 10
            near(CGFloat(CurveMath.evaluate(s, at: x)), CGFloat(want), 1e-6,
                 "S-curve matches the engine at x = \(x)")
        }

        // Endpoints away from the corners: a lifted black and a pulled white,
        // which is where an implementation that assumes 0,0 and 1,1 breaks.
        let lifted = [CurvePoint(x: 0, y: 0.1), CurvePoint(x: 0.5, y: 0.4),
                      CurvePoint(x: 1, y: 0.9)]
        let liftedWant: [Float] = [0.1000000015, 0.1576000154, 0.2128000110, 0.2692000270, 0.3303999901, 0.4000000060, 0.4839999974, 0.5820000172, 0.6880000234, 0.7960000038, 0.8999999762]

        for (i, want) in liftedWant.enumerated() {
            let x = Float(i) / 10
            near(CGFloat(CurveMath.evaluate(lifted, at: x)), CGFloat(want), 1e-6,
                 "lifted curve matches the engine at x = \(x)")
        }

        // The identity has to be exactly the identity, or every image picks up
        // a faint contrast change the moment the panel is opened.
        for i in 0...20 {
            let x = Float(i) / 20
            near(CGFloat(CurveMath.evaluate(ToneCurve.identity, at: x)), CGFloat(x), 1e-7,
                 "the identity curve is the identity at x = \(x)")
        }

        // Monotone means monotone: no control point arrangement may make the
        // output go backwards, which in a photo reads as a tonal reversal.
        let harsh = [CurvePoint(x: 0, y: 0), CurvePoint(x: 0.05, y: 0.6),
                     CurvePoint(x: 0.1, y: 0.62), CurvePoint(x: 0.9, y: 0.65),
                     CurvePoint(x: 1, y: 1)]
        var previous = CurveMath.evaluate(harsh, at: 0)
        var monotone = true
        for i in 1...400 {
            let y = CurveMath.evaluate(harsh, at: Float(i) / 400)
            if y < previous - 1e-6 { monotone = false; break }
            previous = y
        }
        report(monotone, "a steep curve never reverses")
    }

    /// A curve's control points must strictly ascend in x.
    ///
    /// The engine treats a non-ascending curve as malformed and falls back to
    /// the identity — correctly, since the interpolator assumes ordering. But
    /// the panel could produce one: clicking near the right edge appended a
    /// second point at x = 1, and the whole curve silently snapped back, which
    /// reads as the panel being broken rather than as input being rejected.
    static func testCurvePointsStayOrdered() {
        var points = ToneCurve.identity

        // Every click position, twice over, including the edges that broke it
        // and the repeats that broke the first fix.
        let clicks: [Float] = [-0.5, 0, 0.001, 0.25, 0.5, 0.75, 0.999, 1, 1.5,
                               0, 0.001, 1, 0.999, 0.5, 0.5, 0.25]

        for f in clicks {
            guard let placed = CurveMath.insertion(of: f, into: points) else { continue }
            points.insert(CurvePoint(x: placed.x,
                                     y: CurveMath.evaluate(points, at: placed.x)),
                          at: placed.index)

            var ascending = true
            for i in 1..<points.count where points[i].x <= points[i - 1].x {
                ascending = false
            }
            report(ascending, "points ascend after a click at \(f)")
            report(points[0].x == 0 && points[points.count - 1].x == 1,
                   "the endpoints survive a click at \(f)")
        }

        // And a curve packed with points must decline rather than duplicate.
        var packed = ToneCurve.identity
        for i in 1..<40 {
            let f = Float(i) / 40
            if let placed = CurveMath.insertion(of: f, into: packed) {
                packed.insert(CurvePoint(x: placed.x, y: placed.x), at: placed.index)
            }
        }
        var stillAscending = true
        for i in 1..<packed.count where packed[i].x <= packed[i - 1].x {
            stillAscending = false
        }
        report(stillAscending, "a densely packed curve still ascends")
    }
}
