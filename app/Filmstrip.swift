import SwiftUI

/// The filmstrip — browsing and culling.
///
/// Ratings render as dots rather than stars. At thumbnail scale a five-pointed
/// star is mush, while five dots are countable at a glance, which is the only
/// thing the mark has to do here.
struct Filmstrip: View {
    @Bindable var library: Library
    let selected: URL?
    let onSelect: (URL) -> Void

    private let cellHeight: CGFloat = 66

    var body: some View {
        VStack(spacing: 0) {
            Rectangle().fill(Palette.line).frame(height: 1)

            HStack(spacing: 0) {
                filterBar
                Rectangle().fill(Palette.line).frame(width: 1)
                strip
            }
            .frame(height: 98)
        }
        .background(Palette.panel)
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
        .padding(.horizontal, 14)
        // Fixed, because the summary's width changes the moment you reject
        // something — and a filter bar that grows shoves the whole strip
        // sideways, so the thumbnail under the cursor is no longer the one you
        // were about to click.
        .frame(width: 300, alignment: .leading)
    }

    private var controls: some View {
        HStack(spacing: 8) {
            Picker("", selection: $library.filter) {
                ForEach(Library.Filter.allCases) { Text($0.title).tag($0) }
            }
            .pickerStyle(.menu)
            .labelsHidden()
            .frame(width: 96)

            // Named rather than an anonymous ✕ glyph, and it says which way it
            // is about to go. A bare icon left the whole idea of rejecting
            // undiscoverable unless you already knew the keystroke.
            if let selected, let photo = library.photos.first(where: { $0.url == selected }) {
                Button {
                    library.toggleRejected(selected)
                } label: {
                    HStack(spacing: 4) {
                        Image(systemName: photo.rejected
                              ? "arrow.uturn.backward" : "xmark")
                            .font(.system(size: 9, weight: .semibold))
                        Text(photo.rejected ? "Keep" : "Reject")
                            .font(.system(size: 10))
                    }
                    .padding(.horizontal, 8)
                    .padding(.vertical, 4)
                    .foregroundStyle(photo.rejected ? Palette.text : Palette.dim)
                    .background(photo.rejected ? Palette.rejected : Palette.raised,
                                in: RoundedRectangle(cornerRadius: 4))
                    .contentShape(RoundedRectangle(cornerRadius: 4))
                }
                .buttonStyle(.plain)
                .help("Reject or keep this photo (R)")
            }

            Spacer(minLength: 0)
        }
    }

    private var strip: some View {
        ScrollViewReader { proxy in
            ScrollView(.horizontal, showsIndicators: false) {
                LazyHStack(spacing: 8) {
                    ForEach(library.visible) { photo in
                        cell(photo)
                            .id(photo.url)
                            .onTapGesture { onSelect(photo.url) }
                    }
                }
                .padding(.horizontal, 14)
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
        .overlay(
            Rectangle()
                .strokeBorder(isSelected ? Palette.accent : .clear, lineWidth: 2)
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
