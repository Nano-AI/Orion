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
    var kind: Int32 = 0            // 1 linear, 2 radial, 3 brush, 4 matte, 5 range, 6 colour
    /// 0 add, 1 subtract, 2 intersect. The group folds from zero, so the first
    /// component's op has no effect when it is add and zeroes the group when it
    /// is not — the panel does not offer one on the first row.
    var compose: Int32 = 0
    var invert = false
    /// Hidden by the eye button. Keeps every setting and contributes nothing.
    var hidden = false
    /// Begins a new layer. A layer is a run of consecutive components with its
    /// own coverage and its own adjustments; row 1 always starts one.
    var startsLayer = false
    var centreX: Float = 0.5
    var centreY: Float = 0.5
    var angle: Float = 0
    var length: Float = 0.5
    var radiusX: Float = 0.3
    var radiusY: Float = 0.3
    var feather: Float = 0.5
    var roundness: Float = 2

    /// Luminance range (kind 5), in stops. research/masking.md §4b.
    var rangeLo: Float = -2
    var rangeHi: Float = 2
    var rangeSoft: Float = 1

    /// Colour range (kind 6): the picked shade in scene-linear Rec.2020 RGB,
    /// with a tolerance and a softness in Oklab chromaticity.
    /// research/masking.md §4c.
    var colourR: Float = 0.18
    var colourG: Float = 0.18
    var colourB: Float = 0.18
    var colourTol: Float = 0.10
    var colourSoft: Float = 0.05

    var brushRadius: Float = 0.08
    var brushFlow: Float = 0.5
    var brushHardness: Float = 0.5
    /// Interleaved x, y so the sidecar stays a plain JSON array of numbers
    /// rather than a list of objects — a stroke is thousands of points and the
    /// object form doubles the file for nothing.
    var brushStroke: [Float] = []
    /// One value per dab, 1 where the dab **erases**. ⚠ A parallel array rather
    /// than a third number interleaved into `brushStroke`: that array is a flat
    /// list of numbers in the sidecar, and re-interleaving it would read every
    /// stroke saved before erasing existed as garbage — silently, because a
    /// scrambled stroke is still a valid stroke. Absent means the whole stroke
    /// paints, which is exactly what those older sidecars mean.
    var brushErase: [Float] = []

    /// Raster matte (kind 4): the id of the sibling PNG holding it, and which
    /// producer made it. See `MatteStore`.
    ///
    /// ⚠ **The raster itself is deliberately not here.** A matte is ~700k alpha
    /// values; JSON-encoding it into the sidecar attribute would be about seven
    /// megabytes of text rewritten every time autosave settles, 900 ms after any
    /// slider moves. What lives in the state is the reference.
    ///
    /// `matteSource` is the label the panel shows ("Subject", "Sky"). It is
    /// stored rather than derived because it cannot be recovered from the
    /// raster, and a row that reopens saying only "a selection" is a row nobody
    /// can tell apart from the one below it.
    var matteId: String?
    var matteSource: String?

    /// Decoding takes what the sidecar has and leaves the rest at its default,
    /// for the same reason `DevelopState` does: the synthesized decoder throws
    /// on a missing key, so adding one field would discard every component in
    /// every sidecar written before it.
    /// ⚠ **Every stored property must appear here.** This enum is named `Key`
    /// rather than `CodingKeys`, so Swift synthesises its *own* keys for the
    /// encoder from the stored properties while the decoder below reads these —
    /// which means a field added to the struct joins the sidecar immediately and
    /// is read back never. No warning, and working code from both ends.
    ///
    /// That is exactly what happened: kind 5 shipped in session 2026-07-30b and
    /// `rangeLo`, `rangeHi` and `rangeSoft` were written to every sidecar and
    /// silently dropped on the way back in for five sessions. Set a luminance
    /// band, close the photo, reopen it: the band is back at its default and the
    /// mask selects something else.
    ///
    /// `DevelopState` had the identical defect and was fixed in 2026-07-30e with
    /// a round-trip test — but that test's fixture never filled the *nested*
    /// component's range fields, so the guard could not see them. A round trip
    /// is only as good as the state it round-trips.
    private enum Key: String, CodingKey {
        case kind, compose, invert, hidden, startsLayer, centreX, centreY, angle, length
        case radiusX, radiusY, feather, roundness
        case rangeLo, rangeHi, rangeSoft
        case colourR, colourG, colourB, colourTol, colourSoft
        case brushRadius, brushFlow, brushHardness, brushStroke, brushErase
        case matteId, matteSource
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
        hidden = (try? c.decodeIfPresent(Bool.self, forKey: .hidden)).flatMap { $0 } ?? hidden
        startsLayer = (try? c.decodeIfPresent(Bool.self, forKey: .startsLayer)).flatMap { $0 } ?? startsLayer
        centreX = float(.centreX) ?? centreX
        centreY = float(.centreY) ?? centreY
        angle = float(.angle) ?? angle
        length = float(.length) ?? length
        radiusX = float(.radiusX) ?? radiusX
        radiusY = float(.radiusY) ?? radiusY
        feather = float(.feather) ?? feather
        roundness = float(.roundness) ?? roundness
        rangeLo = float(.rangeLo) ?? rangeLo
        rangeHi = float(.rangeHi) ?? rangeHi
        rangeSoft = float(.rangeSoft) ?? rangeSoft
        colourR = float(.colourR) ?? colourR
        colourG = float(.colourG) ?? colourG
        colourB = float(.colourB) ?? colourB
        colourTol = float(.colourTol) ?? colourTol
        colourSoft = float(.colourSoft) ?? colourSoft
        brushRadius = float(.brushRadius) ?? brushRadius
        brushFlow = float(.brushFlow) ?? brushFlow
        brushHardness = float(.brushHardness) ?? brushHardness
        brushStroke = (try? c.decode([Float].self, forKey: .brushStroke)) ?? brushStroke
        brushErase = (try? c.decode([Float].self, forKey: .brushErase)) ?? brushErase
        matteId = (try? c.decodeIfPresent(String.self, forKey: .matteId)).flatMap { $0 }
        matteSource = (try? c.decodeIfPresent(String.self, forKey: .matteSource)).flatMap { $0 }
    }
}

