// The `--agent` surface's pure logic, checked without a GPU: argument
// parsing, the edit merge and the histogram arithmetic. `AgentCLIDriver.swift`
// is what actually opens a photograph and is exercised by hand
// (docs/superpowers/plans/2026-09-13-agent-mcp.md, Task 1 Step 7) since
// nothing here can open a raw file.

import Foundation

extension ViewportTests {

    static func testAgentParsesProxy() {
        do {
            let command = try AgentCLI.parse(
                ["--agent", "proxy", "/a.arw", "--max", "512", "--out", "/o.jpg"])
            report(command == .proxy(raw: "/a.arw", max: 512, out: "/o.jpg", state: nil),
                   "parses proxy with --max and --out", "\(command)")
        } catch {
            report(false, "parse should not throw on a well-formed proxy command", "\(error)")
        }
    }

    /// An option a verb does not consume — `--sate` for `--state` — is a
    /// usage error naming the flag, not silently parsed and dropped.
    static func testAgentRejectsAnOptionTheVerbDoesNotTake() {
        do {
            _ = try AgentCLI.parse(
                ["--agent", "proxy", "/a.arw", "--max", "512", "--out", "/o.jpg",
                 "--sate", "/s.json"])
            report(false, "parse should reject an option proxy does not take")
        } catch AgentCLI.Failure.usage(let message) {
            report(message.contains("sate"), "names the bad flag", message)
        } catch {
            report(false, "wrong error for an unconsumed option", "\(error)")
        }
    }

