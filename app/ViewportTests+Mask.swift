// Mask overlay geometry: outlines, handles, dragging and brush spacing.
//
// Split out of ViewportTests.swift 2026-07-31.

import CoreGraphics
import Foundation

extension ViewportTests {

    /// The one that matters: the outline is drawn where the falloff actually
    /// is.
    ///
    /// `maskAlpha` is a transcription of `mask_gradient.slang`, so this asks
    /// the shader's own question rather than "did we draw an ellipse" — every
    /// point of the boundary curve must be exactly the end of the ramp, and
    /// every point of the inner curve exactly the start of it. A test that only
    /// checked the outline was closed and centerd would pass on a plain screen
    /// circle, which is the wrong curve on every frame that is not square.
    static func testMaskOutlineLandsOnTheFalloff() {
        for roundness in [2.0, 4.0, 8.0] as [CGFloat] {
            for feather in [0.2, 0.5, 0.8] as [CGFloat] {
                for angle in [0.0, 0.4, 1.1, -0.9] as [CGFloat] {
                    var m = CanvasLayout.MaskPlacement()
                    m.kind = 2
                    m.center = CGPoint(x: 0.42, y: 0.55)
                    m.radius = CGSize(width: 0.3, height: 0.18)
                    m.angle = angle
                    m.feather = feather
                    m.roundness = roundness

                    let tag = "r\(roundness) f\(feather) a\(angle)"

                    var worstEdge: CGFloat = 0
                    var worstInner: CGFloat = 0
                    for p in CanvasLayout.maskOutline(m, at: 1, samples: 64) {
                        worstEdge = max(worstEdge, abs(CanvasLayout.maskAlpha(p, m) - 0))
                    }
                    for p in CanvasLayout.maskOutline(m, at: 1 - feather, samples: 64) {
                        worstInner = max(worstInner, abs(CanvasLayout.maskAlpha(p, m) - 1))
                    }
                    near(worstEdge, 0, 1e-5, "outline is the end of the ramp (\(tag))")
                    near(worstInner, 0, 1e-5, "inner outline starts the ramp (\(tag))")

                    // And the boundary really is a boundary: a hair inside is
                    // covered, a hair outside is not.
                    let e = CanvasLayout.maskOutline(m, at: 0.98, samples: 16)
                    let o = CanvasLayout.maskOutline(m, at: 1.02, samples: 16)
                    report(e.allSatisfy { CanvasLayout.maskAlpha($0, m) > 0 },
                           "inside the outline is covered (\(tag))")
                    report(o.allSatisfy { CanvasLayout.maskAlpha($0, m) <= 1e-9 },
                           "outside the outline is not (\(tag))")
                }
            }
        }

        // A hard-edged mask has nothing to draw between the two curves, and the
        // overlay must not imply a feather it does not have.
        var hard = CanvasLayout.MaskPlacement()
        hard.kind = 2
        hard.feather = 0
        let outer = CanvasLayout.maskOutline(hard, at: 1, samples: 32)
        let inner = CanvasLayout.maskOutline(hard, at: 1, samples: 32)
        var gap: CGFloat = 0
        for (a, b) in zip(outer, inner) { gap = max(gap, hypot(a.x - b.x, a.y - b.y)) }
        near(gap, 0, 1e-9, "at feather 0 the two curves coincide")
    }

