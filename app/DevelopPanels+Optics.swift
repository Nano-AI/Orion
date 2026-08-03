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
            } else {
                // ⚠ **There was no `else` here at all until 2026-08-03.** A
                // photograph whose lens the database does not carry showed two
                // bare sliders and no reason — and the developer's own first
                // question on seeing it was "how come it can't find my lens?",
                // which is the interface failing to answer something it knows.
                //
                // The two cases want different sentences because they have
                // different fixes: a lens the file names and the database lacks
                // is a data gap, and a file naming no lens at all is every
                // manual lens ever made and can never be matched by name.
                Text(engine.photoLensName.isEmpty
                     ? "This file records no lens name, which is what an adapted "
                       + "or fully manual lens does — there is nothing to match "
                       + "against. Correct it by hand below."
                     : "No profile for “\(engine.photoLensName)” in the bundled "
                       + "lensfun database, so distortion and vignetting are "
                       + "manual below.")
                    .font(.system(size: 10))
                    .foregroundStyle(Palette.faint)
                    .fixedSize(horizontal: false, vertical: true)
            }

            // ⚠ **The picker, and it is deliberately not a fallback.**
            // `lookup` refuses a near-miss on purpose — applying one optical
            // design's distortion to another's picture is worse than applying
            // none — so nothing here guesses. What it does is let the person
            // holding the camera say what is on it, which the file sometimes
            // cannot: the developer's own lens is named correctly in EXIF and
            // simply absent from the bundled database (#144).
            LensPicker(engine: engine)

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

/// Choose a lens profile by hand, by typing part of its name.
///
/// ⚠ **A filter over a list, not a `Picker`.** The database carries about 1,450
/// lenses; a menu of that many rows is unusable and SwiftUI builds every one of
/// them. This shows nothing until somebody types, then at most a dozen matches.
///
/// ⚠ The catalogue is fetched **once** — `Engine.lensCatalogue()` crosses the
/// facade 1,450 times to build it, and doing that per keystroke is the kind of
/// cost that does not show up until somebody types quickly.
private struct LensPicker: View {
    let engine: Engine

    @State private var query = ""
    @State private var catalogue: [String] = []

    private var matches: [String] {
        let q = query.trimmingCharacters(in: .whitespaces).lowercased()
        guard q.count >= 2 else { return [] }
        // Every typed word must appear, in any order — "sigma 24 art" finds
        // "Sigma 24mm F1.4 DG DN | Art 023" without demanding the exact
        // spacing, which nobody remembers.
        let words = q.split(separator: " ")
        return catalogue.filter { name in
            let lower = name.lowercased()
            return words.allSatisfy { lower.contains($0) }
        }
        .prefix(12)
        .map { $0 }
    }

    var body: some View {
        VStack(alignment: .leading, spacing: 6) {
            if !engine.lensChoice.isEmpty {
                HStack(spacing: 6) {
                    Engraved.Label(text: "Chosen by hand", color: Palette.dim)
                    Spacer(minLength: 0)
                    Button("Clear") { engine.lensChoice = "" }
                        .buttonStyle(.plain)
                        .font(.system(size: 10))
                        .foregroundStyle(Palette.accent)
                }
            }

            TextField("Find a lens…", text: $query)
                .textFieldStyle(.roundedBorder)
                .controlSize(.small)
                .font(.system(size: 11))

            ForEach(matches, id: \.self) { name in
                Button {
                    engine.lensChoice = name
                    query = ""
                } label: {
                    Text(name)
                        .font(.system(size: 10))
                        .foregroundStyle(name == engine.lensChoice
                                         ? Palette.accent : Palette.text)
                        .frame(maxWidth: .infinity, alignment: .leading)
                        .contentShape(Rectangle())
                }
                .buttonStyle(.plain)
            }

            if query.trimmingCharacters(in: .whitespaces).count >= 2 && matches.isEmpty {
                Text("Nothing in the bundled database matches that.")
                    .font(.system(size: 10))
                    .foregroundStyle(Palette.faint)
            }
        }
        .task {
            if catalogue.isEmpty { catalogue = Engine.lensCatalogue() }
        }
    }
}
