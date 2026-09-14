import Foundation

/// `Orion --agent <verb> …` — the agent surface for an MCP client (an
/// external agent culls and edits RAWs through proxies and JSON, never
/// touching the RAW itself). See
/// docs/superpowers/specs/2026-09-13-agent-mcp-design.md for the verb
/// contract; the field names and exit codes here are what `mcp/server.ts` is
/// built against in parallel, so they are not renamed casually.
///
/// Split the way `BatchExport`/`BatchExportDriver` and `HdrMergeFlow`/
/// `HdrMergeDriver` are: this half is pure logic — parsing, the edit merge,
/// the histogram arithmetic — and is compiled into `orion-viewport-tests`,
/// which has no `Engine` and no GPU. `AgentCLIDriver.swift` is the part that
/// opens a real photograph and only compiles into the app.
enum AgentCLI {
    enum Failure: Error {
        case usage(String)
        case unknownKeys([String])
        case run(String)
    }

    enum Command: Equatable {
        case stats(raw: String)
        case proxy(raw: String, max: UInt32, out: String, state: String?)
        case apply(raw: String, edits: String, out: String, state: String?)
        case commit(raw: String, state: String)
        case flag(raw: String, rating: Int?, reject: Bool?)
    }

    static let usageLine = "usage: Orion --agent <stats|proxy|apply|commit|flag> <raw> [options]"

    /// Walks `--agent <verb> <raw> [--key value]…` out of the full process
    /// argument list, the same shape `--batch-export` and `--hdr-merge` read.
    static func parse(_ args: [String]) throws -> Command {
        guard let i = args.firstIndex(of: "--agent"), i + 2 < args.count else {
            throw Failure.usage(usageLine)
        }
        let verb = args[i + 1]
        let raw = args[i + 2]

        var options: [String: String] = [:]
        var j = i + 3
        while j < args.count {
            guard args[j].hasPrefix("--"), j + 1 < args.count else {
                throw Failure.usage("bad option near '\(args[j])'")
            }
            options[String(args[j].dropFirst(2))] = args[j + 1]
            j += 2
        }

        switch verb {
        case "stats":
            return .stats(raw: raw)
        case "proxy":
            guard let maxText = options["max"], let max = UInt32(maxText) else {
                throw Failure.usage("proxy needs --max <px>")
            }
            guard let out = options["out"] else {
                throw Failure.usage("proxy needs --out <jpg>")
            }
            return .proxy(raw: raw, max: max, out: out, state: options["state"])
        case "apply":
            guard let edits = options["edits"] else {
                throw Failure.usage("apply needs --edits <json-file>")
            }
            guard let out = options["out"] else {
                throw Failure.usage("apply needs --out <json-file>")
            }
            return .apply(raw: raw, edits: edits, out: out, state: options["state"])
        case "commit":
            guard let state = options["state"] else {
                throw Failure.usage("commit needs --state <json-file>")
            }
            return .commit(raw: raw, state: state)
        case "flag":
            let rating = options["rating"].flatMap(Int.init)
            let reject = options["reject"].flatMap(Int.init).map { $0 != 0 }
            return .flag(raw: raw, rating: rating, reject: reject)
        default:
            throw Failure.usage("unknown verb '\(verb)'")
        }
    }

    /// `apply`'s merge: overlays every key of `edits` onto `base`, both
    /// JSON-encoded `DevelopState`, and proves the result still decodes as
    /// one before handing it back.
    ///
    /// A key `edits` names that `base` does not have is the model inventing
    /// vocabulary — that is a usage error (exit 2), not a silent drop, so the
    /// failure teaches the caller the real field names
    /// (`DevelopState.fieldRoster`).
    static func mergeEdits(base: Data, edits: Data) throws -> (state: Data, changed: [String]) {
        guard let baseObject = try JSONSerialization.jsonObject(with: base) as? [String: Any] else {
            throw Failure.run("base state is not a JSON object")
        }
        guard let editsObject = try JSONSerialization.jsonObject(with: edits) as? [String: Any] else {
            throw Failure.run("edits are not a JSON object")
        }

        let unknown = Set(editsObject.keys).subtracting(baseObject.keys)
        guard unknown.isEmpty else {
            throw Failure.unknownKeys(unknown.sorted())
        }

        var merged = baseObject
        for (key, value) in editsObject { merged[key] = value }

        let mergedData = try JSONSerialization.data(withJSONObject: merged)
        // Round-trips through DevelopState so a structurally valid but
        // ill-typed edit (a string where a Float belongs) fails here, not in
        // whatever reads the file `apply` wrote.
        let state = try JSONDecoder().decode(DevelopState.self, from: mergedData)
        let reencoded = try JSONEncoder().encode(state)
        return (reencoded, editsObject.keys.sorted())
    }

    /// Per-channel clip shares and weighted mean of a `bins × 3`,
    /// channel-major histogram (`Engine.histogram(bins:)`: all of channel 0's
    /// bins, then channel 1's, then channel 2's) — each already normalized to
    /// 0…1.
    static func statsJSON(histogram: [UInt32], bins: Int)
        -> (clipLow: [Double], clipHigh: [Double], mean: [Double]) {
        var clipLow = [Double](repeating: 0, count: 3)
        var clipHigh = [Double](repeating: 0, count: 3)
        var mean = [Double](repeating: 0, count: 3)
        guard bins > 1 else { return (clipLow, clipHigh, mean) }

        for channel in 0..<3 {
            let base = channel * bins
            guard base + bins <= histogram.count else { continue }
            let slice = histogram[base..<(base + bins)]
            let total = slice.reduce(0.0) { $0 + Double($1) }
            guard total > 0 else { continue }

            clipLow[channel] = Double(slice.first ?? 0) / total
            clipHigh[channel] = Double(slice.last ?? 0) / total
            let weighted = slice.enumerated()
                .reduce(0.0) { $0 + Double($1.offset) * Double($1.element) }
            mean[channel] = (weighted / total) / Double(bins - 1)
        }
        return (clipLow, clipHigh, mean)
    }
}