    /// A linear gradient's three lines are lines of constant coverage.
    ///
    /// Perpendicularity is measured in normalized coordinates, not on screen —
    /// so the check is that every point along the drawn line has the *same*
    /// ramp parameter, which is the property the shader defines and the one a
    /// screen-space right angle would break on any non-square frame.
    static func testMaskIsoLinesAreIsoAlpha() {
        for angle in [0.0, 0.3, 1.2, -0.7, 2.9] as [CGFloat] {
            for length in [0.2, 0.6, 1.4] as [CGFloat] {
                var m = CanvasLayout.MaskPlacement()
                m.kind = 1
                m.center = CGPoint(x: 0.45, y: 0.52)
                m.angle = angle
                m.length = length

                for (t, want) in [(0.0, 0.0), (0.5, 0.5), (1.0, 1.0)] as [(CGFloat, CGFloat)] {
                    let (a, b) = CanvasLayout.maskIsoLine(m, at: t)
                    let tag = "t\(t) a\(angle) l\(length)"

                    // Both ends, and points along the way.
                    for s in [0.0, 0.25, 0.5, 0.75, 1.0] as [CGFloat] {
                        let q = CGPoint(x: a.x + (b.x - a.x) * s,
                                        y: a.y + (b.y - a.y) * s)
                        near(CanvasLayout.maskLinearT(q, m), want, 1e-6,
                             "iso line holds its parameter (\(tag), s\(s))")
                    }
                }

                // The zero and full lines pass through the two endpoints the
                // engine hands the shader.
                let (z0, z1) = CanvasLayout.maskIsoLine(m, at: 0)
                let d = hypot(z1.x - z0.x, z1.y - z0.y)
                report(d > 1, "the iso line is long enough to cross the frame")
                near(CanvasLayout.maskLinearT(m.zeroEnd, m), 0, 1e-6,
                     "zero endpoint is t = 0 (a\(angle))")
                near(CanvasLayout.maskLinearT(m.fullEnd, m), 1, 1e-6,
                     "full endpoint is t = 1 (a\(angle))")
            }
        }
    }

    /// Drag an endpoint and it lands under the cursor — exactly, not nearly.
    ///
    /// This is the property that says the angle is being taken in the space the
    /// handle is drawn from. Take `atan2` on screen instead and the handle
    /// redraws off the cursor by an amount that grows with how far the frame is
    /// from square, which reads as the control "slipping".
    static func testMaskEndpointLandsUnderTheCursor() {
        let view = CGSize(width: 1200, height: 800)
        let viewAspect = view.width / view.height

        for image in [landscape, portrait, 1.0] as [CGFloat] {
            let v = Viewport()
            let map = CanvasLayout.pictureMap(
                quadScale: v.quadScale(imageAspect: image, viewAspect: viewAspect),
                visible: v.visibleFraction(imageAspect: image, viewAspect: viewAspect),
                center: v.center, in: view)

            var m = CanvasLayout.MaskPlacement()
            m.kind = 1
            m.center = CGPoint(x: 0.5, y: 0.5)

            let grab = CanvasLayout.maskHandlePoint(.fullEnd, m, map)
            for target in [CGPoint(x: 700, y: 430), CGPoint(x: 540, y: 330),
                           CGPoint(x: 660, y: 500)] {
                for handle in [CanvasLayout.MaskHandle.fullEnd, .zeroEnd] {
                    let moved = CanvasLayout.maskDrag(handle, from: grab, to: target,
                                                      start: m, map)
                    let where_ = CanvasLayout.maskHandlePoint(handle, moved, map)
                    near(where_.x, target.x, 1e-6,
                         "\(handle) lands on the cursor x (\(image))")
                    near(where_.y, target.y, 1e-6,
                         "\(handle) lands on the cursor y (\(image))")

                    // The center does not move when an end is pulled.
                    near(moved.center.x, m.center.x, 1e-9, "endpoint drag keeps the center x")
                    near(moved.center.y, m.center.y, 1e-9, "endpoint drag keeps the center y")
                }
            }
        }
    }

