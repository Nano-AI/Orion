import AppKit
import Foundation
import SwiftUI

/// The scenes that are checks rather than pictures.
///
/// A scene that *asserts* is a different kind of thing from a scene that
/// *poses*, and this file is the first kind. Each was written against a named
/// mutation that deleted shipped interface with every check in the repository
/// green (decision #125), so each exists to be red on exactly one deletion.
///
/// | Scene | Fires when | Exits nonzero |
/// |---|---|---|
/// | `detail-tail` | anything below the fold in Detail is deleted or moved | when nothing overflows the panel column |
/// | `render-failed` | the footer stops drawing `engine.lastFailure` | yes — it renders the control itself and compares |
/// | `menu` | a `PhotoCommands` item is deleted or renamed | when a command is missing |
///
/// **All three exit nonzero on their own mutation, so `tools/check-screens.py`
/// can run them as a gate.** `render-failed` was the exception until
/// 2026-08-07: its oracle was two PNGs and a person, which is to say it ran
/// nowhere. See `assertFailureLineDrawn`.
///
/// ⚠ **Adding a check-scene is this file plus one `case` in
/// `Screenshot+Scenes.swift`.** What belongs here is the assertion and the
/// knobs the assertion needs — the required list, the window a scene demands,
/// whether it is captured scrolled. What belongs there is the engine state the
/// scene poses in, which a check-scene has like any other.
extension Screenshot {

    // MARK: The menu

    /// Set once this process has handed itself back to `OrionApp`.
    nonisolated(unsafe) static var relaunched = false

    /// Every command `PhotoCommands` puts in front of a photographer.
    ///
    /// ⚠ **Titles, not closures, and that distinction is the whole point.** The
    /// actions behind these live on `CullActions`, and `CullActions` is reachable
    /// from a test — so a check written against it would have gone green on the
    /// mutation that motivated this one, which deletes the *button* and leaves
    /// the action it called sitting there unreferenced. What was unchecked was
    /// whether the command is in a menu at all.
    ///
    /// Written out one by one rather than counted: a count says the menu is the
    /// size it was, and this says which command went missing. The parenthetical
    /// keys are part of the title — the bare keys are the local monitor's, so
    /// the menu spells them out instead of owning them (`OrionApp+Commands`).
    /// The three titles that change with state are given in their idle form,
    /// which is the form a launch with nothing open produces either way: `cull`
    /// is nil before a window takes focus, and its `url` is nil after.
    private static let requiredCommands: [String] = [
        // The tool tabs, ⌘1–⌘7.
        "Light", "Color", "Detail", "Optics", "Mask", "Crop", "Presets",
        // ⚠ **This entry is `Compare Original  (\)` and it is the fixed
        // spelling, not the shipped bug.** A `Button`'s string is a
        // `LocalizedStringKey`, whose escape character is the backslash, so the
        // item shipped as **`Compare Original  ()`** — the one command that
        // spells its shortcut only in its title was the one that lost it.
        // Nothing could see it until something read the real menu bar (#125).
        // `OrionApp+Commands` now builds this title with `Text(verbatim:)`, so
        // the backslash survives and this line matches what AppKit installs.
        // ⚠ Reverting that `Text(verbatim:)` to a bare string reddens this.
        "Compare Original  (\\)",
        // Idle form: `inGallery` is false with no window focused, so the
        // launch menu says enter, not exit.
        "Gallery  (G)",
        "Fit in Window  (0)", "Actual Size  (9)",
        "Export…",
        // ⚠ The batch, at the title it carries with nothing open — which is the
        // state this check runs in. It counts the photographs once there are
        // some ("Export All 45 in View…"), so pinning the empty form is what can
        // be pinned here; that it *is* on the menu is the part that was missing,
        // and the part a photographer looking for bulk export needs.
        "Export All…",
        "Reveal Session Log in Finder",
        "Reset Adjustments",
        "Reject  (R)",
        "1 Star  (1)", "2 Stars  (2)", "3 Stars  (3)", "4 Stars  (4)",
        "5 Stars  (5)", "No Rating  (`)",
        "Next Photo  (→)", "Previous Photo  (←)",
        "Select All Photos", "Deselect",
        "Move to Trash…", "Delete Rejected Photos…",
        "Apply Crop  (⏎)", "Cancel Crop  (⎋)",
    ]

