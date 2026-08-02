import Foundation
import Metal
import MetalKit
import AppKit

/// Swift-side owner of the C engine handle.
///
/// Note what is NOT here: no SwiftUI types, no view state. The model stays a
/// plain observable object so any panel built on it can be re-hosted in AppKit
/// without touching engine code (decision #26).
@Observable
final class Engine {

    enum Failure: LocalizedError {
        case create(String)
        case open(String)
        case export(String)

        var errorDescription: String? {
            switch self {
            case .create(let m): return "Could not start the engine: \(m)"
            case .open(let m):   return "Could not open that photo: \(m)"
            case .export(let m): return "Could not export: \(m)"
            }
        }
    }

    internal(set) var camera = ""
    internal(set) var imageWidth: UInt32 = 0
    internal(set) var imageHeight: UInt32 = 0

    /// The whole frame after rotation, before any crop. The crop rectangle is
    /// normalized against this, and the crop tool's canvas is sized from it —
    /// `imageWidth` cannot stand in, because it is the *cropped* result.
    internal(set) var frameWidth: UInt32 = 0
    internal(set) var frameHeight: UInt32 = 0

    internal(set) var lastRenderMs: Double = 0
    internal(set) var isLoaded = false

    /// Why the last render failed, or `nil` if it did not.
    ///
    /// ⚠ This exists because a failed render used to be **completely silent**.
    /// `render()` guarded on the status and returned, `lastRenderMs` stayed at
    /// its initial 0, and the canvas kept whatever texture it had — which, on
    /// the first render after an open, is nothing. The photographer saw a black
    /// rectangle, "0.0 ms", and no explanation, on a file the engine renders
    /// correctly from the scenario runner. The detail was there the whole time:
    /// `orion_last_error` had it and nobody read it.
    internal(set) var lastFailure: String?

    var temperatureK: Float = 5500 { didSet { pushAndRender() } }
    var tint: Float = 0            { didSet { pushAndRender() } }
    var exposureEv: Float = 0      { didSet { pushAndRender() } }
    var highlights: Float = 0      { didSet { pushAndRender() } }
    var shadows: Float = 0         { didSet { pushAndRender() } }
    var whites: Float = 0          { didSet { pushAndRender() } }
    var blacks: Float = 0          { didSet { pushAndRender() } }
    var vibrance: Float = 0        { didSet { pushAndRender() } }
    var saturation: Float = 0      { didSet { pushAndRender() } }
    /// The base rendering a file opens with.
    ///
    /// AgX is a *neutral* scene-referred transform, so straight out of it a
    /// photograph looks flatter than the camera's own JPEG — which is the
    /// picture the photographer saw on the back of the camera and thinks of as
    /// "the original colors". Every raw editor answers this the same way, by
    /// shipping a base contrast rather than showing the neutral transform raw.
    ///
    /// 1.15 was measured, not chosen by eye: against the embedded JPEG of the
    /// test frame it puts mean luma at 0.051 against the camera's 0.052, and
    /// mean saturation at 0.81 against 0.85. Drag the slider to 1.00 for the
    /// neutral transform.
    var contrast: Float = 1.45     { didSet { pushAndRender() } }

    /// Extra quarter turns clockwise, on top of the camera's own orientation.
    var rotateQuarters: Int32 = 0  { didSet { constrainCrop(); pushAndRender() } }

    /// Straighten angle. Turning the dial shrinks the crop rather than letting
    /// it reach past the turned frame, which is what would leave transparent
    /// wedges in the corners of the export.
    var straightenDeg: Float = 0   { didSet { constrainCrop(); pushAndRender() } }

    /// Perspective correction — converging verticals, converging horizontals,
    /// and the squeeze a strong correction leaves behind. research/perspective.md.
    ///
    /// ⚠ These deliberately do **not** call `constrainCrop`. The straighten has
    /// to, because turning the frame leaves the crop rectangle reaching past
    /// it; perspective does not, because `persp::autoScale` has already zoomed
    /// the whole frame back to full, so anything already inside the frame is
    /// still inside it. Shrinking the crop here would take away picture the
    /// correction had just given back.
    var perspectiveVertical: Float = 0   { didSet { pushAndRender() } }
    var perspectiveHorizontal: Float = 0 { didSet { pushAndRender() } }
    var perspectiveAspect: Float = 0     { didSet { pushAndRender() } }

    /// True while the crop tool is open.
    var cropPreview = false        { didSet { pushAndRender() } }

    var constraining = false

