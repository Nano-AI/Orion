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
                            .onTapGesture { onSelect(photo.url) }
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

    private func cell(_ photo: Library.Photo) -> some View {
        let isSelected = photo.url == selected
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
            .padding(3)
            .overlay(
                Rectangle()
                    .strokeBorder(isSelected ? Palette.accent : .white.opacity(0.09),
                                  lineWidth: isSelected ? 2 : 1)
            )

            sprockets(width: frameWidth, top: false)
        }
        .frame(width: frameWidth)
        .background(Palette.filmBase)
        .contentShape(Rectangle())
        .help(photo.name)
        .contextMenu {
            Button(photo.rejected ? "Keep" : "Reject") {
                library.toggleRejected(photo.url)
            }
            Divider()
            ForEach(1...5, id: \.self) { n in
                Button("\(n) star\(n == 1 ? "" : "s")") {
                    library.setRating(n, for: photo.url)
                }
            }
            Button("No rating") { library.setRating(0, for: photo.url) }
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
