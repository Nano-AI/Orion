import Foundation
import Metal
import MetalKit
import AppKit

/// The mask group and the local adjustments it carries — research/masking.md.
///
/// A mask is a *list* of components folded left in listed order (§6); the
/// `mask…` properties are views onto whichever row is selected. This file holds
/// the row list's operations and those views together, so adding a mask kind is
/// one file rather than two.
///
/// The brush is mask kind 3 and lives in `Engine+Brush.swift`. It is split out
/// because its live-stroke path is performance-critical in a way none of the
/// closed-form primitives are (decisions #102 and #108), not because it is a
/// different feature.

extension Engine {

    /// ⚠ **Derived, not stored.** Which layer a component belongs to is how
    /// many layer boundaries precede it, and that moves whenever a row is
    /// added, removed or reordered. Storing it would be a second copy of the
    /// grouping, and the two would disagree the first time a row moved.
    var selectedLayer: Int {
        min(MaskLayers.layerIndex(ofComponent: selectedMask, in: maskComponents),
            Engine.maxMaskComponents - 1)
    }

    /// How many layers the stack has. `MaskLayers.group` is the one grouping
    /// definition — the panel's cards read the same one, so the two cannot
    /// disagree about where a mask begins.
    var layerCount: Int { MaskLayers.group(maskComponents).count }

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
    /// The four tone bands, in the globals' units. Highlights and shadows read
    /// the frame's one guided estimate — research/masking.md §2b.
    var localHighlights: Float {
        get { layer.highlights }
        set { editLayer { $0.highlights = newValue } }
    }
    var localShadows: Float {
        get { layer.shadows }
        set { editLayer { $0.shadows = newValue } }
    }
    var localWhites: Float {
        get { layer.whites }
        set { editLayer { $0.whites = newValue } }
    }
    var localBlacks: Float {
        get { layer.blacks }
        set { editLayer { $0.blacks = newValue } }
    }

    /// The selected component, or nil when the group is empty.
    var selected: MaskComponentState? {
        guard selectedMask >= 0 && selectedMask < maskComponents.count else { return nil }
        return maskComponents[selectedMask]
    }

    /// Edits the selected component in place and pushes once.
    ///
    /// Every `mask…` setter routes through here rather than mutating the array
    /// directly, so there is one place that knows what "selected" means and one
    /// place that renders.
    func editSelected(_ change: (inout MaskComponentState) -> Void) {
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
        get { selected?.centerX ?? 0.5 }
        set { editSelected { $0.centerX = newValue } }
    }
    var maskCenterY: Float {
        get { selected?.centerY ?? 0.5 }
        set { editSelected { $0.centerY = newValue } }
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
        get { selected?.colorTol ?? 0.10 }
        set { editSelected { $0.colorTol = newValue } }
    }
    var maskColorSoft: Float {
        get { selected?.colorSoft ?? 0.05 }
        set { editSelected { $0.colorSoft = newValue } }
    }