    var cropX: Float = 0           { didSet { pushAndRender() } }
    var cropY: Float = 0           { didSet { pushAndRender() } }
    var cropW: Float = 1           { didSet { pushAndRender() } }
    var cropH: Float = 1           { didSet { pushAndRender() } }

    /// Tone curve, applied after the display transform. The engine has had the
    /// spline and the LUT since M2; nothing reached them until now.
    var curve = ToneCurve()       { didSet { pushAndRender() } }

    /// Lens corrections. Manual, unless `lensProfileEnabled` is on and this
    /// photo's lens is in the database — then the measured coefficients replace
    /// both of these and the sliders are disabled rather than fighting the data.
    var lensDistortion: Float = 0 { didSet { pushAndRender() } }
    var lensVignette: Float = 0   { didSet { pushAndRender() } }

    /// Whether to apply the measured profile. On by default when one is found:
    /// a correction the lens is known to need is not a creative decision, and
    /// every other converter applies it without being asked.
    var lensProfileEnabled = true { didSet { pushAndRender() } }

    /// What the database knows about this photo's lens. Empty name means no
    /// match — which includes every manual lens, since those record no name.
    internal(set) var lensProfileName = ""
    internal(set) var lensProfileApproximate = false
    var hasLensProfile: Bool { !lensProfileName.isEmpty }
    var lensCaRed: Float = 0      { didSet { pushAndRender() } }
    var lensCaBlue: Float = 0     { didSet { pushAndRender() } }

    /// Highlight reconstruction. **Off by default.**
    ///
    /// It shipped defaulted on, and on a night frame it drew a magenta halo
    /// round every blown light. The fit that recovers a clipped channel is a
    /// linear model, and a linear model asked to reach far past the data it was
    /// fitted on will invent whatever it likes. The guards added since —
    /// fitting only on the highlight's shoulder, refusing to extrapolate past
    /// the brightest pixel that informed the fit, and capping each channel at a
    /// ratio the neighborhood actually showed — make it much more
    /// conservative, but "much more conservative" is not the same as verified,
    /// and a correction that damages pictures should not be the default while
    /// that is still true.
    var highlightRecovery: Float = 0 { didSet { pushAndRender() } }

    /// Three-way color grading. Each is [x, y, luminance] — the wheel's puck
    /// in the unit disc, then that zone's slope. The engine turns (x, y) into a
    /// zero-sum RGB offset, which is what keeps a wheel a color control rather
    /// than a second brightness one. research/color-grading.md.
    var gradeShadow: [Float] = [0, 0, 0] { didSet { pushAndRender() } }
    var gradeMidtone: [Float] = [0, 0, 0] { didSet { pushAndRender() } }
    var gradeHighlight: [Float] = [0, 0, 0] { didSet { pushAndRender() } }

    /// Where the split between those three zones sits, -1..+1. The zones are
    /// Gaussians fixed at -2.5 / 0 / +2.5 EV; this slides all three centres
    /// together, so the photographer decides how much of the picture counts as
    /// shadow. Positive is toward the highlights. Decision #101.
    ///
    /// With every wheel centred it is a no-op — the engine will not switch the
    /// grading node on for it, and will not re-push the block for it either.
    var gradeBalance: Float = 0 { didSet { pushAndRender() } }

    /// Profiled wavelet denoise, in multiples of the frame's own measured
    /// noise level. Zero switches eight nodes off rather than running them at
    /// no strength.
    var denoiseLuma: Float = 0     { didSet { pushAndRender() } }
    var denoiseColor: Float = 0   { didSet { pushAndRender() } }

    /// How much of the loaded creative LUT to apply. The LUT itself is a file,
    /// loaded through `loadLut`, not an adjustment.
    var lutStrength: Float = 1     { didSet { pushAndRender() } }

    /// Film grain. Amount is the peak standard deviation in display units; Size
    /// is the grain radius in *frame* pixels. research/film-grain.md, #81/#82.
    ///
    /// ⚠ Amount 0 does not merely render silent grain — it disables the node,
    /// so the graph costs exactly what it did before the feature existed. See
    /// `DevelopPipeline::retargetOutputChain`.
    var grainAmount: Float = 0     { didSet { pushAndRender() } }
    var grainSize: Float = 1.5     { didSet { pushAndRender() } }

    /// The **creative** vignette. `research/vignette.md`, decision #103.
    ///
    /// ⚠ Not `lensVignette`, which removes a falloff the lens measured. This
    /// one adds one, centred on the crop rather than on the frame, and a
    /// photograph can carry both without either touching the other.
    ///
    /// Amount is the exposure change at the corner of the composition, in
    /// stops; Field angle is the half-diagonal field angle of the lens whose
    /// natural cos⁴ falloff it imitates.
    var vignetteAmount: Float = 0        { didSet { pushAndRender() } }
    var vignetteFieldAngle: Float = 45   { didSet { pushAndRender() } }

