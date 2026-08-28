import AppKit
import SwiftUI

// What a key press does.
//
// ⚠ **Both mechanisms live here, and they have to be read together**, because
// each exists to cover what the other cannot. A menu command routes through the
// responder chain, so it keeps firing once the Metal canvas has taken first
// responder — which is what killed the bare `onKeyPress` handlers these
// replaced. A *bare* letter or digit as a menu key equivalent then fires in
// every key window, which took `r` and the digits out of the Open panel's
// type-ahead and Return and Escape off its buttons — which is why the unmodified
// keys are a local event monitor instead. Neither history is decoration: both
// were bugs, and the comments that record them sit on the code they explain.
//
// So: a shortcut with ⌘ is a `PhotoCommands` button; a shortcut without one is a
// case in `handleKey`; and anything the menu has to *read* — a title that says
// Keep rather than Reject, whether the item is greyed — is a field of
// `CullActions` filled in by `Editor.cullActions`. All four are in this file, so
// adding a command is one file no matter which of the four it needs.

/// Culling, as menu commands.
///
/// These were bare `onKeyPress` handlers on the editor's root view, which only
/// fire while SwiftUI holds focus — and the Metal canvas takes first responder
/// on any click, so rejecting stopped working the moment you touched the photo.
/// A menu command routes through the responder chain regardless, and has the
/// side benefit of being findable: the shortcut is written next to its name
/// instead of having to be known in advance.
struct PhotoCommands: Commands {
    @FocusedValue(\.cull) private var cull

    /// Nothing open means nothing to cull.
    private var idle: Bool { cull?.url == nil }

    /// True while the gallery is up, which is when every command that needs a
    /// canvas greys out - there is no canvas to zoom, compare or crop.
    private var gallery: Bool { cull?.inGallery == true }

    var body: some Commands {
        CommandGroup(after: .sidebar) {
            ForEach(Array(ToolTab.allCases.enumerated()), id: \.element) { index, t in
                Button(t.title) { cull?.selectTab(t) }
                    .keyboardShortcut(KeyEquivalent(Character("\(index + 1)")),
                                      modifiers: [.command])
                    .disabled(idle || gallery)
            }

            Divider()

            // The bare G lives in the key monitor with the other unmodified
            // keys; the title spells it, the way "Reject  (R)" does.
            Button(gallery ? "Exit Gallery  (G)" : "Gallery  (G)") {
                cull?.toggleGallery()
            }
            .disabled(idle)

            Divider()

            // ⚠ `Text(verbatim:)`, not a bare string. A `Button`'s title is a
            // `LocalizedStringKey`, whose escape character is the backslash — so
            // `"Compare Original  (\\)"` is `Compare Original  (\)` in Swift and
            // ships as **`Compare Original  ()`**. The one item in the bar that
            // spells its shortcut only in its title was the one item that lost
            // it, and it read as a typo rather than a bug. Found by the menu
            // check on its first run (#125); fixed here.
            Button {
                cull?.toggleCompare()
            } label: {
                Text(verbatim: cull?.comparing == true
                     ? "Hide Original  (\\)" : "Compare Original  (\\)")
            }
            .disabled(idle || gallery)

            Divider()

            Button("Fit in Window  (0)") { cull?.fit() }
                .keyboardShortcut("0", modifiers: [.command])
                .disabled(idle || gallery)
            Button("Actual Size  (9)") { cull?.actualSize() }
                .keyboardShortcut("9", modifiers: [.command])
                .disabled(idle || gallery)
        }

        CommandGroup(after: .importExport) {
            Button("Export…") { cull?.export() }
                .keyboardShortcut("e", modifiers: [.command])
                .disabled(idle)

            Divider()

            // The session log, which is a runnable scenario. Findable from a
            // menu rather than documented in a file nobody opens: the whole
            // point of it is that a person reporting a bug can hand it over,
            // and a path they have to be told is a path they have to be told
            // every time.
            Button("Reveal Session Log in Finder") {
                NSWorkspace.shared.activateFileViewerSelecting([InteractionLog.url])
            }
        }

        CommandGroup(after: .undoRedo) {
            Button("Reset Adjustments") { cull?.resetEdits() }
                .keyboardShortcut("r", modifiers: [.command])
                .disabled(idle || gallery)
        }

        CommandMenu("Photo") {
            // The bare keys are handled by the canvas, which only has them
            // while it is first responder. As menu key equivalents they fired
            // in every window, including the Open panel — where "r" and the
            // digits ate the type-ahead and Return and Escape took the
            // buttons, so a raw file could not be opened at all.
            Button(cull?.isRejected == true ? "Keep  (R)" : "Reject  (R)") {
                cull?.toggleReject()
            }
            .disabled(idle)

            Divider()

            ForEach(1...5, id: \.self) { n in
                Button("\(n) Star\(n == 1 ? "" : "s")  (\(n))") { cull?.rate(n) }
                    .disabled(idle)
            }
            Button("No Rating  (`)") { cull?.rate(0) }
                .disabled(idle)

            Divider()

            Button("Next Photo  (→)") { cull?.step(1) }
                .keyboardShortcut(.rightArrow, modifiers: [.command])
                .disabled(idle)
            Button("Previous Photo  (←)") { cull?.step(-1) }
                .keyboardShortcut(.leftArrow, modifiers: [.command])
                .disabled(idle)

            Divider()

            // ⌘A and ⌘⇧A rather than ⌘A and Escape: Escape already cancels the
            // crop, and a key that does two unrelated things depending on which
            // tool is open is a key nobody trusts.
            Button("Select All Photos") { cull?.selectAll() }
                .keyboardShortcut("a", modifiers: [.command])
                .disabled(idle)
            Button(cull.map { $0.selectionCount > 1
                              ? "Deselect \($0.selectionCount) Photos"
                              : "Deselect" } ?? "Deselect") {
                cull?.collapseSelection()
            }
            .keyboardShortcut("a", modifiers: [.command, .shift])
            .disabled((cull?.selectionCount ?? 0) < 2)

            Divider()

            // Both confirm before touching a file, which is why the titles
            // carry an ellipsis. The counts live in the confirmation rather
            // than in these titles - the menu check pins titles by string, and
            // a title that counts is a title that is never the same twice.
            Button("Move to Trash…") { cull?.trashFocused() }
                .keyboardShortcut(.delete, modifiers: [.command])
                .disabled(idle)
            Button("Delete Rejected Photos…") { cull?.trashRejected() }
                .disabled((cull?.rejectedCount ?? 0) == 0)

            Divider()

            Button("Apply Crop  (⏎)") { cull?.applyCrop() }
                .disabled(cull?.cropping != true)
            Button("Cancel Crop  (⎋)") { cull?.cancelCrop() }
                .disabled(cull?.cropping != true)
        }
    }
}

