// Presets as patches, sync, and batch export.
//
// Split out of ViewportTests.swift 2026-07-31.

import CoreGraphics
import Foundation

extension ViewportTests {

    /// A photograph with something set in every group, so any field a preset
    /// wrongly copies shows up as a change.
    /// A state with **every** field set to a non-default value.
    ///
    /// ⚠ Exhaustive on purpose, and it was not at first: `maskRefine` was left
    /// at its default, so a mutation deleting its line from the decoder changed
    /// nothing and survived. A round-trip test is only as good as the state it
    /// round-trips — a field this function forgets is a field the suite cannot
    /// see. Anything added to `DevelopState` belongs here the same day.
    static func busyState() -> DevelopState {
        var s = DevelopState()
        s.temperatureK = 4200; s.tint = 0.3
        s.exposureEv = 1.1; s.contrast = 1.7
        s.highlights = -0.4; s.shadows = 0.35; s.whites = 0.2; s.blacks = -0.15
        s.highlightRecovery = 0.65
        s.vibrance = 0.4; s.saturation = -0.2
        s.hueShift[3] = 0.5; s.satShift[5] = -0.3; s.lumShift[1] = 0.25
        s.gradeShadow = [0.1, -0.2, 0.05]
        s.gradeMidtone = [-0.05, 0.15, 0.02]
        s.gradeHighlight = [0.2, 0.03, -0.12]
        s.sharpenAmount = 1.3; s.sharpenRadius = 2.2; s.sharpenMasking = 0.45
        s.denoiseLuma = 2.0; s.denoiseColor = 1.4
        s.lensDistortion = 0.4; s.lensVignette = -0.35
        s.lensCaRed = 0.22; s.lensCaBlue = -0.18
        s.clarity = 0.6; s.dehaze = 0.3; s.fusion = 0.7; s.lutStrength = 0.5

        var curve = ToneCurve()
        curve.master = [CurvePoint(x: 0, y: 0.05),
                        CurvePoint(x: 0.5, y: 0.42),
                        CurvePoint(x: 1, y: 0.97)]
        curve.red = [CurvePoint(x: 0, y: 0), CurvePoint(x: 0.6, y: 0.7),
                     CurvePoint(x: 1, y: 1)]
        s.curve = curve

        s.rotateQuarters = 1; s.straightenDeg = 4
        s.cropX = 0.1; s.cropY = 0.2; s.cropW = 0.7; s.cropH = 0.6
        // ⚠ These eight were **absent for as long as they have existed**, and
        // `testDevelopStateRoster` is what found them (decision #110). A field
        // the fixture never moves round-trips its own default through the
        // encoder and back, so `testEveryFieldSurvivesTheSidecar` was green for
        // perspective, Balance, grain and the creative vignette without ever
        // having carried one of them.
        s.perspectiveVertical = 0.45; s.perspectiveHorizontal = -0.3
        s.perspectiveAspect = 0.22
        s.gradeBalance = -0.6
        s.grainAmount = 0.018; s.grainSize = 2.4
        s.vignetteAmount = -0.9; s.vignetteFieldAngle = 62

        var spot = SpotState()
        spot.destX = 0.31; spot.destY = 0.62
        spot.srcX = 0.44; spot.srcY = 0.58
        spot.radius = 0.035; spot.feather = 0.4; spot.heal = false
        s.spots = [spot]

        // ⚠ kind 1, not the default. A kind-0 component is "off" and the
        // decoder drops it on purpose, so a default-constructed one would make
        // this state fail a round trip for a reason that is not a bug.
        var m = MaskComponentState()
        m.kind = 1; m.compose = 2; m.invert = true; m.hidden = true
        m.startsLayer = true
        m.centerX = 0.4; m.centerY = 0.7; m.angle = 0.9; m.length = 0.33
        m.radiusX = 0.21; m.radiusY = 0.44; m.feather = 0.66; m.roundness = 3.5
        m.brushRadius = 0.05; m.brushFlow = 0.8; m.brushHardness = 0.15
        m.brushStroke = [0.1, 0.2, 0.3, 0.4]; m.brushErase = [0, 1]
        // ⚠ The range and colour fields too, and their absence here is what let
        // `rangeLo`, `rangeHi` and `rangeSoft` be written to every sidecar and
        // read back from none for five sessions. `DevelopState`'s own fixture
        // was made exhaustive in 2026-07-30e; the *nested* component's was not,
        // so the guard could not see the fields it was guarding.
        m.rangeLo = -3.25; m.rangeHi = 1.75; m.rangeSoft = 0.8
        m.colorR = 0.42; m.colorG = 0.11; m.colorB = 0.27
        m.colorTol = 0.19; m.colorSoft = 0.07
        s.maskComponents = [m]
        s.maskRefine = 0.72
        // ⚠ Two layers, not one: a fixture with a single layer cannot see a
        // round trip that drops every layer after the first.
        s.layers = [LocalAdjustState(exposureEv: 1.5, contrast: 0.42,
                                     saturation: -0.33, warmth: 0.27, tint: -0.19),
                    LocalAdjustState(exposureEv: -0.8, contrast: -0.2,
                                     saturation: 0.66, warmth: -0.4, tint: 0.31)]
        return s
    }