    // ── Local adjustments: the mask group (M4) ────────────────────────────
    //
    // A mask is a *list* of components folded left in listed order
    // (research/masking.md §6), and one local adjustment is applied through the
    // combined coverage. The list is the storage; the `mask…` properties below
    // are views onto whichever row is selected, which is what the sliders, the
    // canvas overlay and the screenshot harness all bind to.

    /// How many components one group holds. Matches the engine's own cap; the
    /// facade rejects an index past it.
    static let maxMaskComponents = 4

    internal(set) var maskComponents: [MaskComponentState] = []

    /// Rows whose saved matte file could not be read when the photograph opened.
    ///
    /// ⚠ Not a cosmetic flag. Without it a kind-4 row whose PNG has gone missing
    /// renders with no coverage — the picture changes and nothing on screen says
    /// why, which is the shape of failure this project keeps paying for. The
    /// panel reads this and says the matte is missing.
    ///
    /// Cleared by `open`, since it is an answer about the photograph in hand.
    var missingMattes: Set<Int> = []

    /// Which row the panel and the canvas are editing. Clamped on every read,
    /// because removing a row can leave it past the end and a stale index would
    /// silently edit the wrong component.
    var selectedMask: Int = 0 {
        didSet {
            let clamped = maskComponents.isEmpty
                ? 0 : min(max(0, selectedMask), maskComponents.count - 1)
            if clamped != selectedMask { selectedMask = clamped }
        }
    }

    /// Guided feathering of the folded group, 0..1 — research/masking.md §4.
    ///
    /// A property of the group rather than of a component, so it lives here
    /// beside the local adjustment rather than in `MaskComponentState`: what a
    /// photographer wants snapped to an edge is the coverage they can see.
    var maskRefine: Float = 0 { didSet { pushAndRender() } }

    // ── Spot removal (research/spot-removal.md) ───────────────────────────

    /// The most spots one photo can carry. Matches the engine's own cap.
    static let maxSpots = 64

    internal(set) var spots: [SpotState] = []

    /// True while the tool is armed and a click on the photo places a spot.
    ///
    /// View state, not an edit — like `maskOverlay` it never reaches the
    /// sidecar, and unlike it there is nothing to re-render when it changes.
    var spotPlacing = false

    /// Nib settings for the *next* spot, and for the selected one. Not part of
    /// `DevelopState`: they are how the tool is set up, and a photograph with
    /// no spots on it should not remember a radius.
    var spotRadius: Float = 0.02
    var spotFeather: Float = 0.5
    var spotHeal = true

    /// Which spot the canvas has selected, or -1. View state: which handle is
    /// lit is not part of the photograph.
    var selectedSpot: Int = -1

    /// One local adjustment set per layer.
    internal(set) var layers: [LocalAdjustState] = [LocalAdjustState()]

    /// Armed by the panel's picker button. The canvas consumes the next click,
    /// exactly as it does for spot placement.
    var colorPicking = false

    /// What the picked pixel looked like **on screen**, for the panel's swatch.
    ///
    /// ⚠ The swatch cannot be drawn from the stored target. `sampleAt`
    /// normalizes the scene color by its own peak — which is exactly right for
    /// the metric, since Oklab chromaticity is scale invariant — but it means
    /// the stored value always has a channel at 1.0. Drawn directly, a picked
    /// navy sky came back as a bright periwinkle and a dark green as a vivid
    /// one: every color looked like a saturated version of itself, which is
    /// what "the eyedropper pulls a very wrong color" looks like from outside.
    ///
    /// View state, not edit state, so it is deliberately not in `DevelopState`:
    /// it is a picture of a gesture, not part of the photograph. Reopening a
    /// photo leaves it nil and the panel falls back to the target's hue.
    internal(set) var maskColorSwatch: (r: Double, g: Double, b: Double)?

    /// Bumped per component whenever that component's stroke changes.
    ///
    /// The revision is the only thing the engine compares — it never walks a
    /// stroke to find out whether it moved. Change the points without the
    /// revision and the picture does not follow the hand, which is why the two
    /// are set together here rather than being left to callers.
    ///
    /// Not in `DevelopState`: it is a staleness token, not an edit, so it must
    /// not reach the sidecar or undo.
    var brushRevisions = [UInt32](repeating: 0, count: Engine.maxMaskComponents)

