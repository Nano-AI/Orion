import AppKit
import SwiftUI

/// Everything that moves a control: the sliders, the grading wheels, their
/// drags, and the eyedropper that picks a band to move.
///
/// ⚠ **A new slider is one edit here** — a name in `apply(control:value:to:)`,
/// and a second in `controlValue` only if a scenario needs to read it back.
/// Both tables sit at the bottom of this file, beside the verbs that use them,
/// which is the whole point of the seam.
///
/// ⚠ Decision #89: both spellings of every aliased name are permanent.
/// `maskCenterX`/`maskCentreX`, `maskColorTol`/`maskColourTol`, `fusion`/`lift`
/// and the rest are **pairs on one `case`**, and merging a pair would break the
/// checked-in scenarios that use the other spelling.

extension Scenario {

    /// Answers true when this family took the verb.
    static func controlStep(_ verb: String, _ args: [String], engine: Engine,
                            targeted: TargetedAdjust) throws -> Bool {
        switch verb {
        case "set":
            guard args.count >= 2 else { throw Bad(what: "set needs a name and a value") }
            // Through `edit`, because that is what a slider does and it is what
            // records history. A bare assignment renders without recording, and
            // a scenario that used one would not be standing in for the
            // interface it is meant to be testing.
            let name = args[0], v = Float(try number(args, 1))
            var thrown: Error?
            engine.edit(name) {
                do { try apply(control: name, value: v, to: engine) }
                catch { thrown = error }
            }
            if let thrown { throw thrown }

        case "wheel":
            // A whole three-component grading wheel, in one edit.
            //
            // ⚠ The control table below it is scalar, and stays scalar: decision
            // #89 keeps every existing spelling forever, so `gradeShadowX` and
            // `gradeShadowY` are untouched and this is an addition beside them.
            guard args.count >= 3 else {
                throw Bad(what: "wheel needs a name, x and y")
            }
            let luma = args.count >= 4 ? Float(try number(args, 3)) : 0
            let placed = clampToDisc(Float(try number(args, 1)), Float(try number(args, 2)))
            var wheelThrew: Error?
            engine.edit(args[0]) {
                do { try setWheel(args[0], placed.0, placed.1, luma, engine) }
                catch { wheelThrew = error }
            }
            if let wheelThrew { throw wheelThrew }

        case "dragwheel":
            // A wheel drag, and what one tick of it costs.
            //
            // ⚠ **This is the verb `gesture-preview-agrees.txt` was missing.**
            // Five of the six gestures that arm the preview graph could be
            // driven from here and the wheel could not, because the wheels write
            // `[Float]` and `apply(control:value:)` takes one number — so the
            // one control whose arming was never measured was the one nothing
            // could move.
            //
            // It mirrors `ColorWheel`'s drag rather than approximating it: both
            // components inside a **single** `engine.edit`, and the puck clamped
            // to the disc instead of the square that bounds it.
            guard args.count >= 4, let ticks = Int(args[3]), ticks > 1 else {
                throw Bad(what: "dragwheel needs a name, a start x,y, an end x,y "
                              + "and a tick count")
            }
            let wheelName = args[0]
            let from = try point(args[1]), to = try point(args[2])
            quiet = true
            let wheelBegan = DispatchTime.now().uptimeNanoseconds
            for i in 0..<ticks {
                let t = Double(i) / Double(ticks - 1)
                let p = clampToDisc(Float(from.x + (to.x - from.x) * t),
                                    Float(from.y + (to.y - from.y) * t))
                var thrown: Error?
                engine.edit(wheelName) {
                    do { try setWheel(wheelName, p.0, p.1, nil, engine) }
                    catch { thrown = error }
                }
                if let thrown { quiet = false; throw thrown }
            }
            let wheelElapsed = DispatchTime.now().uptimeNanoseconds - wheelBegan
            quiet = false
            let wheelTick = Double(wheelElapsed) / 1_000_000.0 / Double(ticks)
            say(String(format: "  dragwheel %-8@ %.1f ms per tick  (%.0f fps, %d ticks)\n",
                       wheelName as NSString, wheelTick,
                       wheelTick > 0 ? 1000.0 / wheelTick : 0, ticks))

        case "control":
            // Asserts what a control *holds*, not what the picture looks like.
            //
            // ⚠ This exists because "the picture changed" is far too weak an
            // assertion about a button that writes five fields. Measured
            // 2026-07-31: `repro/undo-after-auto.txt` asserted only that Auto
            // moved the frame, and **four of the five assignments could be
            // deleted one at a time with the scenario still green** — the
            // remaining fields moved the picture enough to satisfy it. A button
            // that quietly stopped setting clarity, or whites, or the shadow
            // lift, looked exactly like a working one.
            guard args.count >= 3 else {
                throw Bad(what: "control needs a name, an operator and a value")
            }
            let ctlName = args[0], ctlOp = args[1]
            guard let want = Double(args[2]) else {
                throw Bad(what: "control needs a number, got \(args[2])")
            }
            guard let got = controlValue(ctlName, engine).map(Double.init) else {
                throw Bad(what: "no readable control named \(ctlName)")
            }
            checks += 1
            let ok: Bool
            switch ctlOp {
            case "==": ok = abs(got - want) <= 1e-3
            case "!=": ok = abs(got - want) > 1e-3
            case ">":  ok = got > want
            case "<":  ok = got < want
            default: throw Bad(what: "control takes ==, !=, > or <")
            }
            if ok {
                say(String(format: "  ok    %@ %@ %g  (holds %g)\n",
                           ctlName as NSString, ctlOp as NSString, want, got))
            } else {
                failures += 1
                say(String(format: "  FAIL  %@ %@ %g  (holds %g)\n",
                           ctlName as NSString, ctlOp as NSString, want, got))
            }

        case "pick":
            let p = try point(args.first ?? "")
            try eyedrop(engine: engine, targeted: targeted, at: p, drag: nil)

        case "targeted":
            let p = try point(args.first ?? "")
            try eyedrop(engine: engine, targeted: targeted, at: p,
                        drag: CGFloat(try number(args, 1)))

        case "interact":
            // Arms or disarms the preview path, as a slider's drag does.
            switch args.first {
            case "on":  engine.beginInteraction()
            case "off": engine.endInteraction()
            default: throw Bad(what: "interact takes on or off")
            }

        case "drag":
            // A slider drag, and what one tick of it costs.
            //
            // Distinct values on purpose. `time 40 set dehaze 0.6` measures
            // nothing at all: `apply` compares the adjustment block field by
            // field, so the second tick dirties no node and the loop times an
            // empty render. A drag is a *sweep*, and the sweep is what makes
            // every tick pay for the frame.
            guard args.count >= 4, let ticks = Int(args[3]), ticks > 1 else {
                throw Bad(what: "drag needs a control, a start, an end and a tick count")
            }
            let control = args[0]
            let from = try number(args, 1), to = try number(args, 2)
            quiet = true
            let dragBegan = DispatchTime.now().uptimeNanoseconds
            for i in 0..<ticks {
                let v = Float(from + (to - from) * Double(i) / Double(ticks - 1))
                var thrown: Error?
                engine.edit(control) {
                    do { try apply(control: control, value: v, to: engine) }
                    catch { thrown = error }
                }
                if let thrown { quiet = false; throw thrown }
            }
            let dragElapsed = DispatchTime.now().uptimeNanoseconds - dragBegan
            quiet = false
            let perTick = Double(dragElapsed) / 1_000_000.0 / Double(ticks)
            say(String(format: "  drag %-12@ %.1f ms per tick  (%.0f fps, %d ticks)\n",
                       control as NSString, perTick,
                       perTick > 0 ? 1000.0 / perTick : 0, ticks))

        default:
            return false
        }
        return true
    }

