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

            section("Color Mixer",
                    info: "Pick a band below, or press Target and drag on the "
                        + "photograph itself — that finds which band the pixel "
                        + "under the pointer belongs to, which beats guessing "
                        + "which of eight swatches the sky falls into.") {
                // Targeted adjustment: click a color in the photo and drag.
                //
                // ⚠ **The sentence that used to sit beside this button is in
                // the ⓘ above now.** It was the same instruction the footer
                // already prints once the tool is armed and the button's own
                // tooltip carries unarmed — three copies of one sentence, one
                // of them permanently occupying a control row. `Engraved.Info`
                // exists because this panel already made that trade for the
                // slider prose; this row had simply never been included in it.
                HStack(spacing: 6) {
                    ToolButton(tool: .targeted,
                               icon: "eyedropper", armedIcon: "scope",
                               label: "Target",
                               armedLabel: "Drag on the photo",
                               help: "Targeted adjustment — drag on the photo",
                               engine: engine)

                    if engine.tool == .targeted {
                        Picker("", selection: $targeted.mode) {
                            ForEach(TargetedAdjust.Mode.allCases) { m in
                                Text(m.title).tag(m)
                            }
                        }
                        .pickerStyle(.segmented)
                        .controlSize(.small)
                        .labelsHidden()
                    }
                }

                // The band's name sits at the end of its own rail rather than
                // on a row underneath it. It was a third line of sentence-case
                // gray under a row of circles that already showed which one was
                // chosen — a caption for a control that is its own caption. On
                // the rail it is one line shorter, in the panel's own engraved
                // register, and the eye reads swatch and name together.
                HStack(spacing: 0) {
                    ForEach(HueBand.allCases) { b in
                        Circle()
                            .fill(b.swatch)
                            .frame(width: 16, height: 16)
                            .overlay(Circle().strokeBorder(
                                band == b ? Palette.text : .clear, lineWidth: 1.5))
                            // ⚠ The target is the square, not the dot. A 16-point
                            // circle with 4 points of air around it is a hard
                            // thing to hit and there are eight of them in a row;
                            // padding to 24 makes the whole cell live and puts
                            // the gaps *inside* the targets rather than between
                            // them, so there is no dead strip to miss into.
                            .frame(width: 24, height: 24)
                            .contentShape(Rectangle())
                            .onTapGesture { band = b }
                            .help(b.name)
                            .accessibilityLabel(Text(b.name))
                            .accessibilityAddTraits(band == b ? [.isButton, .isSelected]
                                                              : .isButton)
                    }
                    Spacer(minLength: 8)
                    if let active = targeted.activeBand {
                        Engraved.Label(text: active.name, color: Palette.accent)
                    } else {
                        Engraved.Label(text: band.name)
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
