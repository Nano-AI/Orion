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

    /// `apply`'s merge: a known key lands and is reported changed; an
    /// invented one is a usage error naming the key, not a silent drop.
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