    /// The color-mixer eyedropper, through the path the canvas uses: sample the
    /// pixel, derive its hue, ask which band that is, and hand it to
    /// `TargetedAdjust`. A `drag` then moves that band, which is the half that
    /// actually changes the picture.
    private static func eyedrop(engine: Engine, targeted: TargetedAdjust,
                                at p: CGPoint, drag: CGFloat?) throws {
        guard let s = engine.sample(u: Float(p.x), v: Float(p.y)) else {
            throw Bad(what: "no sample at \(p.x),\(p.y) — is a photo open?")
        }
        // ⚠️ The **scene** color, because that is what `ImageCanvas.beginDrag`
        // reads. This took the display color, which is a different sample
        // through a different code path: the display value comes from the
        // output texture and is already cropped and turned, while the scene
        // value is looked up in the whole frame and has to be carried there.
        // So the runner exercised the one that could not go wrong and the
        // interface used the one that did — the same kind of fidelity gap the
        // `crop` verb had when it skipped `commitCropEdit`.
        guard let hue = TargetedAdjust.hue(r: s.scene.r, g: s.scene.g,
                                          b: s.scene.b) else {
            throw Bad(what: String(format:
                "the pixel at %.2f,%.2f is too near gray to have a hue "
                + "(scene r %.3f g %.3f b %.3f) — the tool refuses, by design",
                p.x, p.y, s.scene.r, s.scene.g, s.scene.b))
        }
        let band = TargetedAdjust.band(forHue: hue)
        targeted.begin(band: band, hue: hue)
        say(String(format: "  picked hue %.1f deg -> band %@\n",
                   hue, "\(band)" as NSString))

        if let drag {
            // What the drag does to the band it picked. Saturation is the tool's
            // default mode.
            let before = engine.satShift[band.rawValue]
            engine.satShift[band.rawValue] = min(1, max(-1, before + Float(drag)))
            targeted.end()
        }
    }

