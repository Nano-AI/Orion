import Foundation

/// What the photographer did, written as a **runnable scenario**.
///
/// The problem this solves is the one that has cost this project the most
/// sessions: a report arrives as "I was messing around with masking and
/// changing the exposure and now compare doesn't work", and the sequence — which
/// is the whole bug — has to be guessed at. Every reproduction in `repro/` was
/// reconstructed by hand from a sentence like that.
///
/// So the log is not prose and not a state dump. It is `app/Scenario.swift`'s
/// own grammar, so a log **is** a reproduction: copy it into `repro/`, run it,
/// and the bug happens again or it does not.
///
/// ## What it records, and why that granularity
///
/// One line per *committed* edit, taken from `EditHistory.record` — the same
/// point undo counts. A slider drag is sixty renders and one commit, so the log
/// gets one `set exposure 2.60` rather than sixty. That is also exactly what a
/// scenario wants: the sixty intermediate values change nothing a replay could
/// observe.
///
/// Changes are found by **diffing `DevelopState`** rather than by calling a
/// logger from forty places. A field added to that struct is logged the day it
/// is added, with no second list to keep in step — the failure mode this
/// codebase has hit repeatedly, most recently with a sidecar that encoded three
/// fields it never decoded.
///
/// ⚠ **Not everything is in `DevelopState`.** The compare split, the mask
/// overlay, which tab is open and which mask row is selected are all view state
/// by deliberate decision, and a bug can live entirely in them — this one did.
/// Those are recorded explicitly by their call sites, and the list of them is
/// the one thing here that has to be maintained by hand. It is short and it is
/// named.
///
/// Not actor-isolated: it touches no UI and only a file, and `Engine` builds
/// one in its own initializer. Isolating it would put an await in front of a
/// property observer.
final class InteractionLog: @unchecked Sendable {

    /// Where a report should be picked up from. Stable rather than timestamped,
    /// because a person asked for it has to be able to find it.
    static let url: URL = {
        let dir = FileManager.default.urls(for: .libraryDirectory, in: .userDomainMask)
            .first?.appendingPathComponent("Logs/Orion", isDirectory: true)
            ?? URL(fileURLWithPath: NSTemporaryDirectory())
        try? FileManager.default.createDirectory(at: dir, withIntermediateDirectories: true)
        return dir.appendingPathComponent("session.txt")
    }()

    /// Lines held in memory. Capped so a long session cannot grow without
    /// bound; a bug is in the last few dozen actions, not the first thousand.
    private var lines: [String] = []
    private static let cap = 2000

    /// The state the last emitted lines described. `nil` until a photo opens.
    private var previous: DevelopState?

    private var enabled = true

    init() { start() }

    /// A fresh header, so a log opened later says which build produced it.
    private func start() {
        lines = ["# Orion session log — this file is a runnable scenario.",
                 "#",
                 "#   ./build/Orion.app/Contents/MacOS/Orion --scenario <this file>",
                 "#",
                 "# Paths are as they were opened. Comments and blank lines are",
                 "# ignored by the runner, so this whole header can stay.",
                 "#",
                 "# \u{26A0} A session log records what was done, not what should be true, so",
                 "# it asserts nothing — and decision #124 makes a run that asserted",
                 "# nothing fail. Without the next line, replaying the log a",
                 "# photographer sent in would exit 1 on the one workflow the log",
                 "# exists for.",
                 "minchecks 0",
                 "#"]
        flush()
    }

    // MARK: Recording

    /// A line in the scenario's own grammar.
    func record(_ line: String) {
        guard enabled else { return }
        lines.append(line)
        if lines.count > Self.cap { lines.removeFirst(lines.count - Self.cap) }
        flush()
    }

    /// A comment — for things a replay cannot act on but a reader wants.
    func note(_ text: String) { record("# \(text)") }

    /// A photograph was opened. Resets the diff baseline, because the state
    /// arriving belongs to a different picture.
    func opened(_ path: String, state: DevelopState) {
        record("")
        record("open \(path)")
        previous = state
    }

    /// An edit was committed. Emits whatever actually changed.
    func committed(_ state: DevelopState, label: String) {
        guard let old = previous else { previous = state; return }
        let emitted = Self.diff(from: old, to: state)
        if emitted.isEmpty {
            // A commit that changed no field of `DevelopState` is a real thing —
            // a matte upload, say — and worth a mark so the sequence is honest.
            note("\(label) (nothing in DevelopState changed)")
        } else {
            for line in emitted { record(line) }
        }
        previous = state
    }

    /// State that is deliberately outside `DevelopState`, recorded by hand.
    ///
    /// ⚠ This list is the one part of the log that can silently fall behind.
    /// Anything here is view state by decision — see `DECISIONS.md` #57 for the
    /// overlay and the note in `Engine` for the compare split.
    func compare(_ split: Double) {
        record(String(format: "compare %.3f", split))
    }
    func overlay(_ on: Bool)      { record("overlay \(on ? "on" : "off")") }
    func tab(_ name: String)      { note("tab \(name)") }
    func selectedMask(_ i: Int)   { note("mask row \(i + 1) selected") }
    func selectedSpot(_ i: Int)   { note("spot \(i + 1) selected") }
    func undo()                   { record("undo") }
    func redo()                   { record("redo") }
    func interacting(_ on: Bool)  { record("interact \(on ? "on" : "off")") }

    // MARK: The diff

    /// `DevelopState` before and after, as scenario lines. `DevelopDiff` is the
    /// one implementation - the agent surface reads the same function, so a
    /// session log and a `state` reply cannot spell an edit two ways.
    static func diff(from a: DevelopState, to b: DevelopState) -> [String] {
        DevelopDiff.lines(from: a, to: b)
    }

    // MARK: Writing

    /// Rewritten whole rather than appended, so the file is always a valid
    /// scenario rather than a valid one plus a partial line. A session log is a
    /// few kilobytes; this is not the expensive part of anything.
    private func flush() {
        let text = lines.joined(separator: "\n") + "\n"
        try? text.write(to: Self.url, atomically: true, encoding: .utf8)
    }
}
