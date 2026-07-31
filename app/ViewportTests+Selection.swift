// Split out of ViewportTests.swift 2026-07-31.

import CoreGraphics
import Foundation

// MARK: - Filmstrip multi-selection
//
// `PhotoSelection` decides what every batch operation acts on — a sync that
// rewrites forty sidecars, an export that writes forty files. Neither is
// undoable across photographs, so "which forty" is the load-bearing question
// and it is pure logic, which means it can be pinned exactly rather than looked
// at in a strip.

extension ViewportTests {

    /// Five frames, as the filmstrip would list them.
    static var strip: [URL] {
        (1...5).map { URL(fileURLWithPath: "/photos/\($0).ARW") }
    }

    /// ⚠ The rule the whole feature rests on: the photograph on the canvas is
    /// always in `chosen`, so a selection of *one* is the resting state of the
    /// interface and not a decision. Until there are two, a batch means
    /// everything in view.
    ///
    /// Get this wrong in the obvious direction — "act on the selection whenever
    /// it is non-empty" — and Export All silently exports one photograph, for
    /// every user, on every folder. It would look like the button being broken.
    static func testOnePhotoIsNotASelection() {
        let s = strip
        var sel = PhotoSelection()

        sel.focus(s[2])
        report(sel.count == 1, "the open photo is in the selection", "\(sel.count)")
        report(!sel.isExplicit, "one photo is not an explicit selection")
        report(sel.targets(in: s) == s,
               "so a batch acts on everything in view",
               "\(sel.targets(in: s).count) of \(s.count)")
        report(sel.summary(in: s).isEmpty,
               "and the strip says nothing about a selection", sel.summary(in: s))

        sel.click(s[3], modifiers: .command, in: s)
        report(sel.isExplicit && sel.targets(in: s) == [s[2], s[3]],
               "two photos is a selection, and a batch narrows to it",
               "\(sel.targets(in: s))")
        report(sel.summary(in: s) == "2 selected",
               "which the strip now says", sel.summary(in: s))
    }

    /// A plain click replaces; command adds and removes; and only a plain click
    /// asks for the photograph to be opened.
    ///
    /// ⚠ That last part is the one worth a check. A modified click returning a
    /// URL would make building a selection of forty frames forty raw decodes —
    /// about twenty seconds of the interface refusing to respond, for a gesture
    /// that was never a request to look at anything.
    static func testModifiedClicksBuildASelection() {
        let s = strip
        var sel = PhotoSelection()
        sel.focus(s[0])

        report(sel.click(s[2], modifiers: .none, in: s) == s[2],
               "a plain click opens the photo it landed on")
        report(sel.count == 1 && sel.contains(s[2]),
               "and collapses the selection onto it", "\(sel.count)")

        report(sel.click(s[4], modifiers: .command, in: s) == nil,
               "a command-click opens nothing")
        report(sel.targets(in: s) == [s[2], s[4]],
               "and adds to the selection", "\(sel.targets(in: s))")

        sel.click(s[4], modifiers: .command, in: s)
        report(!sel.isExplicit && sel.contains(s[2]),
               "command-clicking a selected photo removes it again")
    }

    /// Shift extends from the anchor, and the anchor does not move while it
    /// does — so sweeping a shift-click along the strip grows and shrinks one
    /// range instead of walking a two-frame window down it.
    static func testShiftClickIsARangeFromTheAnchor() {
        let s = strip
        var sel = PhotoSelection()
        sel.focus(s[1])

        sel.click(s[3], modifiers: .shift, in: s)
        report(sel.targets(in: s) == [s[1], s[2], s[3]],
               "shift selects the range from the anchor",
               "\(sel.targets(in: s))")

        // Back the other way, past the anchor. The anchor is still frame 1, so
        // this is 0...1 and not 0...3.
        sel.click(s[0], modifiers: .shift, in: s)
        report(sel.targets(in: s) == [s[0], s[1]],
               "and re-extending measures from the same anchor, not the last end",
               "\(sel.targets(in: s))")

        // Command with shift adds a second range rather than replacing.
        sel.click(s[2], modifiers: .command, in: s)   // anchor moves to 2
        sel.click(s[4], modifiers: [.command, .shift], in: s)
        report(sel.targets(in: s) == [s[0], s[1], s[2], s[3], s[4]],
               "command-shift unions a second range instead of replacing",
               "\(sel.targets(in: s))")
    }

    /// ⚠ The open photograph cannot be command-clicked out of the selection.
    ///
    /// Its settings are what a sync copies *from*, and its panel is what the
    /// photographer is reading while they decide. A sync that wrote the other
    /// thirty-nine and skipped the one on screen would be indefensible, and
    /// nothing on screen would say it had happened.
    static func testTheOpenPhotoCannotBeDeselected() {
        let s = strip
        var sel = PhotoSelection()
        sel.focus(s[2])
        sel.click(s[0], modifiers: .command, in: s)
        sel.click(s[2], modifiers: .command, in: s)

        report(sel.contains(s[2]),
               "command-clicking the open photo does not drop it")
        report(sel.targets(in: s) == [s[0], s[2]],
               "and the rest of the selection is untouched",
               "\(sel.targets(in: s))")

        // A range that skips it still contains it.
        sel.focus(s[4])
        sel.click(s[0], modifiers: .command, in: s)
        sel.click(s[1], modifiers: .shift, in: s)
        report(sel.contains(s[4]),
               "and a range that does not reach it keeps it anyway",
               "\(sel.targets(in: s))")
    }

    /// ⚠ Filtering to Rated must not leave a rejected frame in the target list.
    ///
    /// A selection is a set of URLs and the filter is a view over a different
    /// list; nothing connects them unless something does it on purpose. Without
    /// this, culling to the picks and pressing Export All writes the rejects
    /// that were selected before the filter moved — photographs the person
    /// cannot see, in a list they cannot check.
    static func testAFilterChangeCannotHideATarget() {
        let s = strip
        var sel = PhotoSelection()
        sel.focus(s[0])
        sel.selectAll(in: s)
        report(sel.targets(in: s).count == 5, "select-all takes everything in view")

        // Frames 1 and 3 are filtered out.
        let narrowed = [s[0], s[2], s[4]]
        sel.confine(to: narrowed)
        report(sel.targets(in: narrowed) == narrowed,
               "a filter change drops what it hides",
               "\(sel.targets(in: narrowed))")
        report(!sel.contains(s[1]) && !sel.contains(s[3]),
               "and the hidden frames are gone from the set, not merely unlisted")

        // Narrowing to nothing but the open photo is not a selection any more,
        // so a batch goes back to meaning everything in view.
        sel.confine(to: [s[0]])
        report(!sel.isExplicit && sel.targets(in: [s[0]]) == [s[0]],
               "and collapsing to one photo stops being a selection")
    }

    /// Targets come back in strip order, always — a `Set` has none, and a batch
    /// export names its files in the order it walks them.
    static func testTargetsComeBackInStripOrder() {
        let s = strip
        var sel = PhotoSelection()
        sel.focus(s[4])
        sel.click(s[0], modifiers: .command, in: s)
        sel.click(s[2], modifiers: .command, in: s)

        report(sel.targets(in: s) == [s[0], s[2], s[4]],
               "targets follow the strip, not the set",
               "\(sel.targets(in: s).map(\.lastPathComponent))")

        // And a photo that is selected but not in this list never appears.
        report(sel.targets(in: [s[2], s[4]]) == [s[2], s[4]],
               "a target the caller's list does not contain is not returned",
               "\(sel.targets(in: [s[2], s[4]]))")
    }
}