    /// Whether the brush is currently taking coverage away. Tool state, not
    /// part of the photograph — it belongs to the hand, like `spotPlacing`.
    var brushErasing = false

    // ── A live stroke, which deliberately touches nothing observable ──────
    //
    // ⚠ **This exists because painting cost ~155 ms a pointer event in the app
    // while the headless harness measured 0.9 ms.** The harness drives `Engine`
    // and never renders SwiftUI, so it could not see the 170x. Everything in
    // that gap was work this class did *per event* on behalf of a stroke that
    // is not finished yet:
    //
    //   * `maskComponents[i].brushStroke = …` mutates an `@Observable`
    //     property, and `DevelopPanels` reads `maskComponents` in eleven
    //     places — so every dab invalidated the whole develop panel tree.
    //   * `points.flatMap { [Float($0.x), Float($0.y)] }` rebuilt the entire
    //     stroke and allocated a two-element array per dab, per event.
    //   * `pushAndRender` then built `cAdjustments()` (eighty fields plus the
    //     mask group and the spot list) and called `onEdit?(state)`, which
    //     copies the whole `DevelopState` — brush stroke included — and hands
    //     it to `Autosave.note`, whose first act is `state != saved`, a full
    //     structural compare of the same arrays.
    //
    // All of that is for the *record* of the stroke, and the record only has to
    // be right when the hand stops. During the drag the engine already holds
    // the authoritative dabs, because they were pushed straight across the
    // facade. So the buffers below are `@ObservationIgnored`, they are appended
    // to rather than rebuilt, and `maskComponents` is written exactly **once**,
    // in `endBrushStroke`.
    @ObservationIgnored var liveStroke: [Float] = []
    @ObservationIgnored var liveErase: [Float] = []
    /// Which component the live stroke belongs to; -1 when no stroke is down.
    @ObservationIgnored var liveIndex = -1

    /// Paint the mask's coverage over the picture, so it can be placed by eye.
    ///
    /// Not part of `DevelopState` on purpose: it is how you are *looking* at
    /// the photo, not an edit to it, so it must not land in the sidecar, must
    /// not enter undo history, and must not follow the photo to another
    /// machine. `export` forces it off around the write for the same reason.
    var maskOverlay = false { didSet { log.overlay(maskOverlay); pushAndRender() } }

    /// Exposure fusion. A power on the emitted gain, so zero is exact.
    var fusion: Float = 0          { didSet { pushAndRender() } }

    /// Dehaze. The dark channel prior's own omega, so zero is the identity.
    var dehaze: Float = 0          { didSet { pushAndRender() } }

    /// Local Laplacian clarity. Negative smooths detail, positive increases
    /// its contrast; the endpoints are the exponents Paris et al. illustrate.
    var clarity: Float = 0         { didSet { pushAndRender() } }

    var sharpenAmount: Float = 0   { didSet { pushAndRender() } }
    var sharpenRadius: Float = 1   { didSet { pushAndRender() } }
    var sharpenMasking: Float = 0  { didSet { pushAndRender() } }

    /// Color mixer, eight bands. Index order matches HueBand.allCases.
    var hueShift = [Float](repeating: 0, count: 8) { didSet { pushAndRender() } }
    var satShift = [Float](repeating: 0, count: 8) { didSet { pushAndRender() } }
    var lumShift = [Float](repeating: 0, count: 8) { didSet { pushAndRender() } }

    /// True while a slider is being dragged, so pushes are suppressed until
    /// the value settles. Not used yet — hook for degrade-then-refine.
    /// Called with the new state after every adjustment lands, so the shell can
    /// persist it. The engine deliberately knows nothing about files, URLs or
    /// sidecars — it reports *that* something changed and hands over the value;
    /// what that is worth is the shell's call.
    ///
    /// The state is passed rather than read back, so the callback need not
    /// capture the engine — which would be a retain cycle — and so a deferred
    /// write cannot pick up a *later* state than the one it was queued for.
    @ObservationIgnored var onEdit: ((DevelopState) -> Void)?

    var suspended = false

    /// Bumped after every successful render so the view knows to redraw.
    internal(set) var generation: UInt64 = 0

    /// 128 bins per channel, packed R then G then B.
    internal(set) var histogramBins: [UInt32] = []

    let history = EditHistory()

    /// Suppresses history recording while undo/redo is applying a snapshot —
    /// otherwise stepping back would immediately record the step as a new edit.
    private var restoring = false

    var handle: OpaquePointer?

    init() throws {
        var h: OpaquePointer?
        let status = orion_engine_create(&h)
        guard status == ORION_OK, let h else {
            throw Failure.create(String(cString: orion_status_string(status)))
        }
        handle = h
    }

