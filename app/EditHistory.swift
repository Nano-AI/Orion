import Foundation

/// Undo and redo for the develop settings.
///
/// Snapshots the whole adjustment set rather than recording deltas. That is the
/// right trade here: the settings are a few dozen floats, so a snapshot costs
/// nothing, and it means a new control cannot silently break undo by forgetting
/// to record itself.
///
/// Slider drags are coalesced. Without that, dragging exposure across the panel
/// would push a hundred entries and undo would crawl back one frame at a time
/// instead of returning you to where you started.
@Observable
final class EditHistory {

    struct Entry {
        let label: String
        let state: DevelopState
        let time: Date
    }

    private(set) var entries: [Entry] = []
    private(set) var position = -1

    /// Drags within this window collapse into the previous entry.
    private let coalesceWindow: TimeInterval = 0.6

    var canUndo: Bool { position > 0 }
    var canRedo: Bool { position >= 0 && position < entries.count - 1 }

    var undoLabel: String? { canUndo ? entries[position].label : nil }
    var redoLabel: String? { canRedo ? entries[position + 1].label : nil }

    /// Most recent first, for a history panel.
    var recent: [Entry] { entries.reversed() }

    func reset(to state: DevelopState) {
        entries = [Entry(label: "Opened", state: state, time: Date())]
        position = 0
    }

    /// Records a change. `label` names the control, and is what coalescing
    /// keys on — consecutive edits to the same control merge.
    func record(_ state: DevelopState, label: String) {
        guard position >= 0 else {
            reset(to: state)
            return
        }

        // A new branch discards anything that was redoable.
        if position < entries.count - 1 {
            entries.removeSubrange((position + 1)...)
        }

        let now = Date()
        if let last = entries.last,
           last.label == label,
           now.timeIntervalSince(last.time) < coalesceWindow {
            entries[entries.count - 1] = Entry(label: label, state: state, time: now)
            return
        }

        entries.append(Entry(label: label, state: state, time: now))
        position = entries.count - 1

        // Bound the depth. Fifty steps is well past what anyone walks back
        // through, and it keeps a long session from growing without limit.
        if entries.count > 50 {
            entries.removeFirst(entries.count - 50)
            position = entries.count - 1
        }
    }

    func undo() -> DevelopState? {
        guard canUndo else { return nil }
        position -= 1
        return entries[position].state
    }

    func redo() -> DevelopState? {
        guard canRedo else { return nil }
        position += 1
        return entries[position].state
    }

    /// Jumps to an absolute point, for clicking an entry in a history panel.
    func jump(to index: Int) -> DevelopState? {
        guard entries.indices.contains(index) else { return nil }
        position = index
        return entries[index].state
    }
}

/// Every develop setting, as a value. Snapshotting this is what makes undo work.
/// One component of the mask group. research/masking.md §6.
///
/// The stroke lives *inside* the component rather than beside it, because a
/// group can hold several brushes and a stroke belongs to the one it was painted
/// into — keeping them in parallel arrays is how a reorder puts someone's paint
/// on the wrong component.
struct MaskComponentState: Equatable, Codable {
    var kind: Int32 = 0            // 1 linear, 2 radial, 3 brush
    /// 0 add, 1 subtract, 2 intersect. The group folds from zero, so the first
    /// component's op has no effect when it is add and zeroes the group when it
    /// is not — the panel does not offer one on the first row.
    var compose: Int32 = 0
    var invert = false
    var centreX: Float = 0.5
    var centreY: Float = 0.5
    var angle: Float = 0
    var length: Float = 0.5
    var radiusX: Float = 0.3
    var radiusY: Float = 0.3
    var feather: Float = 0.5
    var roundness: Float = 2

    var brushRadius: Float = 0.08
    var brushFlow: Float = 0.5
    var brushHardness: Float = 0.5
    /// Interleaved x, y so the sidecar stays a plain JSON array of numbers
    /// rather than a list of objects — a stroke is thousands of points and the
    /// object form doubles the file for nothing.
    var brushStroke: [Float] = []

