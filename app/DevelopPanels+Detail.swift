import SwiftUI

// The Detail tab: noise reduction, dust spots, the creative LUT, and the
// finishing controls — shadow lift, grain, vignette, dehaze, clarity and
// sharpening.

extension Editor {

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
        section("Grain") {
            slider("Amount", $engine.grainAmount, 0...0.06, "", 3,
                   resetsTo: engine.defaults.grainAmount)
            slider("Size", $engine.grainSize, 1.2...8, " px", 1,
                   resetsTo: engine.defaults.grainSize)
            Text("Film grain, added to the finished picture rather than to the "
               + "scene — it belongs to the print, not to the light. Loudest in "
               + "the midtones and silent at both ends, which is how a real "
               + "emulsion behaves. Size is in pixels of the *negative*, so "
               + "cropping enlarges the grain instead of resampling it.")
                .font(.system(size: 10))
                .foregroundStyle(Palette.faint)
                .fixedSize(horizontal: false, vertical: true)
        }
        section("Vignette") {
            slider("Amount", $engine.vignetteAmount, -3...3, " EV", 2,
                   resetsTo: engine.defaults.vignetteAmount)
            slider("Field angle", $engine.vignetteFieldAngle, 10...70, "°", 0,
                   resetsTo: engine.defaults.vignetteFieldAngle)
            Text("A deliberate falloff toward the corners of the crop, not a "
               + "lens correction — the Lens panel's Vignetting takes one out, "
               + "this puts one in, and a photograph can carry both. Amount is "
               + "the exposure change at the corner in stops. Field angle is "
               + "the half-diagonal angle of view of the lens whose natural "
               + "cos⁴ falloff is being imitated: wide reaches into the frame, "
               + "narrow stays in the corners.")
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