/// What the Photo menu acts on. Equatable by state rather than by closure, so
/// SwiftUI can tell when the menu's titles need to change.
struct CullActions: Equatable {
    /// nil when nothing is open, which is what grays the menu out.
    let url: URL?
    let isRejected: Bool
    let rating: Int
    let toggleReject: () -> Void
    let rate: (Int) -> Void
    let step: (Int) -> Void
    let fit: () -> Void
    let actualSize: () -> Void
    let resetEdits: () -> Void

    /// True while the crop tool is open, which is when Apply and Cancel mean
    /// anything.
    let cropping: Bool
    let applyCrop: () -> Void
    let cancelCrop: () -> Void

    /// How many photographs a batch will act on, or 0 when no selection has
    /// been made and it is simply everything in view.
    let selectionCount: Int
    let selectAll: () -> Void
    let collapseSelection: () -> Void

    /// Which tool panel is showing, and how to change it. On the menu so the
    /// four panels have shortcuts, and so a keyboard user can reach the tools
    /// without knowing that the tab strip exists.
    let tab: ToolTab
    let selectTab: (ToolTab) -> Void

    let comparing: Bool
    let toggleCompare: () -> Void
    let export: () -> Void

    /// True while the gallery is up. In the equality because half the menu
    /// bar greys on it.
    let inGallery: Bool
    let toggleGallery: () -> Void

    /// Both ask for confirmation; neither touches a file on its own.
    let trashFocused: () -> Void
    let trashRejected: () -> Void
    /// In the equality: it is what enables Delete Rejected Photos…, and a
    /// rejection from a context menu changes it without moving `url` or
    /// `isRejected`.
    let rejectedCount: Int

    static func == (a: CullActions, b: CullActions) -> Bool {
        a.url == b.url && a.isRejected == b.isRejected && a.rating == b.rating
            && a.cropping == b.cropping && a.tab == b.tab && a.comparing == b.comparing
            && a.inGallery == b.inGallery && a.rejectedCount == b.rejectedCount
    }
}

private struct CullActionsKey: FocusedValueKey {
    typealias Value = CullActions
}

extension FocusedValues {
    var cull: CullActions? {
        get { self[CullActionsKey.self] }
        set { self[CullActionsKey.self] = newValue }
    }
}