    /// Reads a control back off the engine, for `control`.
    ///
    /// ⚠ Deliberately a **short** list rather than a mirror of every setter.
    /// Every entry here is one a scenario has an actual reason to read — which
    /// today means the five fields the Auto button writes. A getter for a
    /// control nobody asserts is a second copy of the setter table waiting to
    /// disagree with it.
    private static func controlValue(_ name: String, _ e: Engine) -> Float? {
        switch name {
        case "exposure":       return e.exposureEv
        case "whites":         return e.whites
        case "blacks":         return e.blacks
        case "clarity":        return e.clarity
        case "fusion", "lift": return e.fusion
        case "contrast":       return e.contrast
        case "saturation":     return e.saturation
        case "dehaze":         return e.dehaze
        case "grainAmount":    return e.grainAmount
        case "grainSize":      return e.grainSize
        case "gradeBalance":   return e.gradeBalance
        // The shadow wheel's two puck coordinates. Balance is only observable
        // in a render when some wheel is off centre — it moves the zones, and a
        // zone with nothing in it looks like every other zone with nothing in
        // it — so a scenario that asserts Balance changes the picture needs to
        // be able to push a wheel first.
        case "gradeShadowX":   return e.gradeShadow[0]
        case "gradeShadowY":   return e.gradeShadow[1]
        // The other two wheels' pucks, so a scenario driving `wheel` or
        // `dragwheel` can assert *which* wheel it moved. Added when the
        // three-component verbs were (decision #110); the scalar setters above
        // are untouched.
        case "gradeMidtoneX":   return e.gradeMidtone[0]
        case "gradeMidtoneY":   return e.gradeMidtone[1]
        case "gradeHighlightX": return e.gradeHighlight[0]
        case "gradeHighlightY": return e.gradeHighlight[1]
        case "perspectiveVertical":   return e.perspectiveVertical
        case "perspectiveHorizontal": return e.perspectiveHorizontal
        case "perspectiveAspect":     return e.perspectiveAspect
        case "vignetteAmount":     return e.vignetteAmount
        case "vignetteFieldAngle": return e.vignetteFieldAngle
        default:               return nil
        }
    }

    /// The rim clamp `ColorWheel`'s drag applies, on the disc rather than on the
    /// square that bounds it.
    ///
    /// ⚠ Transcribed here rather than shared, for the reason `CanvasLayout`
    /// carries its own `maskAlpha`: a verb that called the view's code could not
    /// tell whether the view still did it. Past the rim the angle is meaningful
    /// and the radius is not, so the puck slides around the edge.
    private static func clampToDisc(_ x: Float, _ y: Float) -> (Float, Float) {
        let d = (x * x + y * y).squareRoot()
        return d > 1 ? (x / d, y / d) : (x, y)
    }

