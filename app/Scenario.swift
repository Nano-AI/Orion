import AppKit
import SwiftUI

/// A scripted interaction, run headlessly, with its assertions in the file.
///
/// The screenshot harness can already put the app into a *state* — open a photo,
/// pick a scene, measure a region. What it cannot do is a *sequence*: rotate,
/// then paint, then compare, then undo. Every bug that survived a session of
/// looking for it was a sequence, and every one of them was reported by hand
/// because nothing here could express it.
///
/// A scenario is a text file, so a report becomes a file that fails until the bug
/// is fixed and then stays as the regression test. That is the difference between
/// this and a pile of command-line flags.
///
/// ⚠️ **It drives the same objects the interface drives** — `Engine`,
/// `CanvasLayout`, `TargetedAdjust`, `EditHistory` — and never reaches around
/// them. A runner that poked the pipeline directly would exercise code that is
/// already known to work and miss the gesture and view-model layers, which is
/// where the reported failures actually are.
///
/// For the same reason `measure ... canvas` renders through `CanvasBlit` —
/// the real shader, the real transform — rather than reimplementing the split
/// on the CPU. The compare bugs live in that compositing, so a stand-in for it
/// would be the one piece of code the test cannot afford to fake.
///
/// Usage:  Orion --scenario path/to/file.txt
///
/// Grammar, one command per line, `#` comments and blank lines ignored:
///
///     open <path>                       open a raw file
///     rotate <quarter-turns>            through Engine.rotate, as the button does
///     straighten <degrees>
///     crop <x> <y> <w> <h>              normalized
///     preview on | off                  the crop tool's context render
///     set <control> <value>             any slider by name
///     wheel <name> <x> <y> [luma]       a whole grading wheel at once —
///                                       `gradeShadow`, `gradeMidtone` or
///                                       `gradeHighlight`. ⚠ **Added, not a
///                                       rename**: the scalar `gradeShadowX` /
///                                       `gradeShadowY` spellings keep working
///                                       (decision #89). Clamped to the disc, as
///                                       the puck is
///     dragwheel <name> <x,y> <x,y> <n>  sweep a wheel's puck and report the cost
///                                       of one tick. ⚠ Both components move in
///                                       **one** `edit`, because that is what
///                                       `ColorWheel`'s drag does — writing them
///                                       through two `set`s would be two ticks
///                                       and two history entries, and would
///                                       measure a gesture nobody makes
///     mask <kind>                       none | linear | radial | brush |
///                                       matte | range. `matte` selects the
///                                       raster kind without uploading one,
///                                       which is what a reopened photo with a
///                                       saved Subject row actually is
///     matte disc | left | ramp          a synthetic raster matte in frame
///                                       coordinates, for the kind-4 component.
///                                       Saved beside the photo, as the panel
///                                       saves one. `ramp` is the only shape
///                                       with mid-values, so it is the only one
///                                       that can catch a persistence bug
///     reopen                            close and open the photo again, through
///                                       the whole of `Editor.load` — decode,
///                                       sidecar, restore, upload saved mattes,
///                                       sweep orphans
///     select subject | person | sky     runs the detector for real, and reports what
///                                       fraction of the frame it covered
///     refuses subject | person | sky    asserts the detector declines this
///                                       photograph. A refusal is a result and
///                                       gets asserted like one — without this
///                                       verb the only way to "check" one was to
///                                       not ask
///     overlay on | off                  paint the coverage, as `Show mask` does
///     spot <x,y> [radius] [heal|clone]  place a dust spot, as a click does
///     maskadd <kind>                    add a *row* — `mask <kind>` changes
///                                       the selected row instead
///     masklayer <n>                     select layer n (by its first row)
///     masksplit <n>                     start or end a layer at row n
///     maskhide <n>                      the eye button on a mask row
///     maskmove <n> <offset>             reorder a mask row in the fold
///     maskkind <n> <kind>               change what an existing row is
///     spotdrag <n> source|dest <x,y>    drag one of a spot's two handles,
///                                       through the call the overlay makes
///     spotat <n> source|dest <x,y>      where the interface *draws* that
///                                       handle — the oracle for the geometry
///     maskcheck <cells> <ev>            does the mask the *interface draws*
///                                       sit on the coverage the engine
///                                       *renders*? Grids the frame, classifies
///                                       every cell by `CanvasLayout.maskAlpha`
///                                       — the overlay's own oracle — and
///                                       demands the render agree
///     brush <x,y> <x,y> ...             dabs walked by CanvasLayout, as the hand
///                                       does. Appends to the stroke already
///                                       there, and lays paint or erase
///                                       according to `set brushErase`
///     pick <x,y>                        the color-mixer eyedropper
///     maskcolor <x,y>                  the color range mask's picker, as a
///                                       click on the canvas does it
///     targeted <x,y> <delta>            pick, then drag, which is what applies it
///     auto                              the Auto button
///     preset <name>                     apply a built-in look by name
///     snapshot save <name>              save this photo's edit as a version
///     snapshot restore <name>           put a version back, through the same
///                                       `SnapshotStore.restore` the panel
///                                       calls — so the working edit is kept
///                                       first, as the panel's is
///     snapshot rename <name> <new>      rename one; renaming the automatic
///                                       version is what keeps it
///     snapshot delete <name>
///     snapshot count <n>                assert how many versions exist, which
///                                       is how the automatic slot is pinned
///     snapshot clear                    delete this photo's version file, so a
///                                       scenario that counts starts from a
///                                       known state on every run
///     snapshot missing <name> <n>       assert how many of a version's mattes
///                                       are no longer beside the photograph
///     compare <split>                   1 = off, lower reveals the original
///     undo / redo
///     measure <x,y,w,h> <name> [where]  record a value under a name.
///                                       `where` is `output` (default, the
///                                       engine's edited render) or `canvas`
///                                       (the blit the screen actually shows,
///                                       which is where the compare split
///                                       composites its two textures — a
///                                       compare bug is invisible to `output`),
///                                       `preview` (the quarter-linear graph a
///                                       drag renders on) or `analysis` (the
///                                       picture handed to Vision, which
///                                       nothing on screen ever shows)
///     control <name> <op> <value>       what a control *holds*, not what the
///                                       picture looks like. For buttons that
///                                       write several fields, where "the
///                                       picture changed" is satisfied by any
///                                       one of them landing
///     expect <name> <op> <value>        ==, !=, >, < against a recorded value
///     expect <name> == <other-name>     two recordings, equal in *both* mean
///                                       luma and mean saturation — one number
///                                       per patch is too weak a signature to
///                                       say two renders are the same picture
///     time <n> <command...>             repeat a command and report what one
///                                       of them costs. "Slow" is not a report
///                                       anyone can act on; a number is
///     interact on | off                  arm the preview graph, as a drag does
///     drag <control> <from> <to> <n>    sweep a slider and report the cost of
///                                       one tick. Distinct values, because a
///                                       repeated *same* value dirties nothing
///                                       and would time an empty render
///     paint <x,y> <x,y> <n>             a stroke as the *canvas* issues it —
///                                       one push per pointer event, appending
///                                       — and what one event costs. ⚠ Not
///                                       `brush`, which hands over a finished
///                                       stroke in one call and so measures the
///                                       one thing a photographer never does
///     save <path>                       write the state to that photo's sidecar
///     export <path> [key=value ...]     write a real export, through the same
///                                       Engine.export the panel calls.
///                                       depth=8|16, sharpen=none|screen|print,
///                                       metadata=all|nolocation|none,
///                                       size=<longest edge in px>
///     probe <path> <property> <name>    read a written file back and record
///                                       depth, gps, iptclocation or acutance
///                                       under <name>, for `expect`
///     identical <path> <path>           two files, byte for byte
///     shot <path>                       write a PNG
///     print <text>
///
/// Exits nonzero if any `expect` fails, so it is usable as a test.
@MainActor
enum Scenario {

