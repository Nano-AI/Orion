import SwiftUI

/// The three tool panels: Light, Color, Detail.
///
/// Lifted out of `OrionApp.swift`, which was 1,321 lines against a hard
/// constraint of a thousand — and two thirds of it was these three properties.
/// They are an extension rather than their own views because every control here
/// binds straight to the engine and takes its label, its unit and its reset
/// value from the same three helpers; wrapping each panel in a struct would
/// mean threading all of that through an initializer to gain nothing.
///
/// The crop panel stays with the editor: it is the only one that reads the
/// canvas geometry.
/// A panel button that looks like one.
///
/// `.buttonStyle(.plain)` in this palette renders as ordinary text: no border,
/// no fill, nothing that says it can be pressed. Both panel buttons shipped
/// that way, and the screenshot harness is what caught it — the code compiled,
/// the control worked, and it read as a label sitting next to its own caption.
private struct PanelButton: View {
    let title: String
    let action: () -> Void

    var body: some View {
        Button(action: action) {
            Text(title)
                .font(.system(size: 11, weight: .medium))
                .foregroundStyle(Palette.text)
                .lineLimit(1)
                .truncationMode(.middle)
                .padding(.horizontal, 9)
                .padding(.vertical, 4)
                .background(
                    RoundedRectangle(cornerRadius: 5, style: .continuous)
                        .fill(Palette.raised)
                        .overlay(
                            RoundedRectangle(cornerRadius: 5, style: .continuous)
                                .stroke(Palette.line, lineWidth: 1)))
        }
        .buttonStyle(.plain)
    }
}

/// The group toggles for saving a preset. A row of small switches rather than a
/// menu: the set is short, it is the thing most worth seeing before pressing
/// Save, and a menu would hide it behind a click.
private struct FlowGroups: View {
    @Binding var selection: Set<PresetGroup>

    var body: some View {
        VStack(alignment: .leading, spacing: 2) {
            ForEach(PresetGroup.allCases) { g in
                Toggle(isOn: Binding(
                    get: { selection.contains(g) },
                    set: { on in
                        if on { selection.insert(g) } else { selection.remove(g) }
                    })) {
                    HStack(spacing: 4) {
                        Text(g.title).font(.system(size: 10))
                        Text(g.covers)
                            .font(.system(size: 9))
                            .foregroundStyle(Palette.faint)
                            .lineLimit(1)
                    }
                }
                .toggleStyle(.checkbox)
                .controlSize(.mini)
            }
        }
    }
}

extension Editor {

    /// Row labels for the mask group. Named here rather than in the state struct
    /// because they are interface words: the shader's `kind` is a number and
    /// `MaskComponentState` has no business knowing what a photographer calls it.
    static func maskKindName(_ kind: Int32) -> String {
        switch kind {
        case 1:  return "Linear"
        case 2:  return "Radial"
        case 3:  return "Brush"
        case 4:  return "Selection"
        case 5:  return "Range"
        default: return "Off"
        }
    }

    static func composeName(_ compose: Int32) -> String {
        switch compose {
        case 1:  return "subtract"
        case 2:  return "intersect"
        default: return "add"
        }
    }

