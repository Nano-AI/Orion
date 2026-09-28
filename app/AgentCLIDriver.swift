import AppKit
import Foundation

/// The half of `AgentCLI` that touches a real photograph — `Engine`,
/// `Sidecar`, `Autosave` — and so, like `BatchExportDriver` and
/// `HdrMergeDriver`, cannot compile into `orion-viewport-tests`.
/// `AgentCLI.swift` carries everything that can; this is what is left.
extension AgentCLI {

    /// `Orion --agent <verb> <raw> …`. UTF-8 JSON, one object, on stdout.
    /// Exit 2 is a usage error, 1 is a failure, 0 is the only success.
    @MainActor
    static func runCommandLine(_ args: [String]) -> Never {
        NSApplication.shared.setActivationPolicy(.accessory)
        do {
            let command = try parse(args)
            let result = try run(command)
            let data = try JSONSerialization.data(withJSONObject: result)
            FileHandle.standardOutput.write(data)
            FileHandle.standardOutput.write(Data("\n".utf8))
            exit(0)
        } catch Failure.usage(let message) {
            FileHandle.standardError.write(Data("orion: \(message)\n".utf8))
            exit(2)
        } catch Failure.unknownKeys(let keys) {
            let allowed = scalarFieldNames.union(AgentComposite.fieldNames).sorted().joined(separator: ", ")
            FileHandle.standardError.write(Data(
                "orion: unknown keys \(keys.joined(separator: ", ")); allowed: \(allowed)\n"
                    .utf8))
            exit(2)
        } catch {
            FileHandle.standardError.write(Data("orion: \(error)\n".utf8))
            exit(1)
        }
    }

    @MainActor
    private static func run(_ command: Command) throws -> [String: Any] {
        switch command {
        case .stats(let raw, let state, let region):
            return try runStats(raw: raw, state: state, region: region)
        case .proxy(let raw, let max, let out, let state, let region):
            return try runProxy(raw: raw, max: max, out: out, state: state, region: region)
        case .faces(let raw, let state):
            return try runFaces(raw: raw, state: state)
        case .apply(let raw, let edits, let out, let state):
            return try runApply(raw: raw, edits: edits, out: out, state: state)
        case .commit(let raw, let state):
            return try runCommit(raw: raw, state: state)
        case .flag(let raw, let rating, let reject):
            return try runFlag(raw: raw, rating: rating, reject: reject)
        case .keys:
            return ["keys": keysJSON()]
        }
    }

    // MARK: stats

    /// `--state` restores a proposed edit before the histogram is read, the
    /// same layering `proxy` does and through the same `openEngine`, so
    /// "what would this proposal measure" is one call rather than a render a
    /// caller has to eyeball. `--region` adds the patch's own numbers.
    @MainActor
    private static func runStats(raw: String, state: String?, region: Region?)
        throws -> [String: Any] {
        var info = OrionRawInfo()
        guard orion_read_info(raw, &info) == ORION_OK else {
            throw Failure.run("could not read \(raw)")
        }
        let camera = withUnsafeBytes(of: info.camera) { bytes in
            String(cString: bytes.baseAddress!.assumingMemoryBound(to: CChar.self))
        }

        let url = URL(fileURLWithPath: raw)
        let sidecar = Sidecar.read(for: url)
        let engine = try openEngine(url: url, sidecarDevelop: sidecar?.develop, state: state)
        guard let histogram = engine.histogram(bins: 128) else {
            throw Failure.run("no histogram — is a photo actually open")
        }
        let stats = statsJSON(histogram: histogram, bins: 128)

        var out: [String: Any] = [
            "path": raw,
            "width": Int(info.width),
            "height": Int(info.height),
            "camera": camera,
            "rating": sidecar?.rating ?? 0,
            "rejected": sidecar?.rejected ?? false,
            "clipLow": stats.clipLow,
            "clipHigh": stats.clipHigh,
            "mean": stats.mean,
            // The develop state's current white balance — after the sidecar
            // restore above, so a caller with no sidecar sees the camera's
            // as-shot reading (Engine.asShotState(), seated by
            // `Engine.open(restoring:)`) rather than DevelopState()'s
            // constant 5500 K / 0 tint. This is how a caller learns the real
            // starting point before proposing a "warmer" delta on it.
            "temperatureK": engine.temperatureK,
            "tint": engine.tint,
        ]
        if let region {
            out["region"] = try regionJSON(engine: engine, region: region)
        }
        return out
    }

    // MARK: proxy

    @MainActor
    private static func runProxy(raw: String, max: UInt32, out: String,
                                 state: String?, region: Region?)
        throws -> [String: Any] {
        let url = URL(fileURLWithPath: raw)
        let sidecar = Sidecar.read(for: url)
        let engine = try openEngine(url: url, sidecarDevelop: sidecar?.develop, state: state)

        // A region is rendered at full size and cropped, so `--max` caps the
        // crop rather than the frame - `AgentInspect.runProxyRegion`.
        if let region {
            return try runProxyRegion(engine: engine, region: region, max: max, out: out)
        }

        try engine.export(to: out, quality: 0.85, maxDimension: max, depth: 8)

        guard let data = FileManager.default.contents(atPath: out),
              let rep = NSBitmapImageRep(data: data) else {
            throw Failure.run("wrote \(out) but could not read it back")
        }
        return ["path": out, "width": rep.pixelsWide, "height": rep.pixelsHigh,
                 "bytes": data.count]
    }

