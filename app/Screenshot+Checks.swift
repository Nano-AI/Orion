import AppKit
import Foundation

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
/// | `render-failed` | the footer stops drawing `engine.lastFailure` | no — the frame differs |
/// | `menu` | a `PhotoCommands` item is deleted or renamed | when a command is missing |
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
        // ⚠ **Two characters short of what the source says, and that is a
        // finding rather than a typo here.** `OrionApp+Commands` writes
        // `"Compare Original  (\\)"`, which is `Compare Original  (\)` in
        // Swift — but a `Button`'s string is a `LocalizedStringKey`, and a
        // backslash is that grammar's escape character, so AppKit installs
        // **`Compare Original  ()`**: the one item whose key is spelled only in
        // its title has lost the key. Nothing in the repository could see it
        // until something read the real menu bar. Pinned as it ships, because
        // the fix is a line in `OrionApp+Commands.swift` and that file belongs
        // to another story; a check that is red the day it lands is not a check.
        // When it *is* fixed this goes red and prints the bar, which is the
        // right way round.
        "Compare Original  (\\)",
        "Fit in Window  (0)", "Actual Size  (9)",
        "Export…", "Reveal Session Log in Finder",
        "Reset Adjustments",
        "Reject  (R)",
        "1 Star  (1)", "2 Stars  (2)", "3 Stars  (3)", "4 Stars  (4)",
        "5 Stars  (5)", "No Rating  (`)",
        "Next Photo  (→)", "Previous Photo  (←)",
        "Select All Photos", "Deselect",
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