/// One spot: a disc taken from elsewhere in the frame.
/// research/spot-removal.md.
struct SpotState: Equatable, Codable {
    var destX: Float = 0.5
    var destY: Float = 0.5
    var srcX: Float = 0.5
    var srcY: Float = 0.5
    /// Normalized against the frame's *width*, so a spot is round on any aspect.
    var radius: Float = 0.02
    var feather: Float = 0.5
    /// Heal takes the destination's tone; clone does not.
    var heal = true
}

/// One layer's local adjustments — pointwise only, research/masking.md §2b.
struct LocalAdjustState: Equatable, Codable {
    var exposureEv: Float = 0
    var contrast: Float = 0
    var saturation: Float = 0
    /// A colour cast where the mask covers, not a white balance.
    var warmth: Float = 0
    var tint: Float = 0
}

/// Every setting that belongs to a photograph, as one value.
///
/// ⚠ **No stored property here carries an inline default, and that is the whole
/// point.** Decision #110. A default on the property becomes a default on the
/// *memberwise initializer*, and `Engine.state` builds one of these with the
/// memberwise initializer — so a defaulted field could be left out of that call
/// and the build would say nothing. It shipped twice: film grain with Amount
/// stuck at 0, and Grading Balance, both of which reached the screen, did
/// nothing to the sidecar, and left every suite green.
///
/// With the defaults moved into `init()` (in an extension, so the memberwise
/// initializer survives), adding a field breaks the build in two places at once
/// — `DevelopState.init()` and `Engine.state` — and both errors name the field.
///
/// ⚠ The one hole left is a field added *with* an inline default anyway, which
/// re-arms the original trap for that field alone. `testDevelopStateRoster` in
/// the viewport suite is the backstop: it reflects over the struct and fails
/// naming any field the roster does not know about.
struct DevelopState: Equatable, Codable {
    var temperatureK: Float
    var tint: Float
    var exposureEv: Float
    var highlights: Float
    var shadows: Float
    var whites: Float
    var blacks: Float
    var vibrance: Float
    var saturation: Float
    /// Orion's base rendering, not neutral. See Engine.contrast.
    var contrast: Float
    var rotateQuarters: Int32
    var straightenDeg: Float
    /// Perspective correction, each -1..1. research/perspective.md.
    var perspectiveVertical: Float
    var perspectiveHorizontal: Float
    var perspectiveAspect: Float
    var cropX: Float
    var cropY: Float
    var cropW: Float
    var cropH: Float
    var lensDistortion: Float
    var lensVignette: Float
    var lensCaRed: Float
    var lensCaBlue: Float
    /// Off by default. See Engine.highlightRecovery.
    var highlightRecovery: Float
    /// Three-way colour grading, each [x, y, luminance].
    var gradeShadow: [Float]
    var gradeMidtone: [Float]
    var gradeHighlight: [Float]
    /// Where the split between those three zones sits, -1..+1, positive toward
    /// the highlights. Decision #101. Zero is the fixed -2.5 / 0 / +2.5 EV
    /// centres every photograph edited before this existed was graded with.
    var gradeBalance: Float
    var denoiseLuma: Float
    var denoiseColor: Float
    var lutStrength: Float
    /// The mask group, folded left in listed order. Empty means no mask.
    /// research/masking.md §6.
    var maskComponents: [MaskComponentState]
    /// Guided feathering of the folded group, 0..1. research/masking.md §4.
    var maskRefine: Float
    /// Dust and blemishes. research/spot-removal.md.
    var spots: [SpotState]
    /// ⚠ **One set per layer.** A layer is a run of mask components with its
    /// own coverage, so the subject can be graded one way and the sky another.
    ///
    /// The legacy scalar keys are still read into layer 1 — every sidecar
    /// written before layers existed carries exactly one set, and that set is
    /// what layer 1 means.
    var layers: [LocalAdjustState]

