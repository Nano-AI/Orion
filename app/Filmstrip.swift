import AppKit
import SwiftUI

/// The filmstrip — browsing and culling, drawn as film.
///
/// The perforations are not decoration. They give the strip a top and a bottom
/// edge, which is what stops a row of thumbnails reading as a row of buttons,
/// and the gate between frames separates one photo from the next more clearly
/// than the gap alone did. Everything that carries meaning — selection,
/// rating, rejection — still reads first; the film is the ground it sits on.
///
/// Ratings render as dots rather than stars. At thumbnail scale a five-pointed
/// star is mush, while five dots are countable at a glance, which is the only
/// thing the mark has to do here — the frame you are actually looking at gets
/// proper stars over the canvas instead.
struct Filmstrip: View {
    @Bindable var library: Library
    let selected: URL?
    let onSelect: (URL) -> Void

    private let cellHeight: CGFloat = 58

    /// The perforated margin above and below the frames.
    private let sprocketBand: CGFloat = 13

    var body: some View {
        VStack(spacing: 0) {
            Rectangle().fill(Palette.line).frame(height: 1)

            HStack(spacing: 0) {
                filterBar
                Rectangle().fill(Palette.line).frame(width: 1)
                film
            }
            .frame(height: 98)
        }
        .background(Palette.panel)
    }

    /// Film base, perforated top and bottom, with the frames running down the
    /// middle. Drawn behind the scroller rather than inside it so the
    /// perforations stay put while the frames move — the way they do on a
    /// projector, and the way they would not if they scrolled with the photos.
    private var film: some View {
        strip.background(Palette.filmBase)
    }

    /// The perforated margin for one frame.
    ///
    /// **Four holes per frame**, which is the standard 35 mm motion picture
    /// pull-down: KS-1870 perforations at a 0.1870 in (4.75 mm) pitch against a
    /// 0.748 in (19 mm) frame — 1870 is the pitch, in ten-thousandths of an
    /// inch, and four of them is exactly one frame.
    ///
    /// Tying the pitch to the cell rather than fixing it in points is what
    /// makes this read as film instead of as a dotted border: the holes line up
    /// with the frame line at every gate, the way they do on a real strip.
    ///
    /// Drawn per frame, inside the scroll content, so the perforations travel
    /// with the pictures. They were behind the scroller before, which meant the
    /// frames slid past a stationary row of holes — the one thing a strip of
    /// film never does.
    private func sprockets(width: CGFloat, top: Bool) -> some View {
        Canvas { context, size in
            let perFrame: CGFloat = 4
            let pitch = size.width / perFrame
            // KS-1870 is 0.078 x 0.073 in against a 0.187 in pitch: the hole is
            // a little under half the pitch, and slightly wider than tall.
            let holeW = min(pitch * 0.42, 10)
            let holeH = holeW * 0.78
            let y = (size.height - holeH) / 2

            for i in 0..<Int(perFrame) {
                let x = (CGFloat(i) + 0.5) * pitch - holeW / 2
                let rect = CGRect(x: x, y: y, width: holeW, height: holeH)
                context.fill(Path(roundedRect: rect, cornerRadius: 1.5),
                             with: .color(Palette.filmHole))
                // A hole has a thickness. The shadow on the lower lip is what
                // stops it reading as a painted rectangle.
                var lip = Path()
                lip.move(to: CGPoint(x: rect.minX, y: rect.maxY))
                lip.addLine(to: CGPoint(x: rect.maxX, y: rect.maxY))
                context.stroke(lip, with: .color(.black.opacity(0.6)), lineWidth: 1)
            }

            // The rebate: the clear edge between the perforations and the
            // frames, brighter than the base the way undeveloped stock is.
            var edge = Path()
            let edgeY = top ? size.height - 0.5 : 0.5
            edge.move(to: CGPoint(x: 0, y: edgeY))
            edge.addLine(to: CGPoint(x: size.width, y: edgeY))
            context.stroke(edge, with: .color(.white.opacity(0.12)), lineWidth: 1)
        }
        .frame(width: width, height: sprocketBand)
        .allowsHitTesting(false)
    }

