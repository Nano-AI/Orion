import AppKit
import SwiftUI

// The frame around the picture: the toolbar across the top, the tab strip and
// the status footer around the panel column.
//
// Grouped by what a reader is looking for rather than by where it sits in the
// hierarchy — the toolbar is `Editor.body`'s first child and the footer is
// three levels down inside `tools`, and someone changing the instrument's
// chrome wants both. `ToolTab` heads the file because the tab strip is what
// draws it and the panels are what it names.

/// ⚠️ **Optics is its own tab because nobody could find it.** The lens
/// corrections sat second from the top of Detail and were reported missing:
/// distortion, vignetting and fringing are properties of the glass, not of
/// sharpening and noise, and a photographer looking for them does not think
/// "detail". The tab bar had already made this argument once — three of the
/// four tabs were a bare SF Symbol until someone pointed out that a magnifying
/// glass only means Detail to a person who already knows. Naming the thing is
/// the fix both times.
/// ⚠ **There is no eighth tab, and it was tried.** Versions wanted one — they
/// belong to one photograph and a preset belongs to none — and the bar cannot
/// hold it. Seven plates divide 364 points into 48 each; eight leaves 42, and
/// VERSIONS is about 51 points at the 9-point type this bar sets. Rendered
/// rather than argued about (`--scene versions`): the bar came back reading
/// **PRESE… VERSI…**, so the eighth tab cost the seventh its name as well as
/// its own. Setting the whole bar two points smaller to fit one word is the
/// same trade PRESETS already extracted once, and it is not worth making twice.
/// Versions live at the top of the Presets tab instead — see `versionsPanel`.
enum ToolTab: String, CaseIterable, Identifiable {
    case light, color, detail, optics, mask, crop, presets
    var id: String { rawValue }

    var title: String {
        switch self {
        case .light:  "Light"
        case .color: "Color"
        case .detail: "Detail"
        case .optics: "Optics"
        case .mask:   "Mask"
        case .crop:   "Crop"
        case .presets: "Presets"
        }
    }

    var symbol: String {
        switch self {
        case .light:  "sun.max"
        case .color: "circle.lefthalf.filled"
        case .detail: "magnifyingglass"
        case .optics: "camera.aperture"
        case .mask:   "theatermasks"
        case .crop:   "crop"
        case .presets: "square.stack"
        }
    }
}

extension Editor {
    /// One height for every control in the toolbar.
    ///
    /// They used to size themselves: the icon chips carried an explicit 22,
    /// the text chips grew out of their padding, and the Open menu added
    /// whatever `.borderlessButton` felt like on top of that. Three answers,
    /// three heights, and Open visibly taller than the buttons beside it.
    private static let chipHeight: CGFloat = 23