    /// Decoding takes what the sidecar has and leaves the rest at its default,
    /// for the same reason `DevelopState` does: the synthesized decoder throws
    /// on a missing key, so adding one field would discard every component in
    /// every sidecar written before it.
    private enum Key: String, CodingKey {
        case kind, compose, invert, centreX, centreY, angle, length
        case radiusX, radiusY, feather, roundness
        case brushRadius, brushFlow, brushHardness, brushStroke
    }

    init() {}

    init(from decoder: Decoder) throws {
        self.init()
        guard let c = try? decoder.container(keyedBy: Key.self) else { return }
        func float(_ key: Key) -> Float? {
            (try? c.decodeIfPresent(Float.self, forKey: key)).flatMap { $0 }
        }
        kind = (try? c.decodeIfPresent(Int32.self, forKey: .kind)).flatMap { $0 } ?? kind
        compose = (try? c.decodeIfPresent(Int32.self, forKey: .compose)).flatMap { $0 } ?? compose
        invert = (try? c.decodeIfPresent(Bool.self, forKey: .invert)).flatMap { $0 } ?? invert
        centreX = float(.centreX) ?? centreX
        centreY = float(.centreY) ?? centreY
        angle = float(.angle) ?? angle
        length = float(.length) ?? length
        radiusX = float(.radiusX) ?? radiusX
        radiusY = float(.radiusY) ?? radiusY
        feather = float(.feather) ?? feather
        roundness = float(.roundness) ?? roundness
        brushRadius = float(.brushRadius) ?? brushRadius
        brushFlow = float(.brushFlow) ?? brushFlow
        brushHardness = float(.brushHardness) ?? brushHardness
        brushStroke = (try? c.decode([Float].self, forKey: .brushStroke)) ?? brushStroke
    }
}

struct DevelopState: Equatable, Codable {
    var temperatureK: Float = 5500
    var tint: Float = 0
    var exposureEv: Float = 0
    var highlights: Float = 0
    var shadows: Float = 0
    var whites: Float = 0
    var blacks: Float = 0
    var vibrance: Float = 0
    var saturation: Float = 0
    /// Orion's base rendering, not neutral. See Engine.contrast.
    var contrast: Float = 1.45
    var rotateQuarters: Int32 = 0
    var straightenDeg: Float = 0
    var cropX: Float = 0
    var cropY: Float = 0
    var cropW: Float = 1
    var cropH: Float = 1
    var lensDistortion: Float = 0
    var lensVignette: Float = 0
    var lensCaRed: Float = 0
    var lensCaBlue: Float = 0
    /// Off by default. See Engine.highlightRecovery.
    var highlightRecovery: Float = 0
    /// Three-way colour grading, each [x, y, luminance].
    var gradeShadow: [Float] = [0, 0, 0]
    var gradeMidtone: [Float] = [0, 0, 0]
    var gradeHighlight: [Float] = [0, 0, 0]
    var denoiseLuma: Float = 0
    var denoiseColor: Float = 0
    var lutStrength: Float = 1
    /// The mask group, folded left in listed order. Empty means no mask.
    /// research/masking.md §6.
    var maskComponents: [MaskComponentState] = []
    var localExposureEv: Float = 0

    var fusion: Float = 0
    var dehaze: Float = 0
    var clarity: Float = 0
    var sharpenAmount: Float = 0
    var sharpenRadius: Float = 1
    var sharpenMasking: Float = 0
    var curve = ToneCurve()
    var hueShift = [Float](repeating: 0, count: 8)
    var satShift = [Float](repeating: 0, count: 8)
    var lumShift = [Float](repeating: 0, count: 8)
}