    /// ⚠ Says out loud that a matte does not survive a reopen. It is a raster
    /// and the sidecar holds parameters, so it is not written — and a selection
    /// that silently vanished when the photo was opened again would read as
    /// data loss rather than as a limit.
    var matteCaption: String {
        guard let source = matteSource else {
            return "A selection made earlier. It is not saved with the photo, "
                 + "so reopening leaves this row empty until you run it again."
        }
        return "A \(source.lowercased()) selection. Press either button again "
             + "to redo it after a big change to the picture. Not saved with "
             + "the photo."
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
                guard engine.setMaskMatte(m.alpha, width: m.width, height: m.height)
                else {
                    message = "That selection was too large for this photo."
                    return
                }
                engine.maskKind = 4
                matteSource = kind.label
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

    /// Presets, on a tab of their own.
    ///
    /// ⚠ It lived at the top of Light, where it was a hundred and twenty lines
    /// in front of the Exposure slider — the single most-reached-for control in
    /// the program. A section that has to be scrolled *past* on every
    /// photograph is in the wrong place however good it is, and Light had five
    /// sections of which this was by far the largest.
    ///
    /// Last in the bar rather than first: the tab order runs most-used to
    /// least, and a preset is applied occasionally while Light is touched on
    /// every frame.
    var presetsPanel: some View {
        Group {
            section("Presets") {
                if presets.presets.isEmpty {
                    Text("No presets yet.")
                        .font(.system(size: 10))
                        .foregroundStyle(Palette.faint)
                }
                VStack(spacing: 2) {
                    ForEach(presets.presets) { p in
                        HStack(spacing: 6) {
                            Button { engine.apply(preset: p) } label: {
                                HStack(spacing: 6) {
                                    Text(p.name)
                                    Spacer(minLength: 0)
                                    // What it will disturb, before it is
                                    // pressed — a look that silently reset the
                                    // sharpening would be a nasty surprise.
                                    Text(p.groups.count == PresetGroup.allCases.count
                                         ? "all" : "\(p.groups.count) groups")
                                        .foregroundStyle(Palette.faint)
                                }
                                .font(.system(size: 11))
                                .padding(.horizontal, 6)
                                .padding(.vertical, 3)
                                .frame(maxWidth: .infinity)
                                .background(Palette.raised)
                                .clipShape(RoundedRectangle(cornerRadius: 3))
                                .contentShape(RoundedRectangle(cornerRadius: 3))
                            }
                            .buttonStyle(.plain)

                            if !p.builtIn {
                                Button {
                                    presets.remove(p)
                                } label: {
                                    Image(systemName: "minus.circle")
                                        .font(.system(size: 10))
                                        .foregroundStyle(Palette.faint)
                                }
                                .buttonStyle(.plain)
                            }
                        }
                    }
                }

                HStack(spacing: 6) {
                    TextField("New preset", text: $presetName)
                        .textFieldStyle(.roundedBorder)
                        .font(.system(size: 11))
                    Button("Save") {
                        presets.add(name: presetName,
                                    groups: presetGroups,
                                    state: engine.state)
                        presetName = ""
                    }
                    .buttonStyle(.bordered)
                    .controlSize(.small)
                    .disabled(presetName.trimmingCharacters(in: .whitespaces).isEmpty
                              || presetGroups.isEmpty)
                }

                // Which groups a *saved* preset will carry. Shown rather than
                // assumed, because "save a preset" means different things to
                // different people and the difference is exactly this list.
                FlowGroups(selection: $presetGroups)

                // Copy, paste and sync share the group checkboxes above: a
                // paste *is* a preset that was never named, so offering it a
                // second, separate list of groups would be two answers to one
                // question.
                HStack(spacing: 6) {
                    Button("Copy") { copied = engine.state }
                        .disabled(!engine.isLoaded)
                    Button("Paste") {
                        guard let copied else { return }
                        engine.apply(preset: Preset(name: "Paste",
                                                    groups: presetGroups,
                                                    state: copied))
                    }
                    .disabled(copied == nil || presetGroups.isEmpty)
                    Button("Sync all…") { confirmingSync = true }
                        .disabled(copied == nil || presetGroups.isEmpty
                                  || library.photos.count < 2)
                    Spacer(minLength: 0)
                }
                .buttonStyle(.bordered)
                .controlSize(.small)

                if copied != nil {
                    Text("Settings copied. Paste puts them on this photo; "
                       + "Sync writes them to every photo in view.")
                        .font(.system(size: 10))
                        .foregroundStyle(Palette.faint)
                        .fixedSize(horizontal: false, vertical: true)
                }

                // Batch export lives beside sync because they are the same
                // gesture from the photographer's side — do this to all of
                // them — and differ only in whether the result is a sidecar or
                // a file.
                HStack(spacing: 6) {
                    if let p = batchProgress {
                        Text("Exporting \(p.done) of \(p.total)…")
                            .font(.system(size: 10))
                            .foregroundStyle(Palette.dim)
                        Button("Stop") { batchCancelled = true }
                            .buttonStyle(.bordered)
                            .controlSize(.small)
                    } else {
                        Button("Export all…") { runBatchExport() }
                            .buttonStyle(.bordered)
                            .controlSize(.small)
                            .disabled(!engine.isLoaded || library.photos.isEmpty)
                    }
                    Spacer(minLength: 0)
                }

                Text("A preset changes only the groups it carries and leaves "
                   + "everything else alone. The crop, the dust spots and the "
                   + "masks are never included — those belong to one photograph.")
                    .font(.system(size: 10))
                    .foregroundStyle(Palette.faint)
                    .fixedSize(horizontal: false, vertical: true)
            }
        }
    }

    /// Masks — "local" adjustments, on a tab of their own.
    ///
    /// ⚠ It was a section inside Light, and it was 264 lines of one: a row
    /// list, a six-way kind picker, two model buttons, the placement sliders,
    /// refinement and a local exposure. Light is the panel a photographer opens
    /// on every photograph, and it opened on a mask editor.
    ///
    /// It also broke the window. The kind picker is a segmented control and a
    /// sixth kind pushed its intrinsic width past the panel, so a segmented
    /// control that cannot fit does not clip — it forces its parent wider, and
    /// the whole layout went with it. The picker is a grid now, for the same
    /// reason: six named things do not fit on one line at this width and never
    /// will.

    /// One cell of the mask-kind grid. Selecting a kind on an empty group adds
    /// a component; the picker's old meaning is unchanged.
    @ViewBuilder
    private func maskKindCell(_ name: String, _ kind: Int32) -> some View {
        let on = engine.maskKind == kind
        Button { engine.maskKind = kind } label: {
            Text(name)
                .font(.system(size: 11))
                .foregroundStyle(on ? Palette.text : Palette.dim)
                .frame(maxWidth: .infinity)
                .padding(.vertical, 4)
                .background(on ? Palette.raised : Palette.panel)
                .overlay(
                    RoundedRectangle(cornerRadius: 3)
                        .stroke(on ? Palette.accent : Palette.line, lineWidth: on ? 1.5 : 1)
                )
                .clipShape(RoundedRectangle(cornerRadius: 3))
                .contentShape(RoundedRectangle(cornerRadius: 3))
        }
        .buttonStyle(.plain)
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
                Button("Colour range") { add(kind: 6) }
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
                                Button("Colour range") { engine.setMaskKind(6, at: engine.selectedMask) }
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
                        Engraved.Label(
                            text: "Layer \(engine.selectedLayer + 1) of \(engine.layerCount)",
                            color: Palette.accent, size: 9)
                    }

                    // The same specs the global panel renders, pointed at the
                    // local scope. One definition, so the two cannot drift in
                    // look, behaviour or in what they offer.
                    AdjustmentGroup(engine: engine,
                                    specs: AdjustmentCatalogue.localSet,
                                    scope: .local)

                    Text("Warmth and Tint are a colour cast where the mask "
                       + "covers — not a white balance.")
                        .font(.system(size: 10))
                        .foregroundStyle(Palette.faint)
                        .fixedSize(horizontal: false, vertical: true)

                    // Guided feathering, research/masking.md §4. On the group
                    // for the same reason Exposure is: it refines the combined
                    // coverage, which is the boundary the photographer sees.
                    slider("Refine", $engine.maskRefine, 0...1, "", 2,
                           resetsTo: engine.defaults.maskRefine)
                    Text("Pulls the mask's edge onto the nearest edge in the "
                       + "photograph, and leaves it where you put it when there "
                       + "is none to find.")
                        .font(.system(size: 10))
                        .foregroundStyle(Palette.faint)
                        .fixedSize(horizontal: false, vertical: true)

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
                        Text("Selects by brightness rather than by position, "
                           + "measured before your edits — so adjusting through "
                           + "the band cannot change what it selects. Push one "
                           + "end past the picture's range for just the "
                           + "highlights or just the shadows.")
                            .font(.system(size: 10))
                            .foregroundStyle(Palette.faint)
                            .fixedSize(horizontal: false, vertical: true)
                    }

