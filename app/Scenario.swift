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
///     pick <x,y>                        the colour-mixer eyedropper
///     maskcolour <x,y>                  the colour range mask's picker, as a
///                                       click on the canvas does it
///     targeted <x,y> <delta>            pick, then drag, which is what applies it
///     auto                              the Auto button
///     preset <name>                     apply a built-in look by name
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
///     save <path>                       write the state to that photo's sidecar
///     export <path>                     write a real export, through the same
///                                       Engine.export the panel calls
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

    private struct Reading {
        var luma: Double
        var saturation: Double
    }

    private static var readings: [String: Reading] = [:]
    private static var failures = 0
    private static var checks = 0

    /// Set while `time` is running its repeats. Every informational write goes
    /// through `say`, so a timed loop reports its own number instead of the
    /// cost of two thousand lines of stderr.
    private static var quiet = false

    private static func say(_ text: String) {
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

    private struct Bad: LocalizedError {
        let what: String
        var errorDescription: String? { what }
    }

    /// The photograph `open` last opened. A matte is saved beside its
    /// photograph, so the verbs that make one need to know which.
    private static var photo: URL?

    /// Writes the matte down and records the reference on the selected row —
    /// exactly what `findMatte` does when the panel runs a model.
    ///
    /// ⚠ Here rather than left out of the runner, because `repro/README.md`'s
    /// standing warning is that a verb standing in for a gesture must do what
    /// the gesture does. A `matte` verb that skipped the file would exercise the
    /// upload and prove nothing about the save — which is the same gap the
    /// `crop` verb had when it skipped `commitCropEdit`.
    private static func persistMatte(_ alpha: [Float], width: Int, height: Int,
                                     engine: Engine, source: String) throws {
        guard let p = photo else { return }
        let id = try MatteStore.write(alpha, width: width, height: height, photo: p)
        engine.setMatteReference(id: id, source: source)
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
            photo = URL(fileURLWithPath: p)
            try engine.open(path: p)

        case "reopen":
            // Closes and opens the photograph again, through the same steps
            // `Editor.load` takes: decode, read the sidecar, restore the state,
            // upload the saved mattes, sweep the orphans.
            //
            // ⚠ It deliberately re-runs the *loading* path rather than calling
            // `engine.restore` alone. Everything this scenario is about lives
            // between those two — a matte is not in `DevelopState`, so a
            // "reopen" that only restored the state could not tell a saved
            // matte from one that was never written.
            guard let p = photo else { throw Bad(what: "reopen needs an open photo") }
            try engine.open(path: p.path)
            guard let saved = Sidecar.read(for: p)?.develop else {
                throw Bad(what: "no develop state in the sidecar to reopen with")
            }
            engine.restore(encoded: saved)
            engine.restoreMattes(photo: p)
            MatteStore.sweep(photo: p,
                             keeping: MatteStore.referenced(engine.maskComponents))

        case "rotate":
            engine.rotate(Int32(try number(0)))

        case "straighten":
            engine.straightenDeg = Float(try number(0))

        case "crop":
            engine.setCrop(x: Float(try number(0)), y: Float(try number(1)),
                           w: Float(try number(2)), h: Float(try number(3)))
            // ⚠ And commit it, which is what `CropOverlay` does when the drag
            // ends. `setCrop` renders but records nothing — that split exists so
            // a drag is one history entry rather than one per frame — so a
            // scenario that stopped at `setCrop` left the crop out of history
            // and any `undo` after it stepped past the crop instead of over it.
            // The runner's whole claim is that it drives what the interface
            // drives; this was a place it did not.
            engine.commitCropEdit()

        case "preview":
            // What opening the crop tool does to the render: the whole frame
            // with the crop drawn as context, which is a different output shape
            // and so a different thing for compare to hold.
            switch args.first {
            case "on":  engine.cropPreview = true
            case "off": engine.cropPreview = false
            default: throw Bad(what: "preview takes on or off")
            }

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
            // `matte` selects the raster kind *without* uploading one, which is
            // what a sidecar carrying a Subject row looks like on reopening: the
            // row is there and the raster is not. The `matte` verb below is the
            // other half — it uploads one.
            let kinds = ["none": Int32(0), "linear": 1, "radial": 2, "brush": 3,
                         "matte": 4, "range": 5, "colour": 6, "color": 6]
            guard let k = kinds[args.first ?? ""] else {
                throw Bad(what: "mask takes none, linear, radial, brush, matte or range")
            }
            engine.maskKind = k

        case "brush":
            // Walked through CanvasLayout at a fixed spacing, which is what the
            // canvas does with a real drag — not a hand-placed list of centres.
            // `carry` continues the spacing across segments, so a scripted
            // stroke has the same dabs a steady hand would lay.
            //
            // ⚠ It **appends** to whatever the component already holds, exactly
            // as the overlay does, because that is what makes a second pass
            // build on the first — and it is the only way to script painting
            // and then erasing over it.
            var added: [CGPoint] = []
            var carry: CGFloat = 0
            let path = try args.map { try point($0) }
            guard path.count >= 2 else { throw Bad(what: "brush needs two or more points") }
            added.append(path[0])
            for i in 1..<path.count {
                added += CanvasLayout.brushDabs(from: path[i - 1], to: path[i],
                                                radius: CGFloat(engine.brushRadius),
                                                carry: &carry)
            }
            let existing = engine.brushStroke
            var polarity = engine.brushErasePolarity
            if polarity.count < existing.count {
                polarity += Array(repeating: false, count: existing.count - polarity.count)
            }
            polarity += Array(repeating: engine.brushErasing, count: added.count)
            engine.setBrushStroke(existing + added, erasing: polarity)
            engine.commitBrushEdit()

        case "maskcheck":
            guard args.count >= 2, let cells = Int(args[0]), cells >= 2 else {
                throw Bad(what: "maskcheck needs a grid size and a local exposure")
            }
            try maskCheck(engine: engine, cells: cells, ev: Float(try number(1)))

        case "matte":
            // A synthetic matte, in frame coordinates, so the raster component
            // can be driven without a segmentation model. `disc` is a centred
            // circle, `left` a half-plane, `ramp` a horizontal 0→1 gradient —
            // all three have an answer you can predict and none depends on a
            // model whose output moves between OS releases.
            // research/masking.md §5.
            //
            // ⚠ `ramp` exists for the persistence round trip and the reason is
            // the whole point: a matte of nothing but 0 and 1 survives a wrong
            // colour space, a wrong bit depth and a wrong endianness, because
            // black and white land on black and white however the curve between
            // them is mangled. Only the mid-values can tell.
            let shape = args.first ?? ""
            guard shape == "disc" || shape == "left" || shape == "ramp" else {
                throw Bad(what: "matte takes disc, left or ramp")
            }
            let (mw, mh) = engine.maxMatteSize
            guard mw > 0, mh > 0 else { throw Bad(what: "no photo open") }
            if engine.maskComponents.isEmpty { engine.maskKind = 1 }
            var a = [Float](repeating: 0, count: mw * mh)
            for y in 0..<mh {
                for x in 0..<mw {
                    let u = (Double(x) + 0.5) / Double(mw)
                    let v = (Double(y) + 0.5) / Double(mh)
                    if shape == "ramp" {
                        a[y * mw + x] = Float(u)
                        continue
                    }
                    let on: Bool
                    if shape == "left" { on = u < 0.5 }
                    else {
                        let dx = u - 0.5, dy = v - 0.5
                        on = (dx * dx + dy * dy) < (0.25 * 0.25)
                    }
                    a[y * mw + x] = on ? 1 : 0
                }
            }
            guard engine.setMaskMatte(a, width: mw, height: mh) else {
                throw Bad(what: "the engine refused the matte")
            }
            engine.maskKind = 4
            try persistMatte(a, width: mw, height: mh, engine: engine, source: shape)

        case "select":
            // Runs a real segmentation model. ⚠ Not an assertion about what it
            // finds — that moves between OS releases — but the only way the
            // integration is exercised at all rather than shipped on the
            // strength of compiling. research/masking.md §5.
            let which: SubjectMatte.Kind
            switch args.first {
            case "subject": which = .subject
            case "person":  which = .person
            case "sky":     which = .sky
            default: throw Bad(what: "select takes subject or person")
            }
            if engine.maskComponents.isEmpty { engine.addMaskComponent(kind: 4) }
            let m = try SubjectMatte.generateBlocking(engine: engine, kind: which)
            var covered = 0
            for v in m.alpha where v > 0.5 { covered += 1 }
            say(String(format: "  %@ matte %dx%d, %.1f%% covered\n",
                       "\(which)" as NSString, m.width, m.height,
                       100.0 * Double(covered) / Double(max(m.alpha.count, 1))))
            guard engine.setMaskMatte(m.alpha, width: m.width, height: m.height) else {
                throw Bad(what: "the engine refused the matte")
            }
            engine.maskKind = 4
            try persistMatte(m.alpha, width: m.width, height: m.height,
                             engine: engine, source: which.label)

        case "control":
            // Asserts what a control *holds*, not what the picture looks like.
            //
            // ⚠ This exists because "the picture changed" is far too weak an
            // assertion about a button that writes five fields. Measured
            // 2026-07-31: `repro/undo-after-auto.txt` asserted only that Auto
            // moved the frame, and **four of the five assignments could be
            // deleted one at a time with the scenario still green** — the
            // remaining fields moved the picture enough to satisfy it. A button
            // that quietly stopped setting clarity, or whites, or the shadow
            // lift, looked exactly like a working one.
            guard args.count >= 3 else {
                throw Bad(what: "control needs a name, an operator and a value")
            }
            let ctlName = args[0], ctlOp = args[1]
            guard let want = Double(args[2]) else {
                throw Bad(what: "control needs a number, got \(args[2])")
            }
            guard let got = controlValue(ctlName, engine).map(Double.init) else {
                throw Bad(what: "no readable control named \(ctlName)")
            }
            checks += 1
            let ok: Bool
            switch ctlOp {
            case "==": ok = abs(got - want) <= 1e-3
            case "!=": ok = abs(got - want) > 1e-3
            case ">":  ok = got > want
            case "<":  ok = got < want
            default: throw Bad(what: "control takes ==, !=, > or <")
            }
            if ok {
                say(String(format: "  ok    %@ %@ %g  (holds %g)\n",
                           ctlName as NSString, ctlOp as NSString, want, got))
            } else {
                failures += 1
                say(String(format: "  FAIL  %@ %@ %g  (holds %g)\n",
                           ctlName as NSString, ctlOp as NSString, want, got))
            }

        case "refuses":
            // Asserts a detector **declines** this photograph.
            //
            // ⚠ This verb exists because its absence made a check that could not
            // fail. `repro/sky-mask.txt` wanted to pin that the night frames are
            // refused, `select` throws when the detector declines, and a throw
            // fails the scenario — so the file settled for opening the frame,
            // setting a local exposure and asserting the picture had not moved.
            // With no mask row on the photograph a local exposure does nothing,
            // so that check passed whether the detector refused, accepted, or
            // did not exist. It was green for a year of sessions and proved
            // nothing.
            //
            // A refusal is a *result*, so it gets to be asserted like one.
            let refusedKind: SubjectMatte.Kind
            switch args.first {
            case "subject": refusedKind = .subject
            case "person":  refusedKind = .person
            case "sky":     refusedKind = .sky
            default: throw Bad(what: "refuses takes subject, person or sky")
            }
            checks += 1
            do {
                let m = try SubjectMatte.generateBlocking(engine: engine, kind: refusedKind)
                var covered = 0
                for v in m.alpha where v > 0.5 { covered += 1 }
                failures += 1
                say(String(format: "  FAIL  %@ was expected to refuse, and returned "
                                   + "%.1f%% coverage\n",
                           "\(refusedKind)" as NSString,
                           100.0 * Double(covered) / Double(max(m.alpha.count, 1))))
            } catch {
                say("  ok    \(refusedKind) refuses — \(error.localizedDescription)\n")
            }

        case "spot":
            // Places a spot at a point on the displayed picture, exactly as a
            // click does. research/spot-removal.md.
            let at = try point(args.first ?? "")
            if args.count > 1 { engine.spotRadius = Float(try number(1)) }
            if args.count > 2 { engine.spotHeal = args[2] != "clone" }
            guard engine.addSpot(atFrame: at) else {
                throw Bad(what: "the engine refused the spot")
            }

        case "maskadd":
            // ⚠ Adds a *row*. `mask <kind>` does not: on a non-empty group its
            // setter changes the selected row's kind, which is what the old
            // segmented picker needed. A scenario that used it to build a
            // second row silently tested a one-row group.
            let named = ["linear": Int32(1), "radial": 2, "brush": 3,
                         "matte": 4, "range": 5, "colour": 6, "color": 6]
            guard let k = named[args.first ?? ""] else {
                throw Bad(what: "maskadd needs a kind")
            }
            if engine.maskComponents.isEmpty {
                engine.maskKind = k
            } else {
                guard engine.addMaskComponent(kind: k) else {
                    throw Bad(what: "the group is full")
                }
                engine.commitMaskGroupEdit("Add mask")
            }

        case "masklayer":
            // Selects a layer by index, by selecting its first row. Layers are
            // runs of components, so there is no separate layer list to index.
            guard let want = Int(args.first ?? "") else {
                throw Bad(what: "masklayer needs an index")
            }
            var seen = 0
            var found = -1
            for (i, m) in engine.maskComponents.enumerated() {
                if i == 0 || m.startsLayer { if seen == want { found = i; break }; seen += 1 }
            }
            guard found >= 0 else { throw Bad(what: "no layer \(want)") }
            engine.selectedMask = found

        case "masksplit":
            guard let i = Int(args.first ?? "") else {
                throw Bad(what: "masksplit needs a row index")
            }
            engine.toggleLayerBreak(at: i)

        case "maskhide":
            // The eye button, through the same Engine call it makes. ⚠ Not
            // `set maskHidden`, which goes through `editSelected` and pushes —
            // that is precisely the path that could not see the bug.
            guard let i = Int(args.first ?? "") else {
                throw Bad(what: "maskhide needs a row index")
            }
            engine.toggleMaskHidden(i)

        case "maskmove":
            guard args.count >= 2, let i = Int(args[0]), let by = Int(args[1]) else {
                throw Bad(what: "maskmove needs a row index and an offset")
            }
            engine.moveMaskComponent(from: i, by: by)

        case "maskkind":
            guard args.count >= 2, let i = Int(args[0]) else {
                throw Bad(what: "maskkind needs a row index and a kind")
            }
            let named = ["none": Int32(0), "linear": 1, "radial": 2, "brush": 3,
                         "matte": 4, "range": 5, "colour": 6, "color": 6]
            guard let k = named[args[1]] else { throw Bad(what: "no kind \(args[1])") }
            engine.setMaskKind(k, at: i)

        case "spotdrag":
            // Moves a spot's source or its destination, through the same
            // `Engine.moveSpot` the overlay's drag calls. The scenario names a
            // point on the *displayed* picture, as a hand does; the engine does
            // the conversion into frame coordinates.
            guard args.count >= 3 else {
                throw Bad(what: "spotdrag needs an index, source|dest and a point")
            }
            guard let index = Int(args[0]) else { throw Bad(what: "spotdrag needs an index") }
            let to = try point(args[2])
            switch args[1] {
            case "source": engine.moveSpot(index, destination: nil, source: to)
            case "dest":   engine.moveSpot(index, destination: to, source: nil)
            default: throw Bad(what: "spotdrag takes source or dest")
            }
            engine.commitSpotEdit()

        case "spotat":
            // Where the interface *draws* a spot, in displayed coordinates —
            // through `Engine.spotPlacements`, which is what the overlay reads.
            // ⚠ This is the oracle for the geometry: a spot stored in frame
            // coordinates has to come back to the point it was placed at, or
            // the handles are drawn somewhere the photographer did not click.
            guard args.count >= 3, let index = Int(args[0]) else {
                throw Bad(what: "spotat needs an index, source|dest and a point")
            }
            let want = try point(args[2])
            let places = engine.spotPlacements
            guard places.indices.contains(index) else {
                throw Bad(what: "no spot \(index) — there are \(places.count)")
            }
            let got = args[1] == "source" ? places[index].source
                                          : places[index].destination
            checks += 1
            let off = hypot(got.x - want.x, got.y - want.y)
            if off < 0.004 {
                say(String(format: "  ok    spot %d %@ is drawn at %.4f,%.4f\n",
                           index, args[1] as NSString, got.x, got.y))
            } else {
                failures += 1
                say(String(format: "  FAIL  spot %d %@ drawn at %.4f,%.4f, "
                         + "wanted %.4f,%.4f (off by %.4f)\n",
                           index, args[1] as NSString, got.x, got.y,
                           want.x, want.y, off))
            }

        case "preset":
            // Applies a built-in look by name, through the same Engine call the
            // panel uses. A preset is a patch, so what a scenario can check is
            // that it moved what it names and left the frame alone.
            guard let want = args.first else { throw Bad(what: "preset needs a name") }
            let store = PresetStore(url: nil)
            guard let p = store.presets.first(where: {
                $0.name.lowercased().hasPrefix(want.lowercased())
            }) else {
                throw Bad(what: "no preset named \(want)")
            }
            engine.apply(preset: p)
            say("  applied preset \(p.name) (\(p.groups.count) groups)\n")

        case "save":
            // Writes the current state to the photo's sidecar, which is what
            // autosave does when the edits settle. Here so a scenario can set a
            // photograph up for something that reads sidecars rather than the
            // engine — batch export, and sync.
            guard let p = args.first else { throw Bad(what: "save needs a path") }
            guard let encoded = try? JSONEncoder().encode(engine.state) else {
                throw Bad(what: "could not encode the state")
            }
            Sidecar.merge(into: URL(fileURLWithPath: p)) { $0.develop = encoded }

        case "overlay":
            // Paint the coverage over the picture, as `Show mask` does. With
            // `shot`, this is how "is the mask where I put it" becomes
            // something to look at rather than argue about.
            switch args.first {
            case "on":  engine.maskOverlay = true
            case "off": engine.maskOverlay = false
            default: throw Bad(what: "overlay takes on or off")
            }

        case "maskcolour", "maskcolor":
            // The mask colour picker, through the same call `ImageCanvas`
            // makes on a click. A scenario that set the RGB directly would be
            // testing a struct rather than the tool.
            let p = try point(args.first ?? "")
            guard engine.pickMaskColour(at: p) else {
                throw Bad(what: "no colour at \(p.x),\(p.y) — is a mask row selected?")
            }
            let c = engine.maskColour
            say(String(format: "  picked colour %.4f %.4f %.4f\n",
                       c.r, c.g, c.b))

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

        case "interact":
            // Arms or disarms the preview path, as a slider's drag does.
            switch args.first {
            case "on":  engine.beginInteraction()
            case "off": engine.endInteraction()
            default: throw Bad(what: "interact takes on or off")
            }

        case "drag":
            // A slider drag, and what one tick of it costs.
            //
            // Distinct values on purpose. `time 40 set dehaze 0.6` measures
            // nothing at all: `apply` compares the adjustment block field by
            // field, so the second tick dirties no node and the loop times an
            // empty render. A drag is a *sweep*, and the sweep is what makes
            // every tick pay for the frame.
            guard args.count >= 4, let ticks = Int(args[3]), ticks > 1 else {
                throw Bad(what: "drag needs a control, a start, an end and a tick count")
            }
            let control = args[0]
            let from = try number(1), to = try number(2)
            quiet = true
            let dragBegan = DispatchTime.now().uptimeNanoseconds
            for i in 0..<ticks {
                let v = Float(from + (to - from) * Double(i) / Double(ticks - 1))
                var thrown: Error?
                engine.edit(control) {
                    do { try apply(control: control, value: v, to: engine) }
                    catch { thrown = error }
                }
                if let thrown { quiet = false; throw thrown }
            }
            let dragElapsed = DispatchTime.now().uptimeNanoseconds - dragBegan
            quiet = false
            let perTick = Double(dragElapsed) / 1_000_000.0 / Double(ticks)
            say(String(format: "  drag %-12@ %.1f ms per tick  (%.0f fps, %d ticks)\n",
                       control as NSString, perTick,
                       perTick > 0 ? 1000.0 / perTick : 0, ticks))

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
            guard let path = args.first else { throw Bad(what: "export needs a path") }
            do { try engine.export(to: path) }
            catch { throw Bad(what: "export failed — \(error.localizedDescription)") }
            let size = (try? FileManager.default
                .attributesOfItem(atPath: path)[.size] as? Int) ?? nil
            say(String(format: "  wrote %@ (%d bytes)\n",
                       (path as NSString).lastPathComponent as NSString, size ?? -1))

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
        // ⚠️ The **scene** colour, because that is what `ImageCanvas.beginDrag`
        // reads. This took the display colour, which is a different sample
        // through a different code path: the display value comes from the
        // output texture and is already cropped and turned, while the scene
        // value is looked up in the whole frame and has to be carried there.
        // So the runner exercised the one that could not go wrong and the
        // interface used the one that did — the same kind of fidelity gap the
        // `crop` verb had when it skipped `commitCropEdit`.
        guard let hue = TargetedAdjust.hue(r: s.scene.r, g: s.scene.g,
                                          b: s.scene.b) else {
            throw Bad(what: String(format:
                "the pixel at %.2f,%.2f is too near grey to have a hue "
                + "(scene r %.3f g %.3f b %.3f) — the tool refuses, by design",
                p.x, p.y, s.scene.r, s.scene.g, s.scene.b))
        }
        let band = TargetedAdjust.band(forHue: hue)
        targeted.begin(band: band, hue: hue)
        say(String(format: "  picked hue %.1f deg -> band %@\n",
                   hue, "\(band)" as NSString))

        if let drag {
            // What the drag does to the band it picked. Saturation is the tool's
            // default mode.
            let before = engine.satShift[band.rawValue]
            engine.satShift[band.rawValue] = min(1, max(-1, before + Float(drag)))
            targeted.end()
        }
    }

    /// Reads a control back off the engine, for `control`.
    ///
    /// ⚠ Deliberately a **short** list rather than a mirror of every setter.
    /// Every entry here is one a scenario has an actual reason to read — which
    /// today means the five fields the Auto button writes. A getter for a
    /// control nobody asserts is a second copy of the setter table waiting to
    /// disagree with it.
    private static func controlValue(_ name: String, _ e: Engine) -> Float? {
        switch name {
        case "exposure":       return e.exposureEv
        case "whites":         return e.whites
        case "blacks":         return e.blacks
        case "clarity":        return e.clarity
        case "fusion", "lift": return e.fusion
        case "contrast":       return e.contrast
        case "saturation":     return e.saturation
        case "dehaze":         return e.dehaze
        default:               return nil
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
        case "localContrast": e.localContrast = value
        case "localSaturation": e.localSaturation = value
        case "localWarmth": e.localWarmth = value
        case "localTint": e.localTint = value
        case "maskRefine": e.maskRefine = value
        case "brushRadius": e.brushRadius = value
        case "brushFlow":   e.brushFlow = value
        case "maskCentreX": e.maskCentreX = value
        case "maskCentreY": e.maskCentreY = value
        case "maskAngle":   e.maskAngle = value
        case "maskLength":  e.maskLength = value
        case "maskRadiusX": e.maskRadiusX = value
        case "maskRadiusY": e.maskRadiusY = value
        case "maskFeather": e.maskFeather = value
        case "maskRoundness": e.maskRoundness = value
        case "maskRangeLo":   e.maskRangeLo = value
        case "maskRangeHi":   e.maskRangeHi = value
        case "maskRangeSoft": e.maskRangeSoft = value
        case "maskCompose":   e.maskCompose = Int32(value)
        case "maskColourTol", "maskColorTol":   e.maskColourTol = value
        case "maskColourSoft", "maskColorSoft": e.maskColourSoft = value
        case "maskInvert":  e.maskInvert = value != 0
        case "brushErase":  e.brushErasing = value != 0
        case "maskHidden":  e.maskHidden = value != 0
        case "brushHardness": e.brushHardness = value
        default: throw Bad(what: "no control named \(control)")
        }
    }

    /// Does the mask the interface *draws* sit on the coverage the engine
    /// *renders*?
    ///
    /// This is the question "the mask is not aligned with the image" asks, and
    /// nothing could answer it before. The scenario runner measures the render;
    /// the overlay is drawn from `CanvasLayout`, which carries its own
    /// transcription of the mask kernel (`maskAlpha`) precisely so it can be an
    /// oracle. Comparing the two is therefore comparing what the photographer
    /// is shown against what they get.
    ///
    /// ⚠️ Asserted on **exact equality where coverage is zero**, not on a
    /// correlation. A mask shifted by a tenth of the frame still darkens
    /// roughly the right part of the picture and still looks plausible in a
    /// screenshot; what it cannot do is leave the cells the interface calls
    /// "outside" bit-identical. Cells on the falloff are skipped rather than
    /// fudged with a tolerance — the two sides of the boundary are where the
    /// answer is unambiguous.
    private static func maskCheck(engine: Engine, cells: Int, ev: Float) throws {
        var m = CanvasLayout.MaskPlacement()
        m.kind = Int(engine.maskKind)
        m.centre = CGPoint(x: CGFloat(engine.maskCentreX), y: CGFloat(engine.maskCentreY))
        m.angle = CGFloat(engine.maskAngle)
        m.length = CGFloat(engine.maskLength)
        m.radius = CGSize(width: CGFloat(engine.maskRadiusX),
                          height: CGFloat(engine.maskRadiusY))
        m.feather = CGFloat(engine.maskFeather)
        m.roundness = CGFloat(engine.maskRoundness)
        m.invert = engine.maskInvert
        guard m.kind == 1 || m.kind == 2 else {
            throw Bad(what: "maskcheck needs a linear or radial mask; the brush "
                          + "has no closed form for the overlay to draw")
        }

        // Classify every cell by what the interface believes, sampling inside
        // the cell rather than at its centre: a cell whose centre is covered
        // can still straddle the falloff.
        let step = 1.0 / CGFloat(cells)
        var inside: [(Int, Int)] = [], outside: [(Int, Int)] = []
        var map = [[Character]](repeating: [Character](repeating: " ", count: cells),
                                count: cells)
        for j in 0..<cells {
            for i in 0..<cells {
                var lo: CGFloat = 1, hi: CGFloat = 0
                for sj in 0...4 {
                    for si in 0...4 {
                        let q = CGPoint(x: (CGFloat(i) + CGFloat(si) / 4) * step,
                                        y: (CGFloat(j) + CGFloat(sj) / 4) * step)
                        let a = CanvasLayout.maskAlpha(q, m)
                        lo = min(lo, a); hi = max(hi, a)
                    }
                }
                // ⚠️ "Clear" means alpha *exactly* zero, not merely small.
                // The invariant being checked is that zero coverage leaves the
                // pixel bit-identical, and at alpha 0.02 a two-stop local
                // exposure moves luma by about 0.005 — past the one-code
                // tolerance, and rightly so. Calling that cell clear made the
                // test report a defect that was its own classification.
                // smootherstep saturates, so exact zero is reachable.
                if lo >= 0.999 { inside.append((j, i)); map[j][i] = "#" }
                else if hi <= 1e-6 { outside.append((j, i)); map[j][i] = "." }
                else { map[j][i] = "~" }
            }
        }

        func grid() throws -> [[Double]] {
            var g = [[Double]](repeating: [Double](repeating: 0, count: cells), count: cells)
            for j in 0..<cells {
                for i in 0..<cells {
                    let r = CGRect(x: CGFloat(i) * step, y: CGFloat(j) * step,
                                   width: step, height: step)
                    g[j][i] = try read(engine, r, through: .output).luma
                }
            }
            return g
        }

        let held = engine.localExposureEv
        engine.localExposureEv = 0
        let base = try grid()
        engine.localExposureEv = ev
        let got = try grid()
        engine.localExposureEv = held

        let eps = 1.0 / 255.0
        var strayed = 0, missed = 0
        var worstStray = 0.0, worstStrayAt = (0, 0)
        for (j, i) in outside where abs(got[j][i] - base[j][i]) >= eps {
            strayed += 1
            let d = abs(got[j][i] - base[j][i])
            if d > worstStray { worstStray = d; worstStrayAt = (j, i) }
            map[j][i] = "!"
        }
        for (j, i) in inside where abs(got[j][i] - base[j][i]) < eps {
            missed += 1
            map[j][i] = "o"
        }

        say("  the interface's mask, cell by cell "
            + "(# covered, . clear, ~ falloff, ! leaked, o did nothing):\n")
        for j in 0..<cells { say("    " + String(map[j]) + "\n") }

        checks += 2
        if strayed > 0 {
            failures += 1
            say(String(format:
                "  FAIL  coverage where the interface draws none: %d of %d cells, "
                + "worst %.4f luma at row %d col %d\n",
                strayed, outside.count, worstStray, worstStrayAt.0, worstStrayAt.1))
        } else {
            say("  ok    every cell the interface draws clear is bit-identical "
                + "(\(outside.count) cells)\n")
        }
        if missed > 0 {
            failures += 1
            say("  FAIL  no coverage where the interface draws it: "
                + "\(missed) of \(inside.count) cells\n")
        } else {
            say("  ok    every cell the interface draws covered moved "
                + "(\(inside.count) cells)\n")
        }
    }

    private static func read(_ engine: Engine, _ region: CGRect,
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

    private static func fail(_ why: String) -> Never {
        FileHandle.standardError.write(Data("orion: \(why)\n".utf8))
        exit(2)
    }
}
