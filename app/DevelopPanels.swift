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

    var lightPanel: some View {
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
            section("Local") {
                let maskDefaults = MaskComponentState()

                // The group's rows. A mask is a list folded left in listed
                // order, so the list has to be visible — the order is part of
                // the edit, not an implementation detail.
                if !engine.maskComponents.isEmpty {
                    VStack(spacing: 2) {
                        ForEach(Array(engine.maskComponents.enumerated()), id: \.offset) { i, m in
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
                                    if i > 0 {
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
                        }
                    }

                    HStack(spacing: 8) {
                        Button("Add") {
                            if engine.addMaskComponent() {
                                engine.commitMaskGroupEdit("Add mask")
                            }
                        }
                        .disabled(engine.maskComponents.count >= Engine.maxMaskComponents)
                        Button("Remove") {
                            engine.removeMaskComponent(at: engine.selectedMask)
                            engine.commitMaskGroupEdit("Remove mask")
                        }
                        .disabled(engine.maskComponents.isEmpty)
                        Spacer(minLength: 0)
                    }
                    .font(.system(size: 11))
                }

                Picker("", selection: $engine.maskKind) {
                    Text("No mask").tag(Int32(0))
                    Text("Linear").tag(Int32(1))
                    Text("Radial").tag(Int32(2))
                    Text("Brush").tag(Int32(3))
                    Text("Range").tag(Int32(5))
                    Text("Colour").tag(Int32(6))
                }
                .pickerStyle(.segmented)
                .labelsHidden()

                // ⚠ Subject and Person are buttons, not entries in the picker
                // above. They are *actions* — each runs a model and fills the
                // component with what it found — and a picker entry would be a
                // mode a photographer could select into an empty mask, which is
                // indistinguishable from the feature being broken.
                // research/masking.md §5.
                HStack(spacing: 6) {
                    PanelButton(title: matteRunning ? "Selecting…" : "Subject") {
                        findMatte(.subject)
                    }
                    PanelButton(title: "Person") { findMatte(.person) }
                }
                .disabled(matteRunning || !engine.isLoaded)
                .opacity(matteRunning ? 0.5 : 1)

                if engine.maskKind == 4 {
                    Text(matteCaption)
                        .font(.system(size: 10))
                        .foregroundStyle(Palette.faint)
                        .fixedSize(horizontal: false, vertical: true)
                }

                if engine.maskKind != 0 {
                    // The op applies to the *selected* row, and only rows after
                    // the first have one.
                    if engine.selectedMask > 0 {
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
                    slider("Exposure", $engine.localExposureEv, -3...3, " EV", 2,
                           resetsTo: engine.defaults.localExposureEv)

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
                            RoundedRectangle(cornerRadius: 3)
                                .fill(Color(.sRGB,
                                            red: Double(pow(max(engine.maskColour.r, 0), 1 / 2.2)),
                                            green: Double(pow(max(engine.maskColour.g, 0), 1 / 2.2)),
                                            blue: Double(pow(max(engine.maskColour.b, 0), 1 / 2.2)),
                                            opacity: 1))
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
                        // A stroke has no centre or angle to type in — the
                        // whole point is that it is drawn. What is left is the
                        // nib, and those are the three the shader reads.
                        slider("Size", $engine.brushRadius, 0.01...0.4, "", 3,
                               resetsTo: maskDefaults.brushRadius)
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
                        Toggle("Show mask", isOn: $engine.maskOverlay)
                            .toggleStyle(.checkbox)
                    }
                    .font(.system(size: 11))
                }

                Text("A masked exposure scales the parameter, so half coverage "
                   + "at one stop is 2^0.5 \u{2014} a smooth multiplicative ramp "
                   + "in linear light, not a blend of two rendered frames.")
                    .font(.system(size: 10))
                    .foregroundStyle(Palette.faint)
                    .fixedSize(horizontal: false, vertical: true)
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

            Text("Turn on Place spots, then click the photo to cover one. Heal takes the "
               + "brightness from around the spot and the detail from nearby, "
               + "which is what makes it invisible on a sky. ⚠ It has one known "
               + "limit: placed across a hard edge the correction is wrong on "
               + "both sides, because it is a single number for the whole disc.")
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
