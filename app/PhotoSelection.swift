import Foundation

/// Which photographs a batch operation acts on.
///
/// Pure value logic, in its own file with no AppKit and no facade call, so
/// `orion-viewport-tests` can pin it without a GPU or a folder of raws — the
/// same split `MatteGeometry` and `BatchExport` already use. `Library` owns one
/// of these and does the parts that need a filesystem.
///
/// ## ⚠ One selected photo is not a selection
///
/// The photograph on the canvas is always in `chosen`, because it is the one
/// being looked at and every navigation puts it there. So a selection of *one*
/// is the resting state of the interface, not a decision anybody made — and
/// `targets(in:)` therefore falls back to everything in view until there are
/// **two**. Lightroom draws the same line.
///
/// The alternative is a flag recording whether the selection was "deliberate",
/// which is a second piece of state that can disagree with the first. This rule
/// needs no state and is readable off the screen: the filmstrip shows a count
/// only once it means something, so what a batch will do is what the strip says
/// it will do.
struct PhotoSelection: Equatable {

    /// Which modifier a click carried. An `OptionSet` of its own rather than
    /// AppKit's, so this file stays testable and the translation happens once,
    /// at the gesture.
    struct Modifiers: OptionSet {
        let rawValue: Int
        static let command = Modifiers(rawValue: 1 << 0)
        static let shift   = Modifiers(rawValue: 1 << 1)
        static let none: Modifiers = []
    }

    private(set) var chosen: Set<URL> = []

    /// Where a shift-click measures from. Moved by every click that is not a
    /// shift-click, exactly as the platform does it — otherwise a range
    /// extension after a plain click reaches back to wherever the last range
    /// happened to start.
    private(set) var anchor: URL?

    /// The photograph on the canvas. Held here because two rules need it: it
    /// can never be deselected, and it is what the selection is reseeded from
    /// when a filter hides everything that was chosen.
    private(set) var current: URL?

    var count: Int { chosen.count }

    /// True once the selection means something — see the note above.
    var isExplicit: Bool { chosen.count >= 2 }

    func contains(_ url: URL) -> Bool { chosen.contains(url) }

    // MARK: Navigation

    /// The canvas moved to a photograph, by any route: a click, an arrow key,
    /// opening a folder. Collapses the selection onto it.
    ///
    /// ⚠ Collapsing rather than merely adding. Arrowing through a folder would
    /// otherwise accumulate every frame passed over into a selection nobody
    /// asked for, and the next Export All would act on all of them.
    mutating func focus(_ url: URL?) {
        current = url
        anchor = url
        chosen = url.map { [$0] } ?? []
    }

    // MARK: Clicks

    /// A click on a filmstrip cell. Returns the photograph the canvas should
    /// move to, or nil when the click only changed the selection.
    ///
    /// ⚠ A modified click does **not** move the canvas. Opening a photograph
    /// costs a raw decode, and building a selection of forty frames is not
    /// forty requests to look at one.
    @discardableResult
    mutating func click(_ url: URL, modifiers: Modifiers,
                        in visible: [URL]) -> URL? {
        // Shift wins over command when both are held: the gesture is "extend",
        // and command only decides whether the extension replaces or adds.
        if modifiers.contains(.shift), let from = anchor,
           let a = visible.firstIndex(of: from), let b = visible.firstIndex(of: url) {
            let range = visible[min(a, b)...max(a, b)]
            chosen = modifiers.contains(.command)
                ? chosen.union(range)
                : Set(range)
            // The anchor deliberately stays put, so dragging a shift-click
            // along the strip grows and shrinks one range rather than walking.
            if let current { chosen.insert(current) }
            return nil
        }

        if modifiers.contains(.command) {
            // ⚠ The photograph on the canvas cannot be deselected. It is the
            // one being looked at, and a sync that skipped it while its own
            // panel showed the settings being copied would be indefensible.
            if chosen.contains(url), url != current {
                chosen.remove(url)
            } else {
                chosen.insert(url)
            }
            anchor = url
            return nil
        }

        focus(url)
        return url
    }

    // MARK: Whole-list operations

    mutating func selectAll(in visible: [URL]) {
        chosen = Set(visible)
        if let current { chosen.insert(current) }
        if anchor == nil { anchor = visible.first }
    }

    /// Back to just the photograph on the canvas.
    mutating func collapse() { focus(current) }

    /// Drops anything the filter no longer shows.
    ///
    /// ⚠ Called whenever `visible` changes. Without it, filtering to Rated and
    /// exporting would write the rejects that were selected before the filter
    /// moved — photographs the photographer cannot see, in a list they cannot
    /// check.
    mutating func confine(to visible: [URL]) {
        let live = Set(visible)
        chosen.formIntersection(live)
        if let current, live.contains(current) { chosen.insert(current) }
        if let a = anchor, !live.contains(a) { anchor = chosen.isEmpty ? nil : visible.first(where: chosen.contains) }
    }

    // MARK: What a batch acts on

    /// The photographs an operation should touch, in the order they appear.
    ///
    /// Everything in view unless there is a real selection — see the note at
    /// the top. Always filtered through `visible` and returned in its order, so
    /// a caller cannot be handed a photograph the filter is hiding or a list in
    /// set order, which is to say in no order at all.
    func targets(in visible: [URL]) -> [URL] {
        guard isExplicit else { return visible }
        return visible.filter(chosen.contains)
    }

    /// What the interface should say a batch will cover. Empty when there is
    /// nothing worth saying, so the caller can leave the readout out entirely
    /// rather than showing "1 selected" for the resting state.
    func summary(in visible: [URL]) -> String {
        guard isExplicit else { return "" }
        return "\(targets(in: visible).count) selected"
    }
}