    /// The toolbar reads left to right in the order the work happens: what you
    /// open, what you are looking at, what you do to it, and what comes out.
    /// Open used to sit between the rotate buttons and Reset, which put the
    /// start of the workflow in the middle of the middle of it.
    var toolbar: some View {
        HStack(spacing: 12) {
            // Serif wordmark against a sans interface. The instrument is
            // sans because it is read at a glance; the name is serif because
            // it is read once. Both faces ship with macOS.
            Text("Orion")
                .font(.system(size: 17, weight: .regular, design: .serif))
                .tracking(0.5)
                .foregroundStyle(Palette.text)

            Menu {
                Button("Open Photo…") { openFile() }
                Button("Open Folder…") { openFolder() }
            } label: {
                HStack(spacing: 5) {
                    Image(systemName: "folder").font(.system(size: 11))
                    Text("Open").font(.system(size: 11))
                    Image(systemName: "chevron.down").font(.system(size: 8, weight: .semibold))
                }
                .padding(.horizontal, 9)
                .frame(height: Self.chipHeight)
                .contentShape(Rectangle())
            }
            .menuStyle(.borderlessButton)
            .menuIndicator(.hidden)
            .fixedSize()
            .foregroundStyle(Palette.text)
            .background(Palette.raised, in: RoundedRectangle(cornerRadius: 5))
            .overlay(RoundedRectangle(cornerRadius: 5).stroke(Palette.line, lineWidth: 1))
            .help("Open a photo or a folder")

            Text(engine.isLoaded ? engine.camera : "")
                .font(.system(size: 12))
                .foregroundStyle(Palette.dim)
                .frame(maxWidth: .infinity)

            // ── Looking ──────────────────────────────────────────────────
            HStack(spacing: 7) {
                // The other face of the window. Accented while up, like
                // Compare, because both answer "what am I looking at".
                Button {
                    mode == .cull ? leaveGallery() : enterGallery()
                } label: {
                    Text("Gallery")
                        .font(.system(size: 11))
                        .padding(.horizontal, 9)
                        .frame(height: Self.chipHeight)
                        .contentShape(Rectangle())
                }
                .buttonStyle(.plain)
                .foregroundStyle(mode == .cull ? Palette.accent : Palette.dim)
                .overlay(RoundedRectangle(cornerRadius: 5)
                    .stroke(mode == .cull ? Palette.accent : Palette.line, lineWidth: 1))
                .disabled(library.photos.isEmpty)
                .help("Every photo in the folder at once, for culling (G)")

                if engine.isLoaded && mode == .develop {
                    Text("\(viewport.percent)%")
                        .font(.system(size: 11)).monospacedDigit()
                        .foregroundStyle(Palette.faint)
                        .frame(width: 42, alignment: .trailing)
                }
                Button {
                    engine.comparing ? engine.clearCompare()
                                     : engine.setCompare(split: 0.5)
                } label: {
                    Text("Compare")
                        .font(.system(size: 11))
                        .padding(.horizontal, 9)
                        .frame(height: Self.chipHeight)
                        .contentShape(Rectangle())
                }
                .buttonStyle(.plain)
                .foregroundStyle(engine.comparing ? Palette.accent : Palette.dim)
                .overlay(RoundedRectangle(cornerRadius: 5)
                    .stroke(engine.comparing ? Palette.accent : Palette.line, lineWidth: 1))
                .disabled(!engine.isLoaded || mode == .cull)
                .help("Split the view against the original")

                if engine.comparing {
                    iconChip(engine.compareVertical
                             ? "rectangle.split.2x1" : "rectangle.split.1x2",
                             enabled: true) {
                        engine.compareVertical.toggle()
                        engine.generationBump()
                    }
                    .help("Swap between a vertical and horizontal split")
                }
            }

            divider

            // ── Editing ──────────────────────────────────────────────────
            // Greyed as a group in the gallery: every one of these edits the
            // photograph on a canvas that is not showing.
            HStack(spacing: 7) {
                iconChip("arrow.uturn.backward",
                         enabled: engine.history.canUndo && mode == .develop) {
                    engine.undo()
                }
                .help(engine.history.undoLabel.map { "Undo \($0)" } ?? "Undo")

                iconChip("arrow.uturn.forward",
                         enabled: engine.history.canRedo && mode == .develop) {
                    engine.redo()
                }
                .help(engine.history.redoLabel.map { "Redo \($0)" } ?? "Redo")

                iconChip("rotate.left", enabled: engine.isLoaded && mode == .develop) {
                    engine.edit("Rotate") { engine.rotate(-1) }; viewport.reset()
                }
                .help("Rotate left")
                iconChip("rotate.right", enabled: engine.isLoaded && mode == .develop) {
                    engine.edit("Rotate") { engine.rotate(1) }; viewport.reset()
                }
                .help("Rotate right")

                chip("Reset", enabled: engine.isLoaded && mode == .develop) {
                    engine.resetEdits()
                }
                .help("Put every adjustment back")
            }

            divider

            // ── Out ──────────────────────────────────────────────────────
            Button {
                showingExport = true
            } label: {
                Text("Export…")
                    .font(.system(size: 11, weight: .medium))
                    .padding(.horizontal, 11)
                    .frame(height: Self.chipHeight)
                    .contentShape(Rectangle())
            }
            .buttonStyle(.plain)
            .foregroundStyle(engine.isLoaded ? Palette.text : Palette.faint)
            .background(engine.isLoaded ? Palette.accent.opacity(0.18) : .clear,
                        in: RoundedRectangle(cornerRadius: 5))
            .overlay(RoundedRectangle(cornerRadius: 5)
                .stroke(engine.isLoaded ? Palette.accent.opacity(0.55) : Palette.line,
                        lineWidth: 1))
            .disabled(!engine.isLoaded)
        }
        .padding(.horizontal, 14)
        .frame(height: 44)
        .background(Palette.panel)
    }

