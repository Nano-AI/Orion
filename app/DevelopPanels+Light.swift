import SwiftUI

// The Light tab: white balance, the tone sliders, highlight recovery and the
// curve. The panel a photographer opens on every photograph, which is why the
// tab is first and why nothing else has been allowed to grow inside it.

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
}
