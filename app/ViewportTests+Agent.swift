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

    /// POC scope (decided 2026-09-13): `apply` edits scalar top-level keys
    /// only. A composite field — an array or an object, whether the whole
    /// edit value is one or the base field it names is one — is refused, not
    /// silently accepted or silently dropped, and the refusal must not offer
    /// the composite field back as something the caller could have used
    /// instead.
    static func testAgentMergeEditsRejectsCompositeFields() {
        guard let base = try? JSONEncoder().encode(DevelopState()) else {
            report(false, "DevelopState() should encode")
            return
        }

        do {
            let edits = Data(#"{"layers": [{"exposureEv": 1.0}]}"#.utf8)
            _ = try AgentCLI.mergeEdits(base: base, edits: edits)
            report(false, "mergeEdits should reject the composite key layers")
        } catch AgentCLI.Failure.usage(let message) {
            report(!message.contains("layers"),
                   "the message does not offer layers as an allowed key", message)
        } catch {
            report(false, "wrong error for a composite key", "\(error)")
        }

        do {
            let edits = Data(#"{"gradeShadow": [0, 0, 0]}"#.utf8)
            _ = try AgentCLI.mergeEdits(base: base, edits: edits)
            report(false, "mergeEdits should reject the composite key gradeShadow")
        } catch AgentCLI.Failure.usage(let message) {
            report(message.contains("scalar keys only"),
                   "explains that only scalar keys are editable", message)
        } catch {
            report(false, "wrong error for gradeShadow", "\(error)")
        }
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
}