    deinit {
        if let handle { orion_engine_destroy(handle) }
    }

    // ── Degrade-then-refine (ROADMAP M1, Interaction) ─────────────────────
    //
    // Measured before it was built: exposure costs 9.4 ms a tick, clarity 65.7
    // and dehaze 116.4 — `repro/slider-drag-cost.txt`. The two heavy ones are
    // rendered on a quarter-linear graph while the hand is moving and on the
    // full one when it stops.

    /// True while a control is being dragged.
    ///
    /// ⚠ Set by the *control*, not inferred from how fast values arrive. A
    /// timer-based guess renders the first tick of every drag at full
    /// resolution — which is the expensive one, since it is the tick that
    /// dirties the graph — and picks the wrong answer for a slider nudged by
    /// the keyboard.
    internal(set) var interacting = false

    /// The loaded creative LUT's title, or "" when none is loaded.
    internal(set) var lutName: String = ""

    /// Why the last load failed, for the panel to show. A LUT that will not
    /// load is the user's file being wrong, not the app misbehaving, so the
    /// reason has to reach them — the parser names the line.
    internal(set) var lutError: String?

    /// Captures every setting as a value, for undo.
    var state: DevelopState {
        DevelopState(
            temperatureK: temperatureK, tint: tint, exposureEv: exposureEv,
            highlights: highlights, shadows: shadows, whites: whites, blacks: blacks,
            vibrance: vibrance, saturation: saturation, contrast: contrast,
            rotateQuarters: rotateQuarters, straightenDeg: straightenDeg,
            perspectiveVertical: perspectiveVertical,
            perspectiveHorizontal: perspectiveHorizontal,
            perspectiveAspect: perspectiveAspect,
            cropX: cropX, cropY: cropY, cropW: cropW, cropH: cropH,
            lensDistortion: lensDistortion, lensVignette: lensVignette,
            lensCaRed: lensCaRed, lensCaBlue: lensCaBlue,
            highlightRecovery: highlightRecovery,
            gradeShadow: gradeShadow, gradeMidtone: gradeMidtone,
            gradeHighlight: gradeHighlight, gradeBalance: gradeBalance,
            denoiseLuma: denoiseLuma, denoiseColor: denoiseColor,
            lutStrength: lutStrength,
            maskComponents: maskComponents,
            maskRefine: maskRefine,
            spots: spots,
            layers: layers,
            fusion: fusion, dehaze: dehaze, clarity: clarity,
            grainAmount: grainAmount, grainSize: grainSize,
            vignetteAmount: vignetteAmount, vignetteFieldAngle: vignetteFieldAngle,
            sharpenAmount: sharpenAmount, sharpenRadius: sharpenRadius,
            sharpenMasking: sharpenMasking, curve: curve,
            hueShift: hueShift, satShift: satShift, lumShift: lumShift)
    }

    /// Assigns every adjustment from a state, without rendering.
    ///
    /// The only place that lists the fields. Open and Reset each used to carry
    /// their own list, and each was missing whatever had been added since —
    /// Reset left denoise, the lens corrections and the curve applied, which
    /// makes a Reset button that does not reset.
    func assign(_ s: DevelopState) {
        temperatureK = s.temperatureK; tint = s.tint; exposureEv = s.exposureEv
        highlights = s.highlights; shadows = s.shadows
        whites = s.whites; blacks = s.blacks
        vibrance = s.vibrance; saturation = s.saturation; contrast = s.contrast
        rotateQuarters = s.rotateQuarters; straightenDeg = s.straightenDeg
        perspectiveVertical = s.perspectiveVertical
        perspectiveHorizontal = s.perspectiveHorizontal
        perspectiveAspect = s.perspectiveAspect
        cropX = s.cropX; cropY = s.cropY; cropW = s.cropW; cropH = s.cropH
        lensDistortion = s.lensDistortion; lensVignette = s.lensVignette
        lensCaRed = s.lensCaRed; lensCaBlue = s.lensCaBlue
        highlightRecovery = s.highlightRecovery
        gradeShadow = s.gradeShadow
        gradeMidtone = s.gradeMidtone
        gradeHighlight = s.gradeHighlight
        gradeBalance = s.gradeBalance
        denoiseLuma = s.denoiseLuma; denoiseColor = s.denoiseColor
        lutStrength = s.lutStrength
        grainAmount = s.grainAmount; grainSize = s.grainSize
        vignetteAmount = s.vignetteAmount
        vignetteFieldAngle = s.vignetteFieldAngle
        // The whole group at once, then every stroke re-sent — the engine keeps
        // strokes outside the adjustment block, so assigning the list alone
        // would restore the geometry and leave the previous photo's paint in the
        // engine under the same indices.
        maskComponents = Array(s.maskComponents.prefix(Self.maxMaskComponents))
        // Clamped, not reset. Every route through here — undo, redo, a history
        // jump, and compare's two back-to-back renders — used to send the panel
        // back to row 1, and since the `mask…` sliders are views onto the
        // selected row, the controls then read a different component's numbers.
        // "Compare shows different settings" is what that looks like from the
        // outside. Clamping is enough to keep the index in range, which is the
        // only thing resetting was buying.
        selectedMask = maskComponents.isEmpty
            ? 0 : min(max(0, selectedMask), maskComponents.count - 1)
        pushStrokes()
        maskRefine = s.maskRefine
        spots = Array(s.spots.prefix(Self.maxSpots))
        layers = s.layers.isEmpty ? [LocalAdjustState()] : s.layers
        fusion = s.fusion
        dehaze = s.dehaze
        clarity = s.clarity
        sharpenAmount = s.sharpenAmount; sharpenRadius = s.sharpenRadius
        sharpenMasking = s.sharpenMasking; curve = s.curve
        hueShift = s.hueShift; satShift = s.satShift; lumShift = s.lumShift
    }

