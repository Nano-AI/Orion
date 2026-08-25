import AppKit
import Foundation

/// Every state the interface can be photographed in.
///
/// Kept here rather than in a script so a scene is reviewable alongside the
/// code it exercises, and kept in one file so **adding a scene is one `case`
/// in `apply(scene:to:)`** — plus a line in `tab(for:)` when the scene is not
/// on the Light tab.
///
/// ⚠ Scene names are an interface. `repro/`, `planning/` and the briefs name
/// them; nothing here may be renamed without renaming those.
extension Screenshot {

    static func tab(for scene: String) -> ToolTab {
        switch scene {
        case "color":
            return .color
        case "detail", "detail-tail", "noisy", "denoise-off", "denoise-luma",
             "denoise-both", "spots":
            return .detail
        case "optics":
            return .optics
        case "crop", "crop-angle":
            return .crop
        case "presets":
            return .presets
        // Versions are a section of the Presets tab — see the note on `ToolTab`.
        case "versions":
            return .presets
        case "mask", "local", "mask-linear", "mask-linear-feathered",
             "mask-radial", "mask-off", "brush", "range", "color", "layers", "sky":
            return .mask
        default:
            return .light
        }
    }

    /// The version list a scene shows.
    ///
    /// ⚠ Handed in rather than read off a disk, and it writes nothing. A
    /// harness that saved real versions would leave `.orion-snapshots.json`
    /// beside whichever photograph was captured, and a capture that edits the
    /// developer's own folder is a capture nobody runs twice.
    ///
    /// ⚠ **One row of each kind**, because those are the three the panel draws
    /// differently: an ordinary version, the automatic one Orion keeps on the
    /// way into a restore, and one whose raster mask is no longer on disk. The
    /// third is the row that exists to stop a mask silently coming back empty,
    /// and it is the one most likely to be reviewed by reading it rather than
    /// by looking at it. Its matte id is deliberately a name no file has.
    /// ⚠ **A fixed instant, and it is what makes this scene comparable at all.**
    /// These rows were built from `Date()`, and the panel prints an absolute
    /// clock time in `.short` style — so **two runs of the same binary produced
    /// different frames**, differing by 2,380 bytes purely in the timestamp
    /// glyphs. `versions.png` was the one scene of 38 that could never be
    /// compared against anything, including itself, which took it out of the
    /// only oracle the ~35 posing scenes could ever have.
    ///
    /// The fix belongs here and not in `DevelopPanels+Presets`: the product is
    /// right to print the time a version was taken, and a formatter bent to suit
    /// a test is a test changing the thing it measures.
    ///
    /// ⚠ **This buys stability across runs, not across machines.** The string
    /// still goes through a `DateFormatter` in the machine's own locale and time
    /// zone, so two computers render this row differently. That is fine for what
    /// it is for — one binary compared against itself — and it is the reason a
    /// checked-in reference PNG would still be wrong.
    static let epoch = Date(timeIntervalSince1970: 1_700_000_000)

    static func snapshots(for scene: String, photo: String?) -> SnapshotStore? {
        guard scene == "versions", let photo else { return nil }

        var lost = DevelopState()
        var c = MaskComponentState()
        c.kind = 4
        c.matteId = "no-such-matte"
        c.matteSource = "Subject"
        lost.maskComponents = [c]

        var warm = DevelopState()
        warm.temperatureK = 7200
        warm.exposureEv = 0.6

        return SnapshotStore(photo: URL(fileURLWithPath: photo), showing: [
            Snapshot(name: "Before restoring warmer", created: epoch,
                     state: DevelopState(), automatic: true),
            Snapshot(name: "warmer", created: epoch.addingTimeInterval(-3600),
                     state: warm),
            Snapshot(name: "with the sky darkened",
                     created: epoch.addingTimeInterval(-86_400), state: lost),
        ])
    }

