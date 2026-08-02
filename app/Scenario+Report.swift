import AppKit
import SwiftUI

/// What a scenario *reports*: the measurements, the assertions over them, the
/// instruments, and the files a run writes and reads back.
///
/// ⚠ **Adding a verb is one edit, in the switch below.**

extension Scenario {

    /// Answers true when this family took the verb.
    static func reportStep(_ verb: String, _ args: [String], engine: Engine,
                           targeted: TargetedAdjust) throws -> Bool {
        switch verb {
        case "measure":
            guard args.count >= 2 else { throw Bad(what: "measure needs a region and a name") }
            let r = args[0].split(separator: ",").compactMap { Double($0) }
            guard r.count == 4 else { throw Bad(what: "region is x,y,w,h") }
            let surface: Screenshot.Surface
            switch args.count > 2 ? args[2] : "output" {
            case "output": surface = .output
            case "canvas": surface = .canvas
            case "preview": surface = .preview
            case "analysis": surface = .analysis
            default:
                throw Bad(what: "measure takes output, canvas, preview or "
                              + "analysis, got \(args[2])")
            }
            let reading = try read(engine,
                                  CGRect(x: r[0], y: r[1], width: r[2], height: r[3]),
                                  through: surface)
            readings[args[1]] = reading
            say(String(format: "  %-22@ luma %.4f  sat %.4f  (%@)\n",
                       args[1] as NSString, reading.luma, reading.saturation,
                       (surface == .canvas ? "canvas" : "output") as NSString))

        case "expect":
            guard args.count >= 3 else { throw Bad(what: "expect needs name, op, value") }
            try check(args[0], args[1], args[2])

        case "time":
            // Repeats another command and reports what one of them costs.
            //
            // "Slow" is not a bug report anyone can act on; a number is. The
            // repeats run quiet, because at a few microseconds a call the
            // stderr line dominates whatever is being measured.
            guard args.count >= 2, let n = Int(args[0]), n > 0 else {
                throw Bad(what: "time needs a count and a command")
            }
            let inner = args[1]
            let innerArgs = Array(args.dropFirst(2))
            quiet = true
            let began = DispatchTime.now().uptimeNanoseconds
            for _ in 0..<n {
                try step(inner, innerArgs, engine: engine, targeted: targeted)
            }
            let elapsed = DispatchTime.now().uptimeNanoseconds - began
            quiet = false
            say(String(format: "  %@ x%d: %.1f us each (%.1f ms total)\n",
                       ([inner] + innerArgs).joined(separator: " ") as NSString, n,
                       Double(elapsed) / 1000.0 / Double(n),
                       Double(elapsed) / 1_000_000.0))

        case "export":
            // Through `Engine.export`, which is the call the Export panel
            // makes. ⚠ The point of driving the real one is the overlay guard
            // inside it: `export` forces the coverage overlay off around the
            // write and restores it after, and that guard has never had a test.
            //
            // The settings are given the way the panel gives them — as an
            // `ExportSettings` — rather than as loose numbers, so a scenario
            // exercises the same `effectiveDepth` the interface does. A depth
            // written straight through would skip exactly the guard that stops
            // a JPEG asking for the undithered graph.
            guard let path = args.first else { throw Bad(what: "export needs a path") }
            let settings = ExportSettings()
            var longestEdge: UInt32 = 0
            settings.format = path.hasSuffix(".png") ? .png
                            : (path.hasSuffix(".tif") || path.hasSuffix(".tiff")) ? .tiff
                            : .jpeg
            for option in args.dropFirst() {
                let parts = option.split(separator: "=", maxSplits: 1).map(String.init)
                guard parts.count == 2 else {
                    throw Bad(what: "export options are key=value, got \(option)")
                }
                switch (parts[0], parts[1]) {
                case ("depth", "8"):          settings.depth = .eight
                case ("depth", "16"):         settings.depth = .sixteen
                case ("sharpen", "none"):     settings.sharpening = .none
                case ("sharpen", "screen"):   settings.sharpening = .screen
                case ("sharpen", "print"):    settings.sharpening = .print
                case ("metadata", "all"):     settings.metadata = .all
                case ("metadata", "nolocation"): settings.metadata = .noLocation
                case ("metadata", "none"):    settings.metadata = .none
                case ("size", let v):
                    guard let px = UInt32(v) else { throw Bad(what: "size takes pixels") }
                    longestEdge = px
                default: throw Bad(what: "unknown export option \(option)")
                }
            }
            do {
                try engine.export(
                    to: path,
                    maxDimension: longestEdge,
                    metadata: settings.metadata.rawValue,
                    depth: settings.effectiveDepth.rawValue,
                    sharpen: settings.sharpening.rawValue)
            }
            catch { throw Bad(what: "export failed — \(error.localizedDescription)") }
            let size = (try? FileManager.default
                .attributesOfItem(atPath: path)[.size] as? Int) ?? nil
            say(String(format: "  wrote %@ (%d bytes)\n",
                       (path as NSString).lastPathComponent as NSString, size ?? -1))

        case "probe":
            // A property of a file that was written, recorded under a name so
            // `expect` can assert on it exactly as it does on a measurement.
            //
            // ⚠ This reads the *file*, not the settings that produced it. The
            // three controls it serves all fail invisibly: a file that is eight
            // bits when sixteen was asked for looks the same in a thumbnail, and
            // one that still carries GPS after "Strip location" looks the same
            // to everyone except whoever receives it.
            guard args.count >= 3 else {
                throw Bad(what: "probe needs a path, a property and a name")
            }
            guard let property = ExportProbe.Property(rawValue: args[1]) else {
                throw Bad(what: "probe takes "
                    + ExportProbe.Property.allCases.map(\.rawValue).joined(separator: ", ")
                    + ", got \(args[1])")
            }
            guard let value = ExportProbe.measure(args[0], property) else {
                throw Bad(what: "could not read \(args[1]) from \(args[0])")
            }
            // Both fields, so `expect a == b` between two probes compares the
            // one number rather than silently passing on the unused half.
            readings[args[2]] = Reading(luma: value, saturation: value)
            say(String(format: "  %-22@ %@ %.5f\n", args[2] as NSString,
                       args[1] as NSString, value))

        case "identical":
            // Two files, byte for byte. A size comparison would pass on two
            // JPEGs that differ in every pixel and happen to compress alike.
            guard args.count >= 2 else { throw Bad(what: "identical needs two paths") }
            let a = FileManager.default.contents(atPath: args[0])
            let b = FileManager.default.contents(atPath: args[1])
            checks += 1
            if let a, let b, a == b {
                say("  ok    \(args[0]) and \(args[1]) are byte-identical\n")
            } else {
                failures += 1
                say("  FAIL  \(args[0]) and \(args[1]) differ — "
                  + "\(a?.count ?? -1) vs \(b?.count ?? -1) bytes\n")
            }

        case "shot":
            guard let p = args.first else { throw Bad(what: "shot needs a path") }
            Screenshot.writeCanvas(engine, to: p)

        case "print":
            say("  " + args.joined(separator: " ") + "\n")

        default:
            return false
        }
        return true
    }

