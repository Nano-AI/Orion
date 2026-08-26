import CoreGraphics
import Foundation

/// The crop drag, `CanvasLayout.cropDrag`.
///
/// A `DragGesture` closure cannot be driven from any test (#110.3), so the
/// gesture's maths lives in `CanvasLayout` and these checks are the coverage
/// the overlay itself cannot have. The invariant that matters most is the last
/// one: a locked result must already satisfy `clampedCrop`, because the engine
/// applies that clamp per axis and would otherwise quietly break the ratio.
extension ViewportTests {

    private static let cropCorners: [CanvasLayout.CropHandle] =
        [.topLeft, .topRight, .bottomLeft, .bottomRight]

    /// The arithmetic that used to be inlined in `CropOverlay.drag(_:)`,
    /// kept here as the reference the extraction is checked against.
    private static func inlineDrag(_ h: CanvasLayout.CropHandle, _ start: CGRect,
                                   _ dx: CGFloat, _ dy: CGFloat) -> CGRect {
        var r = start
        switch h {
        case .move:
            r.origin.x += dx
            r.origin.y += dy
        case .topLeft:
            r.origin.x += dx; r.size.width -= dx
            r.origin.y += dy; r.size.height -= dy
        case .topRight:
            r.size.width += dx
            r.origin.y += dy; r.size.height -= dy
        case .bottomLeft:
            r.origin.x += dx; r.size.width -= dx
            r.size.height += dy
        case .bottomRight:
            r.size.width += dx
            r.size.height += dy
        }
        return CanvasLayout.clampedCrop(r)
    }

    private static func ratio(_ r: CGRect) -> CGFloat { r.width / r.height }

    /// The corner of `r` opposite to the handle - the one a locked drag must
    /// never move.
    private static func anchor(of h: CanvasLayout.CropHandle, _ r: CGRect) -> CGPoint {
        switch h {
        case .topLeft:     CGPoint(x: r.maxX, y: r.maxY)
        case .topRight:    CGPoint(x: r.minX, y: r.maxY)
        case .bottomLeft:  CGPoint(x: r.maxX, y: r.minY)
        case .bottomRight: CGPoint(x: r.minX, y: r.minY)
        case .move:        CGPoint(x: r.midX, y: r.midY)
        }
    }

    static func testCropDragFreeMatchesTheOldArithmetic() {
        let starts = [CGRect(x: 0.1, y: 0.2, width: 0.6, height: 0.5),
                      CGRect(x: 0.0, y: 0.0, width: 1.0, height: 1.0),
                      CGRect(x: 0.4, y: 0.1, width: 0.2, height: 0.8)]
        let drags: [(CGFloat, CGFloat)] = [(0.1, 0.05), (-0.2, 0.3), (0.7, -0.9), (0, 0.01)]
        for start in starts {
            for (dx, dy) in drags {
                for h in cropCorners + [.move] {
                    let got = CanvasLayout.cropDrag(h, start: start, dx: dx, dy: dy,
                                                    lockAspect: false)
                    let want = inlineDrag(h, start, dx, dy)
                    report(got == want, "free drag matches the old arithmetic",
                           "\(h) dx \(dx) dy \(dy): got \(got), want \(want)")
                }
            }
        }
    }

    static func testCropDragShiftKeepsTheStartRatio() {
        let start = CGRect(x: 0.15, y: 0.2, width: 0.5, height: 0.4)
        let drags: [(CGFloat, CGFloat)] = [(0.1, 0.02), (-0.08, 0.1), (0.05, -0.05),
                                           (-0.1, -0.12), (0.2, 0.2)]
        for h in cropCorners {
            for (dx, dy) in drags {
                let r = CanvasLayout.cropDrag(h, start: start, dx: dx, dy: dy,
                                              lockAspect: true)
                near(ratio(r), ratio(start), 1e-6, "locked drag keeps the start ratio (\(h))")
                let a = anchor(of: h, start), b = anchor(of: h, r)
                report(a == b, "locked drag never moves the opposite corner",
                       "\(h): anchor moved \(a) -> \(b)")
            }
        }
    }

