import AppKit
import SwiftUI

/// The mask group and the tools that draw into it: the row list, the brush,
/// the rasters, the colour and range pickers, the spots, and the overlay.
///
/// ⚠ **Adding a verb is one edit, in the switch below.**
///
/// ⚠ Decision #89: `maskcolor`/`maskcolour` is one `case` with two spellings,
/// as are the `color`/`colour` kind names in the three kind tables. They are
/// aliases on purpose and must not be collapsed.

extension Scenario {

    /// Answers true when this family took the verb.
    static func maskStep(_ verb: String, _ args: [String],
                         engine: Engine) throws -> Bool {
        switch verb {
        case "mask":
            // `matte` selects the raster kind *without* uploading one, which is
            // what a sidecar carrying a Subject row looks like on reopening: the
            // row is there and the raster is not. The `matte` verb below is the
            // other half — it uploads one.
            let kinds = ["none": Int32(0), "linear": 1, "radial": 2, "brush": 3,
                         "matte": 4, "range": 5, "color": 6, "colour": 6]
            guard let k = kinds[args.first ?? ""] else {
                throw Bad(what: "mask takes none, linear, radial, brush, matte or range")
            }
            engine.maskKind = k

        case "brush":
            // Walked through CanvasLayout at a fixed spacing, which is what the
            // canvas does with a real drag — not a hand-placed list of centers.
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
            try maskCheck(engine: engine, cells: cells, ev: Float(try number(args, 1)))

        case "matte":
            // A synthetic matte, in frame coordinates, so the raster component
            // can be driven without a segmentation model. `disc` is a centerd
            // circle, `left` a half-plane, `ramp` a horizontal 0→1 gradient —
            // all three have an answer you can predict and none depends on a
            // model whose output moves between OS releases.
            // research/masking.md §5.
            //
            // ⚠ `ramp` exists for the persistence round trip and the reason is
            // the whole point: a matte of nothing but 0 and 1 survives a wrong
            // color space, a wrong bit depth and a wrong endianness, because
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

        case "maskdowngrade":
            // Rewrites the sidecar's develop blob into the display-space
            // era's form — what an old build would have written — so the
            // load-time migration can be watched doing its job rather than
            // reasoned about. The `corrupt` verb's shape, for the same
            // reason: the unreadable branch there and the legacy branch here
            // are both unreachable headlessly without a verb that
            // manufactures the past.
            //
            // ⚠ Only under a geometry this verb can invert *exactly*:
            // quarter turns, no crop, no straighten, no keystone. Turns are
            // rigid in normalized coordinates, so the inverse is the point
            // map plus k·π/2 on the angle and nothing else — and the
            // round-trip assertion in `repro/mask-survives-the-fix.txt` is
            // then about the migration, not about this verb's arithmetic.
            guard let p = args.first else {
                throw Bad(what: "maskdowngrade needs a path")
            }
            let target = URL(fileURLWithPath: p)
            guard let blob = Sidecar.read(for: target)?.develop,
                  var s = try? JSONDecoder().decode(DevelopState.self, from: blob)
            else { throw Bad(what: "no develop state in \(p) to downgrade") }
            guard s.maskSpace == 1 else {
                throw Bad(what: "the sidecar is already legacy")
            }
            guard s.cropX == 0, s.cropY == 0, s.cropW == 1, s.cropH == 1,
                  s.straightenDeg == 0, s.perspectiveVertical == 0,
                  s.perspectiveHorizontal == 0, s.perspectiveAspect == 0 else {
                throw Bad(what: "maskdowngrade only inverts a turns-only "
                              + "geometry exactly")
            }
            // The engine's own frame → display map, which under turns alone
            // is the exact inverse of what the migration will apply.
            let down = engine.frameDisplayMap
            let k = ((engine.quarterTurns % 4) + 4) % 4
            for i in s.maskComponents.indices {
                var c = s.maskComponents[i]
                let d = down.display(CGPoint(x: CGFloat(c.centerX),
                                             y: CGFloat(c.centerY)))
                c.centerX = Float(d.x)
                c.centerY = Float(d.y)
                c.angle += Float(k) * .pi / 2
                var stroke = c.brushStroke
                for j in stride(from: 0, to: stroke.count - 1, by: 2) {
                    let q = down.display(CGPoint(x: CGFloat(stroke[j]),
                                                 y: CGFloat(stroke[j + 1])))
                    stroke[j] = Float(q.x)
                    stroke[j + 1] = Float(q.y)
                }
                c.brushStroke = stroke
                s.maskComponents[i] = c
            }
            s.maskSpace = 0
            guard let encoded = try? JSONEncoder().encode(s),
                  Sidecar.merge(into: target, { $0.develop = encoded }) else {
                throw Bad(what: "the sidecar refused the downgraded write")
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
            if args.count > 1 { engine.spotRadius = Float(try number(args, 1)) }
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
                         "matte": 4, "range": 5, "color": 6, "colour": 6]
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
            // `MaskLayers.group` is the one grouping definition — the same one
            // the cards and `Engine.selectedLayer` read.
            guard let want = Int(args.first ?? "") else {
                throw Bad(what: "masklayer needs an index")
            }
            let runs = MaskLayers.group(engine.maskComponents)
            guard runs.indices.contains(want) else { throw Bad(what: "no layer \(want)") }
            engine.selectedMask = runs[want][0]

        case "masksplit":
            // ⚠ Sets, never toggles. A toggle verb's meaning would flip with
            // the default a new row gets, and a script has to keep meaning
            // what it said when the default moves.
            guard let i = Int(args.first ?? "") else {
                throw Bad(what: "masksplit needs a row index")
            }
            engine.setLayerBreak(true, at: i)

        case "masklink":
            guard let i = Int(args.first ?? "") else {
                throw Bad(what: "masklink needs a row index")
            }
            engine.setLayerBreak(false, at: i)

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
                         "matte": 4, "range": 5, "color": 6, "colour": 6]
            guard let k = named[args[1]] else { throw Bad(what: "no kind \(args[1])") }
            engine.setMaskKind(k, at: i)

        case "maskname":
            // Renames the mask whose run starts at the row — the card's rename
            // field, through the same Engine call it makes. The rest of the
            // line is the name, spaces and all.
            guard let i = Int(args.first ?? "") else {
                throw Bad(what: "maskname needs a row index and a name")
            }
            engine.renameMask(layerStartingAt: i, to: args.dropFirst().joined(separator: " "))

        case "maskshape":
            // The Add menu's other direction: a shape folded into the mask
            // containing the selected row, continuing its run.
            let intoNamed = ["linear": Int32(1), "radial": 2, "brush": 3,
                             "matte": 4, "range": 5, "color": 6, "colour": 6]
            guard let k = intoNamed[args.first ?? ""] else {
                throw Bad(what: "maskshape needs a kind")
            }
            guard engine.addShape(kind: k, intoLayerContaining: engine.selectedMask) else {
                throw Bad(what: "the group is full or empty")
            }
            engine.commitMaskGroupEdit("Add shape")

        case "maskmergeup":
            // The card's context menu: fold the mask starting at the row into
            // the mask above it, with the op that says why. One undoable act.
            guard args.count >= 2, let i = Int(args[0]) else {
                throw Bad(what: "maskmergeup needs a row index and an op")
            }
            let ops = ["add": Int32(0), "subtract": 1, "intersect": 2]
            guard let op = ops[args[1]] else { throw Bad(what: "no op \(args[1])") }
            engine.mergeIntoLayerAbove(at: i, compose: op)

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

        case "overlay":
            // Paint the coverage over the picture, as `Show mask` does. With
            // `shot`, this is how "is the mask where I put it" becomes
            // something to look at rather than argue about.
            switch args.first {
            case "on":  engine.maskOverlay = true
            case "off": engine.maskOverlay = false
            default: throw Bad(what: "overlay takes on or off")
            }

        case "maskcolor", "maskcolour":
            // The mask color picker, through the same call `ImageCanvas`
            // makes on a click. A scenario that set the RGB directly would be
            // testing a struct rather than the tool.
            let p = try point(args.first ?? "")
            guard engine.pickMaskColor(at: p) else {
                throw Bad(what: "no color at \(p.x),\(p.y) — is a mask row selected?")
            }
            let c = engine.maskColor
            say(String(format: "  picked color %.4f %.4f %.4f\n",
                       c.r, c.g, c.b))

        case "paint":
            // A brush stroke as the **canvas** issues it, and what one pointer
            // event of it costs.
            //
            // ⚠ This is not what `brush` measures, and the difference is the
            // whole point. `brush` hands the engine one finished stroke in a
            // single `setBrushStroke`; `MaskOverlay.paint` calls it again on
            // every pointer event, appending as the hand moves. Sixty of those a
            // second, each one re-flattening a stroke that is growing, is the
            // cost a photographer actually feels — and nothing in this
            // repository measured it until this verb existed. See ROADMAP,
            // "Slider latency, end to end".
            //
            // The walk mirrors the gesture exactly: `carry` continues the dab
            // spacing across events so a scripted stroke lays the dabs a steady
            // hand would, and the first event appends the press point alone.
            //
            // ⚠ **It does not arm the preview graph, and must not.** The real
            // gesture does, as of 2026-08-01 — but a verb that armed it itself
            // would report the same number whether `MaskOverlay` still called
            // `beginInteraction` or not, which is a measurement that cannot see
            // the thing it exists to measure. Arming is the `interact` verb's
            // job, so a scenario can time both sides.
            //
            // ⚠ What that leaves uncovered: nothing here asserts the *gesture*
            // arms. `Scenario` drives `Engine` and `CanvasLayout`, never a
            // SwiftUI view, so the one line that matters is reachable only by
            // reading it. Said plainly rather than implied to be tested.
            guard args.count >= 3, let events = Int(args[2]), events > 1 else {
                throw Bad(what: "paint needs a start point, an end point and an event count")
            }
            let a = try point(args[0]), b = try point(args[1])

            engine.beginBrushStroke()
            var laid = 0

            var carry: CGFloat = 0
            var last = a
            // ⚠ How many pointer events invalidate `maskComponents`.
            //
            // This is the number the ~155 ms report turned on. `Engine` is
            // `@Observable` and Observation is property-granular, so a write to
            // `maskComponents` invalidates every view whose body read it — and
            // `DevelopPanels` reads it in eleven places, including a `ForEach`
            // over the mask rows. One write per pointer event therefore rebuilt
            // the whole develop panel sixty times a second, and **no headless
            // instrument here could see it**, because `Scenario` never renders
            // SwiftUI. Counting the invalidations is the part that *can* be
            // measured from here; what each one costs still needs the app.
            var invalidations = 0
            let began = DispatchTime.now().uptimeNanoseconds
            quiet = true
            for i in 0..<events {
                let t = CGFloat(i) / CGFloat(events - 1)
                let here = CGPoint(x: a.x + (b.x - a.x) * t,
                                   y: a.y + (b.y - a.y) * t)
                var batch: [CGPoint] = []
                if i == 0 {
                    batch = [here]
                } else {
                    batch = CanvasLayout.brushDabs(from: last, to: here,
                                                   radius: CGFloat(engine.brushRadius),
                                                   carry: &carry)
                    // ⚠ The gesture returns early on an event that laid no dab,
                    // so this one must too. Pushing anyway would measure a
                    // cheaper tick than the app ever issues.
                    if batch.isEmpty { last = here; continue }
                }
                last = here
                var fired = false
                withObservationTracking {
                    _ = engine.maskComponents
                } onChange: {
                    fired = true
                }
                engine.appendBrushDabs(batch, erasing: engine.brushErasing)
                laid += batch.count
                if fired { invalidations += 1 }
            }
            let elapsed = DispatchTime.now().uptimeNanoseconds - began
            quiet = false
            // The one observable write, and the one history entry.
            if engine.endBrushStroke() { engine.commitBrushEdit() }

            let perEvent = Double(elapsed) / 1_000_000.0 / Double(events)
            let dabs = engine.brushStroke.count
            say(String(format: "  paint %d events, %d dabs (+%d)  %.1f ms per event  (%.0f fps)  %d/%d invalidate the panel\n",
                       events, dabs, laid, perEvent,
                       perEvent > 0 ? 1000.0 / perEvent : 0,
                       invalidations, events))

        default:
            return false
        }
        return true
    }


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
        m.center = CGPoint(x: CGFloat(engine.maskCenterX), y: CGFloat(engine.maskCenterY))
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
        // the cell rather than at its center: a cell whose center is covered
        // can still straddle the falloff.
        //
        // ⚠ The grid is display cells — the rendered output being measured —
        // and the mask is stored in frame coordinates, so every sample
        // crosses through the engine's own map first, exactly as the overlay
        // does. This is also what sharpens the check: the render's pixels
        // went display → frame through `geometry.slang`, the oracle's samples
        // go through `mask::displayMatrix`, and the two derivations agreeing
        // on every cell is now part of what a green run means.
        let fd = engine.frameDisplayMap
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
                        let a = CanvasLayout.maskAlpha(fd.frame(q), m)
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
}