    /// ⚠ The property the whole design rests on: a preset touches its groups
    /// and *nothing else*. Checked one group at a time, because a preset that
    /// assigned the whole state would pass any test that only enabled all of
    /// them at once.
    static func testPresetIsAPatch() {
        let base = busyState()

        // A preset carrying defaults everywhere. Applying it with one group
        // enabled must move exactly that group's fields to the default and
        // leave every other field of `base` untouched.
        for group in PresetGroup.allCases {
            let p = Preset(name: "t", groups: [group], state: DevelopState())
            let out = p.applied(to: base)

            // Pick one witness field from each *other* group and demand it
            // survived. Fields, not the whole struct, so the failure message
            // says which group leaked.
            if group != .light {
                report(out.exposureEv == base.exposureEv,
                       "\(group.rawValue) leaves Light alone", "\(out.exposureEv)")
            }
            if group != .color {
                report(out.vibrance == base.vibrance && out.hueShift == base.hueShift,
                       "\(group.rawValue) leaves Color alone", "\(out.vibrance)")
            }
            if group != .whiteBalance {
                report(out.temperatureK == base.temperatureK,
                       "\(group.rawValue) leaves White Balance alone",
                       "\(out.temperatureK)")
            }
            if group != .detail {
                report(out.sharpenAmount == base.sharpenAmount
                       && out.denoiseLuma == base.denoiseLuma,
                       "\(group.rawValue) leaves Detail alone", "\(out.sharpenAmount)")
            }
            if group != .effects {
                report(out.clarity == base.clarity && out.dehaze == base.dehaze,
                       "\(group.rawValue) leaves Effects alone", "\(out.clarity)")
            }
        }

        // And the group it *does* name is actually applied — the checks above
        // are all satisfied by a preset that does nothing whatever.
        let light = Preset(name: "t", groups: [.light], state: DevelopState())
        let out = light.applied(to: base)
        report(out.exposureEv == 0 && out.contrast == DevelopState().contrast,
               "and the group it names is applied",
               "\(out.exposureEv), \(out.contrast)")
    }