    /// The rotate handle keeps a fixed screen distance and stays on the ray
    /// from the center through the cursor. It cannot sit *on* the cursor —
    /// it is a lollipop at a fixed stem length — so the property to check is
    /// the direction, and on screen, which is where the hand is.
    static func testMaskRotateStaysOnTheCursorRay() {
        let view = CGSize(width: 1200, height: 800)
        let viewAspect = view.width / view.height

        for image in [landscape, portrait] as [CGFloat] {
            let v = Viewport()
            let map = CanvasLayout.pictureMap(
                quadScale: v.quadScale(imageAspect: image, viewAspect: viewAspect),
                visible: v.visibleFraction(imageAspect: image, viewAspect: viewAspect),
                center: v.center, in: view)

            var m = CanvasLayout.MaskPlacement()
            m.kind = 2
            m.center = CGPoint(x: 0.5, y: 0.5)
            let origin = map.point(m.center)
            let grab = CanvasLayout.maskHandlePoint(.rotate, m, map)

            for target in [CGPoint(x: 800, y: 300), CGPoint(x: 400, y: 620),
                           CGPoint(x: 620, y: 200), CGPoint(x: 300, y: 400)] {
                let moved = CanvasLayout.maskDrag(.rotate, from: grab, to: target,
                                                  start: m, map)
                let h = CanvasLayout.maskHandlePoint(.rotate, moved, map)

                let a = CGPoint(x: target.x - origin.x, y: target.y - origin.y)
                let b = CGPoint(x: h.x - origin.x, y: h.y - origin.y)
                let cross = a.x * b.y - a.y * b.x
                let dot = a.x * b.x + a.y * b.y
                let scale = max(hypot(a.x, a.y) * hypot(b.x, b.y), 1e-9)

                near(cross / scale, 0, 1e-6, "rotate handle is on the cursor ray (\(image))")
                report(dot > 0, "rotate handle is on the cursor's side (\(image))")

                // Fixed stem: the handle sits a constant distance beyond the
                // +X handle however the picture is shaped or turned.
                let edge = CanvasLayout.maskHandlePoint(.plusX, moved, map)
                near(hypot(h.x - edge.x, h.y - edge.y),
                     CanvasLayout.maskRotateStem, 1e-6,
                     "the stem keeps its length (\(image))")

                // Rotating changes nothing but the angle.
                near(moved.radius.width, m.radius.width, 1e-9, "rotate keeps radius x")
                near(moved.radius.height, m.radius.height, 1e-9, "rotate keeps radius y")
                near(moved.center.x, m.center.x, 1e-9, "rotate keeps the center")
            }
        }
    }

    /// Dragging the body moves the mask by the distance the hand moved — on
    /// screen, which is the only place the photographer can judge it.
    static func testMaskBodyDragMovesByTheDrag() {
        let view = CGSize(width: 1200, height: 800)
        let viewAspect = view.width / view.height

        for image in [landscape, portrait] as [CGFloat] {
            for zoom in [1.0, 3.0] as [CGFloat] {
                let v = Viewport()
                v.zoomBy(zoom, anchor: CGPoint(x: 0.5, y: 0.5),
                         visible: CGSize(width: 1, height: 1))
                let vis = v.visibleFraction(imageAspect: image, viewAspect: viewAspect)
                v.clamp(to: vis)
                let map = CanvasLayout.pictureMap(
                    quadScale: v.quadScale(imageAspect: image, viewAspect: viewAspect),
                    visible: vis, center: v.center, in: view)

                var m = CanvasLayout.MaskPlacement()
                m.kind = 2
                m.center = CGPoint(x: 0.5, y: 0.5)

                let grab = map.point(m.center)
                let delta = CGSize(width: 37, height: -21)
                let to = CGPoint(x: grab.x + delta.width, y: grab.y + delta.height)

                for handle in [CanvasLayout.MaskHandle.body, .center] {
                    let moved = CanvasLayout.maskDrag(handle, from: grab, to: to,
                                                      start: m, map)
                    let now = map.point(moved.center)
                    near(now.x - grab.x, delta.width, 1e-6,
                         "\(handle) drag moves by the drag x (\(image), \(zoom)x)")
                    near(now.y - grab.y, delta.height, 1e-6,
                         "\(handle) drag moves by the drag y (\(image), \(zoom)x)")

                    near(moved.angle, m.angle, 1e-9, "moving does not rotate")
                    near(moved.radius.width, m.radius.width, 1e-9, "moving does not resize")
                }
            }
        }
    }

