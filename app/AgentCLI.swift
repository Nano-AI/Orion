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
///
/// Two more pieces sit beside those, each for the reason this file is split at
/// all - nothing here is allowed to grow past the size one person can hold in
/// their head: `AgentVocabulary.swift` is the `keys` payload and its notes,
/// and `AgentInspect.swift` is the region and face verbs.
enum AgentCLI {
    enum Failure: Error {
        case usage(String)
        case unknownKeys([String])
        case run(String)
    }

    enum Command: Equatable {
        case stats(raw: String, state: String?, region: Region?)
        case proxy(raw: String, max: UInt32, out: String, state: String?, region: Region?)
        case apply(raw: String, edits: String, out: String, state: String?)
        case commit(raw: String, state: String)
        case flag(raw: String, rating: Int?, reject: Bool?)
        /// Vision's face rectangles, in both spaces a caller needs them in.
        /// See `AgentFaces`.
        case faces(raw: String, state: String?)
        /// No photograph — just the edit vocabulary `apply` accepts, so a
        /// caller can learn units and ranges before proposing an edit rather
        /// than after `apply` rejects one.
        case keys
    }

    /// A rectangle of the DISPLAYED picture - what `--region x,y,w,h` parses
    /// to. Fractions of the displayed frame, origin top-left, the same space
    /// `Screenshot.regionStats` reads in. ⚠ **Not** the frame space a mask's
    /// `centerX`/`radiusX` live in; a crop or a turn separates the two.
    struct Region: Equatable {
        var x = 0.0, y = 0.0, w = 1.0, h = 1.0
    }

    static let usageLine =
        "usage: Orion --agent <stats|proxy|apply|commit|flag|faces> <raw> [options]"
            + "  |  Orion --agent keys"

    /// `--region x,y,w,h`. Refuses a rectangle outside 0…1 or with no area
    /// (exit 2) rather than clamping one: a caller that asked for a region off
    /// the picture asked the wrong question, and a silently clamped answer
    /// reads as the region it named.
    static func parseRegion(_ text: String) throws -> Region {
        let parts = text.split(separator: ",").map {
            Double($0.trimmingCharacters(in: .whitespaces))
        }
        guard parts.count == 4, let x = parts[0], let y = parts[1],
              let w = parts[2], let h = parts[3] else {
            throw Failure.usage(
                "--region takes x,y,w,h as four numbers, fractions of the "
                    + "displayed picture from its top-left corner")
        }
        guard w > 0, h > 0 else {
            throw Failure.usage("--region \(text) has no area - w and h must be above 0")
        }
        guard x >= 0, y >= 0, x + w <= 1.000001, y + h <= 1.000001 else {
            throw Failure.usage(
                "--region \(text) is outside 0…1 - x, y, x+w and y+h must all "
                    + "land inside the picture")
        }
        return Region(x: x, y: y, w: min(w, 1 - x), h: min(h, 1 - y))
    }

    /// Every verb's own option vocabulary. An option outside it is very
    /// likely a typo (`--sate` for `--state`) that would otherwise be parsed
    /// happily and then silently ignored.
    private static let allowedOptions: [String: Set<String>] = [
        "stats": ["state", "region"],
        "proxy": ["max", "out", "state", "region"],
        "apply": ["edits", "out", "state"],
        "commit": ["state"],
        "flag": ["rating", "reject"],
        "faces": ["state"],
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

        let region = try options["region"].map(parseRegion)

        switch verb {
        case "stats":
            return .stats(raw: raw, state: options["state"], region: region)
        case "proxy":
            guard let maxText = options["max"], let max = UInt32(maxText) else {
                throw Failure.usage("proxy needs --max <px>")
            }
            guard let out = options["out"] else {
                throw Failure.usage("proxy needs --out <jpg>")
            }
            return .proxy(raw: raw, max: max, out: out,
                          state: options["state"], region: region)
        case "faces":
            return .faces(raw: raw, state: options["state"])
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
    /// failure teaches the caller the real field names (`scalarFieldNames`
    /// union `AgentComposite.fieldNames`).
    ///
    /// **A composite key — `layers`, `spots`, `maskComponents`, `curve`, the
    /// three `grade*` triples, `hueShift`/`satShift`/`lumShift` — REPLACES the
    /// whole field**, validated by `AgentComposite` before it ever reaches
    /// `merged`. That validation is what makes replacing safe: `DevelopState`
    /// and its nested structs decode leniently (`try?` throughout, so a
    /// missing or ill-typed field silently falls back to a default rather than
    /// throwing) — exactly what let a partial edit zero its siblings before
    /// `AgentComposite` existed. `photo` threads through only for
    /// `maskComponents`' kind-4 matte-exists check.
    static func mergeEdits(base: Data, edits: Data, photo: URL? = nil)
        throws -> (state: Data, changed: [String]) {
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

        // Every composite key gets its own strict, range-checked validator —
        // AgentComposite.swift, file:line cited there the way the scalar
        // ranges below are cited in AgentKeys.swift.
        for (key, value) in editsObject where AgentComposite.fieldNames.contains(key) {
            do {
                try AgentComposite.validate(key: key, value: value, photo: photo)
            } catch AgentComposite.Failure.rejected(let message) {
                throw Failure.usage(message)
            }
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