extension Editor {
    /// Published to the scene so the Photo and View menus can act on whatever
    /// is open, from wherever focus happens to be.
    var cullActions: CullActions {
        // ⚠ In the gallery the menu acts on the *focused* photograph, not the
        // decoded one - pressing 3 while looking at a grid cell that is not
        // on the canvas must rate the cell being looked at, or the menu and
        // the grid disagree about what the keys do.
        let focused = mode == .cull ? (galleryFocus ?? current) : current
        let photo = focused.flatMap { url in library.photos.first { $0.url == url } }
        return CullActions(
            url: focused,
            isRejected: photo?.rejected ?? false,
            rating: photo?.rating ?? 0,
            // ⚠ Over the selection when there is one, so the Photo menu and the
            // filmstrip's context menu agree about what R and the digits do.
            // Two rating paths with two scopes is how a photographer rates one
            // frame from the menu and forty from the strip and cannot say which
            // rule they are under.
            toggleReject: {
                guard let focused, let photo else { return }
                library.setRejected(!photo.rejected, for: library.cullScope(focused))
            },
            rate: { stars in
                guard let focused else { return }
                library.setRating(stars, for: library.cullScope(focused))
            },
            // Stepping in the gallery moves the focus ring, never the canvas:
            // ⌘→ across two hundred frames must not be two hundred decodes.
            step: { offset in
                mode == .cull ? moveGalleryFocus(offset > 0 ? .right : .left)
                              : step(offset)
            },
            fit: { viewport.reset() },
            actualSize: { viewport.toggleFitAndActual() },
            resetEdits: { engine.resetEdits() },
            cropping: tab == .crop && mode == .develop,
            // Applying is just leaving the tool: the preview canvas goes away
            // and the engine renders the crop itself.
            applyCrop: { tab = .light },
            cancelCrop: { engine.edit("Crop") { engine.resetCrop() }; tab = .light },
            selectionCount: library.hasExplicitSelection ? library.targets.count : 0,
            selectAll: { library.selectAll() },
            collapseSelection: { library.collapseSelection() },
            tab: tab,
            selectTab: { tab = $0 },
            comparing: engine.comparing,
            toggleCompare: {
                engine.comparing ? engine.clearCompare() : engine.setCompare(split: 0.5)
            },
            export: { showingExport = true },
            inGallery: mode == .cull,
            toggleGallery: { mode == .cull ? leaveGallery() : enterGallery() },
            trashFocused: {
                guard let focused else { return }
                confirmTrash(library.cullScope(focused))
            },
            trashRejected: { confirmTrashRejected() },
            rejectedCount: library.photos.filter(\.rejected).count)
    }

    // MARK: The gallery's comings and goings

    func enterGallery() {
        guard !library.photos.isEmpty else { return }
        galleryFocus = current ?? library.visible.first?.url
        if let galleryFocus { library.focus(galleryFocus) }
        mode = .cull
    }

    /// Back to the canvas, which still holds whatever it held - leaving the
    /// gallery is free and never decodes, with one exception: a canvas whose
    /// photograph was trashed from the grid has nothing honest to show, so
    /// stepping out of the gallery then loads the focused photograph instead
    /// of presenting a picture that is in the Trash.
    func leaveGallery() {
        mode = .develop
        if current == nil, let next = galleryFocus ?? library.visible.first?.url {
            load(next)
        } else {
            library.focus(current)
        }
    }

    /// Return or a double-click: the one gallery gesture that costs a decode.
    func openFromGallery(_ url: URL) {
        mode = .develop
        load(url)
    }

    func moveGalleryFocus(_ direction: GalleryLayout.Direction) {
        let list = library.visibleURLs
        guard !list.isEmpty else { return }
        let i = galleryFocus.flatMap { list.firstIndex(of: $0) } ?? 0
        let next = GalleryLayout.move(from: i, direction: direction,
                                      columns: galleryColumns, count: list.count)
        galleryFocus = list[next]
        // The selection follows the focus the way it follows the canvas -
        // arrowing must not accumulate a selection nobody asked for.
        library.focus(list[next])
    }