    private var filterBar: some View {
        VStack(alignment: .leading, spacing: 5) {
            controls
            // On its own line, below the controls. Beside them it sat level
            // with the thumbnails and read as a caption on the first one.
            Text(library.summary)
                .font(.system(size: 10))
                .monospacedDigit()
                .foregroundStyle(Palette.faint)
                .lineLimit(1)
        }
        .padding(.horizontal, 12)
        // Fixed, because the summary's width changes the moment you reject
        // something — and a filter bar that grows shoves the whole strip
        // sideways, so the thumbnail under the cursor is no longer the one you
        // were about to click.
        //
        // 172 rather than 300: the reject button lost its label to an icon, and
        // rating moved over the canvas, so this holds a menu and a count and
        // nothing else. The 128 points it gives back are two more frames.
        .frame(width: 172, alignment: .leading)
    }

    private var controls: some View {
        HStack(spacing: 6) {
            Picker("", selection: $library.filter) {
                ForEach(Library.Filter.allCases) { Text($0.title).tag($0) }
            }
            .pickerStyle(.menu)
            .labelsHidden()
            .frame(width: 84)

            // Icon only now. The label was here because rejecting was
            // otherwise undiscoverable — but the named button over the canvas
            // says it in full, so this one can go back to being a shortcut for
            // the hand already down here.
            if let selected, let photo = library.photos.first(where: { $0.url == selected }) {
                Button {
                    library.toggleRejected(selected)
                } label: {
                    Image(systemName: photo.rejected ? "arrow.uturn.backward" : "xmark")
                        .font(.system(size: 9, weight: .semibold))
                        .frame(width: 22, height: 20)
                        .foregroundStyle(photo.rejected ? Palette.text : Palette.dim)
                        .background(photo.rejected ? Palette.rejected : Palette.raised,
                                    in: RoundedRectangle(cornerRadius: 4))
                        .contentShape(RoundedRectangle(cornerRadius: 4))
                }
                .buttonStyle(.plain)
                .help(photo.rejected ? "Keep this photo (R)" : "Reject this photo (R)")
            }

            Spacer(minLength: 0)
        }
    }

    private var strip: some View {
        ScrollViewReader { proxy in
            ScrollView(.horizontal, showsIndicators: false) {
                LazyHStack(spacing: 0) {
                    ForEach(library.visible) { photo in
                        cell(photo)
                            .id(photo.url)
                            .onTapGesture { tap(photo.url) }
                            // Tap-only cells were unreachable without a mouse
                            // and invisible to VoiceOver beyond a tooltip. A
                            // button is the right shape: it is one thing, it
                            // has one action, and the platform already knows
                            // how to focus and announce it.
                            .accessibilityElement(children: .combine)
                            .accessibilityAddTraits(
                                photo.url == selected || library.isSelected(photo.url)
                                    ? [.isButton, .isSelected] : .isButton)
                            .accessibilityLabel(Text(spoken(photo)))
                            .accessibilityAction { onSelect(photo.url) }
                            .accessibilityAction(named: Text(photo.rejected ? "Keep" : "Reject")) {
                                library.toggleRejected(photo.url)
                            }
                            .focusable()
                            .onKeyPress(.return) { onSelect(photo.url); return .handled }
                            .onKeyPress(.space)  { onSelect(photo.url); return .handled }
                    }
                }
                .padding(.horizontal, 8)
            }
            .onChange(of: selected) { _, url in
                guard let url else { return }
                withAnimation(.easeOut(duration: 0.2)) {
                    proxy.scrollTo(url, anchor: .center)
                }
            }
        }
    }

    /// A click on a frame, with whatever modifiers the hand was holding.
    ///
    /// ⚠ The modifiers come from `NSEvent.modifierFlags` rather than from a
    /// `TapGesture().modifiers(_:)` stack. Three gestures competing for one tap
    /// have a resolution order, and getting it wrong fails *silently* — a
    /// command-click that falls through to the plain handler looks exactly like
    /// a plain click. One handler asking what is held has one outcome, and the
    /// decision it feeds is in `PhotoSelection`, where the suite can see it.
    private func tap(_ url: URL) {
        let flags = NSEvent.modifierFlags
        var modifiers: PhotoSelection.Modifiers = []
        if flags.contains(.command) { modifiers.insert(.command) }
        if flags.contains(.shift)   { modifiers.insert(.shift) }

        if let open = library.click(url, modifiers: modifiers) { onSelect(open) }
    }

    /// What a rating or a rejection from this cell applies to.
    ///
    /// The clicked photo alone unless it is part of a real selection — acting
    /// on a group the click was not part of is how a context menu rates the
    /// wrong forty photographs.
    private func scope(_ url: URL) -> [URL] { library.cullScope(url) }