    /// The picked shade, scene-linear Rec.2020 RGB. Read for the panel's
    /// swatch; written only by `pickMaskColor`.
    var maskColor: (r: Float, g: Float, b: Float) {
        guard let m = selected else { return (0.18, 0.18, 0.18) }
        return (m.colorR, m.colorG, m.colorB)
    }

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
                $0.colorR = Float(s.scene.r)
                $0.colorG = Float(s.scene.g)
                $0.colorB = Float(s.scene.b)
            }
        }
        maskColorSwatch = s.display
        return true
    }

    /// Appends a component and selects it. Returns false when the group is full.
    ///
    /// A new row composes with `add` — the only op that does anything on a first
    /// row, and the one a photographer means by "and also this".
    ///
    /// ⚠ **A new mask starts its own layer.** The linked default made "add a
    /// mask, grade it" grade the previous mask too, because the new row folded
    /// into its run and shared its adjustments — a masks list that behaves
    /// like one mask until the link icon is found. The link icon still folds a
    /// row into the layer above when folding is what is wanted. Decision #197.
    @discardableResult
    func addMaskComponent(kind: Int32 = 1) -> Bool {
        guard maskComponents.count < Self.maxMaskComponents else { return false }
        var m = MaskComponentState()
        m.kind = kind
        m.compose = 0
        m.startsLayer = !maskComponents.isEmpty
        maskComponents.append(m)
        // The new layer needs somewhere to keep its adjustments — the same
        // growth `setLayerBreak` does.
        while layers.count < layerCount { layers.append(LocalAdjustState()) }
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

    /// Sets a row's layer break outright.
    ///
    /// ⚠ Row 1 always begins one — a stack has to start somewhere, and a first
    /// row that could be "continue" would continue from nothing.
    ///
    /// The scenario verbs come here rather than to the toggle: a toggle's
    /// meaning depends on the default a new row was given, and a script that
    /// says `masksplit` has to mean split whatever that default is.
    func setLayerBreak(_ starts: Bool, at index: Int) {
        guard maskComponents.indices.contains(index), index > 0,
              maskComponents[index].startsLayer != starts else { return }
        edit(starts ? "Split layer" : "Merge layer") {
            maskComponents[index].startsLayer = starts
            // A new layer needs somewhere to keep its adjustments.
            while layers.count < layerCount { layers.append(LocalAdjustState()) }
            pushAndRender()
        }
    }

    /// Starts or ends a layer at this row — the panel's link button.
    func toggleLayerBreak(at index: Int) {
        guard maskComponents.indices.contains(index) else { return }
        setLayerBreak(!maskComponents[index].startsLayer, at: index)
    }

    /// Renames the mask whose run starts at `start` — the card's rename field.
    ///
    /// A whitespace-only submission clears the field rather than storing it, so
    /// the card falls back to its default name instead of going blank. No
    /// render: a name never crosses the facade, so the picture cannot change —
    /// but it is an edit like any other and lands in history and the sidecar.
    func renameMask(layerStartingAt start: Int, to newName: String) {
        guard maskComponents.indices.contains(start) else { return }
        let trimmed = newName.trimmingCharacters(in: .whitespacesAndNewlines)
        let value: String? = trimmed.isEmpty ? nil : trimmed
        guard maskComponents[start].name != value else { return }
        edit("Rename mask") { maskComponents[start].name = value }
    }

    /// Folds the mask that starts at `start` into the mask above it, with the
    /// op that says why — the context menu's "Subtract from mask above".
    ///
    /// One undoable act. This is `setLayerBreak(false)` and the compose in the
    /// same step, because doing them separately leaves an intermediate state —
    /// merged but still adding — that renders once and lands in history, and
    /// because the two are one intention: #197 made every new mask its own
    /// layer, so *combining* is now always a deliberate act with a direction.
    func mergeIntoLayerAbove(at start: Int, compose: Int32) {
        guard maskComponents.indices.contains(start), start > 0,
              maskComponents[start].startsLayer else { return }
        let label = compose == 1 ? "Subtract from mask above"
                  : compose == 2 ? "Intersect with mask above" : "Add to mask above"
        edit(label) {
            maskComponents[start].startsLayer = false
            maskComponents[start].compose = compose
            pushAndRender()
        }
    }

    /// Adds a shape into the layer that contains `index` — the Add menu's
    /// "Add shape to current mask", against `addMaskComponent`'s new-layer
    /// default (#197).
    ///
    /// The row goes at the **end of the run**, continuing it, and composes with
    /// add until told otherwise. ⚠ Inserting mid-list shifts every later slot,
    /// and the engine indexes brush strokes by slot — `pushStrokes` re-sends
    /// them, the same hazard remove and reorder document.
    @discardableResult
    func addShape(kind: Int32, intoLayerContaining index: Int) -> Bool {
        guard maskComponents.count < Self.maxMaskComponents,
              !maskComponents.isEmpty else { return false }
        let runs = MaskLayers.group(maskComponents)
        let layer = MaskLayers.layerIndex(ofComponent: index, in: maskComponents)
        guard runs.indices.contains(layer) else { return false }
        var m = MaskComponentState()
        m.kind = kind
        m.compose = 0
        m.startsLayer = false
        let at = runs[layer].last! + 1
        maskComponents.insert(m, at: at)
        selectedMask = at
        pushStrokes()
        pushAndRender()
        return true
    }

    /// The card's eye: hides or shows the whole mask — every row of its run in
    /// one step, one history entry, so undo brings the mask back whole rather
    /// than a row at a time.
    func toggleLayerHidden(_ layer: Int) {
        let runs = MaskLayers.group(maskComponents)
        guard runs.indices.contains(layer) else { return }
        let hide = !runs[layer].allSatisfy { maskComponents[$0].hidden }
        edit(hide ? "Hide mask" : "Show mask") {
            for i in runs[layer] { maskComponents[i].hidden = hide }
            // ⚠ The render is not implied — `edit` records and does not push,
            // and `maskComponents` has no didSet. `toggleMaskHidden` documents
            // the shape (#67's mirror).
            pushAndRender()
        }
    }

    /// Whether every row of a layer is hidden — what the card's eye shows.
    func layerHidden(_ layer: Int) -> Bool {
        let runs = MaskLayers.group(maskComponents)
        guard runs.indices.contains(layer) else { return false }
        return runs[layer].allSatisfy { maskComponents[$0].hidden }
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
    /// These are **frame** coordinates — the stored numbers, passed through
    /// raw. The overlay crosses into display space through
    /// `FrameDisplayMap`, which the engine also owns; nothing in between
    /// converts anything.
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
}
