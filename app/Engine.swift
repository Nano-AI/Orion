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

    private(set) var camera = ""
    private(set) var imageWidth: UInt32 = 0
    private(set) var imageHeight: UInt32 = 0

    /// The whole frame after rotation, before any crop. The crop rectangle is
    /// normalized against this, and the crop tool's canvas is sized from it —
    /// `imageWidth` cannot stand in, because it is the *cropped* result.
    private(set) var frameWidth: UInt32 = 0
    private(set) var frameHeight: UInt32 = 0

    /// The frame's width over its height, which is what every rotated-bounds
    /// calculation needs.
    var frameAspect: CGFloat {
        guard frameWidth > 0, frameHeight > 0 else { return 1 }
        return CGFloat(frameWidth) / CGFloat(frameHeight)
    }
    private(set) var lastRenderMs: Double = 0
    private(set) var isLoaded = false

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

    /// The region the crop preview renders, in crop coordinates. The overlay
    /// draws against the same numbers the engine renders from, which is the
    /// only way the white rectangle can be trusted to sit on the pixels.
    var previewCanvas: (origin: CGPoint, size: CGFloat) {
        guard cropPreview else { return (.zero, 1) }
        return CanvasLayout.previewCanvas(frameAspect: frameAspect,
                                          angleDeg: CGFloat(straightenDeg))
    }

    /// Sets the crop in one push, so a drag is a single render rather than four.
    func setCrop(x: Float, y: Float, w: Float, h: Float) {
        guard isLoaded else { return }
        let r = CanvasLayout.constrainedCrop(
            CGRect(x: CGFloat(x), y: CGFloat(y), width: CGFloat(w), height: CGFloat(h)),
            frameAspect: frameAspect, angleDeg: CGFloat(straightenDeg))

        suspended = true
        cropW = Float(r.width); cropH = Float(r.height)
        cropX = Float(r.origin.x); cropY = Float(r.origin.y)
        suspended = false
        pushAndRender()
    }

    /// Pulls the crop back inside the frame after the angle or the rotation
    /// changed under it. Silent — it runs on every angle tick.
    private func constrainCrop() {
        guard isLoaded, !constraining else { return }
        constraining = true
        defer { constraining = false }

        let r = CanvasLayout.constrainedCrop(
            CGRect(x: CGFloat(cropX), y: CGFloat(cropY),
                   width: CGFloat(cropW), height: CGFloat(cropH)),
            frameAspect: frameAspect, angleDeg: CGFloat(straightenDeg))

        let was = suspended
        suspended = true
        cropW = Float(r.width); cropH = Float(r.height)
        cropX = Float(r.origin.x); cropY = Float(r.origin.y)
        suspended = was
    }

    private var constraining = false

    /// Records one history entry when a crop drag finishes, rather than one per
    /// frame of the drag.
    func commitCropEdit() {
        history.record(state, label: "Crop")
        log.committed(state, label: "Crop")
    }

    /// Moves the crop without changing its size, clamped to the frame.
    func moveCrop(dx: Float, dy: Float) {
        guard isLoaded else { return }
        suspended = true
        cropX = min(max(cropX + dx, 0), 1 - cropW)
        cropY = min(max(cropY + dy, 0), 1 - cropH)
        suspended = false
        pushAndRender()
    }
    var cropX: Float = 0           { didSet { pushAndRender() } }
    var cropY: Float = 0           { didSet { pushAndRender() } }
    var cropW: Float = 1           { didSet { pushAndRender() } }
    var cropH: Float = 1           { didSet { pushAndRender() } }

    /// Common crop ratios. nil is freeform.
    func setAspect(_ ratio: Float?) {
        guard isLoaded else { return }
        guard let ratio else { return }

        // Fit the largest rectangle of this ratio inside the current frame,
        // centered, so choosing a ratio never crops information the user has
        // not asked to lose beyond what the ratio requires.
        let frame = Float(frameAspect)
        var w: Float = 1, h: Float = 1
        if ratio > frame { h = frame / ratio } else { w = ratio / frame }

        suspended = true
        cropW = w; cropH = h
        cropX = (1 - w) / 2; cropY = (1 - h) / 2
        suspended = false
        pushAndRender()
    }

    func resetCrop() {
        suspended = true
        cropX = 0; cropY = 0; cropW = 1; cropH = 1; straightenDeg = 0
        suspended = false
        guard isLoaded else { return }
        pushAndRender()
    }

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
    private(set) var lensProfileName = ""
    private(set) var lensProfileApproximate = false
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

    private(set) var maskComponents: [MaskComponentState] = []

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

    private(set) var spots: [SpotState] = []

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

    /// Places a spot at a point on the displayed picture.
    ///
    /// ⚠ The source is chosen automatically, and the rule is Orion's own: the
    /// same distance away as two and a half radii, along whichever axis has
    /// room. Lightroom searches for a matching patch; that is a different and
    /// much larger feature, and for sensor dust — which sits on smooth
    /// backgrounds by definition — anywhere nearby is as good as anywhere else.
    /// The photographer can see the result immediately and place it again if
    /// the guess was poor.
    @discardableResult
    func addSpot(atFrame displayed: CGPoint) -> Bool {
        guard isLoaded, let handle, spots.count < Self.maxSpots else { return false }

        // ⚠ Converted here, once, rather than on every render. Dust is on the
        // sensor, so a spot has to follow the subject through a later crop or
        // quarter turn — the opposite of a mask, which stays where it was put on
        // screen. Same transform, applied at a different moment, and that
        // moment is the whole difference between the two behaviors.
        var fx: Float = 0, fy: Float = 0
        guard orion_engine_to_frame(handle, Float(displayed.x), Float(displayed.y),
                                    &fx, &fy) == ORION_OK else { return false }
        let p = CGPoint(x: CGFloat(fx), y: CGFloat(fy))

        let step = spotRadius * 2.5
        var sx = Float(p.x) + step
        if sx > 1 - spotRadius { sx = Float(p.x) - step }
        var sy = Float(p.y)
        // Nowhere to go sideways on a very small frame; try the other axis.
        if sx < spotRadius || sx > 1 - spotRadius {
            sx = Float(p.x)
            sy = Float(p.y) + step
            if sy > 1 - spotRadius { sy = Float(p.y) - step }
        }

        var s = SpotState()
        s.destX = Float(p.x); s.destY = Float(p.y)
        s.srcX = min(max(sx, 0), 1); s.srcY = min(max(sy, 0), 1)
        s.radius = spotRadius
        s.feather = spotFeather
        s.heal = spotHeal
        spots.append(s)
        selectedSpot = spots.count - 1
        pushAndRender()
        history.record(state, label: "Spot"); log.committed(state, label: "Spot")
        return true
    }

    /// A point in frame coordinates, as a point on the displayed picture.
    /// The inverse of the transform `addSpot` uses.
    func displayedPoint(fromFrame p: CGPoint) -> CGPoint? {
        guard isLoaded, let handle else { return nil }
        var x: Float = 0, y: Float = 0
        guard orion_engine_from_frame(handle, Float(p.x), Float(p.y),
                                      &x, &y) == ORION_OK else { return nil }
        return CGPoint(x: CGFloat(x), y: CGFloat(y))
    }

    /// Which spot the canvas has selected, or -1. View state: which handle is
    /// lit is not part of the photograph.
    var selectedSpot: Int = -1

    /// The spots as the canvas handles them, in displayed coordinates.
    ///
    /// ⚠ Converted here and nowhere else. `CanvasLayout` and the overlay work
    /// entirely in displayed coordinates; giving either of them the frame
    /// transform would be a second copy of it.
    var spotPlacements: [CanvasLayout.SpotPlacement] {
        spots.compactMap { s in
            guard let d = displayedPoint(fromFrame: CGPoint(x: CGFloat(s.destX),
                                                            y: CGFloat(s.destY))),
                  let src = displayedPoint(fromFrame: CGPoint(x: CGFloat(s.srcX),
                                                              y: CGFloat(s.srcY)))
            else { return nil }
            return CanvasLayout.SpotPlacement(destination: d, source: src,
                                              radius: CGFloat(s.radius),
                                              heal: s.heal)
        }
    }

    /// Moves a spot's destination or its source, from a displayed point.
    ///
    /// Renders without recording, exactly as `setCrop` does — a drag is one
    /// history entry, committed on release, not sixty.
    func moveSpot(_ index: Int, destination: CGPoint?, source: CGPoint?) {
        guard isLoaded, let handle, spots.indices.contains(index) else { return }
        func toFrame(_ p: CGPoint) -> (Float, Float)? {
            var fx: Float = 0, fy: Float = 0
            guard orion_engine_to_frame(handle, Float(p.x), Float(p.y),
                                        &fx, &fy) == ORION_OK else { return nil }
            return (fx, fy)
        }
        if let d = destination, let (fx, fy) = toFrame(d) {
            spots[index].destX = fx; spots[index].destY = fy
        }
        if let src = source, let (fx, fy) = toFrame(src) {
            spots[index].srcX = fx; spots[index].srcY = fy
        }
        pushAndRender()
    }

    /// One history entry when a spot drag finishes.
    ///
    /// ⚠ **Labelled "Move spot", not "Spot".** `EditHistory` coalesces
    /// consecutive entries carrying the same label — which is what makes a
    /// slider drag one undo step instead of sixty — so a placement and a later
    /// move both labeled "Spot" merged into one entry, and undoing the move
    /// deleted the spot instead. Two different acts need two different names.
    ///
    /// The place-and-drag gesture is deliberately *not* two entries: `addSpot`
    /// has already recorded, and the overlay skips this call while the spot it
    /// is dragging the source of is the one it just created.
    func commitSpotEdit() { history.record(state, label: "Move spot"); log.committed(state, label: "Move spot") }

    /// Removes one spot by index, which is what a selected spot and a Delete
    /// key mean. `removeLastSpot` stays for the panel's Undo spot button.
    func removeSpot(_ index: Int) {
        guard spots.indices.contains(index) else { return }
        spots.remove(at: index)
        selectedSpot = -1
        pushAndRender()
        history.record(state, label: "Spot"); log.committed(state, label: "Spot")
    }

    func removeLastSpot() {
        guard !spots.isEmpty else { return }
        spots.removeLast()
        pushAndRender()
        history.record(state, label: "Spot"); log.committed(state, label: "Spot")
    }

    func clearSpots() {
        guard !spots.isEmpty else { return }
        spots.removeAll()
        pushAndRender()
        history.record(state, label: "Clear spots")
    }

    /// One local adjustment set per layer.
    private(set) var layers: [LocalAdjustState] = [LocalAdjustState()]

    /// ⚠ **Derived, not stored.** Which layer a component belongs to is how
    /// many layer boundaries precede it, and that moves whenever a row is
    /// added, removed or reordered. Storing it would be a second copy of the
    /// grouping, and the two would disagree the first time a row moved.
    var selectedLayer: Int {
        guard !maskComponents.isEmpty else { return 0 }
        let upTo = min(max(selectedMask, 0), maskComponents.count - 1)
        // ⚠ `1...upTo` is an invalid range when `upTo` is zero, and Swift traps
        // on it rather than producing an empty sequence — selecting the first
        // row crashed the process. A half-open range over a prefix has no such
        // edge, which is why it is written this way now.
        var n = 0
        for m in maskComponents[..<upTo].dropFirst() where m.startsLayer { n += 1 }
        if upTo > 0 && maskComponents[upTo].startsLayer { n += 1 }
        return min(n, Engine.maxMaskComponents - 1)
    }

    /// How many layers the stack has.
    var layerCount: Int {
        guard !maskComponents.isEmpty else { return 0 }
        return maskComponents.dropFirst().reduce(1) { $1.startsLayer ? $0 + 1 : $0 }
    }

    private func editLayer(_ change: (inout LocalAdjustState) -> Void) {
        let i = selectedLayer
        while layers.count <= i { layers.append(LocalAdjustState()) }
        change(&layers[i])
        pushAndRender()
    }
    private var layer: LocalAdjustState {
        let i = selectedLayer
        return layers.indices.contains(i) ? layers[i] : LocalAdjustState()
    }

    /// The selected layer's adjustments, as the panel binds them.
    var localExposureEv: Float {
        get { layer.exposureEv }
        set { editLayer { $0.exposureEv = newValue } }
    }
    var localContrast: Float {
        get { layer.contrast }
        set { editLayer { $0.contrast = newValue } }
    }
    var localSaturation: Float {
        get { layer.saturation }
        set { editLayer { $0.saturation = newValue } }
    }
    /// ⚠ A color **cast**, not a white balance. Temperature is applied in
    /// `linearize`, before the demosaic, so a local one would mean demosaicing
    /// the frame twice. Named Warmth and Tint so the two are not confused.
    var localWarmth: Float {
        get { layer.warmth }
        set { editLayer { $0.warmth = newValue } }
    }
    var localTint: Float {
        get { layer.tint }
        set { editLayer { $0.tint = newValue } }
    }

    /// The selected component, or nil when the group is empty.
    private var selected: MaskComponentState? {
        guard selectedMask >= 0 && selectedMask < maskComponents.count else { return nil }
        return maskComponents[selectedMask]
    }

    /// Edits the selected component in place and pushes once.
    ///
    /// Every `mask…` setter routes through here rather than mutating the array
    /// directly, so there is one place that knows what "selected" means and one
    /// place that renders.
    private func editSelected(_ change: (inout MaskComponentState) -> Void) {
        guard selectedMask >= 0 && selectedMask < maskComponents.count else { return }
        change(&maskComponents[selectedMask])
        pushAndRender()
    }

    /// The selected component's primitive. Zero means the group is empty.
    ///
    /// Setting it off zero on an empty group *adds* a component, and setting it
    /// to zero removes the selected one — so the existing segmented picker keeps
    /// meaning what it did before there were rows, and "No mask" on the only row
    /// still clears the mask rather than leaving a live component covering
    /// nothing.
    var maskKind: Int32 {
        get { selected?.kind ?? 0 }
        set {
            if newValue == 0 {
                if !maskComponents.isEmpty { removeMaskComponent(at: selectedMask) }
                return
            }
            if maskComponents.isEmpty {
                addMaskComponent(kind: newValue)
            } else {
                editSelected { $0.kind = newValue }
            }
        }
    }

    var maskInvert: Bool {
        get { selected?.invert ?? false }
        set { editSelected { $0.invert = newValue } }
    }
    /// 0 add, 1 subtract, 2 intersect. Meaningless on the first row — the fold
    /// starts from zero, so subtract or intersect there gives an empty group.
    var maskCompose: Int32 {
        get { selected?.compose ?? 0 }
        set { editSelected { $0.compose = newValue } }
    }
    var maskCenterX: Float {
        get { selected?.centreX ?? 0.5 }
        set { editSelected { $0.centreX = newValue } }
    }
    var maskCenterY: Float {
        get { selected?.centreY ?? 0.5 }
        set { editSelected { $0.centreY = newValue } }
    }
    var maskAngle: Float {
        get { selected?.angle ?? 0 }
        set { editSelected { $0.angle = newValue } }
    }
    var maskLength: Float {
        get { selected?.length ?? 0.5 }
        set { editSelected { $0.length = newValue } }
    }
    var maskRadiusX: Float {
        get { selected?.radiusX ?? 0.3 }
        set { editSelected { $0.radiusX = newValue } }
    }
    var maskRadiusY: Float {
        get { selected?.radiusY ?? 0.3 }
        set { editSelected { $0.radiusY = newValue } }
    }
    var maskFeather: Float {
        get { selected?.feather ?? 0.5 }
        set { editSelected { $0.feather = newValue } }
    }
    var maskRoundness: Float {
        get { selected?.roundness ?? 2 }
        set { editSelected { $0.roundness = newValue } }
    }

    // ── The luminance range (mask kind 5) ─────────────────────────────────
    var maskRangeLo: Float {
        get { selected?.rangeLo ?? -2 }
        set { editSelected { $0.rangeLo = newValue } }
    }
    var maskRangeHi: Float {
        get { selected?.rangeHi ?? 2 }
        set { editSelected { $0.rangeHi = newValue } }
    }
    var maskRangeSoft: Float {
        get { selected?.rangeSoft ?? 1 }
        set { editSelected { $0.rangeSoft = newValue } }
    }

    // ── The color range (mask kind 6) ────────────────────────────────────
    var maskColorTol: Float {
        get { selected?.colourTol ?? 0.10 }
        set { editSelected { $0.colourTol = newValue } }
    }
    var maskColorSoft: Float {
        get { selected?.colourSoft ?? 0.05 }
        set { editSelected { $0.colourSoft = newValue } }
    }

    /// The picked shade, scene-linear Rec.2020 RGB. Read for the panel's
    /// swatch; written only by `pickMaskColor`.
    var maskColor: (r: Float, g: Float, b: Float) {
        guard let m = selected else { return (0.18, 0.18, 0.18) }
        return (m.colourR, m.colourG, m.colourB)
    }

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
    private(set) var maskColorSwatch: (r: Double, g: Double, b: Double)?

    /// Takes the color under a click on the displayed picture into the
    /// selected component. Returns false when there was nothing to sample.
    ///
    /// ⚠ The **scene** color, not what is on screen: reading the edited result
    /// would mean shifting hue through the mask changes what the mask selects.
    /// Same texture the color-mixer eyedropper reads, and the same reason.
    ///
    /// That value arrives normalized by its own peak, which does not matter
    /// here and is the point of the metric: Oklab chromaticity is exactly
    /// invariant under a multiply, so a normalized target and an unnormalized
    /// pixel land in the same place. research/masking.md §4c.
    @discardableResult
    func pickMaskColor(at displayed: CGPoint) -> Bool {
        guard isLoaded, selected != nil,
              let s = sample(u: Float(displayed.x), v: Float(displayed.y))
        else { return false }
        edit("Mask color") {
            editSelected {
                $0.colourR = Float(s.scene.r)
                $0.colourG = Float(s.scene.g)
                $0.colourB = Float(s.scene.b)
            }
        }
        maskColorSwatch = s.display
        return true
    }

    // ── The brush (mask kind 3) ───────────────────────────────────────────
    var brushRadius: Float {
        get { selected?.brushRadius ?? 0.08 }
        set { editSelected { $0.brushRadius = newValue } }
    }
    var brushFlow: Float {
        get { selected?.brushFlow ?? 0.5 }
        set { editSelected { $0.brushFlow = newValue } }
    }
    var brushHardness: Float {
        get { selected?.brushHardness ?? 0.5 }
        set { editSelected { $0.brushHardness = newValue } }
    }

    /// Appends a component and selects it. Returns false when the group is full.
    ///
    /// A new row composes with `add` — the only op that does anything on a first
    /// row, and the one a photographer means by "and also this".
    @discardableResult
    func addMaskComponent(kind: Int32 = 1) -> Bool {
        guard maskComponents.count < Self.maxMaskComponents else { return false }
        var m = MaskComponentState()
        m.kind = kind
        m.compose = 0
        maskComponents.append(m)
        selectedMask = maskComponents.count - 1
        pushAndRender()
        return true
    }

    /// Removes a component, keeping the strokes of the ones after it with them.
    ///
    /// The engine indexes strokes by component, so removing row 1 of three has
    /// to shift row 2's paint down with it — otherwise the surviving component
    /// renders the removed one's stroke. `pushStrokes` re-sends every one.
    func removeMaskComponent(at index: Int) {
        guard index >= 0 && index < maskComponents.count else { return }
        maskComponents.remove(at: index)
        selectedMask = maskComponents.isEmpty ? 0 : min(index, maskComponents.count - 1)
        pushStrokes()
        pushAndRender()
    }

    /// The selected component's stroke, as normalized points on the displayed
    /// picture.
    var brushStroke: [CGPoint] {
        guard let m = selected else { return [] }
        return Self.points(m.brushStroke)
    }

    /// Pairs a flattened stroke back into points. An odd count is a truncated
    /// sidecar; drop the stray rather than reading past the end.
    private static func points(_ xy: [Float]) -> [CGPoint] {
        var pts: [CGPoint] = []
        pts.reserveCapacity(xy.count / 2)
        for i in stride(from: 0, to: xy.count - 1, by: 2) {
            pts.append(CGPoint(x: CGFloat(xy[i]), y: CGFloat(xy[i + 1])))
        }
        return pts
    }

    /// Bumped per component whenever that component's stroke changes.
    ///
    /// The revision is the only thing the engine compares — it never walks a
    /// stroke to find out whether it moved. Change the points without the
    /// revision and the picture does not follow the hand, which is why the two
    /// are set together here rather than being left to callers.
    ///
    /// Not in `DevelopState`: it is a staleness token, not an edit, so it must
    /// not reach the sidecar or undo.
    private var brushRevisions = [UInt32](repeating: 0, count: Engine.maxMaskComponents)

    /// The selected component's per-dab polarity, as booleans.
    var brushErasePolarity: [Bool] {
        (selected?.brushErase ?? []).map { $0 != 0 }
    }

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
    @ObservationIgnored private var liveStroke: [Float] = []
    @ObservationIgnored private var liveErase: [Float] = []
    /// Which component the live stroke belongs to; -1 when no stroke is down.
    @ObservationIgnored private var liveIndex = -1

    /// The press. Seeds the buffers from whatever paint is already on the
    /// component, so a second pass builds on the first.
    func beginBrushStroke() {
        guard selectedMask >= 0 && selectedMask < maskComponents.count else { return }
        liveIndex = selectedMask
        liveStroke = maskComponents[liveIndex].brushStroke
        liveErase = maskComponents[liveIndex].brushErase
        // ⚠ Padded here rather than trusted, for the same reason `pushStroke`
        // pads: the two arrays reach the sidecar separately and a file that
        // predates erasing has a stroke and no polarity.
        let dabs = liveStroke.count / 2
        if liveErase.count < dabs {
            liveErase += [Float](repeating: 0, count: dabs - liveErase.count)
        }
    }

    /// One pointer event's worth of dabs. Appends; never rebuilds.
    func appendBrushDabs(_ points: [CGPoint], erasing: Bool) {
        guard liveIndex >= 0, !points.isEmpty else { return }
        liveStroke.reserveCapacity(liveStroke.count + points.count * 2)
        for p in points {
            liveStroke.append(Float(p.x))
            liveStroke.append(Float(p.y))
        }
        liveErase.append(contentsOf:
            repeatElement(erasing ? 1 : 0, count: points.count))
        pushLiveStroke()
    }

    /// The release. One observable write, and the record is correct again.
    ///
    /// ⚠ Returns whether anything was painted, so the caller can skip the
    /// history entry for a press that laid nothing — recording that would put a
    /// step in the history that undoes to itself.
    @discardableResult
    func endBrushStroke() -> Bool {
        guard liveIndex >= 0 else { return false }
        let index = liveIndex
        liveIndex = -1
        let changed = maskComponents[index].brushStroke != liveStroke
        if changed {
            maskComponents[index].brushStroke = liveStroke
            maskComponents[index].brushErase = liveErase
        }
        liveStroke = []; liveErase = []
        return changed
    }

    /// Sends the live buffers across the facade and renders. `pushAndRender`
    /// minus `onEdit`, and the omission is the whole optimisation.
    ///
    /// ⚠ **The adjustment block still has to go, and the first draft of this
    /// skipped it.** A stroke's dabs travel by their own call, so skipping the
    /// block looked free — but `brush_revision` rides *in* the block, and it is
    /// the only thing the engine compares to decide the mask node is stale (it
    /// never walks a stroke to find out). Uploading dabs without it left the
    /// node clean: the kernel never re-ran, `render()` returned in microseconds
    /// having done nothing, and the paint simply did not appear. It measured
    /// **0.0 ms an event at 45,000 fps**, which is what "did no work" looks
    /// like when you are hoping for "fast".
    ///
    /// `repro/gesture-preview-agrees.txt` caught it, on the check that the
    /// settled picture is the same armed or not — written the day before for a
    /// different reason entirely.
    ///
    /// What *is* safe to skip is `onEdit?(state)`, and that is where the cost
    /// was: it builds a whole `DevelopState` — copying the stroke it was just
    /// handed — and gives it to `Autosave.note`, whose first act is
    /// `state != saved`, a full structural compare of the same arrays. The
    /// autosave's business is the finished stroke, and `endBrushStroke`
    /// delivers one through the normal observable path.
    private func pushLiveStroke() {
        guard isLoaded, !suspended, let handle, liveIndex >= 0 else { return }
        let dabs = liveStroke.count / 2
        guard dabs > 0 else { return }
        let ok: OrionStatus = liveStroke.withUnsafeBufferPointer { p in
            liveErase.withUnsafeBufferPointer { e in
                orion_engine_set_brush_stroke(handle, Int32(liveIndex),
                                              p.baseAddress, e.baseAddress,
                                              Int32(dabs))
            }
        }
        guard ok == ORION_OK else { return }
        brushRevisions[liveIndex] &+= 1

        var adj = cAdjustments()
        orion_engine_set_adjustments(handle, &adj)
        if interacting { renderPreview() } else { render() }
    }

    func setBrushStroke(_ points: [CGPoint], erasing: [Bool]? = nil) {
        guard selectedMask >= 0 && selectedMask < maskComponents.count else { return }
        maskComponents[selectedMask].brushStroke =
            points.flatMap { [Float($0.x), Float($0.y)] }
        if let erasing {
            maskComponents[selectedMask].brushErase = erasing.map { $0 ? 1 : 0 }
        } else {
            maskComponents[selectedMask].brushErase =
                [Float](repeating: 0, count: points.count)
        }
        guard pushStroke(selectedMask) else { return }
        pushAndRender()
    }

    /// Sends one component's stroke across the facade and bumps its revision.
    @discardableResult
    private func pushStroke(_ index: Int) -> Bool {
        guard isLoaded, let handle,
              index >= 0 && index < maskComponents.count else { return false }
        let xy = maskComponents[index].brushStroke

        // An empty stroke is a real state — it is what clearing the brush
        // means — so pass the null the facade documents rather than a dangling
        // pointer into an empty array.
        // ⚠ Padded to the dab count rather than trusted. The two arrays are
        // written to the sidecar separately, so a file edited by hand — or by a
        // build between these two changes — can arrive with a stroke and no
        // polarity for the tail of it. Short means "paints", which is the same
        // thing an absent array means.
        var erase = maskComponents[index].brushErase
        let dabs = xy.count / 2
        if erase.count < dabs { erase += [Float](repeating: 0, count: dabs - erase.count) }

        let status: OrionStatus = xy.isEmpty
            ? orion_engine_set_brush_stroke(handle, Int32(index), nil, nil, 0)
            : xy.withUnsafeBufferPointer { p in
                  erase.withUnsafeBufferPointer { e in
                      orion_engine_set_brush_stroke(handle, Int32(index),
                                                    p.baseAddress, e.baseAddress,
                                                    Int32(dabs))
                  }
              }
        guard status == ORION_OK else { return false }
        brushRevisions[index] &+= 1
        return true
    }

    /// Re-sends every component's stroke. Needed whenever the *indexing*
    /// changes rather than the paint — a restore, or a removal that shifts the
    /// rows after it down.
    private func pushStrokes() {
        for i in 0..<Self.maxMaskComponents {
            if i < maskComponents.count {
                pushStroke(i)
            } else if isLoaded, let handle {
                // Clear the tail, or a component removed from a group of three
                // leaves its paint in the engine for the next one added to
                // inherit.
                if orion_engine_set_brush_stroke(handle, Int32(i), nil, nil, 0) == ORION_OK {
                    brushRevisions[i] &+= 1
                }
            }
        }
    }

    /// Total clockwise quarter turns — the camera's own EXIF orientation plus
    /// the user's rotation. A matte producer needs it; see `MatteGeometry`.
    var quarterTurns: Int {
        guard let handle, isLoaded else { return 0 }
        var t: Int32 = 0
        guard orion_engine_quarter_turns(handle, &t) == ORION_OK else { return 0 }
        return Int(t)
    }

    /// The developed picture with its geometry neutralised, for something that
    /// wants to *analyze* it rather than show it — today, segmentation.
    ///
    /// ⚠ The crop, the straighten and the user's rotation are reset around the
    /// render, so the only difference between what comes back and the frame the
    /// mask kernel works in is the EXIF quarter turn — which is an exact
    /// permutation and needs no resample. Neutralising rather than correcting
    /// is what stops a matte having no data outside the crop rectangle.
    /// research/masking.md §5.
    ///
    /// Two renders and a readback, and it restores the caller's state exactly —
    /// the same shape as `captureOriginal`, for the same reason.
    func renderForAnalysis(longEdge: Int) -> CGImage? {
        guard isLoaded else { return nil }

        let held = state
        var neutral = held
        neutral.cropX = 0; neutral.cropY = 0
        neutral.cropW = 1; neutral.cropH = 1
        neutral.straightenDeg = 0
        neutral.rotateQuarters = 0
        // ⚠ And the perspective, for exactly the reason the other three are
        // here: a raster matte is stored in FRAME coordinates, and a model
        // handed a keystone-corrected picture would return a selection that
        // fits the corrected picture and nothing else. The mask kernel does no
        // correction of its own by design, so the correction has to be off on
        // the way in.
        neutral.perspectiveVertical = 0
        neutral.perspectiveHorizontal = 0
        neutral.perspectiveAspect = 0
        let heldPreview = cropPreview

        // ⚠ And the coverage overlay, for the same reason `export` turns it
        // off: it tints the render red wherever the group covers, and this
        // render is fed to a segmentation model. "Show mask" is on exactly when
        // a photographer is working with masks, which is exactly when they
        // press Subject — so the failure is the common case, not a corner, and
        // it would look like the model being bad at this photograph.
        let heldOverlay = maskOverlay

        maskOverlay = false
        cropPreview = false
        apply(neutral)
        defer {
            apply(held)
            cropPreview = heldPreview
            maskOverlay = heldOverlay
        }

        let size = MatteGeometry.previewSize(frameWidth: Int(imageWidth),
                                             frameHeight: Int(imageHeight),
                                             longEdge: longEdge)
        guard size.width > 0, size.height > 0 else { return nil }
        return Screenshot.developedCGImage(self, fitting: size)
    }

    /// Uploads a raster matte for the selected component — kind 4,
    /// research/masking.md §5.
    ///
    /// ⚠ `alpha` is in **frame** coordinates: the whole uncropped, unturned
    /// frame, row-major from the top left. Not the displayed picture. A
    /// producer that worked from what is on screen has to undo the geometry
    /// first — the engine does no correction, which is exactly what lets a
    /// matte stay on its subject through a crop and a quarter turn.
    ///
    /// Returns false if the matte is larger than the engine will take, which is
    /// reported rather than silently downscaled.
    /// `index` defaults to the selected row, which is what every gesture means.
    /// Restoring a saved matte passes one explicitly: it walks the whole list on
    /// open, and moving the selection to each row in turn would be a visible
    /// side effect of loading a file.
    @discardableResult
    func setMaskMatte(_ alpha: [Float], width: Int, height: Int,
                      at index: Int? = nil) -> Bool {
        let which = index ?? selectedMask
        guard let handle, isLoaded,
              which >= 0, which < maskComponents.count else { return false }
        let status = alpha.withUnsafeBufferPointer {
            orion_engine_set_mask_matte(handle, Int32(which),
                                        $0.baseAddress, Int32(width), Int32(height))
        }
        guard status == ORION_OK else { return false }
        // The matte lives outside `Adjustments`, like a brush stroke, so
        // nothing above has been dirtied yet.
        pushAndRender()
        return true
    }

    /// Records which saved file holds the selected row's matte, and which
    /// producer made it. `MatteStore` writes the file first and calls this with
    /// the id it got back, so the state never names a file that is not there.
    func setMatteReference(id: String?, source: String?) {
        editSelected { $0.matteId = id; $0.matteSource = source }
        missingMattes.remove(selectedMask)
    }

    /// Uploads every saved matte the restored state names, and records the rows
    /// whose file could not be read.
    ///
    /// ⚠ A missing file is **recorded, not swallowed**. Leaving the row with no
    /// coverage would change how the picture looks with nothing on screen
    /// saying why — the same shape as every silent-output defect this project
    /// has paid for. The panel reads `missingMattes` and says so.
    ///
    /// Called after `restore`, because it walks the components that call put
    /// there.
    func restoreMattes(photo: URL) {
        var missing: Set<Int> = []
        for (i, c) in maskComponents.enumerated() {
            guard c.kind == 4, let id = c.matteId else { continue }
            guard let m = MatteStore.read(photo: photo, id: id) else {
                missing.insert(i); continue
            }
            if !setMaskMatte(m.alpha, width: m.width, height: m.height, at: i) {
                missing.insert(i)
            }
        }
        missingMattes = missing
    }

    /// The label to describe the selected row's matte, if it has one.
    var maskMatteSource: String? { selected?.matteSource }

    /// Whether the selected row's matte is on disk beside the photograph.
    ///
    /// ⚠ Separate from `maskMatteSource` on purpose. A matte can exist in the
    /// engine and not yet in a file — the screenshot harness makes one that
    /// way, and so does any run with no photograph open — and a caption that
    /// read the label and then promised the file would be claiming a save that
    /// did not happen. That is the class of lie this panel's copy exists to
    /// avoid, not to commit.
    var maskMatteSaved: Bool { selected?.matteId != nil }

    /// True when the selected row is a matte whose file could not be read.
    var maskMatteMissing: Bool { missingMattes.contains(selectedMask) }

    /// The largest matte this image will accept.
    var maxMatteSize: (width: Int, height: Int) {
        guard let handle, isLoaded else { return (0, 0) }
        var w: UInt32 = 0, h: UInt32 = 0
        guard orion_engine_max_matte_size(handle, &w, &h) == ORION_OK else { return (0, 0) }
        return (Int(w), Int(h))
    }

    /// One history entry when a brush stroke finishes, rather than one per dab.
    func commitBrushEdit() { history.record(state, label: "Brush"); log.committed(state, label: "Brush") }

    /// Paint the mask's coverage over the picture, so it can be placed by eye.
    ///
    /// Not part of `DevelopState` on purpose: it is how you are *looking* at
    /// the photo, not an edit to it, so it must not land in the sidecar, must
    /// not enter undo history, and must not follow the photo to another
    /// machine. `export` forces it off around the write for the same reason.
    var maskOverlay = false { didSet { log.overlay(maskOverlay); pushAndRender() } }

    /// Wipes the selected component's stroke. Undoable, because painting for a
    /// minute and losing it to a misclick is not a thing a photographer should
    /// have to fear.
    func clearBrushStroke() {
        guard !brushStroke.isEmpty else { return }
        setBrushStroke([])
        history.record(state, label: "Clear brush"); log.committed(state, label: "Clear brush")
    }

    /// The selected row's visibility, for the panel and the scenario runner.
    var maskHidden: Bool {
        get { selected?.hidden ?? false }
        set { editSelected { $0.hidden = newValue } }
    }

    /// The eye button. View-ish but genuinely part of the edit: a hidden mask
    /// is a decision about the photograph, so it round-trips through the
    /// sidecar and undo like anything else.
    func toggleMaskHidden(_ index: Int) {
        guard maskComponents.indices.contains(index) else { return }
        let label = maskComponents[index].hidden ? "Show mask row" : "Hide mask row"
        edit(label) {
            maskComponents[index].hidden.toggle()
            // ⚠ **The render is not implied.** `edit` runs the change and
            // records history; it does not push. Every other control gets its
            // render from a `didSet` on the property it writes, and
            // `maskComponents` has none — so mutating a row inside `edit` and
            // stopping there changes the model, the sidecar and the undo stack
            // and leaves the picture exactly as it was. The eye toggled, the
            // list dimmed, and the photograph did not move.
            //
            // Decision #67 records the same shape from the other direction: a
            // bare assignment that renders but records nothing. This is the
            // mirror of it, and it is why `editSelected` calls this explicitly.
            pushAndRender()
        }
    }

    /// Starts or ends a layer at this row.
    ///
    /// ⚠ Row 1 always begins one — a stack has to start somewhere, and a first
    /// row that could be "continue" would continue from nothing.
    func toggleLayerBreak(at index: Int) {
        guard maskComponents.indices.contains(index), index > 0 else { return }
        let starting = maskComponents[index].startsLayer
        edit(starting ? "Merge layer" : "Split layer") {
            maskComponents[index].startsLayer.toggle()
            // A new layer needs somewhere to keep its adjustments.
            while layers.count < layerCount { layers.append(LocalAdjustState()) }
            pushAndRender()
        }
    }

    /// Moves a row up or down the fold.
    ///
    /// ⚠ **Every stroke has to be re-sent.** The engine indexes brush strokes
    /// by component, so swapping two rows in this array leaves their paint
    /// where it was — row 2's stroke would render under row 1's geometry. The
    /// same hazard removal already documents.
    func moveMaskComponent(from index: Int, by offset: Int) {
        let to = index + offset
        guard maskComponents.indices.contains(index),
              maskComponents.indices.contains(to) else { return }
        edit("Reorder mask") {
            maskComponents.swapAt(index, to)
            selectedMask = to
            pushStrokes()
            pushAndRender()
        }
    }

    /// Changes the selected row's primitive, keeping everything else.
    ///
    /// ⚠ Not `maskKind`'s setter: that one adds a component when the group is
    /// empty and removes it on `No mask`, which is what the old picker needed
    /// and is wrong for "this row is a radial now".
    func setMaskKind(_ kind: Int32, at index: Int) {
        guard maskComponents.indices.contains(index) else { return }
        edit("Mask kind") {
            maskComponents[index].kind = kind
            pushAndRender()
        }
    }

    /// One history entry when a component is added or removed.
    func commitMaskGroupEdit(_ label: String) {
        history.record(state, label: label)
        log.committed(state, label: label)
    }

    /// The mask as the canvas overlay handles it.
    ///
    /// The overlay works in one struct because a drag moves several of these at
    /// once — an endpoint sets the angle and the length together — and writing
    /// them one at a time would push a render per field and, worse, leave the
    /// mask briefly in a state that is neither where it was nor where it is
    /// going. Setting it is one suspended batch and one render, the same shape
    /// as `setCrop`.
    ///
    /// These are *displayed* coordinates. The engine puts them where
    /// `develop:linear` can use them; the overlay never sees that transform.
    var maskPlacement: CanvasLayout.MaskPlacement {
        get {
            CanvasLayout.MaskPlacement(
                kind: Int(maskKind),
                center: CGPoint(x: CGFloat(maskCenterX), y: CGFloat(maskCenterY)),
                angle: CGFloat(maskAngle),
                length: CGFloat(maskLength),
                radius: CGSize(width: CGFloat(maskRadiusX), height: CGFloat(maskRadiusY)),
                feather: CGFloat(maskFeather),
                roundness: CGFloat(maskRoundness),
                invert: maskInvert)
        }
        set { setMask(newValue) }
    }

    /// Sets the mask's geometry in one push, so a drag is a single render
    /// rather than five.
    func setMask(_ m: CanvasLayout.MaskPlacement) {
        guard isLoaded else { return }
        suspended = true
        maskCenterX  = Float(m.center.x)
        maskCenterY  = Float(m.center.y)
        maskAngle    = Float(m.angle)
        maskLength   = Float(m.length)
        maskRadiusX  = Float(m.radius.width)
        maskRadiusY  = Float(m.radius.height)
        suspended = false
        pushAndRender()
    }

    /// One history entry when a mask drag finishes, rather than one per frame.
    func commitMaskEdit() { history.record(state, label: "Mask") }

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

    private var suspended = false

    /// Bumped after every successful render so the view knows to redraw.
    private(set) var generation: UInt64 = 0

    /// 128 bins per channel, packed R then G then B.
    private(set) var histogramBins: [UInt32] = []

    let history = EditHistory()

    /// Suppresses history recording while undo/redo is applying a snapshot —
    /// otherwise stepping back would immediately record the step as a new edit.
    private var restoring = false

    private var handle: OpaquePointer?

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

    var metalDevice: MTLDevice? {
        guard let handle, let raw = orion_engine_metal_device(handle) else { return nil }
        return Unmanaged<AnyObject>.fromOpaque(raw).takeUnretainedValue() as? MTLDevice
    }

    var outputTexture: MTLTexture? {
        guard let handle, let raw = orion_engine_output_texture(handle) else { return nil }
        return Unmanaged<AnyObject>.fromOpaque(raw).takeUnretainedValue() as? MTLTexture
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
    private(set) var interacting = false

    /// The preview graph's output, or nil when there is none.
    var previewTexture: MTLTexture? {
        guard let handle, let raw = orion_engine_preview_texture(handle) else {
            return nil
        }
        return Unmanaged<AnyObject>.fromOpaque(raw).takeUnretainedValue() as? MTLTexture
    }

    /// The texture the canvas should show right now.
    ///
    /// ⚠ Only the canvas asks. `outputTexture` is what export, the histogram,
    /// the eyedropper and the screenshot harness read, and all of them must
    /// keep reading it — a preview-resolution export is a mistake only the
    /// person receiving the file would find.
    var displayTexture: MTLTexture? {
        (interacting ? previewTexture : nil) ?? outputTexture
    }

    var previewSize: (width: UInt32, height: UInt32) {
        guard let handle else { return (0, 0) }
        var w: UInt32 = 0, h: UInt32 = 0
        guard orion_engine_preview_size(handle, &w, &h) == ORION_OK else {
            return (0, 0)
        }
        return (w, h)
    }

    /// A drag started. Renders go to the preview graph until `endInteraction`.
    func beginInteraction() {
        // ⚠ **Not while comparing.** The preview graph renders at a quarter
        // linear, and the split samples the held original and the edited render
        // through *one* set of UVs taken from the edited one — so with a
        // preview-sized edited texture the two no longer line up, and the
        // canvas dealt with that by suspending the split for the length of the
        // drag. Which means the Original side showed the edited picture while a
        // slider moved: the exact thing compare exists to prevent, at the exact
        // moment someone is using it to judge a change.
        //
        // Compare is a deliberate, short-lived viewing mode. Paying
        // full-resolution latency inside it is the right trade against a split
        // that lies.
        guard !comparing else { return }
        guard isLoaded, previewTexture != nil, !interacting else { return }
        log.interacting(true)
        interacting = true
    }

    /// The hand stopped. Renders the full graph once and goes back to it.
    func endInteraction() {
        guard interacting else { return }
        log.interacting(false)
        interacting = false
        // ⚠ The full graph is *stale* at this point — every tick of the drag
        // went to the preview. This render is not a refinement of what is on
        // screen, it is the first time the full graph has seen these values,
        // and skipping it would leave the photograph at whatever it looked like
        // when the drag began.
        pushAndRender()
    }

    /// The loaded creative LUT's title, or "" when none is loaded.
    private(set) var lutName: String = ""

    /// Why the last load failed, for the panel to show. A LUT that will not
    /// load is the user's file being wrong, not the app misbehaving, so the
    /// reason has to reach them — the parser names the line.
    private(set) var lutError: String?

    func loadLut(path: String, displayName: String) {
        guard let handle else { return }

        let status = orion_engine_load_lut(handle, path)
        guard status == ORION_OK else {
            lutError = errorText(status)
            return
        }

        lutError = nil
        var buffer = [CChar](repeating: 0, count: 256)
        orion_engine_lut_title(handle, &buffer, Int32(buffer.count))
        let title = String(cString: buffer)
        // A .cube is not obliged to carry a TITLE, and most do not. The file's
        // own name is what the user picked and what they will look for.
        lutName = title.isEmpty ? displayName : title
        pushAndRender()
    }

    func clearLut() {
        guard let handle else { return }
        orion_engine_clear_lut(handle)
        lutName = ""
        lutError = nil
        pushAndRender()
    }

    func open(path: String) throws {
        guard let handle else { return }

        let status = orion_engine_open_raw(handle, path)
        guard status == ORION_OK else {
            throw Failure.open(errorText(status))
        }

        var w: UInt32 = 0, h: UInt32 = 0
        orion_engine_image_size(handle, &w, &h)
        imageWidth = w
        imageHeight = h
        camera = String(cString: orion_engine_camera(handle))
        // An answer about the photograph being left. `MatteStore.restore` fills
        // it again for the one arriving.
        missingMattes = []

        var profile = OrionLensProfile()
        if orion_engine_lens_profile(handle, &profile) == ORION_OK, profile.found != 0 {
            let name = withUnsafeBytes(of: profile.lens) { raw in
                String(cString: raw.baseAddress!.assumingMemoryBound(to: CChar.self))
            }
            let maker = withUnsafeBytes(of: profile.maker) { raw in
                String(cString: raw.baseAddress!.assumingMemoryBound(to: CChar.self))
            }
            lensProfileName = name.hasPrefix(maker) || maker.isEmpty
                ? name : "\(maker) \(name)"
            lensProfileApproximate = profile.approximate != 0
        } else {
            lensProfileName = ""
            lensProfileApproximate = false
        }

        // The held original is the *previous* photo's unedited render. Keeping
        // it means compare shows one picture against another one entirely,
        // which is worse than showing nothing. Compare itself stays on across a
        // switch — that is the point of it while culling — so the render below
        // captures this photo's own original through `refreshOriginal`.
        originalTexture = nil
        originalGeometry = nil
        maskColorSwatch = nil

        // Reset to the camera's own settings before marking loaded, so the
        // didSet observers don't each trigger a render on a half-set model.
        suspended = true
        defaults = asShotState()
        assign(defaults)
        suspended = false

        isLoaded = true
        history.reset(to: state)
        log.opened(path, state: state)
        pushAndRender()
    }

    /// Restores a state saved to a sidecar. Silent on malformed data: a
    /// sidecar written by a newer build should leave the photo openable.
    func restore(encoded: Data) {
        guard let s = try? JSONDecoder().decode(DevelopState.self, from: encoded) else {
            return
        }
        suspended = true
        assign(s)
        suspended = false
        history.reset(to: s)
        pushAndRender()
    }

    /// Rotates by a quarter turn, wrapping. Clockwise is positive.
    /// A quarter turn swaps the frame's width and height, so a crop rectangle
    /// expressed against the old one no longer means anything. Resetting it is
    /// honest; carrying it over would silently reframe the picture.
    func rotate(_ turns: Int32) {
        suspended = true
        cropX = 0; cropY = 0; cropW = 1; cropH = 1
        // Predict the swap rather than waiting for the render to report it,
        // so the constraint that runs on the next edit has the right aspect.
        if turns % 2 != 0 { swap(&frameWidth, &frameHeight) }
        suspended = false
        // The render this triggers re-takes the held original: a quarter turn
        // swaps the output's width and height, and the split samples both
        // textures through one valid rectangle. `refreshOriginal` sees that.
        rotateQuarters = ((rotateQuarters + turns) % 4 + 4) % 4
    }

    /// Applies a preset over the current state, as one history entry.
    ///
    /// One entry, not one per field: a preset is a single act from the
    /// photographer's side, and undo should take it back in a single step the
    /// way it takes back a brush stroke.
    func apply(preset: Preset) {
        guard isLoaded else { return }
        let next = preset.applied(to: state)
        suspended = true
        assign(next)
        suspended = false
        pushAndRender()
        history.record(state, label: preset.name)
        log.record("preset \(preset.name)")
        log.committed(state, label: preset.name)
    }

    /// Returns every adjustment to its default, with white balance back to
    /// what the camera chose. One push, one render.
    func resetEdits() {
        guard isLoaded else { return }
        suspended = true
        assign(defaults)
        suspended = false
        pushAndRender()
        history.record(state, label: "Reset"); log.committed(state, label: "Reset")
    }

    struct Sample {
        /// What is on screen. This is what a swatch must show.
        var display: (r: Double, g: Double, b: Double)
        /// The color before any user adjustment. Hue bands derive from this,
        /// so adjusting a band cannot change which band you pick next.
        var scene: (r: Double, g: Double, b: Double)
    }

    func sample(u: Float, v: Float) -> Sample? {
        guard let handle, isLoaded else { return nil }
        var display = [Float](repeating: 0, count: 3)
        var scene = [Float](repeating: 0, count: 3)
        guard orion_engine_sample(handle, u, v, &display, &scene) == ORION_OK else {
            return nil
        }
        return Sample(
            display: (Double(display[0]), Double(display[1]), Double(display[2])),
            scene: (Double(scene[0]), Double(scene[1]), Double(scene[2])))
    }

    /// Per-channel histogram of the rendered image.
    func histogram(bins: Int = 128) -> [UInt32]? {
        guard let handle, isLoaded else { return nil }
        var out = [UInt32](repeating: 0, count: bins * 3)
        guard orion_engine_histogram(handle, &out, UInt32(bins)) == ORION_OK else {
            return nil
        }
        return out
    }

    /// `depth` is `OrionBitDepth` — 8 or 16, and 0 keeps the engine's default of
    /// sixteen. ⚠ Pass what the *file* will hold, not what the control shows:
    /// eight renders the narrow, dithered graph and sixteen renders the wide
    /// one, so a JPEG asked for at sixteen would skip the dither and then be
    /// rounded to eight anyway. `ExportSettings.effectiveDepth` is that value.
    func export(to path: String, quality: Float = 0.92,
                maxDimension: UInt32 = 0, space: Int32 = 0,
                rating: Int32 = -1, metadata: Int32 = 1,
                depth: Int32 = 0, sharpen: Int32 = 0) throws {
        guard let handle else { return }

        // The coverage overlay is a viewing aid. Exporting with it on would
        // write a red-tinted photograph and nothing in the file would say why,
        // so it is forced off around the write and restored after — including
        // when the export throws.
        let wasOverlay = maskOverlay
        if wasOverlay { maskOverlay = false }
        defer { if wasOverlay { maskOverlay = true } }

        var options = OrionExportOptions(format: -1, quality: quality,
                                         max_dimension: maxDimension, space: space,
                                         rating: rating, metadata: metadata,
                                         bit_depth: depth, sharpen: sharpen)
        let status = orion_engine_export(handle, path, &options)
        guard status == ORION_OK else { throw Failure.export(errorText(status)) }
    }

    /// Encodes with these options and reports the byte count without writing.
    /// Real work — a 24 MP JPEG is about a sixth of a second — so callers
    /// debounce it.
    func exportedSize(format: Int32, quality: Float, maxDimension: UInt32,
                      space: Int32 = 0, depth: Int32 = 0, sharpen: Int32 = 0) -> Int? {
        guard let handle else { return nil }
        // No rating and no metadata source: the estimate measures the pixels,
        // and a few hundred bytes of EXIF is below its resolution anyway. The
        // depth and the sharpening are not below its resolution and are passed.
        var options = OrionExportOptions(format: format, quality: quality,
                                         max_dimension: maxDimension, space: space,
                                         rating: -1, metadata: 1,
                                         bit_depth: depth, sharpen: sharpen)
        var bytes: UInt64 = 0
        guard orion_engine_export_size(handle, &options, &bytes) == ORION_OK else {
            return nil
        }
        return Int(bytes)
    }

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
            gradeHighlight: gradeHighlight,
            denoiseLuma: denoiseLuma, denoiseColor: denoiseColor,
            lutStrength: lutStrength,
            maskComponents: maskComponents,
            maskRefine: maskRefine,
            spots: spots,
            layers: layers,
            fusion: fusion, dehaze: dehaze, clarity: clarity,
            grainAmount: grainAmount, grainSize: grainSize,
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
    private func assign(_ s: DevelopState) {
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
        denoiseLuma = s.denoiseLuma; denoiseColor = s.denoiseColor
        lutStrength = s.lutStrength
        grainAmount = s.grainAmount; grainSize = s.grainSize
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

    private func apply(_ s: DevelopState) {
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
    private(set) var defaults = DevelopState()

    private func asShotState() -> DevelopState {
        var fresh = DevelopState()
        guard let handle else { return fresh }
        var asShot = OrionAdjustments()
        orion_engine_as_shot(handle, &asShot)
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
    private(set) var originalTexture: MTLTexture?

    var comparing: Bool { compareSplit < 0.999 }

    /// The geometry the held original was rendered at.
    ///
    /// ⚠️ The blit samples the edited texture and the held original through
    /// **one** set of UVs, derived from the *edited* render's valid rectangle.
    /// So the moment a crop, a straighten or a quarter turn changes that
    /// rectangle, the held copy is read through the wrong window and the two
    /// halves stop being the same photograph. Measured: a crop under a live
    /// split put luma 0.7404 on the original side where 0.1432 belonged.
    ///
    /// Recorded and compared rather than re-captured at each call site, because
    /// hand-listing the sites is exactly what failed — `rotate` carried its own
    /// `captureOriginal()` call and `setCrop` carried nothing, so one of the two
    /// geometry controls was right by accident of who remembered.
    private struct OriginalGeometry: Equatable {
        var rotateQuarters: Int32
        var straightenDeg: Float
        /// Perspective does not change the output *rectangle*, so it is not
        /// here for the reason the other fields are. It is here because the
        /// held original is rendered with the geometry copied across, and a
        /// split showing a corrected edit beside an uncorrected original is two
        /// different photographs.
        var perspectiveVertical: Float
        var perspectiveHorizontal: Float
        var perspectiveAspect: Float
        var cropX: Float, cropY: Float, cropW: Float, cropH: Float
        /// The crop preview renders the whole frame with the crop as context,
        /// which is a different output shape again.
        var cropPreview: Bool
    }

    private var originalGeometry: OriginalGeometry?

    private var currentGeometry: OriginalGeometry {
        OriginalGeometry(rotateQuarters: rotateQuarters, straightenDeg: straightenDeg,
                         perspectiveVertical: perspectiveVertical,
                         perspectiveHorizontal: perspectiveHorizontal,
                         perspectiveAspect: perspectiveAspect,
                         cropX: cropX, cropY: cropY, cropW: cropW, cropH: cropH,
                         cropPreview: cropPreview)
    }

    /// `captureOriginal` renders twice, and every render asks whether the held
    /// original is still good. Without this it would ask itself, forever.
    private var capturingOriginal = false

    /// Renders the image as shot into a held texture, then restores the edit.
    /// Two renders once, rather than two renders per frame of the divider.
    func captureOriginal() {
        guard isLoaded, !capturingOriginal else { return }
        capturingOriginal = true
        defer { capturingOriginal = false }
        originalGeometry = currentGeometry

        let current = state
        var neutral = DevelopState()
        neutral.temperatureK = temperatureK      // as shot is not an edit
        neutral.tint = tint
        neutral.rotateQuarters = rotateQuarters
        neutral.straightenDeg = straightenDeg
        neutral.perspectiveVertical = perspectiveVertical
        neutral.perspectiveHorizontal = perspectiveHorizontal
        neutral.perspectiveAspect = perspectiveAspect
        neutral.cropX = cropX; neutral.cropY = cropY
        neutral.cropW = cropW; neutral.cropH = cropH

        apply(neutral)

        // Copy out, because the next render overwrites the pipeline's output.
        if let src = outputTexture, let device = metalDevice {
            let desc = MTLTextureDescriptor.texture2DDescriptor(
                pixelFormat: src.pixelFormat, width: src.width, height: src.height,
                mipmapped: false)
            desc.usage = [.shaderRead, .shaderWrite]
            desc.storageMode = .shared

            if let copy = device.makeTexture(descriptor: desc),
               let queue = device.makeCommandQueue(),
               let buffer = queue.makeCommandBuffer(),
               let blit = buffer.makeBlitCommandEncoder() {
                blit.copy(from: src, to: copy)
                blit.endEncoding()
                buffer.commit()
                buffer.waitUntilCompleted()
                originalTexture = copy
            }
        }

        apply(current)
    }

    func setCompare(split: Double) {
        // The split goes first. captureOriginal re-renders twice, and every
        // render asks whether the held original is still good — which, while
        // the split still reads 1.0, means dropping it. So the original was
        // captured and thrown away in the same call, and compare showed the
        // edit on both sides.
        compareSplit = min(max(split, 0), 1)
        log.compare(compareSplit)
        refreshOriginal()
        generation &+= 1
    }

    func generationBump() { generation &+= 1 }

    /// Sixteen bits out of the tail of the graph instead of eight.
    ///
    /// The screen path is eight bits: the drawable is `bgra8Unorm`, so wider is
    /// bytes moved for precision nothing can show, and it costs about 3.5 ms of
    /// a 16 ms budget. Export widens the tail itself. This is here for the
    /// screenshot harness, which reads the output texture directly and measures
    /// to four decimal places — quantised to 8 bits, the differences this
    /// codebase hunts (0.0001 chroma) round to nothing.
    ///
    /// Reallocates two full-resolution textures. Not for a slider.
    func setWideOutput(_ wide: Bool) {
        guard let handle, isLoaded else { return }
        _ = orion_engine_set_wide_output(handle, wide ? 1 : 0)
        pushAndRender()
    }

    func clearCompare() {
        log.compare(1.0)
        compareSplit = 1.0
        originalTexture = nil
        originalGeometry = nil
        generation &+= 1
    }

    /// Brings the held original into line with what compare needs right now:
    /// nothing when the split is off, and a render at the *current* geometry
    /// when it is on.
    ///
    /// Called from `render`, so it covers every route into a geometry change —
    /// the crop rectangle, the straighten slider, a quarter turn, the crop
    /// preview, an undo that walks back through any of them — rather than the
    /// handful of call sites someone remembered to annotate.
    ///
    /// It costs two full renders when it fires. That is affordable because the
    /// only geometry control reachable while comparing is the rotate button,
    /// which is a click: the crop tab clears compare on entry, so the sliders
    /// that would trip this per tick cannot be moved with a split up.
    private func refreshOriginal() {
        guard !capturingOriginal else { return }
        guard comparing else {
            originalTexture = nil
            originalGeometry = nil
            return
        }
        guard originalTexture == nil || originalGeometry != currentGeometry else { return }
        captureOriginal()
    }

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
    private func cAdjustments() -> OrionAdjustments {
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
        a.denoise_luma = denoiseLuma; a.denoise_color = denoiseColor
        a.lut_strength = lutStrength
        a.grain_amount = grainAmount; a.grain_size = grainSize

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
            c.center_x = m.centreX; c.center_y = m.centreY
            c.angle = m.angle; c.length = m.length
            c.radius_x = m.radiusX; c.radius_y = m.radiusY
            c.feather = m.feather; c.roundness = m.roundness
            c.range_lo = m.rangeLo; c.range_hi = m.rangeHi
            c.range_soft = m.rangeSoft
            c.color_r = m.colourR; c.color_g = m.colourG; c.color_b = m.colourB
            c.color_tol = m.colourTol; c.color_soft = m.colourSoft
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

    private func pushAndRender() {
        guard isLoaded, !suspended, let handle else { return }
        var adj = cAdjustments()
        // Both graphs, always — the engine fans this out. A preview left stale
        // is the graph read the instant the drag ends.
        orion_engine_set_adjustments(handle, &adj)

        if interacting {
            renderPreview()
        } else {
            render()
        }
        onEdit?(state)
    }

    /// Renders the quarter-linear graph and publishes a frame.
    ///
    /// Deliberately does *not* re-read `imageWidth`, recompute the histogram or
    /// drop the compare original: those all describe the full render, and the
    /// preview is a stand-in for looking at, not a new state of the document.
    private func renderPreview() {
        guard let handle else { return }
        var ms: Double = 0
        guard orion_engine_render_preview(handle, &ms) == ORION_OK else {
            // No preview graph on this machine. Fall back rather than showing
            // nothing — degrade-then-refine is a comfort, not a requirement.
            render()
            return
        }
        lastRenderMs = ms
        generation &+= 1
    }

    private func render() {
        guard let handle else { return }
        var ms: Double = 0
        guard orion_engine_render(handle, &ms) == ORION_OK else { return }

        // Re-read the size every frame. A quarter turn swaps width and height,
        // and the canvas uses these to work out which part of the (square)
        // orientation texture is valid — stale values there tear the image.
        var w: UInt32 = 0, h: UInt32 = 0
        if orion_engine_image_size(handle, &w, &h) == ORION_OK {
            imageWidth = w
            imageHeight = h
        }
        var fw: UInt32 = 0, fh: UInt32 = 0
        if orion_engine_frame_size(handle, &fw, &fh) == ORION_OK {
            frameWidth = fw
            frameHeight = fh
        }

        lastRenderMs = ms
        refreshOriginal()
        generation &+= 1
        scheduleHistogram()
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
    private(set) var placeholder: NSImage?

    func showPlaceholder(_ image: NSImage?) {
        placeholder = image
        generation &+= 1
    }

    func clearPlaceholder() {
        guard placeholder != nil else { return }
        placeholder = nil
        generation &+= 1
    }

    private var histogramTask: Task<Void, Never>?

    /// The histogram reads back the whole output texture — ~96 MB at 24 MP —
    /// so recomputing it per render added tens of milliseconds to every slider
    /// tick. It is a readout nobody watches mid-drag, so it updates once the
    /// values settle instead.
    private func scheduleHistogram() {
        histogramTask?.cancel()
        histogramTask = Task { [weak self] in
            try? await Task.sleep(for: .milliseconds(160))
            guard !Task.isCancelled, let self else { return }
            if let bins = self.histogram() { self.histogramBins = bins }
        }
    }

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

    private func errorText(_ status: OrionStatus) -> String {
        guard let handle else { return String(cString: orion_status_string(status)) }
        let detail = String(cString: orion_last_error(handle))
        return detail.isEmpty ? String(cString: orion_status_string(status)) : detail
    }
}
