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

    /// `DevelopState` before and after, as scenario lines.
    ///
    /// Only the controls the runner can drive are emitted, and each is named
    /// with the string `Scenario.apply(control:)` accepts — a line this cannot
    /// replay is worse than no line, because it fails the whole file at a point
    /// that is not the bug.
    static func diff(from a: DevelopState, to b: DevelopState) -> [String] {
        var out: [String] = []
        func f(_ name: String, _ x: Float, _ y: Float, _ places: Int = 3) {
            guard abs(x - y) > 1e-6 else { return }
            out.append("set \(name) \(String(format: "%.\(places)f", y))")
        }

        f("exposure", a.exposureEv, b.exposureEv)
        f("contrast", a.contrast, b.contrast)
        f("highlights", a.highlights, b.highlights)
        f("shadows", a.shadows, b.shadows)
        f("whites", a.whites, b.whites)
        f("blacks", a.blacks, b.blacks)
        f("saturation", a.saturation, b.saturation)
        f("vibrance", a.vibrance, b.vibrance)
        f("temperature", a.temperatureK, b.temperatureK, 0)
        f("tint", a.tint, b.tint)
        f("clarity", a.clarity, b.clarity)
        f("dehaze", a.dehaze, b.dehaze)
        f("grainAmount", a.grainAmount, b.grainAmount)
        f("grainSize", a.grainSize, b.grainSize)
        f("vignetteAmount", a.vignetteAmount, b.vignetteAmount)
        f("vignetteFieldAngle", a.vignetteFieldAngle, b.vignetteFieldAngle, 0)
        f("fusion", a.fusion, b.fusion)
        // ⚠ Per layer, and the layer index is emitted with it — a bare
        // `set localExposure` would replay onto whichever layer the runner
        // happened to have selected, which is not the one that moved.
        for i in 0..<max(a.layers.count, b.layers.count) {
            let x = i < a.layers.count ? a.layers[i] : LocalAdjustState()
            let y = i < b.layers.count ? b.layers[i] : LocalAdjustState()
            guard x != y else { continue }
            out.append("masklayer \(i)")
            f("localExposure", x.exposureEv, y.exposureEv)
            f("localContrast", x.contrast, y.contrast)
            f("localSaturation", x.saturation, y.saturation)
            f("localWarmth", x.warmth, y.warmth)
            f("localTint", x.tint, y.tint)
        }
        f("maskRefine", a.maskRefine, b.maskRefine)

        // Geometry has verbs of its own rather than `set`.
        if a.rotateQuarters != b.rotateQuarters {
            let turns = ((b.rotateQuarters - a.rotateQuarters) % 4 + 4) % 4
            out.append("rotate \(turns)")
        }
        f("straighten", a.straightenDeg, b.straightenDeg, 2)
        f("perspectiveVertical", a.perspectiveVertical, b.perspectiveVertical)
        f("perspectiveHorizontal", a.perspectiveHorizontal, b.perspectiveHorizontal)
        f("perspectiveAspect", a.perspectiveAspect, b.perspectiveAspect)
        if abs(a.cropX - b.cropX) > 1e-6 || abs(a.cropY - b.cropY) > 1e-6
            || abs(a.cropW - b.cropW) > 1e-6 || abs(a.cropH - b.cropH) > 1e-6 {
            out.append(String(format: "crop %.4f %.4f %.4f %.4f",
                              b.cropX, b.cropY, b.cropW, b.cropH))
        }
        // `straighten` is a verb, not a control name — correct it.
        out = out.map { $0.hasPrefix("set straighten ")
            ? "straighten " + $0.dropFirst("set straighten ".count) : $0 }

        // The selected mask row, which is what the `mask…` controls address.
        // ⚠ The kind AND the fields, not one or the other. The first version
        // returned after emitting `mask radial` whenever the component count
        // had changed — and a scenario's `mask` verb does not itself commit, so
        // the *next* edit carried both the new component and a slider move. The
        // slider line was dropped and the replay silently diverged from the
        // session it claims to reproduce, which is the one failure a log of
        // this kind must not have.
        let bc = b.maskComponents.first
        // A component that did not exist is diffed against its defaults, which
        // is what the engine would have created.
        let x = a.maskComponents.first ?? MaskComponentState()
        // ⚠ `x.kind != bc?.kind` compares an Int32 against an Int32? and is
        // therefore *always* true while there is no component — which put a
        // spurious `mask none` after the first edit of every session.
        let toKind = bc?.kind ?? 0
        if a.maskComponents.count != b.maskComponents.count || x.kind != toKind {
            out.append("mask \(kindName(toKind))")
        }
        if let y = bc {
            f("maskCentreX", x.centreX, y.centreX)
            f("maskCentreY", x.centreY, y.centreY)
            f("maskAngle", x.angle, y.angle)
            f("maskLength", x.length, y.length)
            f("maskRadiusX", x.radiusX, y.radiusX)
            f("maskRadiusY", x.radiusY, y.radiusY)
            f("maskFeather", x.feather, y.feather)
            f("maskRoundness", x.roundness, y.roundness)
            f("maskRangeLo", x.rangeLo, y.rangeLo)
            f("maskRangeHi", x.rangeHi, y.rangeHi)
            f("maskRangeSoft", x.rangeSoft, y.rangeSoft)
            f("maskColorTol", x.colourTol, y.colourTol)
            f("maskColorSoft", x.colourSoft, y.colourSoft)
            if x.brushStroke.count != y.brushStroke.count {
                out.append("# brush stroke: \(y.brushStroke.count / 2) dabs")
            }
        }

        // Spots. A placement is a verb; a move is `spotdrag`.
        if b.spots.count > a.spots.count {
            for s in b.spots[a.spots.count...] {
                out.append(String(format: "spot %.4f,%.4f %.4f %@",
                                  s.destX, s.destY, s.radius,
                                  s.heal ? "heal" : "clone"))
                out.append(String(format: "spotdrag %d source %.4f,%.4f",
                                  b.spots.count - 1, s.srcX, s.srcY))
            }
        } else if b.spots.count == a.spots.count {
            for (i, s) in b.spots.enumerated() where s != a.spots[i] {
                if s.destX != a.spots[i].destX || s.destY != a.spots[i].destY {
                    out.append(String(format: "spotdrag %d dest %.4f,%.4f",
                                      i, s.destX, s.destY))
                }
                if s.srcX != a.spots[i].srcX || s.srcY != a.spots[i].srcY {
                    out.append(String(format: "spotdrag %d source %.4f,%.4f",
                                      i, s.srcX, s.srcY))
                }
            }
        } else {
            out.append("# \(a.spots.count - b.spots.count) spot(s) removed")
        }

        return out
    }

    private static func kindName(_ k: Int32) -> String {
        switch k {
        case 1: "linear"
        case 2: "radial"
        case 3: "brush"
        case 4: "matte"
        case 5: "range"
        case 6: "color"
        default: "none"
        }
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