    /// Separates the toolbar's three groups. A hairline rather than a gap:
    /// spacing alone reads as a rhythm, a rule reads as a boundary.
    private var divider: some View {
        Rectangle()
            .fill(Palette.line)
            .frame(width: 1, height: 18)
    }

    private func iconChip(_ symbol: String, enabled: Bool,
                          action: @escaping () -> Void) -> some View {
        Button(action: action) {
            Image(systemName: symbol)
                .font(.system(size: 12))
                .frame(width: 26, height: Self.chipHeight)
                .contentShape(Rectangle())
        }
        .buttonStyle(.plain)
        .foregroundStyle(enabled ? Palette.dim : Palette.faint)
        .overlay(RoundedRectangle(cornerRadius: 5).stroke(Palette.line, lineWidth: 1))
        .disabled(!enabled)
    }

    private func chip(_ title: String, enabled: Bool,
                      action: @escaping () -> Void) -> some View {
        Button(action: action) {
            Text(title)
                .font(.system(size: 11))
                .padding(.horizontal, 9)
                .frame(height: Self.chipHeight)
                .contentShape(Rectangle())
        }
        .buttonStyle(.plain)
        .foregroundStyle(enabled ? Palette.dim : Palette.faint)
        .overlay(RoundedRectangle(cornerRadius: 5).stroke(Palette.line, lineWidth: 1))
        .disabled(!enabled)
    }

    /// File-folder tabs: the selected one is filled with the panel's own color
    /// and covers the strip's rule, so tab and panel read as one surface.
    /// Tabs, and they stay tabs.
    ///
    /// A camera-style mode dial was built here and thrown away: the knurled rim
    /// and the 3D turn looked like a costume on a control that has one job, and
    /// the perspective could not be checked by the screenshot suite at all
    /// (`cacheDisplay` drops 3D transforms). A tab that reads instantly beats a
    /// tab with a story.
    /// Four engraved plates, all four named.
    ///
    /// The icons are gone and so is the collapse. Three of the four tabs used to
    /// be a bare SF Symbol at 40 points — a contrast circle, a magnifier and a
    /// crop mark — and only the selected one said what it was. Two costs: the
    /// symbols are the same ones every editor reaches for, which is a large part
    /// of what read as templated; and a photographer looking for Detail had to
    /// know that a magnifying glass means sharpening. Four words fit the column
    /// with room to spare, and the engraved caps carry the identity that the
    /// borrowed glyphs did not.
    ///
    /// The selected plate is filled in the panel's own color so it reads as
    /// continuous with the panel below it, and marked at its top edge in
    /// film-rebate amber.
    var tabBar: some View {
        HStack(alignment: .bottom, spacing: 2) {
            ForEach(ToolTab.allCases) { t in
                let selected = t == tab
                Button { withAnimation(.easeOut(duration: 0.16)) { tab = t } } label: {
                    // ⚠ Nine point rather than the panel's ten, and one line
                    // rather than as many as it likes. A sixth tab arrived with
                    // the longest name of the six, and PRESETS wrapped inside
                    // its plate — the word broke after PRESET and the S sat on a
                    // second line, half outside the tab. Every label is set a
                    // point smaller so the bar stays one typographic rank rather
                    // than one tab being visibly different from the other five.
                    Engraved.Label(text: t.title,
                                   color: selected ? Palette.text : Palette.faint,
                                   size: 9, tracking: 0.5)
                        .lineLimit(1)
                        .fixedSize(horizontal: false, vertical: true)
                        .frame(maxWidth: .infinity)
                        .frame(height: selected ? 30 : 26)
                        .background(selected ? Palette.panel : Palette.raised)
                        .overlay(alignment: .top) {
                            Rectangle()
                                .fill(Palette.star)
                                .frame(height: 2)
                                .opacity(selected ? 1 : 0)
                        }
                        .clipShape(UnevenRoundedRectangle(topLeadingRadius: 4,
                                                          topTrailingRadius: 4))
                        .overlay(
                            UnevenRoundedRectangle(topLeadingRadius: 4, topTrailingRadius: 4)
                                .stroke(Palette.line, lineWidth: 1)
                        )
                }
                .buttonStyle(.plain)
                .help(t.title)
            }
        }
        .padding(.horizontal, 8)
        .padding(.top, 6)
        .background(Palette.ground)
    }

