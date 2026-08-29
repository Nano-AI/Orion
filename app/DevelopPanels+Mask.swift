import SwiftUI

// The Mask tab: the group's rows, the Add menu, the placement controls each
// kind needs, and the local adjustments the combined coverage carries.
//
// The matte helpers are here rather than beside the model in
// `SubjectMatte.swift` because they are what the *panel* says and does about a
// selection — the caption it shows, and the button that fills a row.

extension Editor {

    /// Row labels for the mask group. Interface words live in `MaskLayers`
    /// beside the grouping the list is drawn from; this forwarder keeps the
    /// panel's existing call sites.
    static func maskKindName(_ kind: Int32) -> String { MaskLayers.kindName(kind) }

    static func composeName(_ compose: Int32) -> String {
        switch compose {
        case 1:  return "subtract"
        case 2:  return "intersect"
        default: return "add"
        }
    }

    /// ⚠ Short on purpose, and the two that remain are the two that are *news*.
    ///
    /// A missing file and an unsaved selection both mean the row will not do
    /// what it looks like it does, so they still get a sentence. "It worked"
    /// does not: a saved selection says which producer made it and stops. The
    /// long version — that it is never re-run on its own because a model that
    /// changed between OS releases would silently give a different selection —
    /// is in `DECISIONS.md` #79 and the gap table, which is where a reason
    /// belongs when it is not something to act on.
    var matteCaption: String {
        if engine.maskMatteMissing {
            return "Missing file — this row covers nothing. Run it again."
        }
        guard let source = engine.maskMatteSource else {
            return "Empty. Press Subject, Person or Sky."
        }
        guard engine.maskMatteSaved else {
            return "\(source) selection — not saved yet, so it will not survive "
                 + "reopening."
        }
        return "\(source) selection, saved with the photo."
    }

    /// Runs a segmentation model and puts what it finds into the selected
    /// component. research/masking.md §5.
    ///
    /// A component is added if the group is empty, so pressing Subject on a
    /// fresh photo does the obvious thing rather than silently nothing.
    func findMatte(_ kind: SubjectMatte.Kind) {
        guard engine.isLoaded, !matteRunning else { return }
        matteRunning = true

        Task { @MainActor in
            defer { matteRunning = false }
            do {
                let m = try await SubjectMatte.generate(engine: engine, kind: kind)
                if engine.maskComponents.isEmpty { engine.addMaskComponent(kind: 4) }

                // ⚠ The file goes down **before** the engine sees the matte and
                // before the id reaches the state. A write that fails must fail
                // the whole selection: uploading anyway would leave a matte that
                // renders now and is gone on reopen, which is the bug this
                // whole story exists to remove.
                //
                // The id is fresh every time and nothing is overwritten in
                // place, so the worst a crash here can leave is an orphan file,
                // and orphans are swept when the photograph is next opened.
                var id: String?
                if let photo = current {
                    id = try MatteStore.write(m.alpha, width: m.width,
                                              height: m.height, photo: photo)
                }

                guard engine.setMaskMatte(m.alpha, width: m.width, height: m.height)
                else {
                    message = "That selection was too large for this photo."
                    return
                }
                engine.maskKind = 4
                engine.setMatteReference(id: id, source: kind.label)
                // One history entry for the whole operation, the way a brush
                // stroke records once when the hand lifts rather than per dab.
                engine.commitMaskGroupEdit("\(kind.label) selection")
            } catch {
                // Said out loud. "Nothing was found" is a real answer from the
                // model, and a silently empty mask is indistinguishable from a
                // broken feature.
                message = error.localizedDescription
            }
        }
    }

