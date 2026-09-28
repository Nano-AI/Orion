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
        case "workflowcheck":
            guard let photo else { throw Bad(what: "workflowcheck needs an open photo") }
            try checkDesktopWorkflow(photo: photo)

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
            // Everything the patch measures on one line (#278). The first two
            // are where they always were, so a reader of an old transcript
            // finds them in the same place.
            say(String(format: "  %-22@ luma %.4f  sat %.4f  hue %.0f  rgb %.3f/%.3f/%.3f  "
                             + "clip %.3f/%.3f  shading %.3f  (%@)\n",
                       args[1] as NSString, reading.luma, reading.saturation, reading.hue,
                       reading.red, reading.green, reading.blue,
                       reading.clippedHigh, reading.clippedLow, reading.shading,
                       (surface == .canvas ? "canvas" : "output") as NSString))

        case "zones":
            // An n×n grid of the picture, luma and saturation each, for a first
            // look at where the light is without rendering anything.
            let n = args.isEmpty ? 3 : Int(try number(args, 0))
            guard n >= 1, n <= 12 else { throw Bad(what: "zones takes 1..12") }
            for row in 0..<n {
                var line = "  "
                for col in 0..<n {
                    let rect = CGRect(x: Double(col) / Double(n), y: Double(row) / Double(n),
                                      width: 1 / Double(n), height: 1 / Double(n))
                    let s = try read(engine, rect, through: .output)
                    line += String(format: "%.2f/%.2f ", s.luma, s.saturation)
                }
                say(line + "\n")
            }

        case "histogram":
            // Integer bins straight from the engine, one line per channel -
            // the picture's tonal shape as text, which is how an agent should
            // read a histogram (never as a rendered chart, #272).
            let bins = args.isEmpty ? 16 : Int(try number(args, 0))
            guard bins >= 2, bins <= 256 else { throw Bad(what: "histogram takes 2..256 bins") }
            guard let h = engine.histogram(bins: bins) else {
                throw Bad(what: "no histogram - is a photo open?")
            }
            for (c, name) in ["R", "G", "B"].enumerated() {
                let row = h[(c * bins)..<((c + 1) * bins)].map(String.init).joined(separator: " ")
                say("  \(name) \(row)\n")
            }

        case "look":
            // The developed picture, small, as a PNG for the agent to open.
            // 768 px on the long edge by default: about 530 visual tokens.
            guard let path = args.first else { throw Bad(what: "look needs a path") }
            let long = args.count > 1 ? Int(try number(args, 1)) : 768
            guard long >= 64, long <= 4096 else { throw Bad(what: "look size is 64..4096") }
            let w = Int(engine.imageWidth), hgt = Int(engine.imageHeight)
            guard w > 0, hgt > 0 else { throw Bad(what: "no photo open") }
            let scale = Double(long) / Double(max(w, hgt))
            let size = (width: max(1, Int(Double(w) * scale)),
                        height: max(1, Int(Double(hgt) * scale)))
            guard let image = Screenshot.developedCGImage(engine, fitting: size),
                  let bytes = Screenshot.writePNG(image, to: path) else {
                throw Bad(what: "could not write \(path)")
            }
            say("  wrote \((path as NSString).lastPathComponent) \(size.width)x\(size.height) (\(bytes) bytes)\n")

        case "zoom":
            // A region at native resolution, with a stated reason: an agent
            // that has to say why it is looking asks for fewer pictures
            // (research/agent-interaction.md).
            guard args.count >= 2 else { throw Bad(what: "zoom needs a path, a region and a reason") }
            let r = args[1].split(separator: ",").compactMap { Double($0) }
            guard r.count == 4 else { throw Bad(what: "region is x,y,w,h") }
            guard args.count >= 4, args[2] == "because" else {
                throw Bad(what: "zoom needs a reason: zoom <path> <x,y,w,h> because <why>")
            }
            let w = Int(engine.imageWidth), hgt = Int(engine.imageHeight)
            guard w > 0, hgt > 0 else { throw Bad(what: "no photo open") }
            guard let full = Screenshot.developedCGImage(engine, fitting: (width: w, height: hgt)),
                  let crop = full.cropping(to: CGRect(x: r[0] * Double(w), y: r[1] * Double(hgt),
                                                      width: r[2] * Double(w), height: r[3] * Double(hgt))),
                  let bytes = Screenshot.writePNG(crop, to: args[0]) else {
                throw Bad(what: "could not write \(args[0])")
            }
            say("  wrote \((args[0] as NSString).lastPathComponent) \(crop.width)x\(crop.height) "
              + "(\(bytes) bytes) because \(args[3...].joined(separator: " "))\n")

        case "faces":
            // Face boxes from Vision on the displayed picture, in display
            // space for `crop` and `measure`, and the box's centre and half
            // sizes carried into frame space through the engine's own map, so
            // the numbers drop straight into `set maskCentreX` (#278).
            //
            // ⚠ The arithmetic lives in `AgentFaces`, which `Orion --agent
            // faces` also calls: one detector, one carry into frame space,
            // two printings of it.
            let faces: [AgentFaces.Face]
            do { faces = try AgentFaces.detect(engine: engine) }
            catch let error as AgentFaces.Failure { throw Bad(what: error.description) }
            if faces.isEmpty { say("  no faces\n") }
            for (i, f) in faces.enumerated() {
                say(String(format: "  face %d  display %.3f,%.3f,%.3f,%.3f  centre %.3f,%.3f  "
                                 + "frame %.3f,%.3f  radius %.3f,%.3f\n",
                           i + 1, f.display.minX, f.display.minY,
                           f.display.width, f.display.height,
                           f.display.midX, f.display.midY,
                           f.centerX, f.centerY, f.radiusX, f.radiusY))
            }

        case "toframe", "todisplay":
            // One point across the frame/display map, both ways printed, so a
            // mask centre can be chosen where `measure` reads (#278).
            guard let p = args.first else { throw Bad(what: "\(verb) needs x,y") }
            let v = p.split(separator: ",").compactMap { Double($0) }
            guard v.count == 2 else { throw Bad(what: "a point is x,y") }
            let map = engine.frameDisplayMap
            let q = verb == "toframe" ? map.frame(CGPoint(x: v[0], y: v[1]))
                                      : map.display(CGPoint(x: v[0], y: v[1]))
            say(String(format: "  %@ %.3f,%.3f is %@ %.3f,%.3f\n",
                       (verb == "toframe" ? "display" : "frame") as NSString, v[0], v[1],
                       (verb == "toframe" ? "frame" : "display") as NSString, q.x, q.y))

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

        case "state":
            // The edit, as the lines that replay it. Against the camera's own
            // settings by default - what *this photograph* has had done to it -
            // so an untouched frame says so in one line instead of listing the
            // white balance the file arrived with.
            guard engine.isLoaded else { throw Bad(what: "no photo open") }
            switch args.first {
            case "masks":
                for line in DevelopDiff.outline(engine.state) { say("  \(line)\n") }
            case "full":
                say("  # process \(engine.process)\n")
                let lines = DevelopDiff.lines(from: DevelopState(), to: engine.state)
                for line in lines { say("  \(line)\n") }
            case nil:
                // A comment, not a `set`: the diff below already emits `set
                // process` when the photograph's differs from a fresh state's,
                // and a reader auditing the edit wants to see it either way
                // (#279) without a replay setting what it need not.
                say("  # process \(engine.process)\n")
                let lines = DevelopDiff.lines(from: engine.defaults, to: engine.state)
                if lines.isEmpty { say("  as shot - nothing changed\n") }
                for line in lines { say("  \(line)\n") }
            default:
                throw Bad(what: "state takes nothing, masks or full")
            }

        case "print":
            say("  " + args.joined(separator: " ") + "\n")

        default:
            return false
        }
        return true
    }

    static func read(_ engine: Engine, _ region: CGRect,
                             through surface: Screenshot.Surface) throws -> Reading {
        guard let s = Screenshot.regionStats(engine, region: region,
                                             through: surface) else {
            throw Bad(what: "could not read the output — is a photo open?")
        }
        return Reading(luma: s.luma, saturation: s.saturation, hue: s.hue,
                       hueStrength: s.hueStrength,
                       red: s.red, green: s.green, blue: s.blue,
                       clippedHigh: s.clippedHigh, clippedLow: s.clippedLow,
                       shading: s.shading)
    }

    private static func check(_ name: String, _ op: String, _ rhs: String) throws {
        checks += 1
        // `name.field` asserts one of the other numbers a patch carries
        // (#278): `cheek.shading > 0.1`, `sky.clippedHigh < 0.01`. The bare
        // name keeps its two-number signature below.
        if let dot = name.firstIndex(of: ".") {
            let base = String(name[..<dot]), field = String(name[name.index(after: dot)...])
            guard let reading = readings[base] else { throw Bad(what: "nothing recorded under \(base)") }
            guard let got = reading.field(field) else {
                throw Bad(what: "\(field) is not a field a reading carries")
            }
            let want: Double
            if let other = rhs.firstIndex(of: "."), let o = readings[String(rhs[..<other])],
               let v = o.field(String(rhs[rhs.index(after: other)...])) { want = v }
            else if let v = Double(rhs) { want = v }
            else { throw Bad(what: "\(rhs) is neither a number nor a recording's field") }
            let eps = field == "hue" ? 1.0 : 1.0 / 255.0
            let ok: Bool
            switch op {
            case "==": ok = abs(got - want) < eps
            case "!=": ok = abs(got - want) >= eps
            case ">":  ok = got > want
            case "<":  ok = got < want
            default: throw Bad(what: "expect takes ==, !=, > or <, got \(op)")
            }
            if ok { say(String(format: "  ok    %@ %@ %@  (got %.4f)\n", name as NSString, op as NSString, rhs as NSString, got)) }
            else {
                failures += 1
                say(String(format: "  FAIL  %@ %@ %@  (got %.4f, wanted %.4f)\n", name as NSString, op as NSString, rhs as NSString, got, want))
            }
            return
        }
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