    func apply(_ s: DevelopState) {
        restoring = true
        suspended = true
        assign(s)
        suspended = false
        restoring = false
        pushAndRender()
    }

    /// A fresh state carrying the camera's own white balance, which is the
    /// starting point rather than an edit.
    /// What every control returns to, and what the panel compares against to
    /// decide whether a control has been touched.
    ///
    /// A fresh `DevelopState` in all but white balance, which is the camera's
    /// own reading and so belongs to the photo rather than to the app —
    /// "reset temperature" has to mean the camera's number, not 5500 K.
    internal(set) var defaults = DevelopState()

    /// ⚠ **The status is read, and the fallback is the struct's own defaults.**
    /// It was discarded, and `OrionAdjustments()` is *zeroed* — so a refused
    /// call did not leave white balance alone, it set the photograph to 0 K and
    /// tint 0. The picture opens deep blue, "reset temperature" puts it back
    /// there, and nothing anywhere says the camera was never asked.
    func asShotState() -> DevelopState {
        var fresh = DevelopState()
        guard let handle else { return fresh }
        var asShot = OrionAdjustments()
        let status = orion_engine_as_shot(handle, &asShot)
        guard status == ORION_OK else {
            let why = errorText(status)
            lastFailure = "the camera's white balance could not be read — \(why)"
            FileHandle.standardError.write(
                Data("orion: as-shot white balance unavailable — \(why)\n".utf8))
            return fresh
        }
        fresh.temperatureK = asShot.temperature_k
        fresh.tint = asShot.tint
        return fresh
    }

    /// Split compare: 1.0 means no split, lower values reveal the original
    /// from the left (or top).
    var compareSplit: Double = 1.0
    var compareVertical = true

    /// The unedited render, held so the split can show both at once without
    /// re-rendering on every drag of the divider.
    internal(set) var originalTexture: MTLTexture?

    var originalGeometry: OriginalGeometry?

    /// `captureOriginal` renders twice, and every render asks whether the held
    /// original is still good. Without this it would ask itself, forever.
    var capturingOriginal = false

    func undo() { log.undo(); if let s = history.undo() { apply(s) } }
    func redo() { log.redo(); if let s = history.redo() { apply(s) } }
    func jumpHistory(to index: Int) { if let s = history.jump(to: index) { apply(s) } }

    /// Names the control being changed, so history entries read like edits
    /// rather than like state dumps, and consecutive drags of one slider
    /// collapse into a single step.
    /// What the photographer did, as a runnable scenario. See
    /// `InteractionLog` — a report that names a *sequence* is the only kind
    /// this project has struggled to reproduce.
    let log = InteractionLog()

    func edit(_ label: String, _ change: () -> Void) {
        change()
        if !restoring {
            history.record(state, label: label)
            log.committed(state, label: label)
        }
    }

