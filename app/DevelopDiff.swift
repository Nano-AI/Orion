import Foundation

/// `DevelopState` spelled as scenario lines.
///
/// Two states in, the lines that take the first to the second out - in the
/// grammar `Scenario` replays, so the result is not a description of an edit
/// but the edit itself. `InteractionLog` writes a session with it; the agent
/// surface answers `state` with it and reports what each verb changed with it.
/// One implementation, because a log that says `set exposure 1.2` and a reply
/// that says `exposure: 1.2` would be two spellings of one fact, and the two
/// would drift.
///
/// ⚠ **Honest about what it cannot replay.** A field with no verb yet is
/// emitted as a `#` comment carrying its value rather than dropped: the reader
/// still learns the value, and a replay still runs, because the runner ignores
/// comments. A silently dropped field is the failure `InteractionLog` was built
/// to avoid - a session log that diverges from the session it claims to be.
///
/// No SwiftUI and no Engine, so it compiles into `orion-viewport-tests`.
enum DevelopDiff {

    /// The lines that take `a` to `b`.
    static func lines(from a: DevelopState, to b: DevelopState) -> [String] {
        var out: [String] = []
        func f(_ name: String, _ x: Float, _ y: Float, _ places: Int = 3) {
            guard abs(x - y) > 1e-6 else { return }
            out.append("set \(name) \(String(format: "%.\(places)f", y))")
        }
        /// A value with no verb. Kept as a comment so nothing is lost.
        func unverbed(_ name: String, _ x: Float, _ y: Float, _ places: Int = 3) {
            guard abs(x - y) > 1e-6 else { return }
            out.append("# \(name) \(String(format: "%.\(places)f", y))  (no verb yet)")
        }

        // ── Masks first: the layers below select by row, and a replay from
        //    nothing has to have built the rows before it can select one.
        masks(from: a.maskComponents, to: b.maskComponents, into: &out)

        // ── Per layer, and the layer index is emitted with it - a bare
        //    `set localExposure` would replay onto whichever layer the runner
        //    happened to have selected, which is not the one that moved.
        for i in 0..<max(a.layers.count, b.layers.count) {
            let x = i < a.layers.count ? a.layers[i] : LocalAdjustState()
            let y = i < b.layers.count ? b.layers[i] : LocalAdjustState()
            guard x != y else { continue }
            out.append("masklayer \(i)")
            f("localExposure", x.exposureEv, y.exposureEv)
            f("localContrast", x.contrast, y.contrast)
            f("localSaturation", x.saturation, y.saturation)
            f("localWarmth", x.warmth, y.warmth)
            f("localTint", x.tint, y.tint)
            f("localHighlights", x.highlights, y.highlights)
            f("localShadows", x.shadows, y.shadows)
            f("localWhites", x.whites, y.whites)
            f("localBlacks", x.blacks, y.blacks)
        }

        // ── The globals, in the panel's order. `maskRefine` is one of them:
        //    it feathers the *folded* group, not a row, and printing it under
        //    the last layer's block read as if it belonged to that layer.
        // First among the globals, because it changes what the four tone
        // sliders below mean; a replay that set them first would render them
        // under the wrong bands until this line arrived.
        if a.process != b.process { out.append("set process \(b.process)") }
        f("temperature", a.temperatureK, b.temperatureK, 0)
        f("tint", a.tint, b.tint)
        f("exposure", a.exposureEv, b.exposureEv)
        f("contrast", a.contrast, b.contrast)
        f("highlights", a.highlights, b.highlights)
        f("shadows", a.shadows, b.shadows)
        f("whites", a.whites, b.whites)
        f("blacks", a.blacks, b.blacks)
        f("vibrance", a.vibrance, b.vibrance)
        f("saturation", a.saturation, b.saturation)
        f("clarity", a.clarity, b.clarity)
        f("dehaze", a.dehaze, b.dehaze)
        f("fusion", a.fusion, b.fusion)
        f("grainAmount", a.grainAmount, b.grainAmount, 4)
        f("grainSize", a.grainSize, b.grainSize)
        f("vignetteAmount", a.vignetteAmount, b.vignetteAmount)
        f("vignetteFieldAngle", a.vignetteFieldAngle, b.vignetteFieldAngle, 0)
        f("gradeBalance", a.gradeBalance, b.gradeBalance)
        f("maskRefine", a.maskRefine, b.maskRefine)
        for (name, x, y) in [("gradeShadow", a.gradeShadow, b.gradeShadow),
                             ("gradeMidtone", a.gradeMidtone, b.gradeMidtone),
                             ("gradeHighlight", a.gradeHighlight, b.gradeHighlight)]
        where x != y && y.count == 3 {
            out.append(String(format: "wheel %@ %.3f %.3f %.3f", name, y[0], y[1], y[2]))
        }

        // ── Fields the grammar cannot set yet. Values kept, replay unbroken.
        unverbed("sharpenAmount", a.sharpenAmount, b.sharpenAmount)
        unverbed("sharpenRadius", a.sharpenRadius, b.sharpenRadius)
        unverbed("sharpenMasking", a.sharpenMasking, b.sharpenMasking)
        unverbed("denoiseLuma", a.denoiseLuma, b.denoiseLuma)
        unverbed("denoiseColor", a.denoiseColor, b.denoiseColor)
        unverbed("lensDistortion", a.lensDistortion, b.lensDistortion)
        unverbed("lensVignette", a.lensVignette, b.lensVignette)
        unverbed("lensCaRed", a.lensCaRed, b.lensCaRed)
        unverbed("lensCaBlue", a.lensCaBlue, b.lensCaBlue)
        unverbed("highlightRecovery", a.highlightRecovery, b.highlightRecovery)
        unverbed("lutStrength", a.lutStrength, b.lutStrength)
        for (name, x, y) in [("hue", a.hueShift, b.hueShift), ("sat", a.satShift, b.satShift),
                             ("lum", a.lumShift, b.lumShift)] where x != y {
            out.append("# mixer \(name) " + y.map { String(format: "%.2f", $0) }
                .joined(separator: " ") + "  (no verb yet)")
        }
        if a.curve != b.curve { out.append("# tone curve changed  (no verb yet)") }
        if a.lensChoice != b.lensChoice {
            out.append("lens \(b.lensChoice)")
        }

        // ── Geometry has verbs of its own rather than `set`.
        if a.rotateQuarters != b.rotateQuarters {
            let turns = ((b.rotateQuarters - a.rotateQuarters) % 4 + 4) % 4
            out.append("rotate \(turns)")
        }
        if abs(a.straightenDeg - b.straightenDeg) > 1e-6 {
            out.append(String(format: "straighten %.2f", b.straightenDeg))
        }
        f("perspectiveVertical", a.perspectiveVertical, b.perspectiveVertical)
        f("perspectiveHorizontal", a.perspectiveHorizontal, b.perspectiveHorizontal)
        f("perspectiveAspect", a.perspectiveAspect, b.perspectiveAspect)
        if abs(a.cropX - b.cropX) > 1e-6 || abs(a.cropY - b.cropY) > 1e-6
            || abs(a.cropW - b.cropW) > 1e-6 || abs(a.cropH - b.cropH) > 1e-6 {
            out.append(String(format: "crop %.4f %.4f %.4f %.4f",
                              b.cropX, b.cropY, b.cropW, b.cropH))
        }

        // ── Spots. A placement is a verb; a move is `spotdrag`.
        if b.spots.count > a.spots.count {
            for s in b.spots[a.spots.count...] {
                out.append(String(format: "spot %.4f,%.4f %.4f %@",
                                  s.destX, s.destY, s.radius, s.heal ? "heal" : "clone"))
                out.append(String(format: "spotdrag %d source %.4f,%.4f",
                                  b.spots.count - 1, s.srcX, s.srcY))
            }
        } else if b.spots.count == a.spots.count {
            for (i, s) in b.spots.enumerated() where s != a.spots[i] {
                if s.destX != a.spots[i].destX || s.destY != a.spots[i].destY {
                    out.append(String(format: "spotdrag %d dest %.4f,%.4f", i, s.destX, s.destY))
                }
                if s.srcX != a.spots[i].srcX || s.srcY != a.spots[i].srcY {
                    out.append(String(format: "spotdrag %d source %.4f,%.4f", i, s.srcX, s.srcY))
                }
            }
        } else {
            out.append("# \(a.spots.count - b.spots.count) spot(s) removed  (no verb yet)")
        }
        return out
    }