    /// ⚠ Geometry, dust and masks are never carried, under any group — not even
    /// all of them at once. A preset that reframed every photograph it touched
    /// would be unusable, and this is the check that says it cannot.
    static func testPresetNeverCarriesTheFrame() {
        let base = busyState()

        var look = DevelopState()
        look.rotateQuarters = 3
        look.straightenDeg = -9
        look.cropX = 0.4; look.cropY = 0.4; look.cropW = 0.2; look.cropH = 0.2
        look.spots = [SpotState(), SpotState()]
        look.maskComponents = [MaskComponentState(), MaskComponentState()]
        look.maskRefine = 0.9
        look.layers = [LocalAdjustState(exposureEv: -2)]

        let all = Preset(name: "everything", groups: Set(PresetGroup.allCases),
                         state: look)
        let out = all.applied(to: base)

        report(out.rotateQuarters == base.rotateQuarters
               && out.straightenDeg == base.straightenDeg,
               "no group carries the rotation or the straighten",
               "\(out.rotateQuarters), \(out.straightenDeg)")
        report(out.cropX == base.cropX && out.cropW == base.cropW,
               "nor the crop", "\(out.cropX), \(out.cropW)")
        report(out.spots == base.spots, "nor the dust spots",
               "\(out.spots.count) vs \(base.spots.count)")
        report(out.maskComponents == base.maskComponents
               && out.maskRefine == base.maskRefine
               && out.layers == base.layers,
               "nor the masks and their local adjustment",
               "\(out.maskComponents.count) vs \(base.maskComponents.count)")

        // Applying a preset twice is the same as applying it once — it is a
        // patch, so it has to be idempotent or a double click would compound.
        report(all.applied(to: out) == out, "and applying it twice changes nothing")
    }

    static func testPresetStoreRoundTrip() {
        let dir = FileManager.default.temporaryDirectory
            .appendingPathComponent("orion-presets-\(UUID().uuidString)")
        try? FileManager.default.createDirectory(at: dir, withIntermediateDirectories: true)
        let file = dir.appendingPathComponent("presets.json")
        defer { try? FileManager.default.removeItem(at: dir) }

        let store = PresetStore(url: file)
        let builtInCount = store.presets.count
        report(builtInCount > 0, "the built-in looks are present on a fresh install")

        var s = DevelopState()
        s.contrast = 1.9
        report(store.add(name: "Mine", groups: [.light], state: s),
               "a preset can be saved")

        // Saving the same name twice replaces rather than appends.
        s.contrast = 1.3
        store.add(name: "Mine", groups: [.light], state: s)
        report(store.presets.filter { $0.name == "Mine" }.count == 1,
               "and saving it again replaces it rather than piling up",
               "\(store.presets.filter { $0.name == "Mine" }.count)")

        let reopened = PresetStore(url: file)
        let mine = reopened.presets.first { $0.name == "Mine" }
        report(mine?.state.contrast == 1.3 && mine?.groups == [.light],
               "and it survives a reopen with its groups",
               "\(String(describing: mine?.state.contrast))")

        // ⚠ Built-ins are not written to disk, so improving one in a later
        // release reaches everybody rather than only new installs.
        let raw = (try? String(contentsOf: file, encoding: .utf8)) ?? ""
        report(!raw.contains("Monochrome"),
               "built-ins are not copied into the user's file")
        report(reopened.presets.count == builtInCount + 1,
               "and are not duplicated on reload",
               "\(reopened.presets.count) vs \(builtInCount + 1)")

        // A built-in cannot be deleted.
        if let builtIn = reopened.presets.first(where: { $0.builtIn }) {
            reopened.remove(builtIn)
            report(reopened.presets.contains(where: { $0.id == builtIn.id }),
                   "a built-in cannot be removed")
        }

        // An empty name or no groups is refused rather than saved as junk.
        report(!reopened.add(name: "   ", groups: [.light], state: s),
               "an empty name is refused")
        report(!reopened.add(name: "Nothing", groups: [], state: s),
               "and so is a preset that would change nothing")
    }