    static func testCropDragShiftFollowsTheDominantAxis() {
        // A mostly-horizontal outward pull on the bottom-right corner: the
        // width grows by the drag, and the height must follow the ratio even
        // though the cursor barely moved vertically.
        let start = CGRect(x: 0.1, y: 0.1, width: 0.4, height: 0.4)
        let r = CanvasLayout.cropDrag(.bottomRight, start: start, dx: 0.3, dy: 0.01,
                                      lockAspect: true)
        near(r.width, 0.7, 1e-9, "dominant axis sets the size")
        near(r.height, 0.7, 1e-9, "the other axis follows the ratio")
        // The cursor asked for a 0.41-tall rectangle and got a 0.7-tall one:
        // the rectangle circumscribes the cursor rather than chasing it.
        report(r.height > 0.4 + 0.01, "the rectangle circumscribes the cursor")
    }

    static func testCropDragShiftClampsAgainstTheFrame() {
        // Anchor near the frame's corner, a huge outward drag: the limiting
        // axis binds exactly on its room and the ratio holds.
        let start = CGRect(x: 0.05, y: 0.1, width: 0.3, height: 0.2)
        let r = CanvasLayout.cropDrag(.bottomRight, start: start, dx: 5, dy: 5,
                                      lockAspect: true)
        report(r.minX >= 0 && r.minY >= 0 && r.maxX <= 1 + 1e-9 && r.maxY <= 1 + 1e-9,
               "locked drag stays inside the frame", "\(r)")
        near(ratio(r), ratio(start), 1e-6, "ratio survives the frame clamp")
        // rho = 1.5; room right of the anchor is 0.95, below it 0.9. The
        // vertical wall binds first: h = 0.9, w = 1.35 would not fit, so
        // w = min(0.95, 0.9 * 1.5) = 0.95 and the *horizontal* wall binds.
        near(r.width, 0.95, 1e-9, "the limiting axis equals its room")
    }

    static func testCropDragShiftRespectsTheMinimum() {
        // Collapse the rectangle through its anchor: the smaller normalized
        // axis lands exactly on the 0.05 floor and the ratio holds.
        let start = CGRect(x: 0.2, y: 0.2, width: 0.6, height: 0.3)
        let r = CanvasLayout.cropDrag(.bottomRight, start: start, dx: -5, dy: -5,
                                      lockAspect: true)
        near(min(r.width, r.height), 0.05, 1e-9, "collapse lands on the minimum")
        near(ratio(r), ratio(start), 1e-6, "ratio survives the minimum")
    }

    static func testCropDragShiftSurvivesTheEngineClamp() {
        // The one that matters: `setCrop` runs `clampedCrop` per axis, which
        // breaks any ratio it touches. So a locked result must already be a
        // fixed point of it, for every handle, ratio and drag.
        let starts = [CGRect(x: 0.1, y: 0.2, width: 0.6, height: 0.5),
                      CGRect(x: 0.0, y: 0.0, width: 1.0, height: 1.0),
                      CGRect(x: 0.7, y: 0.05, width: 0.25, height: 0.9),
                      CGRect(x: 0.02, y: 0.9, width: 0.9, height: 0.08)]
        let drags: [(CGFloat, CGFloat)] = [(0.3, 0.01), (-2, -2), (5, 5), (-0.4, 0.6),
                                           (0.001, -0.001), (0, 0)]
        for start in starts {
            for (dx, dy) in drags {
                for h in cropCorners {
                    let r = CanvasLayout.cropDrag(h, start: start, dx: dx, dy: dy,
                                                  lockAspect: true)
                    let clamped = CanvasLayout.clampedCrop(r)
                    let ok = abs(clamped.minX - r.minX) < 1e-9
                        && abs(clamped.minY - r.minY) < 1e-9
                        && abs(clamped.width - r.width) < 1e-9
                        && abs(clamped.height - r.height) < 1e-9
                    report(ok, "locked result is a fixed point of clampedCrop",
                           "\(h) start \(start) dx \(dx) dy \(dy): \(r) -> \(clamped)")
                }
            }
        }
    }

    static func testCropDragMoveIgnoresTheLock() {
        let start = CGRect(x: 0.3, y: 0.3, width: 0.4, height: 0.25)
        let drags: [(CGFloat, CGFloat)] = [(0.1, 0.2), (-5, 3), (0.05, -0.05)]
        for (dx, dy) in drags {
            let locked = CanvasLayout.cropDrag(.move, start: start, dx: dx, dy: dy,
                                               lockAspect: true)
            let free = CanvasLayout.cropDrag(.move, start: start, dx: dx, dy: dy,
                                             lockAspect: false)
            report(locked == free, "move ignores the lock",
                   "dx \(dx) dy \(dy): \(locked) vs \(free)")
        }
    }
}