    /// The mask stack, row by row.
    ///
    /// A row that did not exist is diffed against the defaults, which is what
    /// the engine creates. Every field line is preceded by `maskrow i`, so a
    /// replay addresses the row that moved and not whichever one was selected.
    private static func masks(from a: [MaskComponentState], to b: [MaskComponentState],
                              into out: inout [String]) {
        for (i, y) in b.enumerated() {
            let existed = i < a.count
            let x = existed ? a[i] : MaskComponentState()
            var rows: [String] = []
            func f(_ name: String, _ p: Float, _ q: Float, _ places: Int = 3) {
                guard abs(p - q) > 1e-6 else { return }
                rows.append("set \(name) \(String(format: "%.\(places)f", q))")
            }
            if !existed {
                // Row 0 comes from `mask`; every later row is added.
                out.append((i == 0 ? "mask " : "maskadd ") + kindName(y.kind))
            } else if x.kind != y.kind {
                out.append("maskkind \(i) \(kindName(y.kind))")
            }
            if i > 0 && x.startsLayer != y.startsLayer {
                out.append((y.startsLayer ? "masksplit " : "masklink ") + "\(i)")
            }
            if x.name != y.name, let name = y.name, i == 0 || y.startsLayer {
                out.append("maskname \(i) \(name)")
            }
            if x.compose != y.compose { rows.append("set maskCompose \(y.compose)") }
            if x.invert != y.invert { rows.append("set maskInvert \(y.invert ? 1 : 0)") }
            if x.hidden != y.hidden { rows.append("set maskHidden \(y.hidden ? 1 : 0)") }
            f("maskCentreX", x.centerX, y.centerX)
            f("maskCentreY", x.centerY, y.centerY)
            f("maskAngle", x.angle, y.angle)
            f("maskLength", x.length, y.length)
            f("maskRadiusX", x.radiusX, y.radiusX)
            f("maskRadiusY", x.radiusY, y.radiusY)
            f("maskFeather", x.feather, y.feather)
            f("maskRoundness", x.roundness, y.roundness)
            f("maskRangeLo", x.rangeLo, y.rangeLo)
            f("maskRangeHi", x.rangeHi, y.rangeHi)
            f("maskRangeSoft", x.rangeSoft, y.rangeSoft)
            f("maskColorTol", x.colorTol, y.colorTol)
            f("maskColorSoft", x.colorSoft, y.colorSoft)
            f("brushRadius", x.brushRadius, y.brushRadius)
            f("brushFlow", x.brushFlow, y.brushFlow)
            f("brushHardness", x.brushHardness, y.brushHardness)
            if x.colorR != y.colorR || x.colorG != y.colorG || x.colorB != y.colorB {
                rows.append(String(format: "# color picked rgb %.3f %.3f %.3f  (use maskcolor x,y)",
                                   y.colorR, y.colorG, y.colorB))
            }
            if x.brushStroke.count != y.brushStroke.count {
                rows.append("# brush stroke: \(y.brushStroke.count / 2) dabs  (use brush)")
            }
            if y.kind == 4 && x.matteId != y.matteId {
                rows.append("# matte \(y.matteSource ?? "raster") \(y.matteId ?? "none")  (use select)")
            }
            if !rows.isEmpty {
                out.append("maskrow \(i)")
                out += rows
            }
        }
        if b.count < a.count {
            out.append("# \(a.count - b.count) mask row(s) removed  (use maskkind i none)")
        }
    }