    /// Single keys, seen before AppKit dispatches them.
    ///
    /// Not menu key equivalents: a bare letter or digit on a menu item fires in
    /// any key window, which took `r` and the digits from the Open panel's
    /// type-ahead and Return and Escape from its buttons. Not the canvas's
    /// keyDown either: that only arrives while the canvas is first responder,
    /// and SwiftUI's own focus machinery holds it most of the time, so the keys
    /// simply did nothing.
    ///
    /// A local monitor sees every key event and hands back the ones that belong
    /// to somebody else — a sheet, a panel, a text field, or anything with a
    /// modifier.
    func installKeyMonitor() {
        guard keyMonitor == nil else { return }
        keyMonitor = NSEvent.addLocalMonitorForEvents(matching: .keyDown) { event in
            guard let window = NSApp.keyWindow, window.sheets.isEmpty,
                  !(window is NSPanel),
                  !(window.firstResponder is NSTextView)
            else { return event }

            return handleKey(event) ? nil : event
        }
    }

    private func handleKey(_ event: NSEvent) -> Bool {
        guard !event.modifierFlags.contains(.command) else { return false }
        let press = (characters: event.charactersIgnoringModifiers ?? "",
                     keyCode: event.keyCode)

        // The gallery has its own key map - same monitor, different face. It
        // does not wait for `engine.isLoaded`: the grid is browsable the
        // moment the folder lists, decoded photograph or not.
        if mode == .cull { return handleGalleryKey(press) }

        // Escape and Return, by key code: their characters are control
        // sequences that do not compare well.
        switch press.keyCode {
        case 53:   // escape
            guard tab == .crop else { return false }
            engine.edit("Crop") { engine.resetCrop() }
            tab = .light
            return true
        case 36, 76:   // return, enter
            guard tab == .crop else { return false }
            tab = .light
            return true
        case 123:  // left arrow
            step(-1); return true
        case 124:  // right arrow
            step(1); return true
        default:
            break
        }

        // Before the `isLoaded` guard: the gallery needs a folder, not a
        // decoded photograph.
        if press.characters.lowercased() == "g" {
            guard !library.photos.isEmpty else { return false }
            enterGallery()
            return true
        }

        guard engine.isLoaded else { return false }

        switch press.characters.lowercased() {
        case "r":
            guard let current else { return false }
            library.toggleRejected(current); return true
        case "1", "2", "3", "4", "5":
            guard let n = Int(press.characters) else { return false }
            rate(n); return true
        case "`":
            rate(0); return true
        case "0":
            viewport.reset(); return true
        case "9":
            viewport.toggleFitAndActual(); return true
        case "\\":
            engine.comparing ? engine.clearCompare() : engine.setCompare(split: 0.5)
            return true
        case "[":
            engine.edit("Rotate") { engine.rotate(-1) }; viewport.reset(); return true
        case "]":
            engine.edit("Rotate") { engine.rotate(1) }; viewport.reset(); return true
        default:
            return false
        }
    }

    /// The gallery's keys. Arrows move the focus ring in two dimensions,
    /// Return opens, Escape and G leave, and the culling keys act on the
    /// focus through the same `cullScope` the menus use. The develop keys
    /// that need a canvas - zoom, compare, rotate - are swallowed rather than
    /// passed on, so they cannot act on a view that is not showing.
    private func handleGalleryKey(
        _ press: (characters: String, keyCode: UInt16)) -> Bool {
        switch press.keyCode {
        case 53:   // escape
            leaveGallery(); return true
        case 36, 76:   // return, enter
            if let galleryFocus { openFromGallery(galleryFocus) }
            return true
        case 123: moveGalleryFocus(.left);  return true
        case 124: moveGalleryFocus(.right); return true
        case 125: moveGalleryFocus(.down);  return true
        case 126: moveGalleryFocus(.up);    return true
        default:
            break
        }

        switch press.characters.lowercased() {
        case "g":
            leaveGallery(); return true
        case "r":
            guard let galleryFocus,
                  let photo = library.photos.first(where: { $0.url == galleryFocus })
            else { return false }
            library.setRejected(!photo.rejected,
                                for: library.cullScope(galleryFocus))
            return true
        case "1", "2", "3", "4", "5":
            guard let galleryFocus, let n = Int(press.characters) else { return false }
            library.setRating(n, for: library.cullScope(galleryFocus))
            return true
        case "`":
            guard let galleryFocus else { return false }
            library.setRating(0, for: library.cullScope(galleryFocus))
            return true
        case "0", "9", "\\", "[", "]":
            // Zoom, compare and rotate belong to the canvas. Swallowed, not
            // forwarded: acting on an invisible view and saying nothing is
            // worse than doing nothing.
            return true
        default:
            return false
        }
    }

    private func rate(_ stars: Int) {
        guard let current else { return }
        library.setRating(stars, for: current)
    }

    private func step(_ offset: Int) {
        guard let current, let next = library.neighbor(of: current, offset: offset) else {
            return
        }
        load(next)
    }
}