    /// `Orion --screenshot x --scene menu` — the Photo and View menus, checked
    /// against the menu bar AppKit actually builds.
    ///
    /// ## ⚠ Why this re-launches the app instead of rendering something
    ///
    /// Every other scene here builds `Editor` directly, and `PhotoCommands` is
    /// not in `Editor` — it is a `Commands`, attached to `OrionApp`'s `Scene`.
    /// A `Commands` is not a `View`: there is no hosting view to put it in and
    /// nothing to photograph. So decision #121's M6 deleted the Reset
    /// Adjustments command from the product and **every check in the repository
    /// stayed green**, because nothing anywhere had ever built the `Scene`.
    ///
    /// The only honest way to see that menu is to let the app launch: clear the
    /// harness's own hook (`relaunched`), call `OrionApp.main()`, and read
    /// `NSApp.mainMenu` once AppKit has installed it. What comes back is the
    /// shipping menu bar, built by the shipping `Scene` from the shipping
    /// `PhotoCommands` — not a second list of what it ought to contain.
    ///
    /// ⚠ **A window really does open**, for about the second this takes. That is
    /// the price of the only path to the thing, and it is why this is a scene
    /// somebody runs rather than a frame in the sweep.
    ///
    /// ⚠ **It asserts presence, not that the items work.** The commands are
    /// disabled at launch — nothing is open, so `cull?.url` is nil and every
    /// item greys out — and firing one would need a photograph, a key window and
    /// focus. Presence is exactly the hole that was measured, and the actions
    /// behind the titles are `CullActions`, which the scenario runner drives.
    static func checkMenu() -> Never {
        relaunched = true

        // The menu bar is built during launch, after `App.init` returns. Read it
        // from the main queue once the run loop is turning.
        DispatchQueue.main.asyncAfter(deadline: .now() + 1.5) {
            guard let bar = NSApp.mainMenu else {
                fail("the app launched with no main menu at all")
            }

            var found: [String] = []
            func walk(_ menu: NSMenu, depth: Int) {
                for item in menu.items where !item.isSeparatorItem {
                    found.append(item.title)
                    if let sub = item.submenu { walk(sub, depth: depth + 1) }
                }
            }
            walk(bar, depth: 0)

            let missing = requiredCommands.filter { !found.contains($0) }
            let report = "orion: the menu bar carries \(found.count) items; "
                + "\(requiredCommands.count - missing.count) of "
                + "\(requiredCommands.count) commands present\n"
            FileHandle.standardError.write(Data(report.utf8))
            for title in missing {
                FileHandle.standardError.write(Data(
                    "orion: MISSING from the menu bar — \"\(title)\"\n".utf8))
            }
            // The whole bar, when something is missing: a command that has been
            // *renamed* is indistinguishable from one that has been deleted
            // until you can see what is there instead.
            if !missing.isEmpty {
                for title in found {
                    FileHandle.standardError.write(Data("orion:   · \(title)\n".utf8))
                }
            }
            exit(missing.isEmpty ? 0 : 1)
        }

        // A launch that never finishes must not look like a menu that is
        // complete. Longer than the wait above by enough that a slow machine is
        // not a failure, and short enough that a hang is not a hang.
        DispatchQueue.main.asyncAfter(deadline: .now() + 30) {
            fail("the app did not finish launching, so the menu was never read")
        }

        OrionApp.main()
        // `main()` is typed as returning, and does not.
        fail("the app's own launch returned before the menu was read")
    }

    /// The failure a photographer is shown when the graph refuses a frame.
    ///
    /// Worded like the engine's own `errorText`, and held in one place so the
    /// string the scene plants and the string a reviewer looks for are the same
    /// string.
    static let failureText = "the compute pipeline could not be built"

    /// A second message, for the comparison that isolates the warning line from
    /// the `failed` readout — see `assertFailureLineDrawn`.
    ///
    /// ⚠ **Deliberately a different length**, so it cannot wrap to the same
    /// glyphs by coincidence, and deliberately plausible: it is a message this
    /// footer could really carry, not `xxxxx`, because it is also what a person
    /// looking at the frame while debugging this check will see.
    static let otherFailureText = "the GPU device went away mid-frame"

    // MARK: The failure line

