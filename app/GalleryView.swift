import AppKit
import SwiftUI

/// Which of the two faces the editor is showing.
///
/// `develop` is the default and the resting state: the canvas, the tool
/// column and the filmstrip. `cull` replaces all three with the gallery grid.
/// The toolbar stays either way, because Open, undo and Export mean the same
/// thing from both.
enum EditorMode {
    case develop, cull
}

/// The gallery - every photograph in the folder at once, for culling.
///
/// A contact sheet, not a second editor: cells are embedded-preview
/// thumbnails, a click moves focus without decoding anything, and the only
/// way a raw file gets opened from here is asking for it (Return, or a
/// double-click). Rating and rejecting act through the same `Library` calls
/// the filmstrip and the Photo menu use, so the three can never disagree
/// about what a key does.
///
/// `LazyVGrid`, knowingly: UI-DECISION.md budgets the eventual retrofit to
/// `NSCollectionView` for tens of thousands of cells, and blesses SwiftUI at
/// folder scale, which is what this view is scoped to.
struct GalleryView: View {
    @Bindable var library: Library
    /// The focused photograph - the one the arrow keys move and the bare
    /// keys act on. Owned by `Editor` (it outlives this view's appearance),
    /// distinct from `Editor.current`, which means "decoded on the canvas".
    let focused: URL?
    /// How many columns the grid is currently laying out, written back so
    /// `Editor.handleKey` can move focus a *row* without knowing the window's
    /// width. Read one place, written one place.
    @Binding var columns: Int

    let onFocus: (URL) -> Void
    /// Return or double-click: decode this photograph and go back to develop.
    let onOpen: (URL) -> Void
    /// Both trash routes confirm before touching a file - the closures here
    /// only *ask*. See `Editor.confirmTrash`.
    let onTrash: ([URL]) -> Void
    let onTrashRejected: () -> Void

    @AppStorage("galleryCellSize") private var cellSize: Double
        = Double(GalleryLayout.defaultCell)

    var body: some View {
        VStack(spacing: 0) {
            header
            Rectangle().fill(Palette.line).frame(height: 1)
            grid
        }
        .background(Palette.ground)
    }

    // MARK: Header

    private var header: some View {
        HStack(spacing: 10) {
            Picker("", selection: $library.filter) {
                ForEach(Library.Filter.allCases) { Text($0.title).tag($0) }
            }
            .pickerStyle(.menu)
            .labelsHidden()
            .frame(width: 84)

            Text(library.summary)
                .font(.system(size: 11))
                .monospacedDigit()
                .foregroundStyle(Palette.faint)
                .lineLimit(1)

            Spacer(minLength: 0)

            Button("Delete Rejected…") { onTrashRejected() }
                .buttonStyle(.plain)
                .font(.system(size: 11))
                .foregroundStyle(rejectedCount > 0 ? Palette.rejected : Palette.faint)
                .disabled(rejectedCount == 0)
                .help(rejectedCount > 0
                      ? "Move the \(rejectedCount) rejected photo\(rejectedCount == 1 ? "" : "s") to the Trash"
                      : "No rejected photos")

            // The size slider trades photos-per-screen against the detail a
            // framing judgement needs. Unlabeled but for the two glyphs: what
            // it does is visible the moment it moves.
            HStack(spacing: 5) {
                Image(systemName: "square.grid.3x3")
                    .font(.system(size: 9))
                    .foregroundStyle(Palette.faint)
                Slider(value: $cellSize,
                       in: Double(GalleryLayout.minCell)...Double(GalleryLayout.maxCell))
                    .frame(width: 110)
                    .controlSize(.mini)
                Image(systemName: "square.grid.2x2")
                    .font(.system(size: 12))
                    .foregroundStyle(Palette.faint)
            }
            .help("Cell size")
        }
        .padding(.horizontal, 12)
        .frame(height: 34)
        .background(Palette.panel)
    }

    // MARK: Grid

    private var grid: some View {
        GeometryReader { geo in
            let cell = CGFloat(cellSize)
            let cols = GalleryLayout.columns(
                width: geo.size.width - GalleryLayout.spacing * 2, cell: cell)

            ScrollViewReader { proxy in
                ScrollView(.vertical) {
                    LazyVGrid(
                        columns: Array(repeating: GridItem(.flexible(),
                                                           spacing: GalleryLayout.spacing),
                                       count: cols),
                        spacing: GalleryLayout.spacing) {
                        ForEach(library.visible) { photo in
                            cellView(photo)
                                .id(photo.url)
                        }
                    }
                    .padding(GalleryLayout.spacing)
                }
                .onChange(of: focused) { _, url in
                    guard let url else { return }
                    withAnimation(.easeOut(duration: 0.15)) {
                        proxy.scrollTo(url, anchor: nil)
                    }
                }
            }
            // Written from layout, read by `handleKey` for the up and down
            // arrows. `onChange` alone misses the first layout, so seed it
            // on appear too.
            .onAppear { columns = cols }
            .onChange(of: cols) { _, now in columns = now }
        }
    }

