import SwiftUI

// The Color tab: presence, the three grading wheels with their balance, and
// the eight-band mixer with its targeted picker.

extension Editor {

    var colorPanel: some View {
        Group {
            section("Presence") {
                slider("Vibrance", $engine.vibrance, -1...1, "", 2,
                       resetsTo: engine.defaults.vibrance, tint: TrackTint.presence)
                slider("Saturation", $engine.saturation, -1...1, "", 2,
                       resetsTo: engine.defaults.saturation, tint: TrackTint.presence)
            }
            section("Color Grading", info: "Angle picks the hue, distance picks how far. The wheels "
                     + "only change color — the slider under each one is what "
                     + "changes that zone's brightness. Balance slides where "
                     + "the three zones sit: right hands more of the picture "
                     + "to the highlight wheel, left to the shadow wheel.") {
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

                // Under the three wheels, because it is the axis they sit on
                // and not a fourth wheel. Decision #101.
                slider("Balance", $engine.gradeBalance, -1...1, "", 2,
                       resetsTo: engine.defaults.gradeBalance)

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
                // The tracks wear the selected band's own colors — the Hue
                // ends are the shader's real ±30° travel, so what the track
                // promises is what the extreme does. See `TrackTint`.
                slider("Hue", bandBinding(\.hueShift), -1...1, "", 2, resetsTo: 0,
                       tint: TrackTint.hue(for: band))
                slider("Saturation", bandBinding(\.satShift), -1...1, "", 2, resetsTo: 0,
                       tint: TrackTint.saturation(for: band))
                slider("Luminance", bandBinding(\.lumShift), -1...1, "", 2, resetsTo: 0,
                       tint: TrackTint.luminance(for: band))
            }
        }
    }
}