    /// What VoiceOver says for one frame. The name first, because that is what
    /// the photographer is looking for, then the marks that decide whether it
    /// stays — a rating nobody can hear is a rating nobody can check.
    private func spoken(_ photo: Library.Photo) -> String {
        var parts = [photo.name]
        if library.hasExplicitSelection && library.isSelected(photo.url) {
            parts.append("selected")
        }
        if photo.rejected { parts.append("rejected") }
        if photo.rating > 0 { parts.append("\(photo.rating) star\(photo.rating == 1 ? "" : "s")") }
        if let label = photo.colorLabel, !label.isEmpty { parts.append("label \(label)") }
        return parts.joined(separator: ", ")
    }

    private func cell(_ photo: Library.Photo) -> some View {
        let isCurrent = photo.url == selected
        // ⚠ Two different marks, because they answer two different questions.
        // The accent gate says "this is the photograph on the canvas"; the
        // dimmer one says "this is in the set a batch will act on". One mark for
        // both would make a forty-frame selection look like forty open photos.
        let inSelection = library.hasExplicitSelection && library.isSelected(photo.url)
        let frameWidth = cellHeight * 1.5

        return VStack(spacing: 0) {
            sprockets(width: frameWidth, top: true)

            ZStack(alignment: .bottomLeading) {
                Group {
                    if let image = photo.thumbnail {
                        Image(nsImage: image)
                            .resizable()
                            .aspectRatio(contentMode: .fill)
                    } else {
                        Palette.raised
                    }
                }
                .frame(width: frameWidth - 6, height: cellHeight)
                .clipped()
                .opacity(photo.rejected ? 0.4 : 1)

                marks(photo)
                    .padding(.leading, 4)
                    .padding(.bottom, 3)
            }
            // The gate: the frame line between negatives. Selection takes it
            // over in the accent rather than adding a second ring on top of it.
            //
            // Order matters and it was wrong. Padding *before* the overlay drew
            // the line on the padded bounds, so 3 pt of film base sat between
            // the frame line and the picture on every side and the photo read
            // as floating inside its own gate. On film the frame line is the
            // image's edge; the base is what separates one frame from the next.
            // So: line on the picture, base outside the line.
            .overlay(
                Rectangle()
                    .strokeBorder(isCurrent ? Palette.accent
                                  : inSelection ? Palette.accent.opacity(0.55)
                                  : .white.opacity(0.09),
                                  lineWidth: isCurrent ? 2 : inSelection ? 1.5 : 1)
            )
            // Horizontal only. Vertical padding put a strip of base between the
            // perforated margin and the top of the picture, so the frame read
            // as sitting in a hole rather than as part of the film. On real
            // stock the rebate *is* the frame's edge — there is nothing between
            // them. Sideways it stays, because that gap is what separates one
            // negative from the next.
            .padding(.horizontal, 3)

            sprockets(width: frameWidth, top: false)
        }
        .frame(width: frameWidth)
        .background(Palette.filmBase)
        .contentShape(Rectangle())
        .help(photo.name)
        .contextMenu {
            let urls = scope(photo.url)
            // Named with the count when it is a group, because "Reject" over a
            // selection of forty and "Reject" over one frame are very different
            // acts and the menu is the last thing read before either happens.
            let suffix = urls.count > 1 ? " (\(urls.count) photos)" : ""
            Button((photo.rejected ? "Keep" : "Reject") + suffix) {
                library.setRejected(!photo.rejected, for: urls)
            }
            Divider()
            ForEach(1...5, id: \.self) { n in
                Button("\(n) star\(n == 1 ? "" : "s")" + suffix) {
                    library.setRating(n, for: urls)
                }
            }
            Button("No rating" + suffix) { library.setRating(0, for: urls) }
        }
    }

    private func marks(_ photo: Library.Photo) -> some View {
        HStack(spacing: 2) {
            if photo.rejected {
                Text("✕")
                    .font(.system(size: 9, weight: .bold))
                    .foregroundStyle(Palette.rejected)
            } else {
                ForEach(0..<photo.rating, id: \.self) { _ in
                    Circle()
                        .fill(Palette.rated)
                        .frame(width: 4, height: 4)
                }
            }
        }
        .shadow(color: .black.opacity(0.8), radius: 1, y: 1)
    }
}
