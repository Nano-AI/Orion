import Foundation

/// The half of `AgentSync` that opens photographs: the source's state, each
/// target's re-detected mattes, and the two writes. App-only, the split
/// `AgentCLI`/`AgentCLIDriver` make for the same reason.
extension AgentCLI {

    @MainActor
    static func runSync(source: String, targets: String, groups: [AgentSync.Group],
                        state: String?, mode: AgentSync.Mode) throws -> [String: Any] {
        let paths = try AgentSync.parseTargets(
            FileManager.default.contents(atPath: targets))

        // Normalised through `DevelopState` before a key is copied, the way
        // `runState` hands it back: a legacy sidecar's conversions (layer
        // tint sign, the lifted local scalars) happen to the SOURCE's values
        // here, once, rather than to whatever they land beside on a target.
        let base = try baseState(url: URL(fileURLWithPath: source), state: state)
        let sourceObject: [String: Any]
        do {
            let normal = try JSONEncoder().encode(
                JSONDecoder().decode(DevelopState.self, from: base.data))
            sourceObject = try JSONSerialization.jsonObject(with: normal) as? [String: Any] ?? [:]
        } catch {
            throw Failure.run("the source's develop state could not be read: \(error)")
        }

        let keys = AgentSync.keys(for: groups)
        // ⚠ Resolved once, before any target: a row no producer can re-detect
        // fails every target identically, and opening three hundred
        // photographs to say so three hundred times would be the slow way.
        var mattes: [(row: Int, kind: SubjectMatte.Kind)] = []
        var unmatchable: String?
        if groups.contains(.masks) {
            for (row, label) in AgentSync.matteRows(sourceObject) {
                if let name = AgentSync.producer(matteSource: label), let kind = matteKind(name) {
                    mattes.append((row, kind))
                } else if unmatchable == nil {
                    unmatchable = "mask row \(row) is a matte with matteSource "
                        + "'\(label ?? "none")', and only Subject, Person or Sky can be "
                        + "re-detected on another photograph"
                }
            }
        }

        var report = AgentSync.Report()
        for path in paths {
            if AgentSync.isSame(path, source) {
                report.skip(path, because: "source")
                continue
            }
            do {
                if let unmatchable { throw Failure.run(unmatchable) }
                guard FileManager.default.fileExists(atPath: path) else {
                    throw Failure.run("no such file: \(path)")
                }
                let target = URL(fileURLWithPath: path)
                var object = sourceObject
                var found: [[String: Any]] = []
                for m in try redetect(mattes, on: target) {
                    object = AgentSync.rewritingMatte(object, row: m.row, id: m.id,
                                                      label: m.label)
                    found.append(["row": m.row, "matteId": m.id, "matteSource": m.label,
                                  "coverage": m.coverage])
                }
                let file = try write(object, keys: keys, to: target, mode: mode)
                report.wrote(path, to: mode == .propose ? "proposed" : "sidecar",
                             file: file.path, mattes: found)
            } catch {
                report.fail(path, AgentSync.describe(error))
            }
        }
        return report.json(source: source, sourceState: base.source, mode: mode,
                           groups: groups, keys: keys)
    }

    /// One target's mattes, produced on the TARGET's own picture and saved
    /// beside it. The engine is opened only when there is a row to detect;
    /// each producer runs once per target however many rows name it.
    @MainActor
    private static func redetect(_ rows: [(row: Int, kind: SubjectMatte.Kind)], on target: URL)
        throws -> [(row: Int, id: String, label: String, coverage: Double)] {
        guard !rows.isEmpty else { return [] }
        let engine = try openEngine(url: target,
                                    sidecarDevelop: Sidecar.read(for: target)?.develop,
                                    state: nil)
        var cache: [String: (alpha: [Float], width: Int, height: Int)] = [:]
        var out: [(row: Int, id: String, label: String, coverage: Double)] = []
        for (row, kind) in rows {
            let label = kind.label
            let matte: (alpha: [Float], width: Int, height: Int)
            if let cached = cache[label] {
                matte = cached
            } else {
                do { matte = try SubjectMatte.generateBlocking(engine: engine, kind: kind) }
                catch {
                    throw Failure.run("mask row \(row) (\(label)): \(error.localizedDescription)")
                }
                cache[label] = matte
            }
            let id = try MatteStore.write(matte.alpha, width: matte.width,
                                          height: matte.height, photo: target)
            out.append((row, id, label, coverage(matte.alpha)))
        }
        return out
    }

    /// The write itself, by mode. Returns the file written.
    ///
    /// `propose` is exactly `apply`: the target's own base (its sidecar, else
    /// as shot - never `DevelopState()`'s 5500 K) with the patch merged on
    /// and validated by `mergeEdits`, so the file decodes as a complete state.
    /// An existing proposal is replaced, not merged.
    ///
    /// `commit` patches the sidecar at the key level, the product's sync
    /// (SyncSettings.swift:11-32): a key the groups do not own and the target
    /// never had stays absent all the way to `Engine.open`.
    @MainActor
    private static func write(_ object: [String: Any], keys: [String], to target: URL,
                              mode: AgentSync.Mode) throws -> URL {
        switch mode {
        case .propose:
            let patch = try JSONSerialization.data(
                withJSONObject: AgentSync.patch(source: object, keys: keys))
            let merged = try mergeEdits(base: baseState(url: target, state: nil).data,
                                        edits: patch, photo: target)
            let file = Proposal.proposedURL(for: target)
            try merged.state.write(to: file, options: .atomic)
            return file
        case .commit:
            guard let data = AgentSync.committed(stored: Sidecar.read(for: target)?.develop,
                                                 source: object, keys: keys) else {
                throw Failure.run("could not build the sidecar patch for \(target.path)")
            }
            guard Sidecar.merge(into: target, { $0.develop = data }) else {
                throw Failure.run("could not write the sidecar for \(target.path)")
            }
            return Sidecar.url(for: target)
        }
    }
}