    /// One way in, grouped by **how the mask decides what it covers**.
    ///
    /// It was three separate controls: an Add button that made a linear
    /// gradient whether or not that was wanted, a six-cell grid of kinds, and a
    /// pair of Subject/Person buttons off to one side — so adding a subject
    /// selection meant Add, then ignore the grid, then find the button. Three
    /// entry points for one act.
    ///
    /// The grouping is not decoration: it is the real distinction between the
    /// three families. **Draw** is placed by hand and stays where it is put.
    /// **Detect** runs a model over the photograph. **Match** measures what the
    /// pixels *are*. That is also the order of how much the photograph is
    /// allowed to decide.
    ///
    /// ⚠ Subject and Person are still *actions* rather than modes — each runs a
    /// model and fills the row with what it found. Putting them in this menu
    /// keeps that: choosing one adds a row **and** runs the model, so there is
    /// no way to select into an empty matte, which is indistinguishable from
    /// the feature being broken. research/masking.md §5.
    private var addMenu: some View {
        Menu {
            Section("Draw") {
                Button("Linear gradient") { add(kind: 1) }
                Button("Radial gradient") { add(kind: 2) }
                Button("Brush") { add(kind: 3) }
            }
            Section("Detect") {
                Button("Subject") { addDetected(.subject) }
                Button("Person") { addDetected(.person) }
                // ⚠ "estimated" in the name, deliberately. It is not a model
                // and not a semantic classifier — it finds the calm region
                // joined to the top edge, and research/sky-detection.md lists
                // what that misses.
                Button("Sky (estimated)") { addDetected(.sky) }
            }
            Section("Match") {
                Button("Brightness range") { add(kind: 5) }
                Button("Color range") { add(kind: 6) }
            }
        } label: {
            Label("Add", systemImage: "plus")
        }
        .menuStyle(.borderlessButton)
        .fixedSize()
        .disabled(engine.maskComponents.count >= Engine.maxMaskComponents
                  || matteRunning || !engine.isLoaded)
    }

    private func add(kind: Int32) {
        if engine.maskComponents.isEmpty {
            engine.maskKind = kind
        } else if engine.addMaskComponent(kind: kind) {
            engine.commitMaskGroupEdit("Add mask")
        }
    }

    /// Adds the row and runs the model in one act, so a Subject row never
    /// exists without having been filled.
    private func addDetected(_ kind: SubjectMatte.Kind) {
        if engine.maskComponents.isEmpty {
            engine.maskKind = 4
        } else if engine.addMaskComponent(kind: 4) {
            engine.commitMaskGroupEdit("Add mask")
        }
        findMatte(kind)
    }