    static func path(_ arguments: [String]) -> String? {
        guard let i = arguments.firstIndex(of: "--scenario"),
              i + 1 < arguments.count else { return nil }
        return arguments[i + 1]
    }

    struct Reading {
        var luma: Double
        var saturation: Double
    }

    static var readings: [String: Reading] = [:]
    static var failures = 0
    static var checks = 0

    /// Set while `time` is running its repeats. Every informational write goes
    /// through `say`, so a timed loop reports its own number instead of the
    /// cost of two thousand lines of stderr.
    static var quiet = false

    static func say(_ text: String) {
        guard !quiet else { return }
        FileHandle.standardError.write(Data(text.utf8))
    }

    static func run(_ file: String) -> Never {
        NSApplication.shared.setActivationPolicy(.accessory)

        guard let text = try? String(contentsOfFile: file, encoding: .utf8) else {
            fail("could not read scenario \(file)")
        }
        guard let engine = try? Engine() else { fail("could not start the engine") }

        // The same view models the editor builds. Held here so a scenario's
        // steps share them exactly as the interface's do.
        let targeted = TargetedAdjust()

        for (n, raw) in text.components(separatedBy: .newlines).enumerated() {
            let line = raw.trimmingCharacters(in: .whitespaces)
            if line.isEmpty || line.hasPrefix("#") { continue }
            let parts = line.split(separator: " ").map(String.init)
            let verb = parts[0]
            let args = Array(parts.dropFirst())

            do {
                try step(verb, args, engine: engine, targeted: targeted)
            } catch {
                fail("line \(n + 1): \(line)\n  \(error.localizedDescription)")
            }
        }

        let summary = "orion: \(checks) checks, \(failures) failures\n"
        FileHandle.standardError.write(Data(summary.utf8))
        exit(failures == 0 ? 0 : 1)
    }