    /// Pulling a side changes that side and nothing else.
    ///
    /// The temptation is to let an axis handle set the angle too, the way a
    /// linear endpoint does. It must not: a photographer nudging a mask wider
    /// would find it had quietly turned, and the angle would drift a little on
    /// every size adjustment with nothing on screen explaining it.
    static func testMaskAxisDragDoesNotRotate() {
        let view = CGSize(width: 1200, height: 800)
        let viewAspect = view.width / view.height
        let v = Viewport()
        let map = CanvasLayout.pictureMap(
            quadScale: v.quadScale(imageAspect: landscape, viewAspect: viewAspect),
            visible: v.visibleFraction(imageAspect: landscape, viewAspect: viewAspect),
            center: v.center, in: view)

        for angle in [0.0, 0.6, -1.3] as [CGFloat] {
            var m = CanvasLayout.MaskPlacement()
            m.kind = 2
            m.center = CGPoint(x: 0.5, y: 0.5)
            m.angle = angle

            for handle in [CanvasLayout.MaskHandle.plusX, .minusX, .plusY, .minusY] {
                let grab = CanvasLayout.maskHandlePoint(handle, m, map)
                // Deliberately off the axis, which is what a real hand does.
                let to = CGPoint(x: grab.x + 44, y: grab.y + 61)
                let moved = CanvasLayout.maskDrag(handle, from: grab, to: to,
                                                  start: m, map)

                near(moved.angle, angle, 1e-12, "\(handle) does not rotate (a\(angle))")
                near(moved.center.x, m.center.x, 1e-12, "\(handle) does not move x")
                near(moved.center.y, m.center.y, 1e-12, "\(handle) does not move y")

                let onX = (handle == .plusX || handle == .minusX)
                near(onX ? moved.radius.height : moved.radius.width,
                     onX ? m.radius.height : m.radius.width, 1e-12,
                     "\(handle) leaves the other axis alone")

                // Pulling outward from the edge grows the mask on that axis.
                let out = map.unit(to)
                let axis = onX ? m.axisX : m.axisY
                let reach = abs((out.x - m.center.x) * axis.width
                              + (out.y - m.center.y) * axis.height)
                near(onX ? moved.radius.width : moved.radius.height,
                     min(max(reach, 0.02), 1), 1e-9,
                     "\(handle) takes the drag's reach along its own axis")
            }
        }
    }

    /// A drag can never produce a mask the panel cannot show.
    ///
    /// The sliders and the canvas write the same variables. If a drag can leave
    /// a value outside a slider's range, the two disagree about the state and
    /// the next touch of that slider snaps the mask somewhere nobody put it.
    static func testMaskDragStaysInSliderRange() {
        let view = CGSize(width: 1200, height: 800)
        let viewAspect = view.width / view.height
        let v = Viewport()
        let map = CanvasLayout.pictureMap(
            quadScale: v.quadScale(imageAspect: landscape, viewAspect: viewAspect),
            visible: v.visibleFraction(imageAspect: landscape, viewAspect: viewAspect),
            center: v.center, in: view)

        // Far outside the view on every side, which is where a determined drag
        // ends up.
        let wild = [CGPoint(x: -4000, y: -3000), CGPoint(x: 9000, y: -2000),
                    CGPoint(x: 9000, y: 7000), CGPoint(x: -5000, y: 6000),
                    CGPoint(x: 600, y: 400)]

        for kind in [1, 2] {
            var m = CanvasLayout.MaskPlacement()
            m.kind = kind
            m.center = CGPoint(x: 0.5, y: 0.5)

            for handle in CanvasLayout.maskHandles(m) {
                let grab = CanvasLayout.maskHandlePoint(handle, m, map)
                for to in wild {
                    let r = CanvasLayout.maskDrag(handle, from: grab, to: to,
                                                  start: m, map)
                    let tag = "\(handle) kind \(kind)"
                    report(CanvasLayout.maskCenterRange.contains(r.center.x),
                           "center x stays in range (\(tag))", "\(r.center.x)")
                    report(CanvasLayout.maskCenterRange.contains(r.center.y),
                           "center y stays in range (\(tag))", "\(r.center.y)")
                    report(CanvasLayout.maskLengthRange.contains(r.length),
                           "length stays in range (\(tag))", "\(r.length)")
                    report(CanvasLayout.maskRadiusRange.contains(r.radius.width),
                           "radius x stays in range (\(tag))", "\(r.radius.width)")
                    report(CanvasLayout.maskRadiusRange.contains(r.radius.height),
                           "radius y stays in range (\(tag))", "\(r.radius.height)")
                    report(abs(r.angle) <= 3.15,
                           "angle stays in the slider's range (\(tag))", "\(r.angle)")
                }
            }
        }
    }