    var fusion: Float
    var dehaze: Float
    var clarity: Float
    /// Film grain — research/film-grain.md, decisions #81 and #82. `grainAmount`
    /// is the peak standard deviation in display units; `grainSize` the grain
    /// radius in *frame* pixels, so a crop enlarges the grain rather than
    /// resampling it.
    var grainAmount: Float
    var grainSize: Float
    /// The creative vignette — research/vignette.md, decision #103. Amount is
    /// the exposure change at the corner of the *composition* in stops, so it
    /// follows a crop; `vignetteFieldAngle` is the half-diagonal field angle of
    /// the lens whose cos⁴ falloff it imitates.
    ///
    /// ⚠ Nothing to do with `lensVignette` above, which is the correction.
    var vignetteAmount: Float
    var vignetteFieldAngle: Float
    var sharpenAmount: Float
    var sharpenRadius: Float
    var sharpenMasking: Float
    var curve: ToneCurve
    var hueShift: [Float]
    var satShift: [Float]
    var lumShift: [Float]
}

extension DevelopState {

    /// A neutral state — every control at the value it returns to on Reset.
    ///
    /// ⚠ **Written in an extension on purpose.** An initializer declared in the
    /// struct's own body suppresses the memberwise initializer; declared out
    /// here, both survive, which is what lets this one delegate to the other and
    /// so lets *one* added field break *both*.
    ///
    /// ⚠ And it delegates rather than assigning field by field, so the compiler
    /// checks the argument list against the stored properties. Assigning here
    /// would compile happily with a field left out — the property would simply
    /// keep whatever the memberwise call gave it — which is the trap again.
    init() {
        self.init(
            temperatureK: 5500, tint: 0, exposureEv: 0,
            highlights: 0, shadows: 0, whites: 0, blacks: 0,
            vibrance: 0, saturation: 0,
            // Orion's base rendering, not neutral. See Engine.contrast.
            contrast: 1.45,
            rotateQuarters: 0, straightenDeg: 0,
            perspectiveVertical: 0, perspectiveHorizontal: 0, perspectiveAspect: 0,
            cropX: 0, cropY: 0, cropW: 1, cropH: 1,
            lensDistortion: 0, lensVignette: 0, lensCaRed: 0, lensCaBlue: 0,
            highlightRecovery: 0,
            gradeShadow: [0, 0, 0], gradeMidtone: [0, 0, 0],
            gradeHighlight: [0, 0, 0], gradeBalance: 0,
            denoiseLuma: 0, denoiseColor: 0,
            lutStrength: 1,
            maskComponents: [],
            maskRefine: 0,
            spots: [],
            layers: [LocalAdjustState()],
            fusion: 0, dehaze: 0, clarity: 0,
            grainAmount: 0, grainSize: 1.5,
            // 45° half-diagonal field angle, not 0 — a 0° lens has a flat cos⁴
            // falloff and the vignette would do nothing at any amount.
            vignetteAmount: 0, vignetteFieldAngle: 45,
            sharpenAmount: 0, sharpenRadius: 1, sharpenMasking: 0,
            curve: ToneCurve(),
            hueShift: [Float](repeating: 0, count: 8),
            satShift: [Float](repeating: 0, count: 8),
            lumShift: [Float](repeating: 0, count: 8))
    }

