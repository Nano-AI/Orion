import AppKit
import SwiftUI

/// The photograph itself: opening it, its geometry, its history and the files
/// beside it.
///
/// ⚠ **Adding a verb is one edit, in the switch below.** The dispatcher in
/// `Scenario.swift` asks each family in turn whether it took the verb and never
/// needs to know the answer in advance — see the note there.

extension Scenario {

    /// Answers true when this family took the verb.
    static func frameStep(_ verb: String, _ args: [String],
                          engine: Engine) throws -> Bool {
        switch verb {
        case "open":
            guard let p = args.first else { throw Bad(what: "open needs a path") }
            photo = URL(fileURLWithPath: p)
            try engine.open(path: p)
            snapshots.open(photo: photo)

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
            // ⚠ The parse's answer, exactly as `Editor.load` reads it, and
            // through the same classifier. Handing `engine.maskComponents` to
            // the sweep after a *failed* decode deletes every matte beside the
            // photograph — see `MatteStore.SidecarState`.
            let restored = engine.restore(encoded: saved)
            if restored { engine.restoreMattes(photo: p) }
            snapshots.open(photo: p)
            MatteStore.sweepAfterLoad(photo: p, blob: saved, restored: restored,
                                      components: engine.maskComponents)
            guard restored else {
                throw Bad(what: "the sidecar's develop state would not decode")
            }

        case "rotate":
            engine.rotate(Int32(try number(args, 0)))

        case "straighten":
            engine.straightenDeg = Float(try number(args, 0))

        case "crop":
            engine.setCrop(x: Float(try number(args, 0)), y: Float(try number(args, 1)),
                           w: Float(try number(args, 2)), h: Float(try number(args, 3)))
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

        case "snapshot":
            // Saved versions, through the same `SnapshotStore` the panel holds.
            //
            // ⚠ `restore` goes through `SnapshotStore.restore` rather than
            // calling `Engine.restore(snapshot:)` directly, because the working
            // edit being kept first is part of what a restore *is* — a runner
            // that skipped it would be exercising the half that cannot lose
            // anybody's work, which is the same gap the `crop` verb had when it
            // skipped `commitCropEdit`.
            guard let sub = args.first else {
                throw Bad(what: "snapshot takes save, restore, rename, delete, "
                              + "count or missing")
            }
            guard let p = photo else { throw Bad(what: "snapshot needs an open photo") }

            // By prefix, as the `preset` verb matches: a scenario splits on
            // spaces and the automatic version's name has two of them.
            func named(_ i: Int) throws -> Snapshot {
                guard i < args.count else { throw Bad(what: "snapshot \(sub) needs a name") }
                guard let s = snapshots.snapshots.first(where: {
                    $0.name.lowercased().hasPrefix(args[i].lowercased())
                }) else {
                    throw Bad(what: "no version named \(args[i])")
                }
                return s
            }

            switch sub {
            case "save":
                guard args.count >= 2 else { throw Bad(what: "snapshot save needs a name") }
                let s = try snapshots.save(name: args[1], state: engine.state)
                say("  saved version \(s.name)\n")

            case "restore":
                let s = try named(1)
                snapshots.restore(s, working: engine.state) {
                    engine.restore(snapshot: $0, photo: p)
                }
                let gone = SnapshotStore.missingMattes(s, photo: p)
                say("  restored version \(s.name)"
                  + (gone.isEmpty ? "\n" : " — \(gone.count) missing: "
                                          + gone.joined(separator: ", ") + "\n"))

            case "rename":
                let s = try named(1)
                guard args.count >= 3 else { throw Bad(what: "snapshot rename needs a new name") }
                try snapshots.rename(s.id, to: args[2])

            case "delete":
                try snapshots.remove(try named(1).id)

            case "clear":
                // ⚠ Scenario hygiene, and it is not optional. A version file
                // lives beside the photograph and outlives the run, so a
                // scenario that asserts a *count* without this passes once and
                // fails on the second run — a check whose answer depends on
                // what was run before it is not a check.
                try? FileManager.default.removeItem(at: SnapshotStore.url(photo: p))
                snapshots.open(photo: p)

            case "count":
                let want = Int(try number(args, 1))
                let got = snapshots.snapshots.count
                checks += 1
                if got == want {
                    say("  ok    \(got) versions\n")
                } else {
                    failures += 1
                    say("  FAIL  \(got) versions, wanted \(want) — "
                      + snapshots.snapshots.map(\.name).joined(separator: ", ") + "\n")
                }

            case "missing":
                let s = try named(1)
                let want = Int(try number(args, 2))
                let got = SnapshotStore.missingMattes(s, photo: p).count
                checks += 1
                if got == want {
                    say("  ok    version \(s.name) is missing \(got) selections\n")
                } else {
                    failures += 1
                    say("  FAIL  version \(s.name) is missing \(got) selections, "
                      + "wanted \(want)\n")
                }

            default:
                throw Bad(what: "snapshot takes save, restore, rename, delete, "
                              + "count or missing, got \(sub)")
            }

        case "save":
            // Writes the current state to the photo's sidecar, which is what
            // autosave does when the edits settle. Here so a scenario can set a
            // photograph up for something that reads sidecars rather than the
            // engine — batch export, and sync.
            guard let p = args.first else { throw Bad(what: "save needs a path") }
            guard let encoded = try? JSONEncoder().encode(engine.state) else {
                throw Bad(what: "could not encode the state")
            }
            guard Sidecar.merge(into: URL(fileURLWithPath: p),
                                { $0.develop = encoded }) else {
                throw Bad(what: "the sidecar refused the write")
            }

        case "corrupt":
            // **Truncates** the sidecar's develop blob — what a half-written
            // file, a bad card or a sync tool that rewrote the XMP leaves
            // behind. Half a JSON object is not a JSON object, so the decoder
            // throws and `Engine.restore` answers false.
            //
            // ⚠ Truncation rather than substitution, and the difference is the
            // interesting part. `DevelopState.init(from:)` is **total** by
            // design (see its comment): every field is `decodeIfPresent`, so
            // *any* well-formed JSON object decodes to a state, keys it has
            // never seen and all. A sidecar from a newer build therefore stays
            // readable, which is the point — and it also means a substituted
            // `{"nope":1}` would decode happily and this branch would never be
            // reached. The reachable failure is bytes that are not JSON.
            //
            // ⚠ This verb is the only way the loader's *unreadable* branch is
            // reachable headlessly. Without it, the branch that decides whether
            // to delete every matte beside a photograph could only be reasoned
            // about.
            guard let p = args.first else { throw Bad(what: "corrupt needs a path") }
            let target = URL(fileURLWithPath: p)
            guard let good = Sidecar.read(for: target)?.develop, good.count > 8 else {
                throw Bad(what: "no develop state in \(p) to corrupt")
            }
            stashedDevelop = good
            let half = good.prefix(good.count / 2)
            guard Sidecar.merge(into: target, { $0.develop = Data(half) }) else {
                throw Bad(what: "the sidecar refused the write")
            }

        case "repair":
            // Puts back the blob `corrupt` replaced, byte for byte.
            guard let p = args.first else { throw Bad(what: "repair needs a path") }
            guard let good = stashedDevelop else {
                throw Bad(what: "nothing was corrupted, so there is nothing to repair")
            }
            guard Sidecar.merge(into: URL(fileURLWithPath: p),
                                { $0.develop = good }) else {
                throw Bad(what: "the sidecar refused the write")
            }

        case "nofailure":
            // ⚠ A successful render must CLEAR the failure, and until 2026-08-02
            // nothing anywhere checked that it did.
            //
            // `reopenrefused` asserts the failure is *set*, and decision #115's
            // whole point was that a silent failure is worse than a loud one.
            // But the other half was unpinned: #117's split deleted both
            // `lastFailure = nil` on the success paths — so the status line
            // would have shown a stale "Render failed" forever, on a photograph
            // that renders perfectly — and 27 byte-compared frames, 40
            // scenarios, 800 engine checks, 3702 viewport checks and the bench
            // all stayed green. A stuck error message is how a working editor
            // convinces a photographer it is broken.
            guard engine.lastFailure == nil else {
                throw Bad(what: "a successful render left the failure set: "
                                + (engine.lastFailure ?? ""))
            }
            say("  no failure outstanding\n")

        case "notflat", "flat":
            // The other half of `nofailure`: a render that *succeeds* and
            // produces one flat colour. See `Engine.flatFrame` — this is the
            // shape of the failure a photographer screenshotted twice and no
            // check in this repository was in a position to see.
            //
            // ⚠ Both spellings, because a probe that can only pass is the
            // defect this project keeps finding. `flat` exists so the detector
            // itself is pinned against a frame that really is one colour, and
            // without it `notflat` would be green on a probe that had been
            // deleted.
            checks += 1
            let want = (verb == "flat")
            let isFlat = engine.flatFrame != nil
            guard isFlat == want else {
                failures += 1
                say(want ? "  FAIL  expected a flat frame, the render has detail\n"
                         : "  FAIL  \(engine.flatFrame ?? "")\n")
                break
            }
            say(want ? "  ok    flat: \(engine.flatFrame ?? "")\n"
                     : "  ok    the frame has detail\n")

        case "reopenrefused":
            // `reopen`, but the sidecar is expected NOT to decode — the
            // photograph is left openable and nothing is collected.
            //
            // ⚠ Every assertion here was green before 2026-08-02 on code that
            // deleted the matte, because `Engine.restore` had no way to say it
            // had failed and `sweepAfterLoad` was handed the *default* empty
            // component list. Decision #115.
            guard let p = photo else { throw Bad(what: "reopenrefused needs an open photo") }
            let before = engine.maskComponents.compactMap(\.matteId)
            try engine.open(path: p.path)
            guard let blob = Sidecar.read(for: p)?.develop else {
                throw Bad(what: "no develop state in the sidecar")
            }
            let took = engine.restore(encoded: blob)
            if took { throw Bad(what: "a corrupt develop blob decoded") }
            guard engine.lastFailure != nil else {
                throw Bad(what: "a refused restore said nothing")
            }
            snapshots.open(photo: p)
            MatteStore.sweepAfterLoad(photo: p, blob: blob, restored: took,
                                      components: engine.maskComponents)
            // The files the previous state named must all still be there.
            for id in before {
                let f = MatteStore.url(photo: p, id: id)
                guard FileManager.default.fileExists(atPath: f.path) else {
                    throw Bad(what: "matte \(id) was collected on an unreadable sidecar")
                }
            }
            say("  \(before.count) matte(s) kept, restore refused: "
                + "\(engine.lastFailure ?? "")\n")

        case "auto":
            engine.autoEnhance()

        case "compare":
            engine.setCompare(split: try number(args, 0))

        case "undo":
            engine.undo()

        case "redo":
            engine.redo()

        default:
            return false
        }
        return true
    }
}
