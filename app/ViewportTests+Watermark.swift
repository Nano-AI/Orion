// The watermark's layout and mask, checked without a GPU.
//
// The blend itself is in orion-tests (`testExportWatermark`) and in
// repro/export-watermark.txt. This file is the part that lives in Swift: where
// the mark lands, and what the mask it produces holds. DECISIONS #284.

import CoreGraphics
import Foundation

extension ViewportTests {

    private static func mark(_ text: String, _ placement: Watermark.Placement,
                             opacity: Double = 0.35) -> Watermark {
        // No file: a test must never read or write the photographer's mark.
        let m = Watermark(url: nil)
        m.content = .text(text, fontFamily: "Helvetica Neue", bold: false)
        m.placement = placement
        m.opacity = opacity
        return m
    }

    /// Bottom-right sits inside the margin, flush against it, and a mark too
    /// long for the frame is shrunk rather than cut off.
    static func testWatermarkAnchorsInsideTheMargin() {
        let frame = CGSize(width: 6000, height: 4000)
        let p = WatermarkLayout.place(markAspect: 5, frame: frame,
                                      placement: .anchor(.bottomRight),
                                      size: 0.04, span: 0.6, margin: 0.03)
        let m: CGFloat = 0.03 * 4000
        near(p.bounds.maxX, frame.width - m, 0.001, "bottom-right ends at the right margin")
        near(p.bounds.maxY, frame.height - m, 0.001, "and at the bottom margin")
        near(p.size.height, 0.04 * 4000, 0.001, "its height is the size times the short edge")
        report(p.angle == 0, "an anchored mark is not rotated")

        let tl = WatermarkLayout.place(markAspect: 5, frame: frame, placement: .anchor(.topLeft),
                                       size: 0.04, span: 0.6, margin: 0.03)
        near(tl.bounds.minX, m, 0.001, "top-left starts at the left margin")
        near(tl.bounds.minY, m, 0.001, "and at the top margin, with y down")

        let c = WatermarkLayout.place(markAspect: 5, frame: frame, placement: .anchor(.center),
                                      size: 0.04, span: 0.6, margin: 0.03)
        near(c.center.x, 3000, 0.001, "center is centered across")
        near(c.center.y, 2000, 0.001, "and down")

        // A 40:1 name at 20% of the short edge would be 32000 px wide.
        let long = WatermarkLayout.place(markAspect: 40, frame: frame,
                                         placement: .anchor(.bottomLeft),
                                         size: 0.2, span: 0.6, margin: 0.03)
        report(long.bounds.minX >= m - 0.001 && long.bounds.maxX <= frame.width - m + 0.001,
               "a mark too long for the frame is shrunk to fit inside the margins",
               "\(long.bounds)")
        near(long.size.width / long.size.height, 40, 0.001, "and keeps its aspect")
    }

    /// The diagonal runs corner to corner at the frame's own angle and never
    /// leaves it, portrait or landscape.
    static func testWatermarkDiagonalFollowsTheFrame() {
        for frame in [CGSize(width: 6000, height: 4000), CGSize(width: 4000, height: 6000)] {
            let p = WatermarkLayout.place(markAspect: 3, frame: frame, placement: .diagonal,
                                          size: 0.04, span: 0.95, margin: 0.03)
            near(CGFloat(p.angle), CGFloat(atan2(frame.height, frame.width)), 1e-9,
                 "the diagonal's angle is atan2(h, w) at \(frame)")
            near(p.center.x, frame.width / 2, 0.001, "the diagonal is centered")
            let b = p.bounds
            report(b.minX >= -0.001 && b.minY >= -0.001
                   && b.maxX <= frame.width + 0.001 && b.maxY <= frame.height + 0.001,
                   "a long diagonal mark stays inside the frame", "\(b)")
        }
    }

    /// An empty name draws nothing, so the export takes no watermark pass at all.
    static func testWatermarkWithNothingToDrawIsNoMask() {
        report(WatermarkRaster.mask(for: mark("", .anchor(.bottomRight)),
                                    width: 400, height: 300) == nil,
               "an empty name is no mask")
        report(WatermarkRaster.mask(for: mark("   ", .diagonal), width: 400, height: 300) == nil,
               "nor is a name of spaces")
        let off = mark("Dhruv Arora", .anchor(.bottomRight))
        off.enabled = false
        report(WatermarkRaster.mask(for: off, settings: ExportSettings(),
                                    sourceWidth: 400, sourceHeight: 300) == nil,
               "a mark switched off is no mask")
    }

    /// Opacity is baked into the values, and the ink lands where the layout
    /// said - bottom-right, not flipped to the top.
    static func testWatermarkMaskCarriesOpacityAndPlace() {
        let w = 600, h = 400
        guard let half = WatermarkRaster.mask(for: mark("Dhruv", .anchor(.bottomRight),
                                                        opacity: 0.5), width: w, height: h),
              let full = WatermarkRaster.mask(for: mark("Dhruv", .anchor(.bottomRight),
                                                        opacity: 1), width: w, height: h)
        else { report(false, "a name draws a mask"); return }

        report(half.width == w && half.height == h && half.bytes.count == w * h,
               "the mask covers the whole frame")
        report(full.bytes.max() == 255, "full opacity reaches full coverage",
               "max \(full.bytes.max() ?? 0)")
        report(half.bytes.max() == 128, "half opacity halves the coverage",
               "max \(half.bytes.max() ?? 0)")

        var topHalf = 0, bottomRight = 0
        for y in 0..<h {
            for x in 0..<w where full.bytes[y * w + x] > 0 {
                if y < h / 2 { topHalf += 1 }
                if y >= h / 2 && x >= w / 2 { bottomRight += 1 }
            }
        }
        report(topHalf == 0 && bottomRight > 0,
               "a bottom-right mark is in the bottom-right quadrant of the mask",
               "top \(topHalf), bottom-right \(bottomRight)")
    }

    /// The saved mark survives a relaunch, SVG bytes and all.
    static func testWatermarkRoundTripsThroughItsFile() {
        let url = FileManager.default.temporaryDirectory
            .appendingPathComponent("orion-watermark-\(UUID().uuidString).json")
        defer { try? FileManager.default.removeItem(at: url) }

        let a = Watermark(url: url)
        a.enabled = true
        a.content = .svg(Data("<svg/>".utf8), name: "mark.svg")
        a.placement = .diagonal
        a.opacity = 0.2
        report(a.save(), "the watermark saves", a.lastFailure ?? "")

        let b = Watermark(url: url)
        report(b.enabled && b.placement == .diagonal && b.opacity == 0.2
               && b.content == .svg(Data("<svg/>".utf8), name: "mark.svg"),
               "and reads back as it was saved")

        let fresh = Watermark(url: url.appendingPathExtension("missing"))
        report(!fresh.enabled && fresh.placement == .anchor(.bottomRight),
               "no file is off, bottom-right")
    }
}