    /// `render-failed`, made to assert on its own — three frames in one process,
    /// no reference image on disk.
    ///
    /// ⚠ **Why it needed one at all.** This scene was the only check-scene that
    /// exited 0 no matter what: the table above said *"no — the frame differs"*,
    /// meaning the oracle was a person opening two PNGs. So the check existed on
    /// paper and ran nowhere, and deleting the footer's failure branch was green
    /// in every gate this repository has.
    ///
    /// The failure frame is already captured when this runs. Clearing
    /// `lastFailure` and laying the same interface out again gives the control,
    /// and **the two must differ** — a footer that stops drawing the warning
    /// draws the ordinary hint in both, and the bytes come back equal.
    ///
    /// ⚠ **The second control is the half that makes this trustworthy, and it is
    /// not redundant.** A frame-differs check passes whenever two renders are
    /// unequal, including when they are unequal for reasons that have nothing to
    /// do with the warning — a clock in the interface, an unsettled layout, a
    /// thumbnail that arrived between passes. That is a check that goes green on
    /// noise, which is the failure mode this project keeps finding in its own
    /// tests. Rendering the control **twice** and demanding those two agree
    /// establishes the harness is byte-stable *in this process, on this run*
    /// before the difference is allowed to mean anything.
    ///
    /// ⚠ The engine stays suspended throughout. A successful render clears
    /// `lastFailure`, and laying the interface out renders — see the comment at
    /// the plant site in `Screenshot.run`, which is a bug this scene has already
    /// had once.
    ///
    /// ⚠⚠ **The second comparison is here because the first one is not enough,
    /// and that was found by running the mutation rather than by reasoning.**
    /// The footer reads `lastFailure` in *two* places: the warning line, and the
    /// readout beside the dimensions, which says `failed` instead of a
    /// millisecond count. So deleting the warning line outright — the exact
    /// mutation this scene exists to catch — still moves bytes through the
    /// readout, and a plain failure-against-no-failure comparison **stayed
    /// green on it**. That is this repository's recurring defect: a check that
    /// names the mutation it is for and does not catch it.
    ///
    /// The fix is to compare two frames that *both* have a failure and differ
    /// only in its **text**. The readout renders `failed` identically in both,
    /// in the same colour, so the only thing that can move a byte is something
    /// drawing the message — and if nothing does, the frames are equal.
    ///
    /// ⚠ **What it still does not pin:** the wording, the colour, the position,
    /// or that a human could read it. A warning line rendered one pixel tall in
    /// the background colour would pass. It pins that the text reaches the
    /// frame.
    static func assertFailureLineDrawn<V: View>(_ failed: Data, view: V,
                                                size: CGSize, engine: Engine) {
        // ⚠ **Checked first, because without it this check accuses the wrong
        // code.** The whole status line is inside `if engine.isLoaded`, so with
        // no photograph open the footer draws nothing in either frame, the two
        // come back equal, and the message below would blame a footer that is
        // working. Run without `--photo` this is the state — and it is exactly
        // the state in which the old, non-asserting version of this scene
        // exited 0 and wrote a frame with no failure line in it.
        guard engine.isLoaded else {
            fail("render-failed needs a photograph: nothing is loaded, so the "
                 + "status line this scene checks is not in the frame at all. "
                 + "Pass --photo <file>")
        }

        engine.lastFailure = nil

        guard let controlA = render(view, size: size),
              let controlB = render(view, size: size) else {
            fail("the control frame for render-failed produced no image")
        }

        guard controlA == controlB else {
            fail("the two control frames disagree (\(controlA.count) bytes "
                 + "against \(controlB.count)), so this harness is not "
                 + "byte-stable on this run and a frame-differs check would "
                 + "pass on the noise rather than on the warning line")
        }

        guard failed != controlA else {
            fail("the footer draws the same frame with a failure planted as "
                 + "without one, so nothing in the interface is showing "
                 + "engine.lastFailure — see OrionApp+Chrome's status line")
        }

        // The same footer state, a different message. Everything else the
        // failure switches — the `failed` readout, its colour, the hint being
        // suppressed — is identical between these two frames, so a difference
        // can only come from the text being drawn.
        engine.lastFailure = otherFailureText
        guard let other = render(view, size: size) else {
            fail("the second failure frame for render-failed produced no image")
        }

        guard failed != other else {
            fail("two different failure messages render the same frame, so the "
                 + "text of engine.lastFailure reaches nothing on screen. The "
                 + "footer's `failed` readout can still be switching — that is "
                 + "why this check is here and the frame-differs one is not "
                 + "enough — but the warning line is gone. "
                 + "See OrionApp+Chrome's status line")
        }

        let note = "orion: the failure line moves "
            + "\(abs(failed.count - controlA.count)) bytes of frame against no "
            + "failure, and \(abs(failed.count - other.count)) against a "
            + "different message; two controls agreed at \(controlA.count)\n"
        FileHandle.standardError.write(Data(note.utf8))
    }

    // MARK: The versions panel's clock