    /// Decision #112: `presets.json` is the file where one bad string used to
    /// cost the lot.
    ///
    /// ⚠ `PresetStore.load` was `try? JSONDecoder().decode([Preset].self)`,
    /// which is all or nothing — the array throws on element one and the
    /// photographer opens Orion to the four built-ins with every saved look
    /// gone and nothing written anywhere to say why. That was a defect before
    /// this story and independent of it; the group rename is only what would
    /// finally have fired it, on every installation at once, on first launch.
    ///
    /// Three things are pinned here: the old `"colour"` still reads, an
    /// unreadable preset costs that preset alone, and what the store writes back
    /// is the new spelling.
    static func testPresetFileSurvivesOneBadPreset() {
        let dir = FileManager.default.temporaryDirectory
            .appendingPathComponent("orion-presets-mixed-\(UUID().uuidString)")
        try? FileManager.default.createDirectory(at: dir, withIntermediateDirectories: true)
        let file = dir.appendingPathComponent("presets.json")
        defer { try? FileManager.default.removeItem(at: dir) }

        // Four presets: one written before the rename, one after, one whose
        // group is a string no build has ever emitted, and one plain. The bad
        // one sits in the *middle*, so "everything after it was skipped" is a
        // different failure from "everything was lost".
        let json = #"""
        [
         {"id":"C9DDB3F0-0000-0000-0000-000000000001","name":"Before",
          "groups":["colour","light"],"state":{"contrast":1.71},"builtIn":false},
         {"id":"C9DDB3F0-0000-0000-0000-000000000002","name":"After",
          "groups":["color"],"state":{"contrast":1.42},"builtIn":false},
         {"id":"C9DDB3F0-0000-0000-0000-000000000003","name":"Broken",
          "groups":["chartreuse"],"state":{"contrast":1.11},"builtIn":false},
         {"id":"C9DDB3F0-0000-0000-0000-000000000004","name":"Last",
          "groups":["effects"],"state":{"contrast":1.05},"builtIn":false}
        ]
        """#
        try? json.data(using: .utf8)?.write(to: file)

        let store = PresetStore(url: file)
        let user = store.presets.filter { !$0.builtIn }
        let names = user.map(\.name)

        report(names.contains("Before"),
               "a preset saved with the old \"colour\" group still loads",
               names.joined(separator: ", "))
        report(user.first { $0.name == "Before" }?.groups == [.color, .light],
               "and \"colour\" is the same group \"color\" is",
               "\(String(describing: user.first { $0.name == "Before" }?.groups))")
        report(names.contains("After"), "a preset saved after the rename loads")
        report(names.contains("Last"),
               "⚠ and a preset *after* the unreadable one still loads — "
                   + "the decode is element by element, not all or nothing",
               names.joined(separator: ", "))
        report(!names.contains("Broken"),
               "the one group nothing can interpret costs that preset and no other")
        report(user.count == 3, "three of the four survive", "\(user.count)")

        // ⚠ Only the preset is dropped, never a group inside one. Silently
        // narrowing what a preset touches turns "this look changed nothing"
        // into a mystery with no missing row to point at.
        report(user.allSatisfy { !$0.groups.isEmpty },
               "and no survivor came back with a group quietly removed")

        // What goes back to disk is the American spelling, and re-reading it
        // gives the same thing — the migration completes on the next save and
        // no file is ever rewritten just to migrate it.
        store.add(name: "Fresh", groups: [.color], state: DevelopState())
        let raw = (try? String(contentsOf: file, encoding: .utf8)) ?? ""
        report(raw.contains("\"color\"") && !raw.contains("\"colour\""),
               "the store writes the American spelling and stops writing the old one",
               raw.contains("\"colour\"") ? "colour survives in the file" : "")
        let reopened = PresetStore(url: file).presets.filter { !$0.builtIn }
        report(reopened.count == 4, "and the rewritten file reads back whole",
               "\(reopened.count)")
        report(reopened.first { $0.name == "Before" }?.groups == [.color, .light],
               "with the migrated preset's groups unchanged")
    }

    // MARK: Copy, paste and sync

    /// ⚠ The check this pair of files exists to have.
    ///
    /// `Preset.applied(to:)` patches a *struct*; `SyncSettings.keys(for:)`
    /// patches its *JSON*. They are the same decision written twice — once
    /// against fields, once against key names — because sync must not decode a
    /// sidecar into a struct (see the note in SyncSettings). Two hand-written
    /// lists drift the first time someone adds a field to one of them.
    ///
    /// So: apply a group both ways to the same state and demand the results
    /// agree, field for field, for every group.
    static func testSyncKeysMatchTheStructPatch() {
        let base = busyState()
        var source = DevelopState()
        // Something different in every field a group can carry, so a key
        // missing from the list shows up as a field that did not move.
        source.temperatureK = 7100; source.tint = -0.44
        source.exposureEv = -0.9; source.contrast = 1.11; source.highlights = 0.6
        source.shadows = -0.5; source.whites = -0.3; source.blacks = 0.22
        source.highlightRecovery = 0.7
        source.vibrance = -0.6; source.saturation = 0.8
        source.hueShift[2] = -0.7; source.satShift[0] = 0.9; source.lumShift[7] = -0.4
        source.gradeShadow = [-0.3, 0.2, -0.1]
        source.gradeMidtone = [0.05, 0.05, 0.05]
        source.gradeHighlight = [0.2, -0.2, 0.1]
        source.curve = ToneCurve()
        source.sharpenAmount = 1.9; source.sharpenRadius = 2.4; source.sharpenMasking = 0.8
        source.denoiseLuma = 3.1; source.denoiseColor = 0.9
        source.lensDistortion = -0.6; source.lensVignette = 0.5
        source.lensCaRed = 0.3; source.lensCaBlue = -0.3
        source.clarity = -0.8; source.dehaze = 0.9; source.fusion = 0.4
        source.lutStrength = 0.25

        for group in PresetGroup.allCases {
            let viaStruct = SyncSettings.pasted(source: source, onto: base,
                                                groups: [group])

            // The same patch through the JSON path, then decoded back so the
            // two can be compared as states.
            guard let storedBase = try? JSONEncoder().encode(base),
                  let patched = SyncSettings.patched(stored: storedBase,
                                                     source: source,
                                                     groups: [group]),
                  let viaJson = try? JSONDecoder().decode(DevelopState.self,
                                                          from: patched)
            else {
                report(false, "\(group.rawValue) round-trips through JSON")
                continue
            }

            report(viaJson == viaStruct,
                   "\(group.rawValue): the JSON key list and the struct patch agree")
        }
    }

    /// ⚠ A photograph with no sidecar must keep its as-shot white balance.
    ///
    /// Its white balance is whatever the camera recorded and is only known once
    /// the file is decoded, so it is *absent* from storage rather than stored.
    /// Decode the sidecar into a `DevelopState` and the missing keys come back
    /// as the struct's defaults — 5500 K — and writing that back would
    /// rewhite-balance every untouched photograph in a selection to a number
    /// nobody chose. This is why sync patches keys and not structs.
    static func testSyncLeavesUnknownWhiteBalanceAlone() {
        var source = DevelopState()
        source.temperatureK = 8000
        source.tint = 0.5
        source.clarity = 0.75

        // No sidecar at all, and a paste that does not include White Balance.
        guard let out = SyncSettings.patched(stored: nil, source: source,
                                             groups: [.effects]),
              let obj = try? JSONSerialization.jsonObject(with: out) as? [String: Any]
        else {
            report(false, "a patch onto a photo with no sidecar produces JSON")
            return
        }

        report(obj["temperatureK"] == nil && obj["tint"] == nil,
               "no white balance is written to a photo that never had one",
               "keys: \(obj.keys.sorted().joined(separator: ", "))")
        report((obj["clarity"] as? Double).map { abs($0 - 0.75) < 1e-6 } ?? false,
               "and the group that was pasted is written",
               "\(String(describing: obj["clarity"]))")

        // With White Balance selected it *is* written — the photographer asked.
        guard let withWb = SyncSettings.patched(stored: nil, source: source,
                                                groups: [.effects, .whiteBalance]),
              let wbObj = try? JSONSerialization.jsonObject(with: withWb)
                as? [String: Any]
        else {
            report(false, "a white-balance patch produces JSON")
            return
        }
        report((wbObj["temperatureK"] as? Double).map { abs($0 - 8000) < 1e-6 } ?? false,
               "but it is written when the paste asks for it",
               "\(String(describing: wbObj["temperatureK"]))")
    }

    /// A target that already has settings keeps the ones the paste does not
    /// name — the same patch property as a preset, at the storage layer.
    static func testSyncPatchesOnlyItsGroups() {
        var target = DevelopState()
        target.exposureEv = 2.2
        target.clarity = -0.5
        target.cropX = 0.3; target.cropW = 0.4
        target.spots = [SpotState()]

        var source = DevelopState()
        source.exposureEv = -1.0
        source.clarity = 0.9
        source.cropX = 0.9; source.cropW = 0.05

        guard let stored = try? JSONEncoder().encode(target),
              let patched = SyncSettings.patched(stored: stored, source: source,
                                                 groups: [.effects]),
              let out = try? JSONDecoder().decode(DevelopState.self, from: patched)
        else {
            report(false, "the patch round-trips")
            return
        }

        report(out.clarity == source.clarity, "sync writes the group it names",
               "\(out.clarity)")
        report(out.exposureEv == target.exposureEv,
               "and leaves the groups it does not", "\(out.exposureEv)")

        // ⚠ And never the frame or the dust, whatever is selected.
        guard let everything = SyncSettings.patched(
                stored: stored, source: source,
                groups: Set(PresetGroup.allCases)),
              let all = try? JSONDecoder().decode(DevelopState.self, from: everything)
        else {
            report(false, "an all-groups patch round-trips")
            return
        }
        report(all.cropX == target.cropX && all.cropW == target.cropW,
               "no group syncs the crop", "\(all.cropX), \(all.cropW)")
        report(all.spots == target.spots, "nor the dust spots",
               "\(all.spots.count)")

        // The count sync reports is the count it wrote.
        var wrote: [URL: Data] = [:]
        let urls = (0..<3).map { URL(fileURLWithPath: "/tmp/orion-sync-\($0).ARW") }
        let n = SyncSettings.sync(source: source, groups: [.light], to: urls,
                                  read: { _ in nil },
                                  write: { url, data in wrote[url] = data })
        report(n == 3 && wrote.count == 3, "sync reports what it wrote", "\(n)")
    }

    /// ⚠ Every field of a fully-set state survives a sidecar round trip.
    ///
    /// This exists because two did not. `DevelopState` synthesises its
    /// *encoder* from the stored properties and hand-writes its *decoder*
    /// against a `Key` list — so a field added to the struct is written to
    /// every sidecar and never read back. `spots` and `maskRefine` were both
    /// in that state for two sessions: dust removal and guided feathering were
    /// saved faithfully and silently gone on reopen.
    ///
    /// The general check is the point. Testing the two that were broken would
    /// pin today's bug; this pins the shape of it, and the next field to be
    /// added fails here rather than in someone's photographs.
    static func testEveryFieldSurvivesTheSidecar() {
        let original = busyState()
        guard let data = try? JSONEncoder().encode(original),
              let back = try? JSONDecoder().decode(DevelopState.self, from: data)
        else {
            report(false, "a busy state encodes and decodes")
            return
        }

        report(back == original,
               "every field of a fully-set state survives the sidecar")

        // Named separately so a failure says which one, rather than only that
        // two structs differ.
        report(back.spots == original.spots, "the dust spots survive",
               "\(back.spots.count) of \(original.spots.count)")
        report(back.maskRefine == original.maskRefine,
               "the mask refinement survives",
               "\(back.maskRefine) vs \(original.maskRefine)")
        report(back.maskComponents == original.maskComponents,
               "the mask group survives",
               "\(back.maskComponents.count) of \(original.maskComponents.count)")
    }

    // MARK: Batch export

    /// ⚠ Nothing is overwritten, and two sources never collide.
    ///
    /// Export is the one operation here that writes files a photographer may
    /// already have, and a batch is where both ways of losing one live: a
    /// target already on disk, and two sources from different folders sharing a
    /// basename.
    static func testBatchNeverOverwrites() {
        let out = URL(fileURLWithPath: "/out")

        // Two different folders, same basename. Nothing on disk yet.
        let sources = [URL(fileURLWithPath: "/a/IMG_0001.ARW"),
                       URL(fileURLWithPath: "/b/IMG_0001.ARW"),
                       URL(fileURLWithPath: "/c/IMG_0002.ARW")]
        let jobs = BatchExport.plan(sources: sources, into: out, extension: "jpg",
                                    exists: { _ in false })

        report(jobs.count == 3, "every source gets a job", "\(jobs.count)")
        report(jobs[0].destination.lastPathComponent == "IMG_0001.jpg",
               "the first keeps its name", jobs[0].destination.lastPathComponent)
        report(jobs[1].destination.lastPathComponent == "IMG_0001-2.jpg",
               "the second is numbered rather than overwriting the first",
               jobs[1].destination.lastPathComponent)
        report(jobs[2].destination.lastPathComponent == "IMG_0002.jpg",
               "and an unrelated name is untouched",
               jobs[2].destination.lastPathComponent)

        // Every destination distinct — the property the numbering exists for,
        // checked directly rather than inferred from the three names above.
        report(Set(jobs.map(\.destination)).count == jobs.count,
               "no two jobs share a destination")

        // Now with something already on disk.
        let onDisk: Set<String> = ["/out/IMG_0001.jpg", "/out/IMG_0001-2.jpg"]
        let jobs2 = BatchExport.plan(sources: [sources[0]], into: out,
                                     extension: "jpg",
                                     exists: { onDisk.contains($0.path) })
        report(jobs2[0].destination.lastPathComponent == "IMG_0001-3.jpg",
               "an existing file is stepped over, not written through",
               jobs2[0].destination.lastPathComponent)

        // ⚠ And the two rules compose: one source collides with disk, the next
        // with the first source's *new* name.
        let jobs3 = BatchExport.plan(sources: [sources[0], sources[1]], into: out,
                                     extension: "jpg",
                                     exists: { $0.path == "/out/IMG_0001.jpg" })
        report(jobs3[0].destination.lastPathComponent == "IMG_0001-2.jpg"
               && jobs3[1].destination.lastPathComponent == "IMG_0001-3.jpg",
               "the in-batch and on-disk rules compose",
               jobs3.map(\.destination.lastPathComponent).joined(separator: ", "))
    }

    /// ⚠ One bad file does not abandon the rest, and canceling stops promptly.
    ///
    /// A folder is likely to contain something the decoder cannot read, and
    /// losing forty good photographs to the eleventh being a stray PNG is not
    /// what anybody wants.
    static func testBatchKeepsGoingAfterAFailure() {
        struct Boom: LocalizedError { var errorDescription: String? { "no" } }

        let jobs = (1...5).map {
            BatchExport.Job(source: URL(fileURLWithPath: "/in/\($0).ARW"),
                            destination: URL(fileURLWithPath: "/out/\($0).jpg"))
        }

        var opened: [String] = []
        let outcome = BatchExport.run(
            jobs,
            openAndRestore: { url in
                opened.append(url.lastPathComponent)
                if url.lastPathComponent == "3.ARW" { throw Boom() }
            },
            exportTo: { _ in })

        report(outcome.written.count == 4 && outcome.failed.count == 1,
               "a failure is collected and the batch continues",
               "\(outcome.written.count) written, \(outcome.failed.count) failed")
        report(opened.count == 5, "every source was still attempted",
               "\(opened.count)")
        report(outcome.failed.first?.0.lastPathComponent == "3.ARW",
               "and the one that failed is named",
               outcome.failed.first?.0.lastPathComponent ?? "none")
        report(outcome.summary.contains("4") && outcome.summary.contains("1 failed"),
               "the summary says both numbers", outcome.summary)

        // Canceling: stops before the next photograph, and says it stopped.
        var done = 0
        let stopped = BatchExport.run(
            jobs,
            openAndRestore: { _ in done += 1 },
            exportTo: { _ in },
            isCanceled: { done >= 2 })
        report(stopped.written.count == 2 && stopped.canceled,
               "canceling stops the batch and is reported",
               "\(stopped.written.count) written, canceled \(stopped.canceled)")
        report(stopped.summary.contains("stopped early"),
               "and the summary says so", stopped.summary)

        // Progress is reported once per photograph plus a final call, so a bar
        // reaches its end rather than stopping one short.
        var seen: [Int] = []
        _ = BatchExport.run(jobs, openAndRestore: { _ in }, exportTo: { _ in },
                            progress: { i, _ in seen.append(i) })
        report(seen.first == 0 && seen.last == jobs.count,
               "progress starts at zero and reaches the total",
               "\(seen)")
    }
}