    /// Masks — "local" adjustments, on a tab of their own.
    ///
    /// ⚠ It was a section inside Light, and it was 264 lines of one: a row
    /// list, a six-way kind picker, two model buttons, the placement sliders,
    /// refinement and a local exposure. Light is the panel a photographer opens
    /// on every photograph, and it opened on a mask editor.
    ///
    /// It also broke the window. The kind picker was a segmented control, and a
    /// sixth kind pushed its intrinsic width past the panel — a segmented
    /// control that cannot fit does not clip, it forces its parent wider, and
    /// the whole layout went with it. That is why the kinds have never gone
    /// back on one row: six named things do not fit at this width and never
    /// will. The grid that replaced the segments is itself gone now — the kinds
    /// are chosen from `addMenu`, which is the one way in.
    var maskPanel: some View {
        Group {
            section("Local") {
                let maskDefaults = MaskComponentState()

                // The group's rows. A mask is a list folded left in listed
                // order, so the list has to be visible — the order is part of
                // the edit, not an implementation detail.
                if !engine.maskComponents.isEmpty {
                    VStack(spacing: 2) {
                        ForEach(Array(engine.maskComponents.enumerated()), id: \.offset) { i, m in
                          HStack(spacing: 4) {
                            // ⚠ Its own button, outside the row's. Inside it, a
                            // press on the eye would also change which row is
                            // selected — so hiding row 3 while working on row 1
                            // would move you to row 3.
                            Button { engine.toggleMaskHidden(i) } label: {
                                Image(systemName: m.hidden ? "eye.slash" : "eye")
                                    .font(.system(size: 10))
                                    .frame(width: 16)
                                    .foregroundStyle(m.hidden ? Palette.faint : Palette.dim)
                                    .contentShape(Rectangle())
                            }
                            .buttonStyle(.plain)
                            .help(m.hidden ? "Show this mask" : "Hide this mask")

                            Button {
                                engine.selectedMask = i
                            } label: {
                                HStack(spacing: 6) {
                                    Text("\(i + 1)")
                                        .foregroundStyle(Palette.faint)
                                        .frame(width: 12, alignment: .trailing)
                                    Text(Self.maskKindName(m.kind))
                                    // The first row's op is not shown because it
                                    // cannot mean anything: the fold starts from
                                    // zero, so add is the identity there and the
                                    // other two give an empty group.
                                    // ⚠ Only where it can mean something. A row
                                    // that begins a layer folds from zero, so
                                    // add is the identity and the other two
                                    // empty the layer — the engine forces add
                                    // there, and showing an op the engine
                                    // ignores is a label that lies.
                                    if i > 0 && !m.startsLayer {
                                        Text(Self.composeName(m.compose))
                                            .foregroundStyle(Palette.faint)
                                    }
                                    Spacer(minLength: 0)
                                    if m.kind == 3 && !m.brushStroke.isEmpty {
                                        Text("\(m.brushStroke.count / 2) dabs")
                                            .foregroundStyle(Palette.faint)
                                    }
                                }
                                .font(.system(size: 11))
                                .padding(.horizontal, 6)
                                .padding(.vertical, 3)
                                .frame(maxWidth: .infinity)
                                .background(i == engine.selectedMask
                                            ? Palette.faint.opacity(0.18)
                                            : Color.clear)
                                .clipShape(RoundedRectangle(cornerRadius: 3))
                            }
                            .buttonStyle(.plain)
                            // A hidden row is dimmed, so the list says at a
                            // glance which of them are actually contributing.
                            .opacity(m.hidden ? 0.45 : 1)

                            // ⚠ The layer break. Rows in one run fold together
                            // into a single coverage and share one set of
                            // adjustments; a break starts a new coverage with
                            // its own. Row 1 always begins a layer, so it has
                            // no control — a first row that could "continue"
                            // would continue from nothing.
                            if i > 0 {
                                Button { engine.toggleLayerBreak(at: i) } label: {
                                    Image(systemName: m.startsLayer
                                          ? "rectangle.split.1x2" : "link")
                                        .font(.system(size: 10))
                                        .frame(width: 16)
                                        .foregroundStyle(m.startsLayer
                                                         ? Palette.accent : Palette.faint)
                                        .contentShape(Rectangle())
                                }
                                .buttonStyle(.plain)
                                .help(m.startsLayer
                                      ? "Starts its own layer — click to fold into the one above"
                                      : "Folds into the layer above — click to start its own")
                            } else {
                                Color.clear.frame(width: 16)
                            }
                          }
                        }
                    }

                    HStack(spacing: 8) {
                        addMenu
                        Button("Remove") {
                            engine.removeMaskComponent(at: engine.selectedMask)
                            engine.commitMaskGroupEdit("Remove mask")
                        }
                        .disabled(engine.maskComponents.isEmpty)

                        Spacer(minLength: 0)

                        // ⚠ Order is part of the edit, not a display
                        // preference: the group folds **left**, so subtract and
                        // intersect mean different things depending on what is
                        // above them. A row list you cannot reorder is a fold
                        // you cannot express.
                        Button {
                            engine.moveMaskComponent(from: engine.selectedMask, by: -1)
                        } label: { Image(systemName: "chevron.up") }
                            .disabled(engine.selectedMask <= 0)
                        Button {
                            engine.moveMaskComponent(from: engine.selectedMask, by: 1)
                        } label: { Image(systemName: "chevron.down") }
                            .disabled(engine.selectedMask >= engine.maskComponents.count - 1)
                    }
                    .font(.system(size: 11))

                    // Changing what an existing row *is*, which the six-cell
                    // grid used to do and the Add menu does not — that one
                    // creates rows. Losing it was a regression from
                    // reorganising the panel.
                    if !engine.maskComponents.isEmpty {
                        HStack(spacing: 6) {
                            Text("Kind").foregroundStyle(Palette.faint)
                            Menu(Self.maskKindName(engine.maskKind)) {
                                Button("Linear") { engine.setMaskKind(1, at: engine.selectedMask) }
                                Button("Radial") { engine.setMaskKind(2, at: engine.selectedMask) }
                                Button("Brush") { engine.setMaskKind(3, at: engine.selectedMask) }
                                Button("Brightness range") { engine.setMaskKind(5, at: engine.selectedMask) }
                                Button("Color range") { engine.setMaskKind(6, at: engine.selectedMask) }
                            }
                            .menuStyle(.borderlessButton)
                            .fixedSize()
                            Spacer(minLength: 0)
                        }
                        .font(.system(size: 11))
                    }
                }

                if engine.maskComponents.isEmpty { addMenu.font(.system(size: 11)) }

                // ⚠ Beside the mask it describes. It was at the bottom of a
                // 264-line section, so the control that shows you where the
                // coverage *is* sat below every slider that moves it.
                Toggle("Show mask", isOn: $engine.maskOverlay)
                    .toggleStyle(.switch)
                    .controlSize(.mini)
                    .font(.system(size: 11))

                if matteRunning {
                    Text("Selecting…")
                        .font(.system(size: 11))
                        .foregroundStyle(Palette.faint)
                }

                if engine.maskKind == 4 {
                    Text(matteCaption)
                        .font(.system(size: 10))
                        .foregroundStyle(Palette.faint)
                        .fixedSize(horizontal: false, vertical: true)
                }

                if engine.maskKind != 0 {
                    // The op applies to the *selected* row, and only to a row
                    // that **continues** a layer.
                    //
                    // ⚠ A row that begins one folds from zero, where add is the
                    // identity and subtract and intersect both give an empty
                    // layer whatever is painted into it. The engine forces add
                    // there; offering the choice anyway would be a control that
                    // silently does nothing on two of its three settings, and
                    // an emptied layer looks exactly like a mask placed wrong.
                    let continues = engine.selectedMask > 0
                        && !engine.maskComponents[engine.selectedMask].startsLayer
                    if continues {
                        Picker("", selection: $engine.maskCompose) {
                            Text("Add").tag(Int32(0))
                            Text("Subtract").tag(Int32(1))
                            Text("Intersect").tag(Int32(2))
                        }
                        .pickerStyle(.segmented)
                        .labelsHidden()
                    }

                    // One exposure for the whole group, not one per component:
                    // research/masking.md §6 is explicit that the adjustment is
                    // applied once through the combined coverage, or two
                    // overlapping components would apply it twice.
                    // ⚠ Which layer these belong to, said out loud. With a
                    // stack, a set of sliders that did not name its layer would
                    // be five controls whose target is a property of the row
                    // selection three rows above them.
                    if engine.layerCount > 1 {
                        // Named, not numbered: "Sky 1 — mask 2 of 3" says which
                        // coverage these sliders belong to in the mask's own
                        // words, which a bare ordinal never did.
                        Engraved.Label(
                            text: MaskLayers.displayName(ofLayer: engine.selectedLayer,
                                                         in: engine.maskComponents)
                                + " — mask \(engine.selectedLayer + 1) of \(engine.layerCount)",
                            color: Palette.accent, size: 9)
                    }

                    // The same specs the global panel renders, pointed at the
                    // local scope. One definition, so the two cannot drift in
                    // look, behavior or in what they offer.
                    AdjustmentGroup(engine: engine,
                                    specs: AdjustmentCatalogue.localSet,
                                    scope: .local)

                    // Guided feathering, research/masking.md §4. On the group
                    // for the same reason Exposure is: it refines the combined
                    // coverage, which is the boundary the photographer sees.
                    slider("Refine", $engine.maskRefine, 0...1, "", 2,
                           resetsTo: engine.defaults.maskRefine)
                    if engine.maskKind == 5 {
                        // In stops, because that is the unit the band is
                        // measured in — see research/masking.md §4b. A slider
                        // in linear luminance would be unusable across most of
                        // its travel.
                        slider("From", $engine.maskRangeLo, -8...8, " EV", 1,
                               resetsTo: maskDefaults.rangeLo)
                        slider("To", $engine.maskRangeHi, -8...8, " EV", 1,
                               resetsTo: maskDefaults.rangeHi)
                        slider("Softness", $engine.maskRangeSoft, 0.05...4, " EV", 2,
                               resetsTo: maskDefaults.rangeSoft)
                    }

                    if engine.maskKind == 6 {
                        // The picked shade, shown as a swatch. Scene-linear, so
                        // it is raised to something a screen can show before it
                        // is drawn — this is a label for the target, not a
                        // rendering of it.
                        HStack(spacing: 8) {
                            // What was actually under the click. See
                            // `maskColorSwatch` for why the stored target
                            // cannot be drawn: it is peak-normalized, so every
                            // color would come back looking saturated.
                            let sw = engine.maskColorSwatch
                                ?? (r: Double(pow(max(engine.maskColor.r, 0), 1 / 2.2)),
                                    g: Double(pow(max(engine.maskColor.g, 0), 1 / 2.2)),
                                    b: Double(pow(max(engine.maskColor.b, 0), 1 / 2.2)))
                            RoundedRectangle(cornerRadius: 3)
                                .fill(Color(.sRGB, red: sw.r, green: sw.g,
                                            blue: sw.b, opacity: 1))
                                .frame(width: 26, height: 18)
                                .overlay(RoundedRectangle(cornerRadius: 3)
                                    .strokeBorder(Palette.line, lineWidth: 1))
                            Button(engine.colorPicking ? "Click the photo…" : "Pick color") {
                                engine.colorPicking.toggle()
                            }
                            .buttonStyle(.plain)
                            .font(.system(size: 11))
                            .foregroundStyle(engine.colorPicking ? Palette.accent : Palette.text)
                            Spacer(minLength: 0)
                        }

                        slider("Tolerance", $engine.maskColorTol, 0.01...0.8, "", 3,
                               resetsTo: maskDefaults.colorTol)
                        slider("Softness", $engine.maskColorSoft, 0.002...0.4, "", 3,
                               resetsTo: maskDefaults.colorSoft)
                    }

                    if engine.maskKind == 3 {
                        // Paint or erase, on the same component. ⚠ A segmented
                        // pair rather than a toggle labeled "Erase": a toggle
                        // has an off state that has to be read as "paint", and
                        // the two are equal acts. Which one is armed is the
                        // first thing to know before drawing on the picture, so
                        // it goes above the nib.
                        Picker("", selection: $engine.brushErasing) {
                            Text("Paint").tag(false)
                            Text("Erase").tag(true)
                        }
                        .pickerStyle(.segmented)
                        .labelsHidden()

                        // A stroke has no center or angle to type in — the
                        // whole point is that it is drawn. What is left is the
                        // nib, and those are the three the shader reads.
                        slider("Size", $engine.brushRadius, 0.01...0.4, "", 3,
                               resetsTo: maskDefaults.brushRadius)
                        // ⚠ Flow, and it is not opacity. Each dab lays down
                        // this fraction of what is left, source-over, so
                        // overlapping passes *build* toward full coverage — a
                        // low flow is a soft repeatable brush rather than a
                        // ceiling. There is no separate opacity: a ceiling
                        // would need the kernel to track a per-stroke maximum,
                        // and Erase is the honest way to come back down.
                        slider("Flow", $engine.brushFlow, 0.01...1, "", 2,
                               resetsTo: maskDefaults.brushFlow)
                        slider("Hardness", $engine.brushHardness, 0...1, "", 2,
                               resetsTo: maskDefaults.brushHardness)

                        HStack(spacing: 8) {
                            Button("Clear stroke") { engine.clearBrushStroke() }
                                .disabled(engine.brushStroke.isEmpty)
                            Text(engine.brushStroke.isEmpty
                                 ? "drag on the photo to paint"
                                 : "\(engine.brushStroke.count) dabs")
                                .font(.system(size: 10))
                                .foregroundStyle(Palette.faint)
                        }
                        .font(.system(size: 11))
                    }

                    if engine.maskKind == 1 || engine.maskKind == 2 {
                        slider("Center X", $engine.maskCenterX, 0...1, "", 2,
                               resetsTo: maskDefaults.centerX)
                        slider("Center Y", $engine.maskCenterY, 0...1, "", 2,
                               resetsTo: maskDefaults.centerY)
                        slider("Angle", $engine.maskAngle, -3.15...3.15, " rad", 2,
                               resetsTo: maskDefaults.angle)
                    }

                    if engine.maskKind == 1 {
                        // No Feather here, and it is not an omission.
                        // `mask_gradient.slang` reads that field only in its
                        // radial branch — a linear gradient's ramp runs from
                        // the zero line to the full line, so Length *is* the
                        // feather. Measured before removing it: 0.50 against
                        // 0.02 gave bit-identical luma on a real frame. A
                        // slider a photographer can move and watch do nothing
                        // is worse than no slider.
                        slider("Length", $engine.maskLength, 0.05...1.5, "", 2,
                               resetsTo: maskDefaults.length)
                    } else if engine.maskKind == 2 {
                        // Explicitly kind 2, not `else`. An `else` here also
                        // catches the brush, which reads none of these — the
                        // stroke carries its own radius and the shader's
                        // feather and roundness fields are radial-only. Four
                        // dead sliders under a brush, and the same class of
                        // defect as the Feather slider that did nothing to a
                        // linear gradient.
                        slider("Feather", $engine.maskFeather, 0...1, "", 2,
                               resetsTo: maskDefaults.feather)
                        slider("Width", $engine.maskRadiusX, 0.02...1, "", 2,
                               resetsTo: maskDefaults.radiusX)
                        slider("Height", $engine.maskRadiusY, 0.02...1, "", 2,
                               resetsTo: maskDefaults.radiusY)
                        slider("Roundness", $engine.maskRoundness, 2...8, "", 1,
                               resetsTo: maskDefaults.roundness)
                    }

                    HStack(spacing: 14) {
                        Toggle("Invert", isOn: $engine.maskInvert)
                            .toggleStyle(.checkbox)
                        // Not in the sidecar and not undoable: this is how you
                        // are looking at the photo, not an edit to it.
                    }
                    .font(.system(size: 11))
                }

                // ⚠ Three explanatory blocks used to sit here — how a masked
                // exposure scales the parameter, `PipelineOrder` (where the
                // mask sits, three lines listing every adjustment), and
                // `LocalRefusals` (nine controls and a reason each). Roughly
                // twenty lines of prose under a panel of sliders. Removed at
                // the developer's request: the reasoning is right and belongs
                // in `research/masking.md` and `DECISIONS.md` #76, which is
                // where it now lives alone. A panel is for controls.
                //
                // Both views are kept, in `AdjustmentGroup.swift`, so restoring
                // either is one line — and nothing else draws them, so this
                // comment is the only thing that says why they are there.
                // ⚠ Neither view has a check of its own. What is pinned is the
                // table underneath them: `ViewportTests+Catalogue` asserts that
                // `AdjustmentCatalogue.refusedLocally` is non-empty, which is
                // what stops `AdjustmentSpec.localRefusal` being silently
                // dropped. The views generate from it and would follow.
            }
        }
    }
}