/// Decoding takes what the sidecar has and leaves the rest at its default.
///
/// Swift's synthesized decoder does not fall back to a property's default — it
/// throws on a missing key. `Engine.restore` swallows that with a `try?`, so
/// adding one field to this struct would have silently discarded *every*
/// adjustment in *every* sidecar written before it, and the only symptom would
/// have been photos quietly opening unedited.
///
/// Each field is optional on the way in instead. Adding one now costs nothing,
/// and a sidecar written by a newer build stays readable by an older one.
///
/// Written out in an extension so the memberwise initializer survives —
/// `Engine.state` builds one field by field.
extension DevelopState {

    /// Mirrors the implicit keys the synthesized encoder writes. Encoding stays
    /// synthesized; only reading is forgiving.
    private enum Key: String, CodingKey {
        case temperatureK, tint, exposureEv, highlights, shadows, whites, blacks
        case vibrance, saturation, contrast, rotateQuarters, straightenDeg
        case cropX, cropY, cropW, cropH
        case lensDistortion, lensVignette, lensCaRed, lensCaBlue
        case highlightRecovery, denoiseLuma, denoiseColor
        case gradeShadow, gradeMidtone, gradeHighlight
        case maskComponents, localExposureEv
        case lutStrength, fusion, dehaze, clarity, sharpenAmount, sharpenRadius, sharpenMasking
        case curve, hueShift, satShift, lumShift

        /// What `denoiseColor` was called before the interface moved to
        /// American spelling. Read, never written: a sidecar from before the
        /// rename still says so, and dropping it would reset the control on a
        /// photo that had already been finished.
        ///
        /// The raw value carries the old spelling; the case is named apart from
        /// it so a future rename sweep cannot collide the two into one key.
        case legacyDenoiseColour = "denoiseColour"

        /// The single mask a sidecar carried before mask groups (M4 step 2).
        /// Read, never written. Every photo finished between the gradient masks
        /// landing and groups landing has these keys and no `maskComponents`, so
        /// dropping them would open those photos with the local edit silently
        /// gone — and the exposure still applied, over the whole frame, because
        /// `localExposureEv` survived under its own name.
        case legacyMaskKind = "maskKind"
        case legacyMaskInvert = "maskInvert"
        case legacyMaskCentreX = "maskCentreX"
        case legacyMaskCentreY = "maskCentreY"
        case legacyMaskAngle = "maskAngle"
        case legacyMaskLength = "maskLength"
        case legacyMaskRadiusX = "maskRadiusX"
        case legacyMaskRadiusY = "maskRadiusY"
        case legacyMaskFeather = "maskFeather"
        case legacyMaskRoundness = "maskRoundness"
        case legacyBrushRadius = "brushRadius"
        case legacyBrushFlow = "brushFlow"
        case legacyBrushHardness = "brushHardness"
        case legacyBrushStroke = "brushStroke"
    }

