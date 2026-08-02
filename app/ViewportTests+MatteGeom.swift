// Turning a matte back into frame coordinates.
//
// Split out of ViewportTests.swift 2026-07-31.

import CoreGraphics
import Foundation

extension ViewportTests {

    /// Undoing the turns has to be an exact permutation, and its own inverse
    /// under negation. Anything that resampled, dropped a row or transposed
    /// without reflecting would break one of these.
    static func testMatteTurnsRoundTrip() {
        // Deliberately non-square and with every value distinct, so a
        // transposition cannot hide behind symmetry. 4 wide, 3 tall.
        let w = 4, h = 3
        let src = (0..<(w * h)).map { Float($0) }

        for k in [0, 1, 2, 3, -1, -2, 5] {
            let r = MatteGeometry.undoTurns(src, width: w, height: h, turns: k)
            let odd = (((k % 4) + 4) % 4) % 2 != 0
            report(r.width == (odd ? h : w) && r.height == (odd ? w : h),
                   "turn \(k) swaps the dimensions exactly when it is odd",
                   "\(r.width)x\(r.height)")
            report(r.pixels.count == w * h && Set(r.pixels) == Set(src),
                   "turn \(k) is a permutation — every value survives, none is invented")

            // Turning back must give the original array, element for element.
            let back = MatteGeometry.undoTurns(r.pixels, width: r.width,
                                               height: r.height, turns: -k)
            report(back.pixels == src && back.width == w && back.height == h,
                   "and undoing turn \(k) restores the raster exactly")
        }
    }

    /// ⚠ The check that matters, and the reason this is not just a rotate
    /// helper: the raster turn must agree with the *point* transform the
    /// parametric masks already use.
    ///
    /// `mask::toFrame` in the engine is derived from "a quarter turn clockwise
    /// sends a frame point (x, y) to (1 - y, x) on screen". If the raster went
    /// the other way, a matte and a gradient placed on the same subject would
    /// land on opposite sides of the picture — each self-consistent, and the
    /// disagreement only visible with both on screen at once.
    static func testMatteTurnAgreesWithTheMaskTransform() {
        let w = 8, h = 5

        // One lit pixel, off-center in both axes so no symmetry can rescue a
        // wrong answer.
        let sx = 6, sy = 1
        var src = [Float](repeating: 0, count: w * h)
        src[sy * w + sx] = 1

        for k in 1...3 {
            let r = MatteGeometry.undoTurns(src, width: w, height: h, turns: k)

            // Where the point transform says that pixel came from. Screen
            // coordinates normalized, run back through the same map
            // `mask::toFrame` applies: (x, y) -> (y, 1 - x), k times.
            var u = (Double(sx) + 0.5) / Double(w)
            var v = (Double(sy) + 0.5) / Double(h)
            for _ in 0..<k { let nu = v; let nv = 1 - u; u = nu; v = nv }

            let fx = Int(u * Double(r.width))
            let fy = Int(v * Double(r.height))
            let hit = r.pixels[fy * r.width + fx]
            report(hit == 1,
                   "the raster turn \(k) puts the pixel where mask::toFrame says it goes",
                   "expected 1 at \(fx),\(fy) of \(r.width)x\(r.height), got \(hit)")
        }
    }

    static func testMattePreviewSize() {
        // Capped on the long edge, aspect preserved.
        let a = MatteGeometry.previewSize(frameWidth: 6024, frameHeight: 4024,
                                          longEdge: 1024)
        report(a.width == 1024 && abs(a.height - 684) <= 1,
               "a landscape frame is capped on its long edge", "\(a.width)x\(a.height)")

        let b = MatteGeometry.previewSize(frameWidth: 4024, frameHeight: 6024,
                                          longEdge: 1024)
        report(b.height == 1024 && abs(b.width - 684) <= 1,
               "and a portrait one on its own long edge", "\(b.width)x\(b.height)")

        // ⚠ Never upscales. Handing a model more pixels than the photograph has
        // is inventing detail for it to segment.
        let c = MatteGeometry.previewSize(frameWidth: 640, frameHeight: 480,
                                          longEdge: 1024)
        report(c.width == 640 && c.height == 480,
               "a frame under the cap is passed at its own size", "\(c.width)x\(c.height)")
    }

    // MARK: Presets
}
