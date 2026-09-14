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
        /// No photograph — just the edit vocabulary `apply` accepts, so a
        /// caller can learn units and ranges before proposing an edit rather
        /// than after `apply` rejects one.
        case keys
    }

    static let usageLine =
        "usage: Orion --agent <stats|proxy|apply|commit|flag> <raw> [options]"
            + "  |  Orion --agent keys"

    /// Every verb's own option vocabulary. An option outside it is very
    /// likely a typo (`--sate` for `--state`) that would otherwise be parsed
    /// happily and then silently ignored.
    private static let allowedOptions: [String: Set<String>] = [
        "stats": [],
        "proxy": ["max", "out", "state"],
        "apply": ["edits", "out", "state"],
        "commit": ["state"],
        "flag": ["rating", "reject"],
    ]

    /// Walks `--agent <verb> <raw> [--key value]…` out of the full process
    /// argument list, the same shape `--batch-export` and `--hdr-merge` read.
    ///
    /// `keys` is the one verb with no `<raw>` — it names no photograph — so
    /// it is peeled off before the rest of this assumes one is there.
    static func parse(_ args: [String]) throws -> Command {
        guard let i = args.firstIndex(of: "--agent"), i + 1 < args.count else {
            throw Failure.usage(usageLine)
        }
        let verb = args[i + 1]
        if verb == "keys" {
            guard i + 2 == args.count else {
                throw Failure.usage("keys takes no arguments")
            }
            return .keys
        }

        guard i + 2 < args.count else { throw Failure.usage(usageLine) }
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

        if let allowed = allowedOptions[verb],
           let bad = options.keys.first(where: { !allowed.contains($0) }) {
            throw Failure.usage("\(verb) does not take --\(bad)")
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
    /// failure teaches the caller the real field names (`scalarFieldNames`).
    ///
    /// **POC scope, decided 2026-09-13: scalar top-level fields only.** Every
    /// array or object field — `layers`, `spots`, `maskComponents`, `curve`,
    /// the three `grade*` triples, `hueShift`/`satShift`/`lumShift` — needs a
    /// deep merge that does not exist yet, so a composite key or a composite
    /// edit value is refused outright. Refused rather than merely ignored:
    /// `DevelopState`'s decoder falls back to the field's default on a type
    /// mismatch (`try?` throughout `init(from:)`), so an edit like
    /// `{"layers": [...]}` given to today's whole-array assignment would
    /// either silently replace the group with something not devised for it,
    /// or — for an unrelated wrong-shaped value — silently do nothing, and
    /// either way `changed` would claim it landed.
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

        let composite = editsObject.keys.contains {
            isComposite(baseObject[$0]) || isComposite(editsObject[$0])
        }
        guard !composite else {
            throw Failure.usage(
                "composite fields are not editable through apply; scalar keys only: "
                    + scalarFieldNames.sorted().joined(separator: ", "))
        }

        // Range check, against the same numbers the product's own sliders
        // enforce (`AgentKeys.specs` — file:line per field there). Every
        // value here is absolute, never a delta: this is the guard that was
        // missing when `temperatureK: 150` — meant as +150 K — was taken
        // literally, rendered a flat black proxy, and got committed anyway.
        // A key with no product range (`AgentKeys.specs[key]?.range == nil`)
        // is left to whatever the type allows, same as before.
        for (key, value) in editsObject {
            guard let range = AgentKeys.specs[key]?.range,
                  let number = value as? NSNumber else { continue }
            let got = number.doubleValue
            guard range.contains(got) else {
                let unit = AgentKeys.specs[key]?.unit ?? "unitless"
                throw Failure.usage(
                    "\(key) \(AgentKeys.format(got)) is outside "
                        + "\(AgentKeys.format(range.lowerBound))…"
                        + "\(AgentKeys.format(range.upperBound)) "
                        + "(\(unit), absolute not a delta)")
            }
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

    private static func isComposite(_ value: Any?) -> Bool {
        value is [Any] || value is [String: Any]
    }

    /// The scalar half of an encoded `DevelopState` — what `apply`'s stdout
    /// echoes back as `"state"`, so a caller sees what the proposed file now
    /// holds without re-reading it and re-deriving which keys are scalar.
    static func scalarValues(from state: Data) throws -> [String: Any] {
        guard let object = try JSONSerialization.jsonObject(with: state) as? [String: Any] else {
            throw Failure.run("state is not a JSON object")
        }
        return object.filter { scalarFieldNames.contains($0.key) }
    }

    /// The top-level `DevelopState` fields `apply` may actually edit —
    /// scalars only. Derived from a fresh `DevelopState()` rather than
    /// hand-listed, so it can never drift from what `mergeEdits` above
    /// accepts.
    static let scalarFieldNames: Set<String> = {
        guard let data = try? JSONEncoder().encode(DevelopState()),
              let object = try? JSONSerialization.jsonObject(with: data) as? [String: Any]
        else { return [] }
        return Set(object.keys.filter { !isComposite(object[$0]) })
    }()

    /// The `keys` verb's payload: one entry per scalar editable field, sorted
    /// by name — the same set `mergeEdits` allows, since both are built from
    /// `scalarFieldNames`. This is the contract `describe_edits` (mcp/) reads,
    /// so the field names in each entry (name, type, unit, min, max, default,
    /// absolute, note) are fixed.
    static func keysJSON() -> [[String: Any]] {
        guard let data = try? JSONEncoder().encode(DevelopState()),
              let object = try? JSONSerialization.jsonObject(with: data) as? [String: Any]
        else { return [] }

        return scalarFieldNames.sorted().map { name in
            var entry: [String: Any] = ["name": name, "absolute": true]
            switch object[name] {
            case let s as String:
                entry["type"] = "string"
                entry["default"] = s
            case let n as NSNumber:
                entry["type"] = "number"
                entry["default"] = n.doubleValue
            default:
                entry["type"] = "number"
            }
            let spec = AgentKeys.specs[name]
            entry["unit"] = spec?.unit ?? "unitless"
            if let range = spec?.range {
                entry["min"] = range.lowerBound
                entry["max"] = range.upperBound
            }
            if let note = spec?.note { entry["note"] = note }
            return entry
        }
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