    struct Bad: LocalizedError {
        let what: String
        var errorDescription: String? { what }
    }

    /// The photograph `open` last opened. A matte is saved beside its
    /// photograph, so the verbs that make one need to know which.
    static var photo: URL?

    /// This photograph's saved versions, held across steps exactly as the
    /// editor holds one across a session — a fresh store per verb would read
    /// the file back each time and so could never show a stale panel, which is
    /// one of the things a scenario is here to be able to see.
    static let snapshots = SnapshotStore()

    /// The develop blob `corrupt` overwrote, so `repair` can put it back.
    ///
    /// ⚠ Held here rather than re-derived from the engine, and that is the
    /// whole reason `repair` exists: the refused reopen calls `engine.open`,
    /// which resets to the camera's own settings, so by then the good state is
    /// gone from memory. Re-saving from the engine at that point would write an
    /// *empty* component list — which is the very mistake this file is about,
    /// committed by the test for it.
    static var stashedDevelop: Data?

    /// Runs one line of a scenario.
    ///
    /// ⚠ **The switch that was here is gone, and that is the point of the
    /// split.** It was 977 lines, so "where does a new verb go" had one answer
    /// — somewhere in the middle of the largest function in the app — and the
    /// grammar above it had drifted from it more than once. Each family now
    /// owns a switch over the verbs it implements and answers whether it took
    /// this one; adding a verb is one edit, in one file, next to the verbs it
    /// resembles.
    ///
    /// ⚠ **The four families claim disjoint verbs, and nothing here enforces
    /// it.** A verb answered by two families would be taken by whichever is
    /// asked first, silently — the same shape of failure decision #89 records,
    /// where collapsing an alias pair into duplicate `case`s made the second
    /// unreachable. The claim is checked by extracting every `case` label from
    /// the four switches and comparing the multiset against this file's
    /// history, not by a runtime guard.
    static func step(_ verb: String, _ args: [String], engine: Engine,
                     targeted: TargetedAdjust) throws {
        if try frameStep(verb, args, engine: engine) { return }
        if try controlStep(verb, args, engine: engine, targeted: targeted) { return }
        if try maskStep(verb, args, engine: engine) { return }
        if try reportStep(verb, args, engine: engine, targeted: targeted) { return }
        throw Bad(what: "unknown command \(verb)")
    }

    /// An `x,y` argument.
    ///
    /// ⚠ This and `number` were closures inside `step`, captured over its
    /// `args`. They are statics now because the four families need them;
    /// `number` therefore takes the array it reads, which is the only change
    /// the split made to a line of a verb's body.
    static func point(_ s: String) throws -> CGPoint {
        let xy = s.split(separator: ",").compactMap { Double($0) }
        guard xy.count == 2 else { throw Bad(what: "expected x,y, got \(s)") }
        return CGPoint(x: xy[0], y: xy[1])
    }

    static func number(_ args: [String], _ i: Int) throws -> Double {
        guard i < args.count, let v = Double(args[i]) else {
            throw Bad(what: "expected a number at argument \(i + 1)")
        }
        return v
    }

    private static func fail(_ why: String) -> Never {
        FileHandle.standardError.write(Data("orion: \(why)\n".utf8))
        exit(2)
    }
}