    /// Each scene is a state the interface can actually be in. Kept here rather
    /// than in a script so a scene is reviewable alongside the code it exercises.
    static func apply(scene: String, to engine: Engine) {
        switch scene {
        case "light":
            engine.exposureEv = 2.6
            engine.highlights = -0.4
            engine.shadows = 0.45
        case "tone":
            engine.exposureEv = 4.2
            engine.highlights = -1.0
            engine.shadows = 0.8
            engine.blacks = -0.3
        case "curve":
            engine.exposureEv = 2.6
            engine.curve.master = [CurvePoint(x: 0, y: 0.04), CurvePoint(x: 0.28, y: 0.19),
                                   CurvePoint(x: 0.68, y: 0.79), CurvePoint(x: 1, y: 1)]
        case "color":
            engine.exposureEv = 2.6
            engine.vibrance = 0.3
            engine.satShift[HueBand.blue.rawValue] = 0.4
        case "detail":
            engine.exposureEv = 2.6
            engine.denoiseLuma = 1.6
            engine.denoiseColor = 2.4
            engine.sharpenAmount = 0.8
            engine.sharpenMasking = 0.4
        case "detail-tail":
            // Every control the Detail panel keeps below the fold, each at a
            // value it does not hold by default, so the section is legible in
            // the frame by its readouts as well as by its title. Captured
            // scrolled — see `scrolls(_:)`.
            engine.exposureEv = 2.6
            engine.grainAmount = 0.028
            engine.grainSize = 3.4
            engine.vignetteAmount = -1.25
            engine.vignetteFieldAngle = 34
            engine.dehaze = 0.45
            engine.clarity = 0.55
            engine.sharpenAmount = 1.2
            engine.sharpenRadius = 1.4
            engine.sharpenMasking = 0.35
        case "render-failed":
            // The status line's failure branch. The value itself is planted
            // after the last render, in `run` — a scene applied here is applied
            // before one, and a successful render clears it.
            engine.exposureEv = 2.6
        case "optics":
            // The tab that exists because the lens corrections could not be
            // found inside Detail. Screenshotted so the claim that they are now
            // in front of the photographer can be looked at rather than assumed
            // — the last panel section inserted without looking was silently
            // not in the interface at all.
            engine.exposureEv = 2.6
        case "spots":
            // Two spots with their sources dragged somewhere deliberate, so the
            // still shows the link lines rather than the automatic offset —
            // which is the whole point of the tool being draggable.
            engine.exposureEv = 2.6
            engine.spotRadius = 0.045
            _ = engine.addSpot(atFrame: CGPoint(x: 0.36, y: 0.55))
            engine.moveSpot(0, destination: nil, source: CGPoint(x: 0.22, y: 0.74))
            engine.spotHeal = false
            _ = engine.addSpot(atFrame: CGPoint(x: 0.62, y: 0.42))
            engine.moveSpot(1, destination: nil, source: CGPoint(x: 0.78, y: 0.60))
            engine.selectedSpot = 1
        case "layers":
            // Two layers over opposite sides, graded in opposite directions —
            // which one shared adjustment cannot do.
            engine.exposureEv = 2.6
            engine.maskKind = 2
            engine.maskCenterX = 0.22; engine.maskCenterY = 0.5
            engine.maskRadiusX = 0.15; engine.maskRadiusY = 0.15
            engine.localExposureEv = -2.5
            engine.localSaturation = -1.0
            // A new mask starts its own layer since #197, so no break to set.
            _ = engine.addMaskComponent(kind: 2)
            engine.maskCenterX = 0.78; engine.maskCenterY = 0.5
            engine.maskRadiusX = 0.15; engine.maskRadiusY = 0.15
            engine.localExposureEv = 1.5
            engine.localWarmth = 1.0
        case "sky":
            engine.exposureEv = 1.0
            if let m = MainActor.assumeIsolated({ try? SubjectMatte.generateBlocking(engine: engine, kind: .sky) }) {
                engine.maskKind = 4
                // ⚠ Not `_ =`. This harness renders the stills the landing page
                // publishes under the words "the interface as it runs today, not
                // a mockup" — so a sky mask that silently failed to upload would
                // put a picture of a feature not working underneath a claim that
                // it does. A refused matte fails the run rather than shipping a
                // quietly wrong photograph of the product.
                guard engine.setMaskMatte(m.alpha, width: m.width, height: m.height) else {
                    FileHandle.standardError.write(Data(
                        "orion: the sky matte was refused — refusing to render a still without it\n".utf8))
                    exit(1)
                }
                // The label, and deliberately no file id: this harness renders a
                // picture, it does not write mattes beside somebody's raws. The
                // caption reads the two separately and says so.
                engine.setMatteReference(id: nil, source: "Sky")
                engine.maskOverlay = true
            }
        case "mask-off":
            // The control for `--measure`: the same frame and the same global
            // exposure with no mask, so the difference measured against
            // `mask-linear` is the mask's doing and not the scene's.
            engine.exposureEv = 2.6
        case "brush":
            // A stroke laid down the same way the canvas lays one: dabs at a
            // fixed spacing along a path, through CanvasLayout, so what the
            // screenshot shows is the real gesture rather than a hand-placed
            // list of points.
            engine.exposureEv = 2.6
            engine.maskKind = 3
            engine.localExposureEv = 2.2
            engine.brushRadius = 0.07
            engine.brushFlow = 0.55
            engine.brushHardness = 0.45
            var stroke: [CGPoint] = []
            var carry: CGFloat = 0
            let path = [CGPoint(x: 0.20, y: 0.66), CGPoint(x: 0.34, y: 0.60),
                        CGPoint(x: 0.48, y: 0.62), CGPoint(x: 0.62, y: 0.58),
                        CGPoint(x: 0.74, y: 0.55)]
            stroke.append(path[0])
            for i in 1..<path.count {
                stroke += CanvasLayout.brushDabs(from: path[i - 1], to: path[i],
                                                 radius: CGFloat(engine.brushRadius),
                                                 carry: &carry)
            }
            engine.setBrushStroke(stroke)
        case "mask-overlay":
            // The same linear mask as `mask-linear`, with the coverage painted
            // on. Measured against that scene, so the difference is the overlay
            // and nothing else.
            engine.exposureEv = 2.6
            engine.maskKind = 1
            engine.localExposureEv = -1.6
            engine.maskCenterX = 0.46
            engine.maskCenterY = 0.44
            engine.maskAngle = 1.05
            engine.maskLength = 0.55
            engine.maskOverlay = true
        case "mask-linear":
            // Placed and angled, because a gradient drawn square to the frame
            // proves nothing: the whole question is whether the three lines
            // stay perpendicular to the ramp in the space the shader measures,
            // and at zero degrees every wrong answer looks like the right one.
            engine.exposureEv = 2.6
            engine.maskKind = 1
            engine.localExposureEv = -1.6
            engine.maskCenterX = 0.46
            engine.maskCenterY = 0.44
            engine.maskAngle = 1.05
            engine.maskLength = 0.55
        case "mask-linear-feathered":
            // Identical to `mask-linear` but for the feather. The shader's
            // linear branch never reads that field, so the two must measure
            // the same — which is the evidence for hiding the control.
            engine.exposureEv = 2.6
            engine.maskKind = 1
            engine.localExposureEv = -1.6
            engine.maskCenterX = 0.46
            engine.maskCenterY = 0.44
            engine.maskAngle = 1.05
            engine.maskLength = 0.55
            engine.maskFeather = 0.02
        case "mask-radial":
            engine.exposureEv = 2.6
            engine.maskKind = 2
            engine.localExposureEv = 1.4
            engine.maskCenterX = 0.44
            engine.maskCenterY = 0.52
            engine.maskAngle = 0.6
            engine.maskRadiusX = 0.3
            engine.maskRadiusY = 0.17
            engine.maskFeather = 0.55
        case "mask-radial-overlay":
            // The hardest case for "does the drawn outline sit on the coverage":
            // a radial, turned, with unequal semi-axes, and the coverage painted
            // on underneath it. Reported as "the mask is not aligned with the
            // image at all" — the engine's placement measures correct through
            // the scenario runner at every rotation, so what was left to check
            // was the outline the photographer actually looks at.
            engine.exposureEv = 2.6
            engine.maskKind = 2
            engine.localExposureEv = 1.4
            engine.maskCenterX = 0.34
            engine.maskCenterY = 0.40
            engine.maskAngle = 0.6
            engine.maskRadiusX = 0.30
            engine.maskRadiusY = 0.14
            engine.maskFeather = 0.10
            engine.maskOverlay = true
        case "mask-range":
            engine.exposureEv = 2.6
            engine.maskKind = 5
            engine.maskRangeLo = 0.5
            engine.maskRangeHi = 99
            engine.maskRangeSoft = 1
            engine.localExposureEv = -1.5
            engine.maskOverlay = true
        case "mask-square":
            // A rounded-rectangle mask, which is the case that separates an
            // outline sampled from the shader's superellipse from one drawn as
            // an ellipse — at roundness 2 the two agree exactly.
            engine.exposureEv = 2.6
            engine.maskKind = 2
            engine.localExposureEv = 1.6
            engine.maskCenterX = 0.45
            engine.maskCenterY = 0.5
            engine.maskAngle = 0.35
            engine.maskRadiusX = 0.3
            engine.maskRadiusY = 0.22
            engine.maskRoundness = 6
            engine.maskFeather = 0.3
        case "crop":
            engine.exposureEv = 2.6
            engine.cropPreview = true
            engine.setCrop(x: 0.1, y: 0.08, w: 0.62, h: 0.7)
        case "crop-angle":
            engine.exposureEv = 2.6
            engine.cropPreview = true
            engine.straightenDeg = 12
            engine.setCrop(x: 0.1, y: 0.08, w: 0.62, h: 0.7)
        case "noisy":
            engine.exposureEv = 2.6
        case "asshot":
            break
        case "reset", "reset-hover":
            // A mix of touched and untouched controls, so the accent readout
            // can be read against the plain one in the same frame.
            engine.exposureEv = 2.6
            engine.highlights = -0.4
            engine.shadows = 0.45
            AdjustmentSlider.previewHover = (scene == "reset-hover")
        case "grade":
            engine.exposureEv = 2.6
            // Cool shadows, warm highlights — the split-tone every grading
            // panel gets used for first, so the screenshot shows the control
            // doing the thing it exists for.
            engine.gradeShadow = [-0.35, -0.55, -0.10]
            engine.gradeHighlight = [0.55, 0.35, 0.08]
        case "recover":
            engine.highlightRecovery = 1.0
        case "c110": engine.contrast = 1.10
        case "c120": engine.contrast = 1.20
        case "c130": engine.contrast = 1.30
        case "c140": engine.contrast = 1.40
        case "denoise-off":
            engine.exposureEv = 2.6
        case "denoise-luma":
            engine.exposureEv = 2.6
            engine.denoiseLuma = 2.0
        case "denoise-both":
            engine.exposureEv = 2.6
            engine.denoiseLuma = 2.0
            engine.denoiseColor = 3.0
        case "compare":
            engine.exposureEv = 2.6
            engine.setCompare(split: 0.5)
        // Distortion at each end of its travel. Negative k₁ is the case that
        // pushes the sample point past the frame edge, so it is the one that
        // says whether the picture still fills the frame.
        // Shadow wheel alone, hard over, so "does this color the whole
        // picture" can be answered with a number instead of an impression.
        case "grade-shadow-only":
            engine.gradeShadow = [-0.9, -0.9, 0.0]
        case "lens-barrel":
            engine.exposureEv = 2.6
            engine.lensDistortion = -1.0
        case "lens-pincushion":
            engine.exposureEv = 2.6
            engine.lensDistortion = 1.0
        default:
            break
        }
    }
}
