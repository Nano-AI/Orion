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

extension Editor {

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
                    Text("sets Exposure, Whites, Blacks, Lift, Clarity")
                        .font(.system(size: 10))
                        .foregroundStyle(Palette.faint)
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
                Picker("", selection: $engine.maskKind) {
                    Text("No mask").tag(Int32(0))
                    Text("Linear").tag(Int32(1))
                    Text("Radial").tag(Int32(2))
                }
                .pickerStyle(.segmented)
                .labelsHidden()

                if engine.maskKind != 0 {
                    slider("Exposure", $engine.localExposureEv, -3...3, " EV", 2,
                           resetsTo: engine.defaults.localExposureEv)
                    slider("Centre X", $engine.maskCentreX, 0...1, "", 2,
                           resetsTo: engine.defaults.maskCentreX)
                    slider("Centre Y", $engine.maskCentreY, 0...1, "", 2,
                           resetsTo: engine.defaults.maskCentreY)
                    slider("Angle", $engine.maskAngle, -3.15...3.15, " rad", 2,
                           resetsTo: engine.defaults.maskAngle)

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
                               resetsTo: engine.defaults.maskLength)
                    } else {
                        slider("Feather", $engine.maskFeather, 0...1, "", 2,
                               resetsTo: engine.defaults.maskFeather)
                        slider("Width", $engine.maskRadiusX, 0.02...1, "", 2,
                               resetsTo: engine.defaults.maskRadiusX)
                        slider("Height", $engine.maskRadiusY, 0.02...1, "", 2,
                               resetsTo: engine.defaults.maskRadiusY)
                        slider("Roundness", $engine.maskRoundness, 2...8, "", 1,
                               resetsTo: engine.defaults.maskRoundness)
                    }

                    Toggle("Invert", isOn: $engine.maskInvert)
                        .font(.system(size: 11))
                        .toggleStyle(.checkbox)
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
}