    /// The current controls as the C block.
    ///
    /// Factored out because auto-enhance has to send the engine the *whole*
    /// state to measure against, not just the fields it is going to change.
    func cAdjustments() -> OrionAdjustments {
        // Field assignment rather than the memberwise initializer, deliberately:
        // the memberwise form is positional over eighty arguments, so any change
        // to the C struct's shape breaks it wholesale — and a transposed pair of
        // same-typed floats would compile and ship. Assignment names every
        // field, and a field this file forgets stays zeroed rather than shifted.
        var a = OrionAdjustments()
        a.temperature_k = temperatureK; a.tint = tint
        a.exposure_ev = exposureEv; a.highlights = highlights; a.shadows = shadows
        a.whites = whites; a.blacks = blacks
        a.vibrance = vibrance; a.saturation = saturation; a.contrast = contrast
        a.rotate_quarters = rotateQuarters; a.straighten_deg = straightenDeg
        a.perspective_vertical = perspectiveVertical
        a.perspective_horizontal = perspectiveHorizontal
        a.perspective_aspect = perspectiveAspect
        a.crop_x = cropX; a.crop_y = cropY; a.crop_w = cropW; a.crop_h = cropH
        a.crop_preview = cropPreview ? 1 : 0
        a.preview_x = Float(previewCanvas.origin.x)
        a.preview_y = Float(previewCanvas.origin.y)
        a.preview_size = Float(previewCanvas.size)
        a.lens_distortion = lensDistortion; a.lens_vignette = lensVignette
        a.lens_ca_red = lensCaRed; a.lens_ca_blue = lensCaBlue
        a.lens_profile = (lensProfileEnabled && hasLensProfile) ? 1 : 0
        a.highlight_recovery = highlightRecovery
        a.grade_shadow = (gradeShadow[0], gradeShadow[1], gradeShadow[2])
        a.grade_midtone = (gradeMidtone[0], gradeMidtone[1], gradeMidtone[2])
        a.grade_highlight = (gradeHighlight[0], gradeHighlight[1], gradeHighlight[2])
        a.grade_balance = gradeBalance
        a.denoise_luma = denoiseLuma; a.denoise_color = denoiseColor
        a.lut_strength = lutStrength
        a.grain_amount = grainAmount; a.grain_size = grainSize
        a.vignette_amount = vignetteAmount
        a.vignette_field_angle = vignetteFieldAngle

        // The mask group. An empty list is a count of zero — not one live
        // component that happens to cover nothing.
        //
        // Swift imports the C array as a tuple, so there is no subscript to loop
        // over; the four are written by name. `withUnsafeMutablePointer` over the
        // tuple would compile and is how this gets written by accident, but the
        // tuple's layout is not a guaranteed C array and a stride mismatch would
        // scatter components into each other's fields.
        let cs = (0..<Self.maxMaskComponents).map { i -> OrionMaskComponent in
            guard i < maskComponents.count else { return OrionMaskComponent() }
            let m = maskComponents[i]
            var c = OrionMaskComponent()
            c.kind = m.kind
            c.compose = m.compose
            c.invert = m.invert ? 1 : 0
            c.hidden = m.hidden ? 1 : 0
            c.starts_layer = m.startsLayer ? 1 : 0
            c.center_x = m.centerX; c.center_y = m.centerY
            c.angle = m.angle; c.length = m.length
            c.radius_x = m.radiusX; c.radius_y = m.radiusY
            c.feather = m.feather; c.roundness = m.roundness
            c.range_lo = m.rangeLo; c.range_hi = m.rangeHi
            c.range_soft = m.rangeSoft
            c.color_r = m.colorR; c.color_g = m.colorG; c.color_b = m.colorB
            c.color_tol = m.colorTol; c.color_soft = m.colorSoft
            c.brush_radius = m.brushRadius; c.brush_flow = m.brushFlow
            c.brush_hardness = m.brushHardness
            c.brush_revision = brushRevisions[i]
            return c
        }
        a.mask_components = (cs[0], cs[1], cs[2], cs[3])
        a.mask_count = Int32(maskComponents.count)
        // Padded to the facade's fixed arrays: a stack with two layers still
        // sends four, and the engine reads only `layerCount` of them.
        var ev = [Float](repeating: 0, count: Self.maxMaskComponents)
        var ct = ev, sa = ev, wa = ev, ti = ev
        for (i, l) in layers.prefix(Self.maxMaskComponents).enumerated() {
            ev[i] = l.exposureEv; ct[i] = l.contrast; sa[i] = l.saturation
            wa[i] = l.warmth; ti[i] = l.tint
        }
        a.local_exposure_ev = (ev[0], ev[1], ev[2], ev[3])
        a.local_contrast = (ct[0], ct[1], ct[2], ct[3])
        a.local_saturation = (sa[0], sa[1], sa[2], sa[3])
        a.local_warmth = (wa[0], wa[1], wa[2], wa[3])
        a.local_tint = (ti[0], ti[1], ti[2], ti[3])
        a.mask_refine = maskRefine

        a.spot_count = Int32(min(spots.count, Self.maxSpots))
        withUnsafeMutableBytes(of: &a.spots) { raw in
            let p = raw.baseAddress!.assumingMemoryBound(to: OrionSpot.self)
            for (i, s) in spots.prefix(Self.maxSpots).enumerated() {
                p[i] = OrionSpot(dest_x: s.destX, dest_y: s.destY,
                                 src_x: s.srcX, src_y: s.srcY,
                                 radius: s.radius, feather: s.feather,
                                 heal: s.heal ? 1 : 0)
            }
        }
        a.mask_overlay = maskOverlay ? 1 : 0

        a.fusion = fusion; a.dehaze = dehaze; a.clarity = clarity
        a.sharpen_amount = sharpenAmount; a.sharpen_radius = sharpenRadius
        a.sharpen_masking = sharpenMasking
        a.hue_shift = toTuple8(hueShift)
        a.sat_shift = toTuple8(satShift)
        a.lum_shift = toTuple8(lumShift)
        a.curve_master = curveChannel(curve.master)
        a.curve_red = curveChannel(curve.red)
        a.curve_green = curveChannel(curve.green)
        a.curve_blue = curveChannel(curve.blue)
        return a
    }

