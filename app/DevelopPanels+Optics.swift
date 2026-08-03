import SwiftUI

// The Optics tab: the lens profile, and the manual corrections it stands in
// for. Its own tab because it was reported missing from Detail — see
// `ToolTab`.

extension Editor {

    /// The glass: distortion, vignetting and fringing.
    ///
    /// Its own tab because it was reported missing from Detail — see `ToolTab`.
    /// These are properties of the lens rather than of the picture, and the two
    /// sliders at the top are the only ones in the app that a *database* can
    /// switch off, which is a different kind of control from a slider you set.
    var opticsPanel: some View {
        Group {
        section("Lens", info: engine.hasLensProfile
                 ? "The fringe controls stay manual: the database's chromatic "
                   + "aberration figures are per-copy, and a wrong one adds "
                   + "colored edges rather than removing them."
                 : "No profile for this lens — every setting here is by eye. "
                   + "Negative distortion pulls the barrel out of a wide lens; "
                   + "negative vignetting lifts the corners. The fringe controls "
                   + "rescale red and blue against green, which is what removes "
                   + "the colored edges at the frame's corners.") {
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
        }
        }
    }
}