    /// A handle standing inside the mask still gets the press.
    ///
    /// The body is the biggest target on screen and every handle sits on it, so
    /// testing the body first makes all of them unreachable. Among handles the
    /// nearest wins rather than a fixed order, because on a small mask their
    /// boxes overlap and a fixed order strands one of them.
    static func testMaskHitPrefersHandlesOverBody() {
        let view = CGSize(width: 1200, height: 800)
        let viewAspect = view.width / view.height
        let v = Viewport()
        let map = CanvasLayout.pictureMap(
            quadScale: v.quadScale(imageAspect: landscape, viewAspect: viewAspect),
            visible: v.visibleFraction(imageAspect: landscape, viewAspect: viewAspect),
            center: v.center, in: view)

        for kind in [1, 2] {
            var m = CanvasLayout.MaskPlacement()
            m.kind = kind
            m.center = CGPoint(x: 0.5, y: 0.5)

            for handle in CanvasLayout.maskHandles(m) {
                let at = CanvasLayout.maskHandlePoint(handle, m, map)
                report(CanvasLayout.maskHit(at, m, map) == handle,
                       "pressing a handle grabs it (\(handle), kind \(kind))",
                       "got \(String(describing: CanvasLayout.maskHit(at, m, map)))")
            }

            // Inside the mask but clear of every handle: the body.
            let inside = kind == 2
                ? map.point(CGPoint(x: 0.5 + 0.12, y: 0.5 + 0.06))
                : map.point(CGPoint(x: 0.5, y: 0.5 + 0.12))
            if CanvasLayout.maskHandles(m).allSatisfy({
                hypot(CanvasLayout.maskHandlePoint($0, m, map).x - inside.x,
                      CanvasLayout.maskHandlePoint($0, m, map).y - inside.y)
                    > CanvasLayout.maskHandleBox / 2
            }) {
                report(CanvasLayout.maskHit(inside, m, map) == .body,
                       "pressing the mask body grabs the body (kind \(kind))")
            }

            // Well outside it: nothing, so the press falls through to panning.
            let outside = CGPoint(x: 40, y: 40)
            report(CanvasLayout.maskHit(outside, m, map) == nil,
                   "pressing away from the mask grabs nothing (kind \(kind))")
        }

        // With no mask there is nothing to grab anywhere.
        let none = CanvasLayout.MaskPlacement()
        report(CanvasLayout.maskHit(CGPoint(x: 600, y: 400), none, map) == nil,
               "no mask, no handles")
    }

    /// The stored angle is measured in normalized coordinates, and on a frame
    /// that is not square that is *not* the angle it subtends on screen.
    ///
    /// This is the check that catches someone deciding the map is "really" a
    /// rotation and dropping the aspect out of it — the same simplification
    /// `pipe/MaskGeometry.h` guards against on the straighten. It has to differ
    /// on 3:2 and it has to agree on a square, or the difference is a bug
    /// rather than the geometry.
    static func testMaskAnglesAreNormalizedNotScreen() {
        let view = CGSize(width: 1200, height: 800)
        let viewAspect = view.width / view.height

        func screenAngle(_ image: CGFloat, _ angle: CGFloat) -> CGFloat {
            let v = Viewport()
            let map = CanvasLayout.pictureMap(
                quadScale: v.quadScale(imageAspect: image, viewAspect: viewAspect),
                visible: v.visibleFraction(imageAspect: image, viewAspect: viewAspect),
                center: v.center, in: view)
            var m = CanvasLayout.MaskPlacement()
            m.kind = 1
            m.center = CGPoint(x: 0.5, y: 0.5)
            m.angle = angle
            let a = map.point(m.center)
            let b = map.point(m.fullEnd)
            return atan2(b.y - a.y, b.x - a.x)
        }

        // A square *picture*: normalized coordinates are a unit square, so the
        // map is a uniform scale only when the rectangle drawn is square too,
        // and the two angles then coincide.
        //
        // ⚠️ Not when the picture merely fills the view. The first version of
        // this check passed the view's own aspect here, which is a 3:2 picture
        // drawn edge to edge — a perfectly anisotropic map — and it read
        // 0.5117 rad against the 0.7 it asserted. The claim was "square" and
        // the measurement was "fills the view"; they are different questions.
        near(screenAngle(1.0, 0.7), 0.7, 1e-6,
             "a square picture makes the two angles agree")

        // 3:2 does not. The gap is the aspect, and it must be a real one — a
        // tolerance-sized difference would mean this test could not tell the
        // two implementations apart.
        let got = screenAngle(landscape, 0.7)
        report(abs(got - 0.7) > 0.05,
               "a 3:2 picture separates the stored angle from the screen angle",
               String(format: "screen %.4f rad vs stored 0.7000", got))

        // On axis, though, they must agree whatever the aspect — a scale along
        // the axes cannot turn a horizontal line.
        for image in [landscape, portrait, 1.0] as [CGFloat] {
            near(screenAngle(image, 0), 0, 1e-9, "0 is 0 at any aspect (\(image))")
        }
    }