    /// Every stored property's name, checked against reflection by
    /// `testDevelopStateRoster`. Decision #110.
    ///
    /// The compiler catches a field added the ordinary way. It cannot catch a
    /// field added *with* an inline default, because that field then has a
    /// default in the memberwise initializer too and `Engine.state` can go on
    /// omitting it. This roster catches that one: `Mirror` reports stored
    /// properties whatever their defaults, so the test goes red naming the field
    /// and pointing at the four places it has to be listed.
    static let fieldRoster: Set<String> = [
        "temperatureK", "tint", "exposureEv", "highlights", "shadows",
        "whites", "blacks", "vibrance", "saturation", "contrast",
        "rotateQuarters", "straightenDeg",
        "perspectiveVertical", "perspectiveHorizontal", "perspectiveAspect",
        "cropX", "cropY", "cropW", "cropH",
        "lensDistortion", "lensVignette", "lensCaRed", "lensCaBlue",
        "highlightRecovery",
        "gradeShadow", "gradeMidtone", "gradeHighlight", "gradeBalance",
        "denoiseLuma", "denoiseColor", "lutStrength",
        "maskComponents", "maskRefine", "spots", "layers",
        "fusion", "dehaze", "clarity",
        "grainAmount", "grainSize",
        "vignetteAmount", "vignetteFieldAngle",
        "sharpenAmount", "sharpenRadius", "sharpenMasking",
        "curve", "hueShift", "satShift", "lumShift",
    ]
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
        case perspectiveVertical, perspectiveHorizontal, perspectiveAspect
        case cropX, cropY, cropW, cropH
        case lensDistortion, lensVignette, lensCaRed, lensCaBlue
        case highlightRecovery, denoiseLuma, denoiseColor
        case gradeShadow, gradeMidtone, gradeHighlight, gradeBalance
        case maskComponents, localExposureEv, maskRefine, spots
        case localContrast, localSaturation, localWarmth, localTint
        case layers
        case lutStrength, fusion, dehaze, clarity, sharpenAmount, sharpenRadius, sharpenMasking
        case grainAmount, grainSize
        case vignetteAmount, vignetteFieldAngle
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
        perspectiveVertical = float(.perspectiveVertical) ?? perspectiveVertical
        perspectiveHorizontal = float(.perspectiveHorizontal) ?? perspectiveHorizontal
        perspectiveAspect = float(.perspectiveAspect) ?? perspectiveAspect
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
        // ⚠ The new key wins when present, and the legacy scalars fill layer 1
        // when it is not. A sidecar written by a newer build carries both,
        // because the encoder is synthesised from the stored properties — and
        // preferring the scalars there would silently discard layers 2 and up.
        if let ls = (try? c.decodeIfPresent([LocalAdjustState].self, forKey: .layers))
                        .flatMap({ $0 }), !ls.isEmpty {
            layers = ls
        } else {
            var first = LocalAdjustState()
            first.exposureEv = float(.localExposureEv) ?? first.exposureEv
            first.contrast = float(.localContrast) ?? first.contrast
            first.saturation = float(.localSaturation) ?? first.saturation
            first.warmth = float(.localWarmth) ?? first.warmth
            first.tint = float(.localTint) ?? first.tint
            layers = [first]
        }
        maskRefine = float(.maskRefine) ?? maskRefine

        // ⚠ `spots` and `maskRefine` were written by the encoder and ignored
        // here for two sessions. Encoding is synthesised from the stored
        // properties; decoding is this hand-written list, so a field added to
        // the struct joins the sidecar and never comes back out. Dust removal
        // and guided feathering both silently vanished on reopen, and nothing
        // noticed because no test round-tripped a state with everything set.
        // `testEveryFieldSurvivesTheSidecar` is that test now.
        if let list = (try? c.decodeIfPresent([SpotState].self, forKey: .spots))
            .flatMap({ $0 }) {
            spots = list
        }

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
        // ⚠ `?? grainAmount` keeps the default, so a sidecar written before
        // grain existed reopens with grain off rather than at some decoded
        // zero-ish value — and `grainSize` keeps its 1.5 rather than becoming
        // an out-of-range 0 the shader would clamp silently.
        grainAmount = float(.grainAmount) ?? grainAmount
        grainSize = float(.grainSize) ?? grainSize
        // Same shape, same reason: a sidecar written before the creative
        // vignette existed must reopen with no vignette and a 45° field angle,
        // not with a 0° one that would make the falloff flat.
        vignetteAmount = float(.vignetteAmount) ?? vignetteAmount
        vignetteFieldAngle = float(.vignetteFieldAngle) ?? vignetteFieldAngle
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
        // A scalar, not a triple. `?? gradeBalance` keeps the zero, so a
        // sidecar written before Balance existed reopens on the centres it was
        // graded with rather than on a decoded absence.
        gradeBalance = float(.gradeBalance) ?? gradeBalance

        hueShift = band(.hueShift) ?? hueShift
        satShift = band(.satShift) ?? satShift
        lumShift = band(.lumShift) ?? lumShift
    }
}