    /// One photograph. The cell's frame is fixed at 3:2 and the picture fits
    /// inside it - letterboxed, never cropped, because the question this view
    /// answers is *framing*, and a crop is the one thing a framing judgement
    /// cannot survive.
    private func cellView(_ photo: Library.Photo) -> some View {
        let isFocused = photo.url == focused
        // Same two marks as the filmstrip, same reason: the accent says "the
        // keys act here", the dimmer ring says "in the set a batch acts on".
        let inSelection = library.hasExplicitSelection && library.isSelected(photo.url)
        let cell = CGFloat(cellSize)

        return VStack(spacing: 4) {
            ZStack(alignment: .bottomLeading) {
                Group {
                    if let image = photo.thumbnail {
                        Image(nsImage: image)
                            .resizable()
                            .aspectRatio(contentMode: .fit)
                    } else {
                        Palette.raised
                            .aspectRatio(GalleryLayout.cellAspect, contentMode: .fit)
                    }
                }
                .frame(maxWidth: .infinity)
                .frame(height: cell / GalleryLayout.cellAspect)
                .background(Palette.filmBase)
                .opacity(photo.rejected ? 0.35 : 1)

                marks(photo)
                    .padding(.leading, 6)
                    .padding(.bottom, 5)
            }
            .overlay(
                RoundedRectangle(cornerRadius: 3)
                    .strokeBorder(isFocused ? Palette.accent
                                  : inSelection ? Palette.accent.opacity(0.55)
                                  : .white.opacity(0.08),
                                  lineWidth: isFocused ? 2 : inSelection ? 1.5 : 1)
            )

            Text(photo.name)
                .font(.system(size: 10))
                .foregroundStyle(isFocused ? Palette.text : Palette.faint)
                .lineLimit(1)
        }
        .contentShape(Rectangle())
        .help(photo.name)
        // Two clicks before one: with the order reversed the double never
        // fires, because the single wins both rounds.
        .onTapGesture(count: 2) { onOpen(photo.url) }
        .onTapGesture { tap(photo.url) }
        .accessibilityElement(children: .combine)
        .accessibilityAddTraits(
            isFocused || inSelection ? [.isButton, .isSelected] : .isButton)
        .accessibilityLabel(Text(spoken(photo)))
        .accessibilityAction { onOpen(photo.url) }
        .accessibilityAction(named: Text(photo.rejected ? "Keep" : "Reject")) {
            library.toggleRejected(photo.url)
        }
        .contextMenu { menu(photo) }
    }

    /// The cell's rating, drawn as stars - the strip's dots exist because a
    /// star is mush at 58 pt, and at gallery size that excuse is gone.
    private func marks(_ photo: Library.Photo) -> some View {
        HStack(spacing: 2) {
            if photo.rejected {
                Image(systemName: "xmark")
                    .font(.system(size: 11, weight: .bold))
                    .foregroundStyle(Palette.rejected)
            } else {
                ForEach(0..<photo.rating, id: \.self) { _ in
                    Image(systemName: "star.fill")
                        .font(.system(size: 9))
                        .foregroundStyle(Palette.rated)
                }
            }
        }
        .shadow(color: .black.opacity(0.8), radius: 1, y: 1)
    }

    @ViewBuilder
    private func menu(_ photo: Library.Photo) -> some View {
        let urls = library.cullScope(photo.url)
        // Counted when it is a group - same wording as the filmstrip, because
        // the menu is the last thing read before forty photographs change.
        let suffix = urls.count > 1 ? " (\(urls.count) photos)" : ""
        Button("Open in Editor") { onOpen(photo.url) }
        Divider()
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
        Divider()
        Button("Move to Trash…" + suffix) { onTrash(urls) }
    }

    /// A click on a cell, with whatever modifiers the hand was holding -
    /// `NSEvent.modifierFlags` read directly, for the reason `Filmstrip.tap`
    /// records: one handler asking what is held has one outcome, and the
    /// decision it feeds is in `PhotoSelection`, where the suite can see it.
    private func tap(_ url: URL) {
        let flags = NSEvent.modifierFlags
        var modifiers: PhotoSelection.Modifiers = []
        if flags.contains(.command) { modifiers.insert(.command) }
        if flags.contains(.shift)   { modifiers.insert(.shift) }

        // A plain click *focuses*; it never decodes. `click` returning a URL
        // means "the canvas should move there", which in this view means the
        // focus ring - opening is Return's and the double-click's job.
        if let open = library.click(url, modifiers: modifiers) { onFocus(open) }
    }

    private var rejectedCount: Int { library.photos.filter(\.rejected).count }

    /// What VoiceOver says for one cell - the filmstrip's wording, because a
    /// photograph should not be announced differently in two views.
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
}