                    if engine.maskKind == 6 {
                        // The picked shade, shown as a swatch. Scene-linear, so
                        // it is raised to something a screen can show before it
                        // is drawn — this is a label for the target, not a
                        // rendering of it.
                        HStack(spacing: 8) {
                            // What was actually under the click. See
                            // `maskColourSwatch` for why the stored target
                            // cannot be drawn: it is peak-normalised, so every
                            // colour would come back looking saturated.
                            let sw = engine.maskColourSwatch
                                ?? (r: Double(pow(max(engine.maskColour.r, 0), 1 / 2.2)),
                                    g: Double(pow(max(engine.maskColour.g, 0), 1 / 2.2)),
                                    b: Double(pow(max(engine.maskColour.b, 0), 1 / 2.2)))
                            RoundedRectangle(cornerRadius: 3)
                                .fill(Color(.sRGB, red: sw.r, green: sw.g,
                                            blue: sw.b, opacity: 1))
                                .frame(width: 26, height: 18)
                                .overlay(RoundedRectangle(cornerRadius: 3)
                                    .strokeBorder(Palette.line, lineWidth: 1))
                            Button(engine.colourPicking ? "Click the photo…" : "Pick colour") {
                                engine.colourPicking.toggle()
                            }
                            .buttonStyle(.plain)
                            .font(.system(size: 11))
                            .foregroundStyle(engine.colourPicking ? Palette.accent : Palette.text)
                            Spacer(minLength: 0)
                        }

                        slider("Tolerance", $engine.maskColourTol, 0.01...0.8, "", 3,
                               resetsTo: maskDefaults.colourTol)
                        slider("Softness", $engine.maskColourSoft, 0.002...0.4, "", 3,
                               resetsTo: maskDefaults.colourSoft)
                        Text("Selects by colour rather than by brightness, and "
                           + "ignores how light or dark it is — so a shade in "
                           + "shadow and the same shade in sun are one "
                           + "selection. Compose it with a brightness range to "
                           + "narrow that.")
                            .font(.system(size: 10))
                            .foregroundStyle(Palette.faint)
                            .fixedSize(horizontal: false, vertical: true)
                    }