    /// `--scene versions` must not draw the time it was run.
    ///
    /// ⚠ **Byte-stability alone does not catch this, and that was measured
    /// rather than assumed.** The obvious check — render twice, demand the
    /// frames agree — went **green on the mutation that puts `Date()` back**,
    /// because the panel formats with `.short` time style, whose resolution is
    /// one **minute**, and two renders three seconds apart nearly always land
    /// inside the same one. The original 2,380-byte difference was a pair of
    /// runs that happened to straddle a minute boundary. So that check catches
    /// this defect roughly one time in twenty, which is worse than not having it
    /// — it reads as coverage.
    ///
    /// This is the deterministic half: the rows must be **old**. A fixed epoch
    /// sits years in the past; anything derived from the clock is within seconds
    /// of now. That distinction does not depend on when the check runs.
    ///
    /// ⚠ **Deliberately not a comparison against `Screenshot.epoch` itself.**
    /// The fixture is built from that constant, so checking one against the
    /// other asks whether a value equals itself and passes for any value,
    /// including `Date()`. The property that matters is *independence from the
    /// clock*, and that is what is asserted.
    ///
    /// ⚠ It says nothing about the rows being right — three of them, in the
    /// right order, with the right names. A panel drawing one row passes.
    static func assertVersionsDoNotShowTheClock(_ store: SnapshotStore?) {
        guard let store else {
            fail("versions needs a photograph: the snapshot fixture is only "
                 + "built when one is given. Pass --photo <file>")
        }

        // A month. Far enough that no plausible fixed fixture date is mistaken
        // for the clock, and far closer than the epoch actually sits.
        let ancient: TimeInterval = 30 * 86_400

        for row in store.snapshots {
            let age: TimeInterval = -row.created.timeIntervalSinceNow
            guard age > ancient else {
                let seconds = Int(age)
                fail("the versions row \"\(row.name)\" is dated \(seconds) "
                     + "seconds ago, so it is being built from the clock "
                     + "rather than from a fixed instant. The panel prints an "
                     + "absolute time, so this frame cannot be compared "
                     + "against another run of the same binary — see "
                     + "Screenshot.epoch")
            }
        }

        let oldest = store.snapshots.map(\.created).min() ?? Date()
        let days = Int(-oldest.timeIntervalSinceNow / 86_400)
        let note = "orion: \(store.snapshots.count) version rows, oldest \(days) "
            + "days back — none of them from the clock\n"
        FileHandle.standardError.write(Data(note.utf8))
    }

    /// Scenes that capture the panel column scrolled to its end rather than at
    /// rest.
    ///
    /// ⚠ **This exists because the Detail panel is taller than the window and
    /// the harness only ever photographed the top of it.** Deleting Grain,
    /// Vignette, Dehaze, Clarity and Sharpening — 60 lines of shipped controls,
    /// every one of them below the fold at 1680×1050 — left every check in the
    /// repository green (decision #122, mutation M9), while deleting Noise
    /// Reduction eleven lines higher reddened six frames. The panel was covered
    /// exactly as far as it was tall.
    static func scrolls(_ scene: String) -> Bool {
        scene == "detail-tail"
    }

    /// The window a scene needs when the default one cannot hold what the scene
    /// exists to show.
    ///
    /// ⚠ **Nil again as of 2026-08-02, and the history is the reason to keep
    /// this comment.** It returned **1500** for `detail-tail`, measured: the
    /// Detail panel's content was **1,701 points** and the default window gives
    /// its scroll view **681**, so the scene at rest accounted for 0–681 and a
    /// frame scrolled to the end for 1,020–1,701 — and *Grain sat in the
    /// 339-point band between them, seen by neither*. At 1,500 the window held
    /// 1,131, the fold was 570 points down, and the two frames overlapped by
    /// 111 points with nothing between them.
    ///
    /// Moving the panels' helper prose into `Engraved.Info` took **554 points**
    /// out of that content — 1,701 → **1,147** — which took the fold at 1,500
    /// down to **16 points**. A gate one row of margin from breaking for a
    /// reason unrelated to what it tests is not a gate, and this one fails
    /// *loudly* when nothing overflows, so it would have gone red on the next
    /// panel edit and blamed the wrong change.
    ///
    /// At the default window the same measurement now reads: content 1,147,
    /// scroll view **681**, fold **466 points down**, the two frames
    /// overlapping by **215** with nothing between them. That is a better fold
    /// than the override ever bought, so the override is gone rather than
    /// retuned. ⚠ **Re-measure this the next time panel content moves** — the
    /// number that matters is the overlap, not the overflow.
    static func minimumHeight(_ scene: String) -> CGFloat? {
        nil   // see above: the panel no longer needs a taller window
    }
}