    init(from decoder: Decoder) throws {
        self.init()
        guard let c = try? decoder.container(keyedBy: Key.self) else { return }

        func float(_ key: Key) -> Float? {
            (try? c.decodeIfPresent(Float.self, forKey: key)).flatMap { $0 }
        }

        temperatureK = float(.temperatureK) ?? temperatureK
        tint = float(.tint) ?? tint
        exposureEv = float(.exposureEv) ?? exposureEv
        highlights = float(.highlights) ?? highlights
        shadows = float(.shadows) ?? shadows
        whites = float(.whites) ?? whites
        blacks = float(.blacks) ?? blacks
        vibrance = float(.vibrance) ?? vibrance
        saturation = float(.saturation) ?? saturation
        contrast = float(.contrast) ?? contrast
        rotateQuarters =
            (try? c.decodeIfPresent(Int32.self, forKey: .rotateQuarters)).flatMap { $0 }
            ?? rotateQuarters
        straightenDeg = float(.straightenDeg) ?? straightenDeg
        cropX = float(.cropX) ?? cropX
        cropY = float(.cropY) ?? cropY
        cropW = float(.cropW) ?? cropW
        cropH = float(.cropH) ?? cropH
        lensDistortion = float(.lensDistortion) ?? lensDistortion
        lensVignette = float(.lensVignette) ?? lensVignette
        lensCaRed = float(.lensCaRed) ?? lensCaRed
        lensCaBlue = float(.lensCaBlue) ?? lensCaBlue
        highlightRecovery = float(.highlightRecovery) ?? highlightRecovery
        denoiseLuma = float(.denoiseLuma) ?? denoiseLuma
        denoiseColor = float(.denoiseColor) ?? float(.legacyDenoiseColour) ?? denoiseColor
        lutStrength = float(.lutStrength) ?? lutStrength
        localExposureEv = float(.localExposureEv) ?? localExposureEv

        // The group, or the single mask a pre-group sidecar carried lifted into
        // one. A component list that is present wins outright: a file holding
        // both was written by a newer build, and its legacy keys are whatever
        // that build's first row happened to be.
        if let list = (try? c.decodeIfPresent([MaskComponentState].self,
                                              forKey: .maskComponents)).flatMap({ $0 }) {
            maskComponents = list.filter { $0.kind != 0 }
        } else if let kind = (try? c.decodeIfPresent(Int32.self, forKey: .legacyMaskKind))
                    .flatMap({ $0 }), kind != 0 {
            var m = MaskComponentState()
            m.kind = kind
            m.compose = 0          // the only op a single mask can have meant
            m.invert = (try? c.decodeIfPresent(Bool.self, forKey: .legacyMaskInvert))
                .flatMap { $0 } ?? m.invert
            m.centreX = float(.legacyMaskCentreX) ?? m.centreX
            m.centreY = float(.legacyMaskCentreY) ?? m.centreY
            m.angle = float(.legacyMaskAngle) ?? m.angle
            m.length = float(.legacyMaskLength) ?? m.length
            m.radiusX = float(.legacyMaskRadiusX) ?? m.radiusX
            m.radiusY = float(.legacyMaskRadiusY) ?? m.radiusY
            m.feather = float(.legacyMaskFeather) ?? m.feather
            m.roundness = float(.legacyMaskRoundness) ?? m.roundness
            m.brushRadius = float(.legacyBrushRadius) ?? m.brushRadius
            m.brushFlow = float(.legacyBrushFlow) ?? m.brushFlow
            m.brushHardness = float(.legacyBrushHardness) ?? m.brushHardness
            m.brushStroke = (try? c.decode([Float].self, forKey: .legacyBrushStroke)) ?? []
            maskComponents = [m]
        }

        fusion = float(.fusion) ?? fusion
        dehaze = float(.dehaze) ?? dehaze
        clarity = float(.clarity) ?? clarity
        sharpenAmount = float(.sharpenAmount) ?? sharpenAmount
        sharpenRadius = float(.sharpenRadius) ?? sharpenRadius
        sharpenMasking = float(.sharpenMasking) ?? sharpenMasking
        curve = (try? c.decodeIfPresent(ToneCurve.self, forKey: .curve))
            .flatMap { $0 } ?? curve

        // A band array of the wrong length would index out of bounds in the
        // panel, so a malformed one is refused rather than trusted.
        func band(_ key: Key) -> [Float]? {
            guard let v = (try? c.decodeIfPresent([Float].self, forKey: key)).flatMap({ $0 }),
                  v.count == 8 else { return nil }
            return v
        }
        // Three, not eight — the grading wheels store x, y and luminance.
        func triple(_ key: Key) -> [Float]? {
            guard let v = (try? c.decodeIfPresent([Float].self, forKey: key)).flatMap({ $0 }),
                  v.count == 3 else { return nil }
            return v
        }
        gradeShadow = triple(.gradeShadow) ?? gradeShadow
        gradeMidtone = triple(.gradeMidtone) ?? gradeMidtone
        gradeHighlight = triple(.gradeHighlight) ?? gradeHighlight

        hueShift = band(.hueShift) ?? hueShift
        satShift = band(.satShift) ?? satShift
        lumShift = band(.lumShift) ?? lumShift
    }
}