    private var hint: String {
        if tab == .crop { return "drag the rectangle or its corners" }
        if !library.photos.isEmpty {
            return viewport.isFit
                ? "← → to browse · 1–5 rates · R rejects"
                : "drag to pan · right-click to fit"
        }
        return viewport.isFit
            ? "scroll or pinch to zoom · right-click to fit"
            : "drag to pan · right-click to fit"
    }

    /// The instrument's own state: what is loaded, and how fast it is answering.
    ///
    /// Also where the canvas hint lives now, so no caption sits on the
    /// photograph. Two lines rather than one: the hint changes with what you are
    /// doing and the frame's numbers do not, and putting them on one row makes
    /// the stable numbers jump every time the hint's length changes.
    /// What the status line has to say, worst first.
    ///
    /// ⚠ **Ordered by what it costs, not by what is newest.** An autosave that
    /// cannot write is the one line a photographer must see *before* they quit,
    /// because quitting is the moment it costs them the session; a folder that
    /// could not be listed costs them the folder; a failed render costs a
    /// redraw. `nil` when there is nothing to say, so the ordinary hint keeps
    /// the line it has always had — a warning that is always on is not one.
    private var complaint: String? {
        autosave.lastFailure ?? library.lastFailure
    }

    var footer: some View {
        VStack(alignment: .leading, spacing: 5) {
            // ⚠ Outside `isLoaded`. A folder that could not be listed leaves
            // nothing open, and the footer used to render nothing at all in
            // that state — so the one message explaining an empty window had
            // nowhere to appear.
            if !engine.isLoaded, let why = complaint {
                Engraved.Label(text: why, color: Palette.star, size: 9)
                    .lineLimit(2)
                    .textSelection(.enabled)
            }
            if engine.isLoaded {
                // ⚠ A failed render reads as a *fast* one if all you show is
                // the millisecond count: it stays at 0.0, which is the best
                // number on the bar, beside a black canvas. So the failure
                // takes the whole line and takes it in the warning colour.
                if let why = complaint {
                    Engraved.Label(text: why, color: Palette.star, size: 9)
                        .lineLimit(2)
                        .textSelection(.enabled)
                } else if let why = engine.lastFailure {
                    Engraved.Label(text: "Render failed — \(why)",
                                   color: Palette.star, size: 9)
                        .lineLimit(2)
                        .textSelection(.enabled)
                } else if let why = engine.flatFrame {
                    // ⚠ Not a failure — the render *succeeded*, which is the
                    // whole difficulty: it reports a plausible millisecond
                    // count beside a rectangle of one colour. See
                    // `Engine.flatFrame`.
                    Engraved.Label(text: "Not a photograph — \(why)",
                                   color: Palette.star, size: 9)
                        .lineLimit(2)
                        .textSelection(.enabled)
                } else {
                    Engraved.Label(text: hint, color: Palette.faint, size: 9)
                        .lineLimit(1)
                }
                HStack(spacing: 0) {
                    Engraved.Readout(text: "\(engine.imageWidth) × \(engine.imageHeight)",
                                     color: Palette.dim)
                    Spacer(minLength: 8)
                    Engraved.Readout(
                        text: engine.lastFailure == nil
                            ? String(format: "%.1f ms", engine.lastRenderMs) : "failed",
                        color: engine.lastFailure != nil ? Palette.star
                             : engine.lastRenderMs < 16 ? Palette.accent : Palette.star)
                }
            }
        }
        .padding(.horizontal, 14).padding(.vertical, 8)
        .overlay(alignment: .top) { Rectangle().fill(Palette.line).frame(height: 1) }
    }
}
