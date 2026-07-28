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

    private let cellHeight: CGFloat = 62

    /// The perforated margin above and below the frames.
    private let sprocketBand: CGFloat = 11

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
        strip
            .background(alignment: .top) { sprockets }
            .background(alignment: .bottom) { sprockets }
            .background(Palette.filmBase)
    }

    private var sprockets: some View {
        Canvas { context, size in
            // 4.5 mm pitch on 35 mm stock, scaled to the band. The regularity
            // is the whole effect: an irregular pitch reads as a texture, an
            // even one reads as film.
            let pitch: CGFloat = 15
            let holeW: CGFloat = 8
            let holeH: CGFloat = 6
            let y = (size.height - holeH) / 2

            var x: CGFloat = 6
            while x < size.width {
                let hole = Path(roundedRect: CGRect(x: x, y: y, width: holeW, height: holeH),
                                cornerRadius: 1.5)
                context.fill(hole, with: .color(Palette.panel))
                x += pitch
            }
        }
        .frame(height: sprocketBand)
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
                LazyHStack(spacing: 4) {
                    ForEach(library.visible) { photo in
                        cell(photo)
                            .id(photo.url)
                            .onTapGesture { onSelect(photo.url) }
                    }
                }
                .padding(.horizontal, 6)
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

        return ZStack(alignment: .bottomLeading) {
            Group {
                if let image = photo.thumbnail {
                    Image(nsImage: image)
                        .resizable()
                        .aspectRatio(contentMode: .fill)
                } else {
                    Palette.raised
                }
            }
            .frame(width: cellHeight * 1.5, height: cellHeight)
            .clipped()
            .opacity(photo.rejected ? 0.4 : 1)

            marks(photo)
                .padding(.leading, 4)
                .padding(.bottom, 3)
        }
        // The gate: a dark border around each exposure, the way the frame line
        // sits between negatives. Selection takes it over in the accent rather
        // than adding a second ring on top of it.
        .padding(2)
        .background(Palette.filmBase)
        .overlay(
            Rectangle()
                .strokeBorder(isSelected ? Palette.accent : .black.opacity(0.55),
                              lineWidth: 2)
        )
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