                    if engine.maskKind == 3 {
                        // Paint or erase, on the same component. ⚠ A segmented
                        // pair rather than a toggle labelled "Erase": a toggle
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

                        // A stroke has no centre or angle to type in — the
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
                        slider("Centre X", $engine.maskCentreX, 0...1, "", 2,
                               resetsTo: maskDefaults.centreX)
                        slider("Centre Y", $engine.maskCentreY, 0...1, "", 2,
                               resetsTo: maskDefaults.centreY)
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

                Text("A masked exposure scales the parameter, so half coverage "
                   + "at one stop is 2^0.5 \u{2014} a smooth multiplicative ramp "
                   + "in linear light, not a blend of two rendered frames.")
                    .font(.system(size: 10))
                    .foregroundStyle(Palette.faint)
                    .fixedSize(horizontal: false, vertical: true)

                // Reference material, so it goes **after** the controls rather
                // than between them. The first version put it directly under
                // the local sliders, where seven lines of prose separated a
                // mask's adjustments from the mask's own geometry.
                PipelineOrder()
                LocalRefusals()
            }
        }
    }

    var lightPanel: some View {
        Group {
            section("White Balance") {
                slider("Temperature", $engine.temperatureK, 2000...12000, " K", 0, resetsTo: engine.defaults.temperatureK)
                slider("Tint", $engine.tint, -1...1, "", 2, resetsTo: engine.defaults.tint)
            }
            section("Light") {
                // Auto-enhance writes Exposure, Whites and Blacks — three of
                // the sliders directly below it — plus Lift and Clarity. It
                // belongs with the controls it moves, not in a section of its
                // own eleven rows further down.
                HStack(spacing: 8) {
                    PanelButton(title: "Auto") { engine.autoEnhance() }
                    // Five control names, engraved, rather than the sentence
                    // "sets Exposure, Whites, Blacks, Lift, Clarity". It is a
                    // list of what the button touches, so it is set as a list —
                    // and it now reads as chrome instead of as prose competing
                    // with the section headings.
                    Engraved.Label(text: "Exp · Wht · Blk · Lift · Clarity",
                                   color: Palette.faint)
                        .lineLimit(1)
                    Spacer(minLength: 0)
                }

                slider("Exposure", $engine.exposureEv, -5...5, " EV", 2, resetsTo: engine.defaults.exposureEv)
                slider("Contrast", $engine.contrast, 0.5...2, "", 2, resetsTo: engine.defaults.contrast)
                slider("Highlights", $engine.highlights, -1...1, "", 2, resetsTo: engine.defaults.highlights)
                slider("Shadows", $engine.shadows, -1...1, "", 2, resetsTo: engine.defaults.shadows)
                slider("Whites", $engine.whites, -1...1, "", 2, resetsTo: engine.defaults.whites)
                slider("Blacks", $engine.blacks, -1...1, "", 2, resetsTo: engine.defaults.blacks)
            }
            section("Highlight Recovery") {
                slider("Amount", $engine.highlightRecovery, 0...1, "", 2, resetsTo: engine.defaults.highlightRecovery)
                Text("Where one channel clips before the others, it stops "
                     + "carrying detail while the rest still do — a bright sky "
                     + "goes flat where blue ran out. This rebuilds that "
                     + "channel from the ones still reading. A fully blown "
                     + "highlight is already rendered white, so this only "
                     + "changes the places where a single channel ran out on "
                     + "its own.")
                    .font(.system(size: 10))
                    .foregroundStyle(Palette.faint)
                    .fixedSize(horizontal: false, vertical: true)
            }
            // Below the sliders, because it is the control you reach for when
            // a slider was not specific enough. The engine has evaluated this
            // spline since M2; nothing reached it until now.
            section("Curve") {
                CurveEditor(engine: engine, histogram: engine.histogramBins)
            }
        }
    }

    var colorPanel: some View {
        Group {
            section("Presence") {
                slider("Vibrance", $engine.vibrance, -1...1, "", 2, resetsTo: engine.defaults.vibrance)
                slider("Saturation", $engine.saturation, -1...1, "", 2, resetsTo: engine.defaults.saturation)
            }
            section("Color Grading") {
                // Three wheels across the panel. Side by side rather than
                // stacked, because grading is a comparison — you push the
                // shadows cool by looking at what it does against the
                // highlights, and a stacked layout puts them a scroll apart.
                HStack(alignment: .top, spacing: 8) {
                    ColorWheel(title: "Shadows", value: $engine.gradeShadow,
                               engine: engine)
                    ColorWheel(title: "Midtones", value: $engine.gradeMidtone,
                               engine: engine)
                    ColorWheel(title: "Highlights", value: $engine.gradeHighlight,
                               engine: engine)
                }
                .frame(maxWidth: .infinity)

                Text("Angle picks the hue, distance picks how far. The wheels "
                     + "only change color — the slider under each one is what "
                     + "changes that zone's brightness.")
                    .font(.system(size: 10))
                    .foregroundStyle(Palette.faint)
                    .fixedSize(horizontal: false, vertical: true)
            }

            section("Color Mixer") {
                // Targeted adjustment: click a color in the photo and drag.
                // Beats guessing which of eight swatches the sky falls into.
                HStack(spacing: 6) {
                    Button {
                        targeted.isActive.toggle()
                        if !targeted.isActive { targeted.clearHover() }
                    } label: {
                        Image(systemName: targeted.isActive
                              ? "scope" : "eyedropper")
                            .font(.system(size: 12))
                            .frame(width: 26, height: 22)
                    }
                    .buttonStyle(.plain)
                    .foregroundStyle(targeted.isActive ? Palette.accent : Palette.dim)
                    .overlay(RoundedRectangle(cornerRadius: 5)
                        .stroke(targeted.isActive ? Palette.accent : Palette.line, lineWidth: 1))
                    .help("Targeted adjustment — drag on the photo")

                    if targeted.isActive {
                        Picker("", selection: $targeted.mode) {
                            ForEach(TargetedAdjust.Mode.allCases) { m in
                                Text(m.title).tag(m)
                            }
                        }
                        .pickerStyle(.segmented)
                        .controlSize(.small)
                        .labelsHidden()
                    } else {
                        Text("Drag on the photo to adjust its color")
                            .font(.system(size: 10))
                            .foregroundStyle(Palette.faint)
                    }
                }

                HStack(spacing: 4) {
                    ForEach(HueBand.allCases) { b in
                        Circle()
                            .fill(b.swatch)
                            .frame(width: 19, height: 19)
                            .overlay(Circle().strokeBorder(
                                band == b ? Palette.text : .clear, lineWidth: 1.5))
                            .onTapGesture { band = b }
                            .help(b.name)
                    }
                }
                HStack {
                    Text(band.name).font(.system(size: 11)).foregroundStyle(Palette.dim)
                    if let active = targeted.activeBand {
                        Spacer()
                        Text("adjusting \(active.name)")
                            .font(.system(size: 10))
                            .foregroundStyle(Palette.accent)
                    }
                }
                slider("Hue", bandBinding(\.hueShift), -1...1, "", 2, resetsTo: 0)
                slider("Saturation", bandBinding(\.satShift), -1...1, "", 2, resetsTo: 0)
                slider("Luminance", bandBinding(\.lumShift), -1...1, "", 2, resetsTo: 0)
            }
        }
    }

    var detailPanel: some View {
        Group {
        section("Noise Reduction") {
            slider("Luminance", $engine.denoiseLuma, 0...4, "", 2, resetsTo: engine.defaults.denoiseLuma)
            slider("Color", $engine.denoiseColor, 0...4, "", 2, resetsTo: engine.defaults.denoiseColor)
            Text("Measured from this frame, so 1.00 means \"remove what is "
                 + "smaller than one standard deviation of its own noise\" "
                 + "rather than a fixed amount.")
                .font(.system(size: 10))
                .foregroundStyle(Palette.faint)
                .fixedSize(horizontal: false, vertical: true)
        }
        section("Spots") {
            HStack(spacing: 8) {
                Picker("", selection: $engine.spotHeal) {
                    Text("Heal").tag(true)
                    Text("Clone").tag(false)
                }
                .pickerStyle(.segmented)
                .labelsHidden()
                .frame(width: 130)
                Spacer(minLength: 0)
                Text("\(engine.spots.count)")
                    .font(.system(size: 10, design: .monospaced))
                    .foregroundStyle(Palette.faint)
            }

            slider("Size", $engine.spotRadius, 0.004...0.12, "", 3, resetsTo: 0.02)
            slider("Softness", $engine.spotFeather, 0...1, "", 2, resetsTo: 0.5)

            HStack(spacing: 8) {
                // ⚠ Armed explicitly. Without this the caption's "click the
                // photo" would be a promise the interface does not keep, and a
                // click on the canvas would still pan — the same class of thing
                // as a control that is drawn but not wired.
                Toggle(isOn: $engine.spotPlacing) {
                    Text(engine.spotPlacing ? "Placing…" : "Place spots")
                        .font(.system(size: 11))
                }
                .toggleStyle(.button)
                .controlSize(.small)
                Spacer(minLength: 0)
            }

            HStack(spacing: 8) {
                Button("Undo spot") { engine.removeLastSpot() }
                    .disabled(engine.spots.isEmpty)
                Button("Clear") { engine.clearSpots() }
                    .disabled(engine.spots.isEmpty)
                Spacer(minLength: 0)
            }
            .buttonStyle(.bordered)
            .controlSize(.small)

            Text("Turn on Place spots, then drag on the photo: press where the "
               + "dust is and pull to choose what covers it. Drag either circle "
               + "afterwards to move it — solid is the spot, dashed is where the "
               + "replacement comes from. Heal takes the brightness from around "
               + "the spot and the detail from the source, which is what makes "
               + "it invisible on a sky. ⚠ It has one known limit: placed across "
               + "a hard edge the correction is wrong on both sides, because it "
               + "is a single number for the whole disc.")
                .font(.system(size: 10))
                .foregroundStyle(Palette.faint)
                .fixedSize(horizontal: false, vertical: true)
        }
        section("Look") {
            HStack(spacing: 6) {
                PanelButton(title: engine.lutName.isEmpty ? "Load LUT…" : engine.lutName) {
                    let panel = NSOpenPanel()
                    panel.allowedContentTypes = [.init(filenameExtension: "cube")].compactMap { $0 }
                    panel.allowsMultipleSelection = false
                    if panel.runModal() == .OK, let url = panel.url {
                        engine.loadLut(path: url.path,
                                       displayName: url.deletingPathExtension().lastPathComponent)
                    }
                }

                Spacer()

                if !engine.lutName.isEmpty {
                    Button("Remove") { engine.clearLut() }
                        .buttonStyle(.plain)
                        .font(.system(size: 10))
                        .foregroundStyle(Palette.faint)
                }
            }

            if let error = engine.lutError {
                Text(error)
                    .font(.system(size: 10))
                    .foregroundStyle(.orange)
                    .fixedSize(horizontal: false, vertical: true)
            }

            if !engine.lutName.isEmpty {
                slider("Strength", $engine.lutStrength, 0...1, "", 2,
                       resetsTo: engine.defaults.lutStrength)
            }

            Text("A .cube look, applied last — after the tone curve, to a "
               + "finished picture. Tetrahedral interpolation, so a LUT with a "
               + "hard edge in it keeps the edge.")
                .font(.system(size: 10))
                .foregroundStyle(Palette.faint)
                .fixedSize(horizontal: false, vertical: true)
        }
        section("Shadow lift") {
            slider("Lift", $engine.fusion, 0...1, "", 2,
                   resetsTo: engine.defaults.fusion)
            Text("Opens the shadows without flattening them: the picture is "
               + "blended with brighter versions of itself, feature by feature "
               + "rather than tone by tone, so local contrast survives.")
                .font(.system(size: 10))
                .foregroundStyle(Palette.faint)
                .fixedSize(horizontal: false, vertical: true)
        }
        section("Dehaze") {
            slider("Dehaze", $engine.dehaze, 0...1, "", 2,
                   resetsTo: engine.defaults.dehaze)
            Text("Estimates how much of each part of the picture is haze rather "
               + "than subject, and subtracts it. It has a known blind spot: a "
               + "large pale surface with no shadow on it \u{2014} white stone, a "
               + "white car \u{2014} reads as haze, and will darken.")
                .font(.system(size: 10))
                .foregroundStyle(Palette.faint)
                .fixedSize(horizontal: false, vertical: true)
        }
        section("Clarity") {
            slider("Clarity", $engine.clarity, -1...1, "", 2,
                   resetsTo: engine.defaults.clarity)
            Text("Local contrast, edge by edge rather than everywhere at once — "
               + "a large edge keeps its shape while the texture inside it "
               + "gains or loses contrast. Negative smooths.")
                .font(.system(size: 10))
                .foregroundStyle(Palette.faint)
                .fixedSize(horizontal: false, vertical: true)
        }
        section("Sharpening") {
            slider("Amount", $engine.sharpenAmount, 0...2, "", 2, resetsTo: engine.defaults.sharpenAmount)
            slider("Radius", $engine.sharpenRadius, 0.5...3, " px", 1, resetsTo: engine.defaults.sharpenRadius)
            slider("Masking", $engine.sharpenMasking, 0...1, "", 2, resetsTo: engine.defaults.sharpenMasking)
            Text("Masking protects flat areas, where noise lives and detail does not.")
                .font(.system(size: 10))
                .foregroundStyle(Palette.faint)
                .fixedSize(horizontal: false, vertical: true)
        }
        }
    }

    /// The glass: distortion, vignetting and fringing.
    ///
    /// Its own tab because it was reported missing from Detail — see `ToolTab`.
    /// These are properties of the lens rather than of the picture, and the two
    /// sliders at the top are the only ones in the app that a *database* can
    /// switch off, which is a different kind of control from a slider you set.
    var opticsPanel: some View {
        Group {
        section("Lens") {
            if engine.hasLensProfile {
                Toggle(isOn: $engine.lensProfileEnabled) {
                    VStack(alignment: .leading, spacing: 1) {
                        Text("Lens profile")
                            .font(.system(size: 11))
                        Text(engine.lensProfileName)
                            .font(.system(size: 10))
                            .foregroundStyle(Palette.dim)
                            .lineLimit(2)
                            .fixedSize(horizontal: false, vertical: true)
                    }
                }
                .toggleStyle(.switch)
                .controlSize(.mini)
                .tint(Palette.accent)

                Text(engine.lensProfileApproximate
                     ? "Measured for a lens the database spells differently, so "
                       + "this is the nearest match rather than your exact copy. "
                       + "Turn it off to correct by hand."
                     : "Distortion and vignetting measured for this lens at this "
                       + "focal length, from the lensfun database. The two "
                       + "sliders below are what you would use instead.")
                    .font(.system(size: 10))
                    .foregroundStyle(Palette.faint)
                    .fixedSize(horizontal: false, vertical: true)
            }

            let profiled = engine.hasLensProfile && engine.lensProfileEnabled
            slider("Distortion", $engine.lensDistortion, -1...1, "", 2, resetsTo: engine.defaults.lensDistortion)
                .disabled(profiled)
                .opacity(profiled ? 0.4 : 1)
            slider("Vignetting", $engine.lensVignette, -1...1, "", 2, resetsTo: engine.defaults.lensVignette)
                .disabled(profiled)
                .opacity(profiled ? 0.4 : 1)
            slider("Fringe R/C", $engine.lensCaRed, -1...1, "", 2, resetsTo: engine.defaults.lensCaRed)
            slider("Fringe B/Y", $engine.lensCaBlue, -1...1, "", 2, resetsTo: engine.defaults.lensCaBlue)
            Text(engine.hasLensProfile
                 ? "The fringe controls stay manual: the database's chromatic "
                   + "aberration figures are per-copy, and a wrong one adds "
                   + "colored edges rather than removing them."
                 : "No profile for this lens — every setting here is by eye. "
                   + "Negative distortion pulls the barrel out of a wide lens; "
                   + "negative vignetting lifts the corners. The fringe controls "
                   + "rescale red and blue against green, which is what removes "
                   + "the colored edges at the frame's corners.")
                .font(.system(size: 10))
                .foregroundStyle(Palette.faint)
                .fixedSize(horizontal: false, vertical: true)
        }
        }
    }
}