    /// The word `mask` and `maskadd` take.
    static func kindName(_ k: Int32) -> String {
        switch k {
        case 1: "linear"
        case 2: "radial"
        case 3: "brush"
        case 4: "matte"
        case 5: "range"
        case 6: "color"
        default: "none"
        }
    }

    /// One line per mask, for `state masks` - the outline an agent reads before
    /// deciding whether to pay for the detail.
    static func outline(_ s: DevelopState) -> [String] {
        let runs = MaskLayers.group(s.maskComponents)
        guard !runs.isEmpty else { return ["masks 0"] }
        var out = ["masks \(runs.count)  rows \(s.maskComponents.count)"]
        for (li, run) in runs.enumerated() {
            let name = MaskLayers.displayName(ofLayer: li, in: s.maskComponents)
            let shapes = run.map { i -> String in
                let c = s.maskComponents[i]
                let op = ["add", "subtract", "intersect"][Int(min(max(c.compose, 0), 2))]
                return "\(i):\(kindName(c.kind))" + (i == run[0] ? "" : "/\(op)")
                    + (c.invert ? "/inverted" : "") + (c.hidden ? "/hidden" : "")
            }.joined(separator: " ")
            let l = li < s.layers.count ? s.layers[li] : LocalAdjustState()
            var local: [String] = []
            for (n, v) in [("exposure", l.exposureEv), ("contrast", l.contrast),
                           ("saturation", l.saturation), ("warmth", l.warmth),
                           ("tint", l.tint), ("highlights", l.highlights),
                           ("shadows", l.shadows), ("whites", l.whites),
                           ("blacks", l.blacks)] where abs(v) > 1e-6 {
                local.append(String(format: "%@ %.2f", n, v))
            }
            out.append("mask \(li) \"\(name)\"  \(shapes)  "
                       + (local.isEmpty ? "(no local adjustment)" : local.joined(separator: ", ")))
        }
        return out
    }
}