    // MARK: apply

    @MainActor
    private static func runApply(raw: String, edits: String, out: String, state: String?)
        throws -> [String: Any] {
        let url = URL(fileURLWithPath: raw)
        let editsData = try Data(contentsOf: URL(fileURLWithPath: edits))

        let base: Data
        if let state {
            base = try Data(contentsOf: URL(fileURLWithPath: state))
        } else if let saved = Sidecar.read(for: url)?.develop {
            base = saved
        } else {
            let engine = try Engine()
            try engine.open(path: raw)
            base = try JSONEncoder().encode(engine.state)
        }

        let merged = try mergeEdits(base: base, edits: editsData, photo: url)
        try merged.state.write(to: URL(fileURLWithPath: out), options: .atomic)
        // The FULL DevelopState the proposed file now holds — scalars and
        // composites — so a caller can read-modify-write a composite field
        // without re-reading `out` itself.
        guard let state = try JSONSerialization.jsonObject(with: merged.state) as? [String: Any] else {
            throw Failure.run("state is not a JSON object")
        }
        return ["path": out, "changed": merged.changed, "state": state]
    }

    // MARK: commit

    private static func runCommit(raw: String, state: String) throws -> [String: Any] {
        let url = URL(fileURLWithPath: raw)
        let data = try Data(contentsOf: URL(fileURLWithPath: state))
        let decoded = try JSONDecoder().decode(DevelopState.self, from: data)
        guard Autosave.toSidecar(url, decoded) else {
            throw Failure.run("could not write the sidecar for \(raw)")
        }
        return ["sidecar": Sidecar.url(for: url).path]
    }

    // MARK: flag

    private static func runFlag(raw: String, rating: Int?, reject: Bool?)
        throws -> [String: Any] {
        guard rating != nil || reject != nil else {
            throw Failure.usage("flag needs --rating or --reject")
        }
        let url = URL(fileURLWithPath: raw)

        // The spec's rule (Part A): a rating above zero clears reject, and
        // reject 1 zeroes the rating. ⚠ Not the same rule as
        // Library.setRating, which clears reject unconditionally on every
        // call, including a rating of 0 — that would make `flag --rating 0`
        // silently un-reject a photo the caller never mentioned rejecting.
        let wrote = Sidecar.merge(into: url) { sidecar in
            if let rating {
                sidecar.rating = max(0, min(5, rating))
                if rating > 0 { sidecar.rejected = false }
            }
            if let reject {
                sidecar.rejected = reject
                if reject { sidecar.rating = 0 }
            }
        }
        guard wrote else { throw Failure.run("could not write the sidecar for \(raw)") }

        let after = Sidecar.read(for: url) ?? Sidecar()
        return ["rating": after.rating, "rejected": after.rejected]
    }

    // MARK: shared

    /// Opens the photograph, restores its sidecar's develop state if it has
    /// one, then layers `--state` on top if the caller gave one — the order
    /// the spec's `proxy` row spells out. `open(restoring:)` skips the
    /// as-shot render whenever a restore is coming right behind it, so a
    /// photo with both a sidecar and a `--state` override still renders once
    /// per restore rather than three times running.
    ///
    /// ⚠ **Both restores' `Bool` is checked.** `Engine.restore(encoded:)`
    /// returns false rather than throwing when the blob will not decode —
    /// see its doc comment — and renders nothing in that case, so a caller
    /// that ignored it would go on to read a histogram or export a JPEG off
    /// whatever the engine last rendered, silently: black pixels for a cold
    /// open, or the *previous* photograph's frame for a warm one. Same
    /// failure OrionApp+Files.swift's loader guards against for the
    /// sidecar's own restore; here it also covers `--state`, which never
    /// existed at that call site.
    ///
    /// ⚠ Not private: `AgentInspect.swift`'s `faces` goes through this same
    /// call, so "restore the sidecar, then the proposal" has one spelling.
    @MainActor
    static func openEngine(url: URL, sidecarDevelop: Data?, state: String?)
        throws -> Engine {
        let engine = try Engine()
        try engine.open(path: url.path, restoring: sidecarDevelop != nil)
        if let sidecarDevelop {
            guard engine.restore(encoded: sidecarDevelop) else {
                throw Failure.run(
                    "the saved edits could not be read: \(Sidecar.url(for: url).path)")
            }
        }
        if let state {
            let data = try Data(contentsOf: URL(fileURLWithPath: state))
            guard engine.restore(encoded: data) else {
                throw Failure.run("the saved edits could not be read: \(state)")
            }
        }
        // ⚠ Once, after the last restore: a subject or sky row is a file on
        // disk, not a number in the state, and the app uploads it at every open
        // it makes. Without this the tools rendered every kind-4 row as covering
        // nothing - a subject layer left the subject as shot and an inverted one
        // lit the whole frame, with no error anywhere. A file that cannot be
        // read is refused by name rather than rendered as a mask selecting
        // nothing, the rule `restoreMattes` states.
        if sidecarDevelop != nil || state != nil {
            engine.restoreMattes(photo: url)
            if let i = engine.missingMattes.min() {
                let id = engine.maskComponents[i].matteId ?? ""
                throw Failure.run(
                    "mask row \(i) names a matte that could not be read: "
                    + MatteStore.url(photo: url, id: id).path)
            }
        }
        return engine
    }
}
