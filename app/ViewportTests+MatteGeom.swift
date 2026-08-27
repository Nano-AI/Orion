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

    /// The engine's matte allocation, transcribed: `kMaxMatteEdge` on the long
    /// side, the short side by **floor** division
    /// (`DevelopPipeline::buildMaskNodes`). The transcription is itself pinned
    /// end to end by `repro/subject-selection.txt`, which drives the real
    /// render → `setMaskMatte` path; this keeps the pure half honest across a
    /// sweep no scenario could afford.
    private static func engineMatteAllocation(frameW: Int, frameH: Int)
        -> (width: Int, height: Int) {
        let edge = 1024
        return frameH > frameW
            ? (max(1, edge * frameW / frameH), edge)
            : (edge, max(1, edge * frameH / frameW))
    }

    static func testMatteAnalysisSize() {
        // ⚠ The case that shipped broken. 7968×5320 is what LibRaw decodes a
        // 42 MP a7R III to; the short edge lands on 683.67, which the old
        // `.rounded()` took to 684 while the engine allocated 683 — so
        // `setMaskMatte` refused every Subject, Person and Sky selection on a
        // full-frame body. The size must be the engine's own number, floored,
        // not a re-derivation that agrees on friendly aspects only.
        let cap = engineMatteAllocation(frameW: 7968, frameH: 5320)
        let broken = MatteGeometry.analysisSize(
            imageWidth: 7968, imageHeight: 5320,
            maxMatteWidth: cap.width, maxMatteHeight: cap.height, turns: 0)
        report(broken.width == 1024 && broken.height == 683,
               "the 42 MP frame that always failed now fits the allocation",
               "\(broken.width)x\(broken.height)")

        // Sweep real and adversarial sensor shapes, both orientations and all
        // four turns: after `undoTurns` puts the raster back into frame
        // coordinates, it must fit the engine's allocation exactly — never one
        // pixel over (the bug), never needlessly under (a quality loss).
        let sensors = [(7968, 5320), (6024, 4024), (8256, 5504), (8192, 5464),
                       (9504, 6336), (6000, 4000), (5472, 3648), (8688, 5792),
                       (4000, 4000), (7728, 4344)]
        for (fw, fh) in sensors {
            for (w, h) in [(fw, fh), (fh, fw)] {
                let alloc = engineMatteAllocation(frameW: w, frameH: h)
                for turns in 0...3 {
                    let odd = turns % 2 != 0
                    let s = MatteGeometry.analysisSize(
                        imageWidth: odd ? h : w, imageHeight: odd ? w : h,
                        maxMatteWidth: alloc.width, maxMatteHeight: alloc.height,
                        turns: turns)
                    let framed = (width: odd ? s.height : s.width,
                                  height: odd ? s.width : s.height)
                    report(framed == alloc,
                           "\(w)x\(h) turned \(turns) fills the allocation exactly",
                           "\(framed.width)x\(framed.height) vs \(alloc.width)x\(alloc.height)")
                }
            }
        }

        // ⚠ Never upscales. Handing a model more pixels than the photograph has
        // is inventing detail for it to segment.
        let small = MatteGeometry.analysisSize(
            imageWidth: 640, imageHeight: 480,
            maxMatteWidth: 1024, maxMatteHeight: 768, turns: 0)
        report(small.width == 640 && small.height == 480,
               "a frame under the allocation is passed at its own size",
               "\(small.width)x\(small.height)")
    }

    // MARK: Presets
}