    /// A three-component control, written whole.
    ///
    /// ⚠ Whole-array assignment for the same reason the scalar
    /// `gradeShadowX` case gives: `gradeShadow` is `[Float]` with a `didSet`,
    /// and spelling the write out keeps the push explicit.
    ///
    /// `luma` of `nil` leaves the third component where it is, which is what a
    /// puck drag does — the wheel's drag gesture writes `[0]` and `[1]` and
    /// never touches `[2]`.
    private static func setWheel(_ name: String, _ x: Float, _ y: Float,
                                 _ luma: Float?, _ e: Engine) throws {
        switch name {
        case "gradeShadow":
            e.gradeShadow = [x, y, luma ?? e.gradeShadow[2]]
        case "gradeMidtone":
            e.gradeMidtone = [x, y, luma ?? e.gradeMidtone[2]]
        case "gradeHighlight":
            e.gradeHighlight = [x, y, luma ?? e.gradeHighlight[2]]
        default:
            throw Bad(what: "no three-component control named \(name) — "
                          + "gradeShadow, gradeMidtone or gradeHighlight")
        }
    }

    private static func apply(control: String, value: Float, to e: Engine) throws {
        switch control {
        case "exposure":    e.exposureEv = value
        case "contrast":    e.contrast = value
        case "highlights":  e.highlights = value
        case "shadows":     e.shadows = value
        case "whites":      e.whites = value
        case "blacks":      e.blacks = value
        case "saturation":  e.saturation = value
        case "vibrance":    e.vibrance = value
        case "temperature": e.temperatureK = value
        case "tint":        e.tint = value
        case "clarity":     e.clarity = value
        case "dehaze":      e.dehaze = value
        case "grainAmount": e.grainAmount = value
        case "grainSize":   e.grainSize = value
        case "gradeBalance": e.gradeBalance = value
        // ⚠ Whole-array assignment, because `gradeShadow` is `[Float]` with a
        // `didSet` — mutating one element in place would still fire it, but
        // spelling it out keeps the push explicit.
        case "gradeShadowX": e.gradeShadow = [value, e.gradeShadow[1], e.gradeShadow[2]]
        case "gradeShadowY": e.gradeShadow = [e.gradeShadow[0], value, e.gradeShadow[2]]
        case "perspectiveVertical":   e.perspectiveVertical = value
        case "perspectiveHorizontal": e.perspectiveHorizontal = value
        case "perspectiveAspect":     e.perspectiveAspect = value
        case "vignetteAmount":     e.vignetteAmount = value
        case "vignetteFieldAngle": e.vignetteFieldAngle = value
        case "fusion", "lift": e.fusion = value
        case "localExposure": e.localExposureEv = value
        case "localContrast": e.localContrast = value
        case "localSaturation": e.localSaturation = value
        case "localWarmth": e.localWarmth = value
        case "localTint": e.localTint = value
        case "localHighlights": e.localHighlights = value
        case "localShadows": e.localShadows = value
        case "localWhites": e.localWhites = value
        case "localBlacks": e.localBlacks = value
        case "maskRefine": e.maskRefine = value
        case "brushRadius": e.brushRadius = value
        case "brushFlow":   e.brushFlow = value
        case "maskCenterX", "maskCentreX": e.maskCenterX = value
        case "maskCenterY", "maskCentreY": e.maskCenterY = value
        case "maskAngle":   e.maskAngle = value
        case "maskLength":  e.maskLength = value
        case "maskRadiusX": e.maskRadiusX = value
        case "maskRadiusY": e.maskRadiusY = value
        case "maskFeather": e.maskFeather = value
        case "maskRoundness": e.maskRoundness = value
        case "maskRangeLo":   e.maskRangeLo = value
        case "maskRangeHi":   e.maskRangeHi = value
        case "maskRangeSoft": e.maskRangeSoft = value
        case "maskCompose":   e.maskCompose = Int32(value)
        case "maskColorTol", "maskColourTol":   e.maskColorTol = value
        case "maskColorSoft", "maskColourSoft": e.maskColorSoft = value
        case "maskInvert":  e.maskInvert = value != 0
        case "brushErase":  e.brushErasing = value != 0
        case "maskHidden":  e.maskHidden = value != 0
        case "brushHardness": e.brushHardness = value
        default: throw Bad(what: "no control named \(control)")
        }
    }
}