    static func read(_ engine: Engine, _ region: CGRect,
                             through surface: Screenshot.Surface) throws -> Reading {
        guard let stats = Screenshot.regionStats(engine, region: region,
                                                 through: surface) else {
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
        //
        // ⚠️ Two recordings are compared on **saturation as well as luma**. A
        // mean is a weak signature for a photograph: a rotate-while-comparing
        // check passed against the wrong picture entirely because the two frames
        // happened to agree on mean luma to 0.0035 — inside the tolerance — while
        // differing by 0.06 in saturation. One number per patch is not enough to
        // say "the same picture".
        let want: Double
        var wantSat: Double?
        if let other = readings[rhs] { want = other.luma; wantSat = other.saturation }
        else if let v = Double(rhs) { want = v }
        else { throw Bad(what: "\(rhs) is neither a number nor a recording") }

        // Tolerance is one 8-bit code. The output is eight bits for the screen,
        // so anything tighter is asserting against quantisation.
        let eps = 1.0 / 255.0
        let sameLuma = abs(got.luma - want) < eps
        let sameSat = wantSat.map { abs(got.saturation - $0) < eps } ?? true
        let ok: Bool
        switch op {
        case "==": ok = sameLuma && sameSat
        case "!=": ok = !sameLuma || !sameSat
        case ">":  ok = got.luma > want
        case "<":  ok = got.luma < want
        default: throw Bad(what: "unknown operator \(op)")
        }
        if !ok { failures += 1 }
        let detail = wantSat.map {
            String(format: "  (got %.4f/%.4f, wanted %.4f/%.4f luma/sat)",
                   got.luma, got.saturation, want, $0)
        } ?? String(format: "  (got %.4f, wanted %.4f)", got.luma, want)
        say(String(format: "  %@  %@ %@ %@%@\n",
                   (ok ? "ok  " : "FAIL") as NSString, name as NSString,
                   op as NSString, rhs as NSString, detail as NSString))
    }
}