    /// Measures the picture and sets the sliders auto-enhance is allowed to
    /// move. They land as ordinary edits — visible, adjustable and undoable —
    /// which is the whole point of it writing sliders rather than applying
    /// something of its own.
    func autoEnhance() {
        guard isLoaded, let handle else { return }
        var adj = cAdjustments()
        guard orion_engine_auto_enhance(handle, &adj) == ORION_OK else { return }

        // One history entry and one render for the whole button.
        //
        // ⚠️ It used to be five bare assignments, with a comment claiming that
        // re-rendering per assignment kept them on the same path an ordinary
        // edit takes. They were not on that path at all: an ordinary edit goes
        // through `edit(_:_:)`, which is what records history, and a bare
        // assignment records nothing. So Auto left no entry, and the next undo
        // stepped past it to the edit *before* it — which is why undoing Auto
        // appeared to throw away everything the photographer had done rather
        // than the automatic correction. Reported exactly that way.
        //
        // Suspended around the five so the frame is rendered once, at the end,
        // instead of four intermediate states nobody asked to see.
        edit("Auto") {
            suspended = true
            exposureEv = adj.exposure_ev
            blacks     = adj.blacks
            whites     = adj.whites
            fusion     = adj.fusion
            clarity    = adj.clarity
            suspended = false
            pushAndRender()
        }
    }

    /// A still of the developed image, drawn in place of the Metal canvas.
    ///
    /// Only the screenshot harness sets this: AppKit cannot capture a Metal
    /// layer, so a still is the only way to photograph the interface. It used
    /// to double as a stand-in during a photo switch, showing the camera's
    /// embedded JPEG until the render landed — but that preview carries its own
    /// orientation, so opening a portrait frame drew it landscape and then
    /// snapped. Holding the previous frame for the 26 ms decode is calmer than
    /// showing a picture that turns.
    internal(set) var placeholder: NSImage?

    var histogramTask: Task<Void, Never>?

    /// Swift imports a C float[8] as a 8-tuple, and there is no nicer bridge.
    /// A curve channel into its C form. The engine treats anything malformed
    /// as the identity, so truncating past the cap here is safe rather than
    /// silently wrong.
    private func curveChannel(_ points: [CurvePoint]) -> OrionCurveChannel {
        var c = OrionCurveChannel()
        let n = min(points.count, 16)
        c.count = Int32(n)
        withUnsafeMutableBytes(of: &c.x) { raw in
            let f = raw.bindMemory(to: Float.self)
            for i in 0..<n { f[i] = points[i].x }
        }
        withUnsafeMutableBytes(of: &c.y) { raw in
            let f = raw.bindMemory(to: Float.self)
            for i in 0..<n { f[i] = points[i].y }
        }
        return c
    }

    private func toTuple8(_ a: [Float]) -> (Float, Float, Float, Float,
                                            Float, Float, Float, Float) {
        let v = a.count >= 8 ? a : a + [Float](repeating: 0, count: 8 - a.count)
        return (v[0], v[1], v[2], v[3], v[4], v[5], v[6], v[7])
    }

    func errorText(_ status: OrionStatus) -> String {
        guard let handle else { return String(cString: orion_status_string(status)) }
        let detail = String(cString: orion_last_error(handle))
        return detail.isEmpty ? String(cString: orion_status_string(status)) : detail
    }
}