    /// `apply`'s merge: a known scalar key lands and is reported changed,
    /// and nothing else in the state moves; an invented key is a usage error
    /// naming the key, not a silent drop.
    static func testAgentMergeEdits() {
        guard let base = try? JSONEncoder().encode(DevelopState()) else {
            report(false, "DevelopState() should encode")
            return
        }

        do {
            let edits = Data(#"{"exposureEv": 1.0}"#.utf8)
            let merged = try AgentCLI.mergeEdits(base: base, edits: edits)
            let state = try JSONDecoder().decode(DevelopState.self, from: merged.state)
            report(state.exposureEv == 1.0, "exposureEv lands in the merged state",
                   "got \(state.exposureEv)")
            report(merged.changed == ["exposureEv"], "changed names exposureEv",
                   "got \(merged.changed)")

            // Every other top-level value is untouched: re-encode both sides
            // with sortedKeys, drop exposureEv from each, and compare bytes.
            guard var baseObject = try JSONSerialization.jsonObject(with: base) as? [String: Any],
                  var mergedObject = try JSONSerialization.jsonObject(with: merged.state)
                      as? [String: Any] else {
                report(false, "base and merged should both decode as JSON objects")
                return
            }
            baseObject["exposureEv"] = nil
            mergedObject["exposureEv"] = nil
            let baseBytes = try JSONSerialization.data(withJSONObject: baseObject,
                                                        options: [.sortedKeys])
            let mergedBytes = try JSONSerialization.data(withJSONObject: mergedObject,
                                                          options: [.sortedKeys])
            report(baseBytes == mergedBytes, "no field other than exposureEv changed")
        } catch {
            report(false, "mergeEdits should not throw on a real DevelopState key", "\(error)")
        }

        do {
            let badEdits = Data(#"{"exposure": 1.0}"#.utf8)
            _ = try AgentCLI.mergeEdits(base: base, edits: badEdits)
            report(false, "mergeEdits should reject a key DevelopState does not have")
        } catch AgentCLI.Failure.unknownKeys(let keys) {
            report(keys == ["exposure"], "names the invented key", "got \(keys)")
        } catch {
            report(false, "wrong error for an unknown key", "\(error)")
        }
    }

    /// Composite fields REPLACE the whole value — a well-formed one lands.
    /// `gradeShadow` is the simplest composite: an in-range [x, y, luminance]
    /// triple is accepted and lands exactly.
    static func testAgentMergeEditsAcceptsAWellFormedGradeWheel() {
        guard let base = try? JSONEncoder().encode(DevelopState()) else {
            report(false, "DevelopState() should encode")
            return
        }
        do {
            let edits = Data(#"{"gradeShadow": [0.2, -0.1, 0.3]}"#.utf8)
            let merged = try AgentCLI.mergeEdits(base: base, edits: edits)
            let state = try JSONDecoder().decode(DevelopState.self, from: merged.state)
            report(state.gradeShadow == [0.2, -0.1, 0.3], "gradeShadow lands exactly",
                   "got \(state.gradeShadow)")
        } catch {
            report(false, "mergeEdits should accept a well-formed gradeShadow", "\(error)")
        }
    }

    /// A grade wheel outside the unit disc (x²+y² > 1) is rejected —
    /// ColorWheel.swift:165-166 clamps the puck to the disc, so a wheel this
    /// far out could never come from the panel's own drag.
    static func testAgentMergeEditsRejectsAGradeWheelOutsideTheUnitDisc() {
        guard let base = try? JSONEncoder().encode(DevelopState()) else {
            report(false, "DevelopState() should encode")
            return
        }
        do {
            let edits = Data(#"{"gradeMidtone": [0.9, 0.9, 0]}"#.utf8)
            _ = try AgentCLI.mergeEdits(base: base, edits: edits)
            report(false, "mergeEdits should reject a grade wheel outside the unit disc")
        } catch AgentCLI.Failure.usage(let message) {
            report(message.contains("unit disc"), "names the unit disc rule", message)
        } catch {
            report(false, "wrong error for an out-of-disc grade wheel", "\(error)")
        }
    }

    /// The bug class this whole file exists to close: a partial `layers`
    /// element used to either silently zero its siblings or (in the old POC)
    /// be refused with a message that did not say why. Now it is refused
    /// naming exactly the missing field and the element's index.
    static func testAgentMergeEditsRejectsAPartialLayer() {
        guard let base = try? JSONEncoder().encode(DevelopState()) else {
            report(false, "DevelopState() should encode")
            return
        }
        do {
            let edits = Data(#"{"layers": [{"exposureEv": 1.0}]}"#.utf8)
            _ = try AgentCLI.mergeEdits(base: base, edits: edits)
            report(false, "mergeEdits should reject a partial layer")
        } catch AgentCLI.Failure.usage(let message) {
            report(message == "layers[0] is missing 'blacks'",
                   "names the missing field and the element index", message)
        } catch {
            report(false, "wrong error for a partial layer", "\(error)")
        }
    }

    /// A complete, in-range layer is accepted and lands exactly at its index
    /// — the REPLACE semantics: `state.layers[0]` equals what was sent, not a
    /// merge of it into whatever layer 1 held before.
    static func testAgentMergeEditsAcceptsACompleteLayer() {
        guard let base = try? JSONEncoder().encode(DevelopState()) else {
            report(false, "DevelopState() should encode")
            return
        }
        var layer = LocalAdjustState()
        layer.exposureEv = 1.5
        layer.contrast = -0.5
        layer.saturation = 0.2
        layer.warmth = 0.1
        layer.tint = -0.2
        layer.highlights = -0.3
        layer.shadows = 0.4
        layer.whites = 0.1
        layer.blacks = -0.1
        guard let layerData = try? JSONEncoder().encode([layer]),
              let layerJSON = String(data: layerData, encoding: .utf8) else {
            report(false, "the fixture layer should encode")
            return
        }
        do {
            let edits = Data(#"{"layers": \#(layerJSON)}"#.utf8)
            let merged = try AgentCLI.mergeEdits(base: base, edits: edits)
            let state = try JSONDecoder().decode(DevelopState.self, from: merged.state)
            report(state.layers.count == 1, "layers has exactly the one sent",
                   "got \(state.layers.count)")
            report(state.layers.first == layer, "state.layers[0] equals the sent layer",
                   "got \(String(describing: state.layers.first))")
        } catch {
            report(false, "mergeEdits should accept a complete, in-range layer", "\(error)")
        }
    }

    /// Local exposure's real range is -3…3 EV (AdjustmentCatalogue.swift:
    /// 108-110), not the global -5…5 AgentKeys documents for the top-level
    /// scalar — a local exposureEv of 4 must be rejected even though 4 sits
    /// inside the global range.
    static func testAgentMergeEditsUsesTheLocalRangeNotTheGlobalOne() {
        guard let base = try? JSONEncoder().encode(DevelopState()) else {
            report(false, "DevelopState() should encode")
            return
        }
        var layer = LocalAdjustState()
        layer.exposureEv = 4
        guard let layerData = try? JSONEncoder().encode([layer]),
              let layerJSON = String(data: layerData, encoding: .utf8) else {
            report(false, "the fixture layer should encode")
            return
        }
        do {
            let edits = Data(#"{"layers": \#(layerJSON)}"#.utf8)
            _ = try AgentCLI.mergeEdits(base: base, edits: edits)
            report(false, "mergeEdits should reject a local exposureEv of 4")
        } catch AgentCLI.Failure.usage(let message) {
            report(message.contains("-3") && message.contains("3"),
                   "names the local -3…3 range, not the global -5…5", message)
        } catch {
            report(false, "wrong error for an out-of-local-range exposureEv", "\(error)")
        }
    }

    /// A curve point past x=1 is rejected — CurveEditor.swift:338 clamps
    /// every point into the unit square.
    static func testAgentMergeEditsRejectsACurvePointPastOne() {
        guard let base = try? JSONEncoder().encode(DevelopState()) else {
            report(false, "DevelopState() should encode")
            return
        }
        let curve = #"{"master":[{"x":0,"y":0},{"x":1.2,"y":1}],"#
                  + #""red":[{"x":0,"y":0},{"x":1,"y":1}],"#
                  + #""green":[{"x":0,"y":0},{"x":1,"y":1}],"#
                  + #""blue":[{"x":0,"y":0},{"x":1,"y":1}]}"#
        do {
            let edits = Data("{\"curve\": \(curve)}".utf8)
            _ = try AgentCLI.mergeEdits(base: base, edits: edits)
            report(false, "mergeEdits should reject a curve point at x=1.2")
        } catch AgentCLI.Failure.usage(let message) {
            report(message.contains("master") && message.contains("outside 0…1"),
                   "names the channel and the 0…1 range", message)
        } catch {
            report(false, "wrong error for a curve point past x=1", "\(error)")
        }
    }

    /// A valid 3-point ascending curve is accepted and lands exactly.
    static func testAgentMergeEditsAcceptsAValidThreePointCurve() {
        guard let base = try? JSONEncoder().encode(DevelopState()) else {
            report(false, "DevelopState() should encode")
            return
        }
        var curve = ToneCurve()
        curve.master = [CurvePoint(x: 0, y: 0), CurvePoint(x: 0.5, y: 0.7), CurvePoint(x: 1, y: 1)]
        guard let curveData = try? JSONEncoder().encode(curve),
              let curveJSON = String(data: curveData, encoding: .utf8) else {
            report(false, "the fixture curve should encode")
            return
        }
        do {
            let edits = Data("{\"curve\": \(curveJSON)}".utf8)
            let merged = try AgentCLI.mergeEdits(base: base, edits: edits)
            let state = try JSONDecoder().decode(DevelopState.self, from: merged.state)
            report(state.curve.master == curve.master, "the 3-point master curve lands exactly",
                   "got \(state.curve.master)")
        } catch {
            report(false, "mergeEdits should accept a valid 3-point curve", "\(error)")
        }
    }

    /// Mask component kind 7 does not exist — EditHistory.swift:102 lists
    /// 1 through 6 — and is rejected rather than silently accepted as some
    /// kind the engine happens to fall back on.
    static func testAgentMergeEditsRejectsMaskComponentKindSeven() {
        guard let base = try? JSONEncoder().encode(DevelopState()) else {
            report(false, "DevelopState() should encode")
            return
        }
        var component = MaskComponentState()
        component.kind = 7
        guard let componentData = try? JSONEncoder().encode([component]),
              let componentJSON = String(data: componentData, encoding: .utf8) else {
            report(false, "the fixture component should encode")
            return
        }
        do {
            let edits = Data("{\"maskComponents\": \(componentJSON)}".utf8)
            _ = try AgentCLI.mergeEdits(base: base, edits: edits)
            report(false, "mergeEdits should reject mask kind 7")
        } catch AgentCLI.Failure.usage(let message) {
            report(message.contains("kind 7") && message.contains("outside 1…6"),
                   "names the bad kind and the 1…6 range", message)
        } catch {
            report(false, "wrong error for mask kind 7", "\(error)")
        }
    }

    /// Kind 4 (raster matte) without a matching matte file is rejected — a
    /// model cannot paint, only reference a matte a producer already saved.
    static func testAgentMergeEditsRejectsKindFourWithNoMatteFile() {
        guard let base = try? JSONEncoder().encode(DevelopState()) else {
            report(false, "DevelopState() should encode")
            return
        }
        let dir = FileManager.default.temporaryDirectory
            .appendingPathComponent("orion-agent-matte-\(UUID().uuidString)")
        try? FileManager.default.createDirectory(at: dir, withIntermediateDirectories: true)
        defer { try? FileManager.default.removeItem(at: dir) }
        let photo = dir.appendingPathComponent("photo.ARW")

        var component = MaskComponentState()
        component.kind = 4
        component.matteId = "no-such-matte"
        guard let componentData = try? JSONEncoder().encode([component]),
              let componentJSON = String(data: componentData, encoding: .utf8) else {
            report(false, "the fixture component should encode")
            return
        }
        do {
            let edits = Data("{\"maskComponents\": \(componentJSON)}".utf8)
            _ = try AgentCLI.mergeEdits(base: base, edits: edits, photo: photo)
            report(false, "mergeEdits should reject kind 4 with no matte file")
        } catch AgentCLI.Failure.usage(let message) {
            report(message.contains("no-such-matte") && message.contains("does not exist"),
                   "names the missing matte", message)
        } catch {
            report(false, "wrong error for a missing matte file", "\(error)")
        }
    }

    /// Kind 4 with a matte file that actually exists beside the photo is
    /// accepted — the file is what MatteStore.swift's own layout would have
    /// written for that photo and id.
    static func testAgentMergeEditsAcceptsKindFourWithAnExistingMatteFile() {
        guard let base = try? JSONEncoder().encode(DevelopState()) else {
            report(false, "DevelopState() should encode")
            return
        }
        let dir = FileManager.default.temporaryDirectory
            .appendingPathComponent("orion-agent-matte-\(UUID().uuidString)")
        try? FileManager.default.createDirectory(at: dir, withIntermediateDirectories: true)
        defer { try? FileManager.default.removeItem(at: dir) }
        let photo = dir.appendingPathComponent("photo.ARW")
        let matteId = "b3c1f0de-0000-4000-8000-000000000001"
        try? Data([0]).write(to: MatteStore.url(photo: photo, id: matteId))

        var component = MaskComponentState()
        component.kind = 4
        component.matteId = matteId
        guard let componentData = try? JSONEncoder().encode([component]),
              let componentJSON = String(data: componentData, encoding: .utf8) else {
            report(false, "the fixture component should encode")
            return
        }
        do {
            let edits = Data("{\"maskComponents\": \(componentJSON)}".utf8)
            let merged = try AgentCLI.mergeEdits(base: base, edits: edits, photo: photo)
            let state = try JSONDecoder().decode(DevelopState.self, from: merged.state)
            report(state.maskComponents.first?.matteId == matteId,
                   "the matte component lands with its id",
                   "got \(String(describing: state.maskComponents.first?.matteId))")
        } catch {
            report(false, "mergeEdits should accept kind 4 with an existing matte file", "\(error)")
        }
    }

    /// The important round trip: `keys`' `layers` example is not just
    /// documentation — sent straight back through `mergeEdits`, it must pass
    /// the very validation it documents. If the example and the validator
    /// ever drift, this is what catches it.
    static func testAgentKeysLayersExampleRoundTripsThroughMergeEdits() {
        guard let base = try? JSONEncoder().encode(DevelopState()) else {
            report(false, "DevelopState() should encode")
            return
        }
        let keys = AgentCLI.keysJSON()
        guard let layersEntry = keys.first(where: { ($0["name"] as? String) == "layers" }) else {
            report(false, "keys contains a layers entry")
            return
        }
        guard let example = layersEntry["example"],
              let exampleData = try? JSONSerialization.data(withJSONObject: ["layers": example])
        else {
            report(false, "the layers example should re-serialize")
            return
        }
        do {
            let merged = try AgentCLI.mergeEdits(base: base, edits: exampleData)
            let state = try JSONDecoder().decode(DevelopState.self, from: merged.state)
            report(!state.layers.isEmpty, "the example layer round-trips into state.layers",
                   "got \(state.layers.count) layers")
        } catch {
            report(false, "the layers example should pass its own validation", "\(error)")
        }
    }

    /// The bug this whole change exists for: `apply` took `temperatureK: 150`
    /// literally — a value meant as a +150 K delta — rendered a flat black
    /// proxy, and then `commit` wrote it to the sidecar anyway. `mergeEdits`
    /// must refuse it before it ever reaches the engine, and must say the
    /// value is absolute, not a delta, so the caller learns why.
    static func testAgentMergeEditsRejectsAnOutOfRangeTemperature() {
        guard let base = try? JSONEncoder().encode(DevelopState()) else {
            report(false, "DevelopState() should encode")
            return
        }
        do {
            let edits = Data(#"{"temperatureK": 150}"#.utf8)
            _ = try AgentCLI.mergeEdits(base: base, edits: edits)
            report(false, "mergeEdits should reject temperatureK 150")
        } catch AgentCLI.Failure.usage(let message) {
            report(
                message == "temperatureK 150 is outside 2000…12000 "
                    + "(kelvin, absolute not a delta)",
                "names the value, the product's real range and that it is absolute",
                message)
        } catch {
            report(false, "wrong error for an out-of-range temperatureK", "\(error)")
        }
    }

    /// `tint`'s product range is -1…1 (DevelopPanels+Light.swift:14) — the
    /// same shape of mistake as temperatureK 150, on the other half of the
    /// white-balance pair, and the boundary showing an in-range value is
    /// still accepted.
    static func testAgentMergeEditsValidatesTintRange() {
        guard let base = try? JSONEncoder().encode(DevelopState()) else {
            report(false, "DevelopState() should encode")
            return
        }
        do {
            let edits = Data(#"{"tint": 15}"#.utf8)
            _ = try AgentCLI.mergeEdits(base: base, edits: edits)
            report(false, "mergeEdits should reject tint 15")
        } catch AgentCLI.Failure.usage(let message) {
            report(message.contains("tint 15 is outside -1…1"),
                   "names tint's real range", message)
        } catch {
            report(false, "wrong error for an out-of-range tint", "\(error)")
        }

        do {
            let edits = Data(#"{"tint": 0.4}"#.utf8)
            let merged = try AgentCLI.mergeEdits(base: base, edits: edits)
            let state = try JSONDecoder().decode(DevelopState.self, from: merged.state)
            report(state.tint == 0.4, "an in-range tint lands", "got \(state.tint)")
        } catch {
            report(false, "mergeEdits should accept an in-range tint", "\(error)")
        }
    }

    /// exposureEv 0.2 is well inside -5…5 (DevelopPanels+Light.swift:35) and
    /// must land untouched by the new range check.
    static func testAgentMergeEditsAcceptsAnInRangeExposure() {
        guard let base = try? JSONEncoder().encode(DevelopState()) else {
            report(false, "DevelopState() should encode")
            return
        }
        do {
            let edits = Data(#"{"exposureEv": 0.2}"#.utf8)
            let merged = try AgentCLI.mergeEdits(base: base, edits: edits)
            let state = try JSONDecoder().decode(DevelopState.self, from: merged.state)
            report(state.exposureEv == 0.2, "an in-range exposureEv lands",
                   "got \(state.exposureEv)")
        } catch {
            report(false, "mergeEdits should accept exposureEv 0.2", "\(error)")
        }
    }

    /// A value exactly at the floor and exactly at the ceiling is accepted —
    /// the check is `ClosedRange.contains`, not an off-by-one `<`/`>`.
    static func testAgentMergeEditsAcceptsExactBoundaries() {
        guard let base = try? JSONEncoder().encode(DevelopState()) else {
            report(false, "DevelopState() should encode")
            return
        }
        for bound in [2000, 12000] {
            do {
                let edits = Data(#"{"temperatureK": \#(bound)}"#.utf8)
                let merged = try AgentCLI.mergeEdits(base: base, edits: edits)
                let state = try JSONDecoder().decode(DevelopState.self, from: merged.state)
                report(state.temperatureK == Float(bound),
                       "temperatureK \(bound) sits exactly on the boundary and is accepted",
                       "got \(state.temperatureK)")
            } catch {
                report(false, "mergeEdits should accept temperatureK \(bound)", "\(error)")
            }
        }
    }

    /// `rotateQuarters` has no product slider and so no product range —
    /// `apply` accepts whatever the JSON type allows, per the same rule
    /// `AgentKeys` documents in `keys`' `note` for this field.
    static func testAgentMergeEditsAcceptsAnyValueForAKeyWithNoRange() {
        guard let base = try? JSONEncoder().encode(DevelopState()) else {
            report(false, "DevelopState() should encode")
            return
        }
        do {
            let edits = Data(#"{"rotateQuarters": 7}"#.utf8)
            let merged = try AgentCLI.mergeEdits(base: base, edits: edits)
            let state = try JSONDecoder().decode(DevelopState.self, from: merged.state)
            report(state.rotateQuarters == 7,
                   "rotateQuarters has no product range to enforce", "got \(state.rotateQuarters)")
        } catch {
            report(false, "mergeEdits should accept any rotateQuarters", "\(error)")
        }
    }

    /// `Orion --agent keys` parses with no `<raw>` — it names no photograph.
    static func testAgentParsesKeys() {
        do {
            let command = try AgentCLI.parse(["--agent", "keys"])
            report(command == .keys, "parses keys with no raw path", "\(command)")
        } catch {
            report(false, "parse should not throw on 'keys'", "\(error)")
        }
    }

    /// The `keys` payload: parses, names exactly the fields `mergeEdits`
    /// allows, sorted, and documents temperatureK the way the spec asks —
    /// kelvin, a real floor well above zero, and marked absolute so a caller
    /// never reads it as a delta again.
    static func testAgentKeysJSON() {
        let keys = AgentCLI.keysJSON()
        report(!keys.isEmpty, "keys is non-empty")

        let names = keys.compactMap { $0["name"] as? String }
        report(names == names.sorted(), "keys is sorted by name", "\(names)")
        let allowed = AgentCLI.scalarFieldNames.union(AgentComposite.fieldNames)
        report(Set(names) == allowed,
               "keys names exactly the fields mergeEdits allows",
               "\(Set(names).symmetricDifference(allowed))")

        guard let temperature = keys.first(where: { ($0["name"] as? String) == "temperatureK" })
        else {
            report(false, "keys contains temperatureK")
            return
        }
        report((temperature["type"] as? String) == "number", "temperatureK is a number")
        report((temperature["unit"] as? String) == "kelvin", "temperatureK's unit is kelvin")
        report((temperature["min"] as? Double).map { $0 >= 1000 } ?? false,
               "temperatureK's min is at least 1000",
               "\(String(describing: temperature["min"]))")
        report((temperature["absolute"] as? Bool) == true,
               "temperatureK is documented as absolute, never a delta")
    }

    /// `Engine.histogram(bins:)` is channel-major: all of a channel's bins in
    /// a row. All the mass in channel 0's last bin should read as fully
    /// clipped high, not at all clipped low, and a mean at the very top.
    static func testAgentStatsJSON() {
        let bins = 128
        var histogram = [UInt32](repeating: 0, count: bins * 3)
        histogram[bins - 1] = 1000

        let stats = AgentCLI.statsJSON(histogram: histogram, bins: bins)
        report(stats.clipHigh[0] == 1.0, "all mass in the last bin clips high",
               "got \(stats.clipHigh[0])")
        report(stats.clipLow[0] == 0.0, "none of it is in the first bin",
               "got \(stats.clipLow[0])")
        report(stats.mean[0] == 1.0, "the weighted mean sits at the top bin",
               "got \(stats.mean[0])")
    }

    /// The photographer in the shipped bug believed a photo had been rated 5
    /// stars before `commit` wrote it. `AgentCLIDriver.runCommit` writes
    /// through `Autosave.toSidecar`, exactly like this test does — so this
    /// proves the actual commit path, not a stand-in for it. `Autosave.
    /// toSidecar` routes through `Sidecar.merge`, a read-modify-write that
    /// only touches `develop`, so the rating already on disk should survive
    /// untouched.
    static func testAgentCommitPreservesAnExistingRating() {
        let dir = FileManager.default.temporaryDirectory
            .appendingPathComponent("orion-agent-commit-\(UUID().uuidString)")
        try? FileManager.default.createDirectory(at: dir, withIntermediateDirectories: true)
        defer { try? FileManager.default.removeItem(at: dir) }

        let photo = dir.appendingPathComponent("photo.ARW")
        var existing = Sidecar()
        existing.rating = 5
        report(existing.write(for: photo), "the fixture sidecar writes")
        report(Sidecar.read(for: photo)?.rating == 5, "the fixture actually holds 5 stars")

        // The same call `runCommit` makes: an out-of-range edit that was
        // caught before it reached this point in the real bug, but the
        // commit path itself must not depend on that — any develop state at
        // all must leave the rating alone.
        var edited = DevelopState()
        edited.exposureEv = 1.2
        report(Autosave.toSidecar(photo, edited), "commit's write lands")

        let after = Sidecar.read(for: photo)
        report(after?.rating == 5, "the rating survives commit",
               "got \(String(describing: after?.rating))")
        report(after?.develop != nil, "commit did write develop settings")
    }

    /// A folder a photographer already has may hold sidecars a camera or
    /// Lightroom wrote — `xmp:Rating` present, `orion:Develop` never having
    /// existed. `Sidecar.read` must not crash on one and must still surface
    /// the rating; `stats`/`apply` fall back to a fresh engine state when
    /// `develop` comes back nil (AgentCLIDriver.runApply's `else` branch),
    /// which this proves is the path such a file actually takes.
    static func testSidecarReadHandlesAFileWithNoOrionDevelop() {
        let dir = FileManager.default.temporaryDirectory
            .appendingPathComponent("orion-foreign-sidecar-\(UUID().uuidString)")
        try? FileManager.default.createDirectory(at: dir, withIntermediateDirectories: true)
        defer { try? FileManager.default.removeItem(at: dir) }

        let photo = dir.appendingPathComponent("photo.ARW")
        let xmp = dir.appendingPathComponent("photo.xmp")
        // Lightroom's own shape: an attribute-form xmp:Rating, no
        // orion:Develop anywhere, because Orion has never opened this file.
        let foreign = """
        <?xpacket begin="\u{FEFF}" id="W5M0MpCehiHzreSzNTczkc9d"?>
        <x:xmpmeta xmlns:x="adobe:ns:meta/" x:xmptk="Adobe XMP Core 6.0">
         <rdf:RDF xmlns:rdf="http://www.w3.org/1999/02/22-rdf-syntax-ns#">
          <rdf:Description rdf:about=""
            xmlns:xmp="http://ns.adobe.com/xap/1.0/"
            xmp:Rating="4"/>
         </rdf:RDF>
        </x:xmpmeta>
        <?xpacket end="w"?>
        """
        try? foreign.write(to: xmp, atomically: true, encoding: .utf8)

        let sidecar = Sidecar.read(for: photo)
        report(sidecar != nil, "a foreign sidecar with no orion:Develop still reads")
        report(sidecar?.rating == 4, "its rating still comes through",
               "got \(String(describing: sidecar?.rating))")
        report(sidecar?.develop == nil, "develop is nil rather than a crash or garbage")
    }
}
