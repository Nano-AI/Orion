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
/// Usage:  Orion --scenario path/to/file.txt
///
/// Grammar, one command per line, `#` comments and blank lines ignored:
///
///     open <path>                       open a raw file
///     rotate <quarter-turns>            through Engine.rotate, as the button does
///     straighten <degrees>
///     crop <x> <y> <w> <h>              normalized
///     set <control> <value>             any slider by name
///     mask <kind>                       none | linear | radial | brush
///     brush <x,y> <x,y> ...             dabs walked by CanvasLayout, as the hand does
///     pick <x,y>                        the colour-mixer eyedropper
///     targeted <x,y> <delta>            pick, then drag, which is what applies it
///     auto                              the Auto button
///     compare <split>                   1 = off, lower reveals the original
///     undo / redo
///     measure <x,y,w,h> <name>          record a value under a name
///     expect <name> <op> <value>        ==, !=, >, < against a recorded value
///     expect <name> == <other-name>     two recordings, exactly equal
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

    private struct Reading {
        var luma: Double
        var saturation: Double
    }

    private static var readings: [String: Reading] = [:]
    private static var failures = 0
    private static var checks = 0

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

    private struct Bad: LocalizedError {
        let what: String
        var errorDescription: String? { what }
    }

    private static func step(_ verb: String, _ args: [String], engine: Engine,
                             targeted: TargetedAdjust) throws {
        func point(_ s: String) throws -> CGPoint {
            let xy = s.split(separator: ",").compactMap { Double($0) }
            guard xy.count == 2 else { throw Bad(what: "expected x,y, got \(s)") }
            return CGPoint(x: xy[0], y: xy[1])
        }
        func number(_ i: Int) throws -> Double {
            guard i < args.count, let v = Double(args[i]) else {
                throw Bad(what: "expected a number at argument \(i + 1)")
            }
            return v
        }

        switch verb {
        case "open":
            guard let p = args.first else { throw Bad(what: "open needs a path") }
            try engine.open(path: p)

        case "rotate":
            engine.rotate(Int32(try number(0)))

        case "straighten":
            engine.straightenDeg = Float(try number(0))

        case "crop":
            engine.setCrop(x: Float(try number(0)), y: Float(try number(1)),
                           w: Float(try number(2)), h: Float(try number(3)))

        case "set":
            guard args.count >= 2 else { throw Bad(what: "set needs a name and a value") }
            // Through `edit`, because that is what a slider does and it is what
            // records history. A bare assignment renders without recording, and
            // a scenario that used one would not be standing in for the
            // interface it is meant to be testing.
            let name = args[0], v = Float(try number(1))
            var thrown: Error?
            engine.edit(name) {
                do { try apply(control: name, value: v, to: engine) }
                catch { thrown = error }
            }
            if let thrown { throw thrown }

        case "mask":
            let kinds = ["none": Int32(0), "linear": 1, "radial": 2, "brush": 3]
            guard let k = kinds[args.first ?? ""] else {
                throw Bad(what: "mask takes none, linear, radial or brush")
            }
            engine.maskKind = k

        case "brush":
            // Walked through CanvasLayout at a fixed spacing, which is what the
            // canvas does with a real drag — not a hand-placed list of centres.
            // `carry` continues the spacing across segments, so a scripted
            // stroke has the same dabs a steady hand would lay.
            var stroke: [CGPoint] = []
            var carry: CGFloat = 0
            let path = try args.map { try point($0) }
            guard path.count >= 2 else { throw Bad(what: "brush needs two or more points") }
            stroke.append(path[0])
            for i in 1..<path.count {
                stroke += CanvasLayout.brushDabs(from: path[i - 1], to: path[i],
                                                 radius: CGFloat(engine.brushRadius),
                                                 carry: &carry)
            }
            engine.setBrushStroke(stroke)
            engine.commitBrushEdit()

        case "pick":
            let p = try point(args.first ?? "")
            try eyedrop(engine: engine, targeted: targeted, at: p, drag: nil)

        case "targeted":
            let p = try point(args.first ?? "")
            try eyedrop(engine: engine, targeted: targeted, at: p,
                        drag: CGFloat(try number(1)))

        case "auto":
            engine.autoEnhance()

        case "compare":
            engine.setCompare(split: try number(0))

        case "undo":
            engine.undo()

        case "redo":
            engine.redo()

        case "measure":
            guard args.count >= 2 else { throw Bad(what: "measure needs a region and a name") }
            let r = args[0].split(separator: ",").compactMap { Double($0) }
            guard r.count == 4 else { throw Bad(what: "region is x,y,w,h") }
            let reading = try read(engine,
                                  CGRect(x: r[0], y: r[1], width: r[2], height: r[3]))
            readings[args[1]] = reading
            FileHandle.standardError.write(Data(String(
                format: "  %-22@ luma %.4f  sat %.4f\n",
                args[1] as NSString, reading.luma, reading.saturation).utf8))

        case "expect":
            guard args.count >= 3 else { throw Bad(what: "expect needs name, op, value") }
            try check(args[0], args[1], args[2])

        case "shot":
            guard let p = args.first else { throw Bad(what: "shot needs a path") }
            Screenshot.writeCanvas(engine, to: p)

        case "print":
            FileHandle.standardError.write(Data(("  " + args.joined(separator: " ") + "\n").utf8))

        default:
            throw Bad(what: "unknown command \(verb)")
        }
    }

    /// The colour-mixer eyedropper, through the path the canvas uses: sample the
    /// pixel, derive its hue, ask which band that is, and hand it to
    /// `TargetedAdjust`. A `drag` then moves that band, which is the half that
    /// actually changes the picture.
    private static func eyedrop(engine: Engine, targeted: TargetedAdjust,
                                at p: CGPoint, drag: CGFloat?) throws {
        guard let s = engine.sample(u: Float(p.x), v: Float(p.y)) else {
            throw Bad(what: "no sample at \(p.x),\(p.y) — is a photo open?")
        }
        guard let hue = TargetedAdjust.hue(r: s.display.r, g: s.display.g,
                                          b: s.display.b) else {
            throw Bad(what: String(format:
                "the pixel at %.2f,%.2f is too near grey to have a hue "
                + "(r %.3f g %.3f b %.3f) — the tool refuses, by design",
                p.x, p.y, s.display.r, s.display.g, s.display.b))
        }
        let band = TargetedAdjust.band(forHue: hue)
        targeted.begin(band: band, hue: hue)
        FileHandle.standardError.write(Data(String(format:
            "  picked hue %.1f deg -> band %@\n", hue, "\(band)" as NSString).utf8))

        if let drag {
            // What the drag does to the band it picked. Saturation is the tool's
            // default mode.
            let before = engine.satShift[band.rawValue]
            engine.satShift[band.rawValue] = min(1, max(-1, before + Float(drag)))
            targeted.end()
        }
    }

    private static func apply(control: String, value: Float, to e: Engine) throws {
        switch control {
        case "exposure":    e.exposureEv = value
        case "contrast":    e.contrast = value
        case "highlights":  e.highlights = value
        case "shadows":     e.shadows = value
        case "whites":      e.whites = value
        case "blacks":      e.blacks = value
        case "saturation":  e.saturation = value
        case "vibrance":    e.vibrance = value
        case "temperature": e.temperatureK = value
        case "tint":        e.tint = value
        case "clarity":     e.clarity = value
        case "dehaze":      e.dehaze = value
        case "fusion", "lift": e.fusion = value
        case "localExposure": e.localExposureEv = value
        case "brushRadius": e.brushRadius = value
        case "brushFlow":   e.brushFlow = value
        case "maskCentreX": e.maskCentreX = value
        case "maskCentreY": e.maskCentreY = value
        case "maskAngle":   e.maskAngle = value
        case "maskLength":  e.maskLength = value
        default: throw Bad(what: "no control named \(control)")
        }
    }

    private static func read(_ engine: Engine, _ region: CGRect) throws -> Reading {
        guard let stats = Screenshot.regionStats(engine, region: region) else {
            throw Bad(what: "could not read the output — is a photo open?")
        }
        return Reading(luma: stats.luma, saturation: stats.saturation)
    }

    private static func check(_ name: String, _ op: String, _ rhs: String) throws {
        checks += 1
        guard let got = readings[name] else {
            throw Bad(what: "nothing recorded under \(name)")
        }
        // The right-hand side is another recording when it names one, so a
        // scenario can assert two states are identical without knowing the value.
        let want: Double
        if let other = readings[rhs] { want = other.luma }
        else if let v = Double(rhs) { want = v }
        else { throw Bad(what: "\(rhs) is neither a number nor a recording") }

        // Tolerance is one 8-bit code. The output is eight bits for the screen,
        // so anything tighter is asserting against quantisation.
        let eps = 1.0 / 255.0
        let ok: Bool
        switch op {
        case "==": ok = abs(got.luma - want) < eps
        case "!=": ok = abs(got.luma - want) >= eps
        case ">":  ok = got.luma > want
        case "<":  ok = got.luma < want
        default: throw Bad(what: "unknown operator \(op)")
        }
        if !ok { failures += 1 }
        FileHandle.standardError.write(Data(String(format:
            "  %@  %@ %@ %@  (got %.4f, wanted %.4f)\n",
            (ok ? "ok  " : "FAIL") as NSString, name as NSString,
            op as NSString, rhs as NSString, got.luma, want).utf8))
    }

    private static func fail(_ why: String) -> Never {
        FileHandle.standardError.write(Data("orion: \(why)\n".utf8))
        exit(2)
    }
}