    // MARK: Brush strokes

    /// Dabs land a fixed distance apart along the stroke.
    ///
    /// The spacing is what decides whether a stroke reads as a line or as a
    /// string of beads, so it is checked as an actual distance between
    /// consecutive dabs rather than as a count — a count is satisfied by dabs
    /// bunched at one end.
    /// The shipped spacing is under the bound the geometry gives it.
    ///
    /// ⚠ **This is the check that pins the constant; the GPU one pins the
    /// model.** `testBrushSpacingRipple` in `orion-tests` measures a real
    /// stroke's edge at 0.25, 0.5 and 1.0 radii and confirms the derivation —
    /// but it names those spacings itself, so raising `brushSpacing` here would
    /// leave it green. This side owns the number.
    ///
    /// The bound, from `research/brush-nib.md`: a stroke is a row of
    /// overlapping discs, and between two centres `k` radii apart the union
    /// dips inward by `1 - sqrt(1 - k²/4)` of the radius. The nib's falloff
    /// hides that dip while it stays inside the feather, and the feather at the
    /// hardest the nib is allowed to be is `1 - 0.98 = 0.02` of the radius.
    /// Equating them gives `k = 2·sqrt(1 - 0.98²) = 0.398`.
    ///
    /// ⚠ **0.02 is the shader's clamp, restated here**, and a change to
    /// `dabCoverage` will not redden this line. What catches that pairing is
    /// the GPU test, whose feather is computed from the same clamp and whose
    /// "inside the feather" check fails when the clamp moves. The two halves
    /// are written down because neither is the whole of it.
    static func testBrushSpacingIsUnderItsBound() {
        let hardestFeather = 1.0 - 0.98
        let bound = 2.0 * (1.0 - (1.0 - hardestFeather) * (1.0 - hardestFeather)).squareRoot()
        let k = Double(CanvasLayout.brushSpacing)
        let dip = 1.0 - (1.0 - k * k / 4.0).squareRoot()

        report(k < bound,
               "the dab spacing is under the bound its own falloff sets",
               String(format: "%.3f radii against a bound of %.3f", k, bound))
        report(dip < hardestFeather,
               "so the dip between two dabs stays inside the hardest feather",
               String(format: "%.5f of the radius against %.5f", dip, hardestFeather))

        // ⚠ The bound is only meaningful if it is one. Stated so a future edit
        // that quietly makes `bound` enormous — by mistyping the clamp, say —
        // cannot leave the two checks above passing on nothing.
        report(bound > 0.05 && bound < 1.0,
               "and the bound is a real constraint rather than a formality",
               String(format: "%.3f", bound))
    }

    static func testBrushDabsAreEvenlySpaced() {
        for radius in [0.02, 0.08, 0.3] as [CGFloat] {
            var carry: CGFloat = 0
            let a = CGPoint(x: 0.1, y: 0.2), b = CGPoint(x: 0.9, y: 0.75)
            let dabs = CanvasLayout.brushDabs(from: a, to: b, radius: radius,
                                              carry: &carry)
            let step = radius * CanvasLayout.brushSpacing
            report(!dabs.isEmpty, "a long drag lays dabs (radius \(radius))")

            // Consecutive gaps, including from the segment's start.
            var prev = a
            var worst: CGFloat = 0
            for (i, d) in dabs.enumerated() {
                let gap = hypot(d.x - prev.x, d.y - prev.y)
                // The first gap is a full step because carry started at zero.
                worst = max(worst, abs(gap - step))
                prev = d
                _ = i
            }
            near(worst, 0, 1e-9, "dabs are one step apart (radius \(radius))")

            // Every dab is on the segment, not near it.
            var offLine: CGFloat = 0
            let dx = b.x - a.x, dy = b.y - a.y
            let len = hypot(dx, dy)
            for d in dabs {
                let cross = abs((d.x - a.x) * dy - (d.y - a.y) * dx) / len
                offLine = max(offLine, cross)
            }
            near(offLine, 0, 1e-9, "dabs lie on the drag (radius \(radius))")

            // A tighter brush must lay more paint over the same distance.
            report(dabs.count == Int(len / step),
                   "the dab count is the distance over the step (radius \(radius))",
                   "\(dabs.count) vs \(Int(len / step))")
        }

        // A drag that does not move lays nothing, or resting the pointer would
        // pile dabs on one spot and burn a hole through the flow ramp.
        var carry: CGFloat = 0
        let still = CanvasLayout.brushDabs(from: CGPoint(x: 0.5, y: 0.5),
                                           to: CGPoint(x: 0.5, y: 0.5),
                                           radius: 0.1, carry: &carry)
        report(still.isEmpty, "a stationary pointer lays no paint")
    }

    /// The same stroke drawn with different event rates must lay the same dabs.
    ///
    /// This is the one that matters and the one a naive implementation fails: a
    /// pointer reports a handful of positions a second, so a fast hand jumps a
    /// long way between two events. Stamping once per event draws a dotted
    /// line, and restarting the spacing at every event clusters dabs wherever
    /// the hand slowed down — which is exactly at the corners of a gesture,
    /// where the extra paint is most visible.
    ///
    /// `carry` is what makes the two agree, so the test feeds one straight line
    /// through a coarse event stream and a fine one and demands the same dabs.
    static func testBrushSpacingSurvivesTheEventRate() {
        let a = CGPoint(x: 0.05, y: 0.5), b = CGPoint(x: 0.95, y: 0.5)
        let radius: CGFloat = 0.05

        func walk(steps: Int) -> [CGPoint] {
            var carry: CGFloat = 0
            var out: [CGPoint] = []
            var prev = a
            for i in 1...steps {
                let f = CGFloat(i) / CGFloat(steps)
                let p = CGPoint(x: a.x + (b.x - a.x) * f, y: a.y + (b.y - a.y) * f)
                out += CanvasLayout.brushDabs(from: prev, to: p, radius: radius,
                                              carry: &carry)
                prev = p
            }
            return out
        }

        let coarse = walk(steps: 3)     // a fast hand
        let fine   = walk(steps: 60)    // a slow one over the same path

        report(coarse.count == fine.count,
               "event rate does not change how much paint a stroke lays",
               "\(coarse.count) dabs vs \(fine.count)")

        if coarse.count == fine.count {
            var worst: CGFloat = 0
            for (p, q) in zip(coarse, fine) {
                worst = max(worst, hypot(p.x - q.x, p.y - q.y))
            }
            near(worst, 0, 1e-9, "and the dabs land in the same places")
        }

        // And the spacing holds across the event boundaries, which is the
        // property `carry` exists for. Measured over the coarse stream, where
        // the boundaries are furthest apart.
        var prev = a
        var worstGap: CGFloat = 0
        for d in coarse {
            worstGap = max(worstGap, abs(hypot(d.x - prev.x, d.y - prev.y)
                                         - radius * CanvasLayout.brushSpacing))
            prev = d
        }
        near(worstGap, 0, 1e-9, "spacing is continuous across events")
    }

    // MARK: Matte geometry — research/masking.md §5
}
