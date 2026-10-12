import Foundation

/// `Orion --agent sync <source> --targets <json> [--groups a,b] [--mode m]` -
/// one photograph's settings onto a shoot, the agent's twin of the product's
/// Sync (`SyncSettings.swift`). Pure: the group vocabulary, which keys a patch
/// writes, the patch itself and the report. Compiled into
/// `orion-viewport-tests`; `AgentSyncDriver.swift` is the half that opens
/// photographs and runs the matte producers.
///
/// ⚠ **The six product groups are `SyncSettings.keys(for:)`, not a copy of
/// it.** A field added to a group reaches this verb the day it reaches the
/// product's sync, and `testAgentSyncGroupsOwnTheProductKeys` fails the day
/// the two are made to disagree. `masks` is the one group the product does not
/// have, and it is not a `PresetGroup` case on purpose: the save sheet
/// enumerates `PresetGroup.allCases` (Presets.swift:31).
enum AgentSync {

    enum Mode: String, Equatable {
        case propose, commit
    }

    enum Group: Hashable {
        case preset(PresetGroup)
        case masks

        /// The `--groups` spelling: a `PresetGroup` raw value, or `masks`.
        var name: String {
            switch self {
            case .preset(let g): return g.rawValue
            case .masks: return "masks"
            }
        }

        /// ⚠ `PresetGroup(rawValue:)`, not its `Decodable` init - that one
        /// also takes the legacy `colour` (Presets.swift:43), which is a file
        /// format's history rather than a word an agent should learn.
        init?(name: String) {
            if name == "masks" { self = .masks; return }
            guard let g = PresetGroup(rawValue: name) else { return nil }
            self = .preset(g)
        }

        static let all: [Group] = PresetGroup.allCases.map { .preset($0) } + [.masks]

        /// The product's default selection (Presets.swift:96), in its order.
        static let defaults: [Group] = PresetGroup.allCases
            .filter { PresetGroup.defaultSelection.contains($0) }
            .map { .preset($0) }

        /// The JSON keys a patch of this group writes.
        ///
        /// ⚠ **`masks` carries its markers with it**, and that is the
        /// white-balance trap's shape a second time (SyncSettings.swift:20-32).
        /// `maskSpace` and `localTintAxis` say how the numbers they sit beside
        /// are read, and absent means LEGACY wherever there is something to
        /// gate (EditHistory.swift:773-822). A commit onto a photograph with no
        /// sidecar that wrote `maskComponents` alone would therefore decode as
        /// display-space and be migrated a second time on open, and `layers`
        /// alone would have every tint negated. `maskRefine` is the folded
        /// group's feather (EditHistory.swift:459) - masks copied without it
        /// are not the same edges.
        var keys: [String] {
            switch self {
            case .preset(let g): return SyncSettings.keys(for: g)
            case .masks:
                return ["maskComponents", "layers", "maskRefine", "maskSpace", "localTintAxis"]
            }
        }
    }

    /// `sync`'s option walk, called from `AgentCLI.parse` with the verb's
    /// options already checked against `allowedOptions`.
    static func command(source: String, options: [String: String]) throws -> AgentCLI.Command {
        guard let targets = options["targets"] else {
            throw AgentCLI.Failure.usage(
                "sync needs --targets <json-file> holding an array of absolute raw paths")
        }
        let groups = try options["groups"].map(parseGroups) ?? Group.defaults
        let modeText = options["mode"] ?? Mode.propose.rawValue
        guard let mode = Mode(rawValue: modeText) else {
            throw AgentCLI.Failure.usage("sync --mode is propose or commit, not '\(modeText)'")
        }
        return .sync(source: source, targets: targets, groups: groups,
                     state: options["state"], mode: mode)
    }

    /// `--groups light,color`. Order kept, repeats dropped; an unknown name is
    /// refused (exit 2) with the vocabulary, never skipped - a typo'd group is
    /// a sync that silently copies less than the caller asked for.
    static func parseGroups(_ text: String) throws -> [Group] {
        var out: [Group] = []
        for part in text.split(separator: ",", omittingEmptySubsequences: false) {
            let name = part.trimmingCharacters(in: .whitespaces)
            guard let group = Group(name: name) else {
                throw AgentCLI.Failure.usage(
                    "sync --groups has no group '\(name)'; groups are "
                        + Group.all.map(\.name).joined(separator: ", "))
            }
            if !out.contains(group) { out.append(group) }
        }
        return out
    }

    /// The targets file: a JSON array of absolute paths. A file rather than
    /// argv because a shoot is three hundred of them.
    static func parseTargets(_ data: Data?) throws -> [String] {
        guard let data, let list = try? JSONSerialization.jsonObject(with: data) as? [Any] else {
            throw AgentCLI.Failure.usage("sync --targets must be a JSON array of raw paths")
        }
        let paths = list.compactMap { $0 as? String }
        guard paths.count == list.count, !paths.isEmpty else {
            throw AgentCLI.Failure.usage(
                "sync --targets must be a non-empty JSON array of raw paths (strings)")
        }
        if let relative = paths.first(where: { !$0.hasPrefix("/") }) {
            throw AgentCLI.Failure.usage("sync --targets paths must be absolute: '\(relative)'")
        }
        return paths
    }

    /// Every key the chosen groups write, in group order.
    static func keys(for groups: [Group]) -> [String] {
        var out: [String] = []
        for key in groups.flatMap(\.keys) where !out.contains(key) { out.append(key) }
        return out
    }

    /// `{key: source[key]}` for every key named - the edit `apply` would be
    /// handed. A key the source lacks is left out, the rule
    /// `SyncSettings.patched` states for the same case.
    static func patch(source: [String: Any], keys: [String]) -> [String: Any] {
        var out: [String: Any] = [:]
        for key in keys { if let v = source[key] { out[key] = v } }
        return out
    }

    /// The source's kind-4 rows, by index. Their mattes are pixels of the
    /// SOURCE, so each is re-detected on every target rather than copied.
    static func matteRows(_ source: [String: Any]) -> [(row: Int, matteSource: String?)] {
        let rows = source["maskComponents"] as? [[String: Any]] ?? []
        return rows.enumerated().compactMap { i, row in
            (row["kind"] as? NSNumber)?.intValue == 4 ? (i, row["matteSource"] as? String) : nil
        }
    }

    /// A row's producer as a `matte --kind` value, case-insensitively from the
    /// `matteSource` label the product writes (`SubjectMatte.Kind.label`).
    /// Nil for a row nothing here can re-detect - a hand-made or foreign one.
    static func producer(matteSource: String?) -> String? {
        guard let s = matteSource?.lowercased(), AgentCLI.matteKinds.contains(s) else { return nil }
        return s
    }

    /// The source object with row `row` naming the target's own matte.
    static func rewritingMatte(_ source: [String: Any], row: Int, id: String,
                               label: String) -> [String: Any] {
        guard var rows = source["maskComponents"] as? [[String: Any]],
              rows.indices.contains(row) else { return source }
        rows[row]["matteId"] = id
        rows[row]["matteSource"] = label
        var out = source
        out["maskComponents"] = rows
        return out
    }

    /// Commit mode's sidecar blob: `SyncSettings.patched`, the product's own
    /// key-level patch, plus `process` where the target never stated one.
    ///
    /// ⚠ **`process` is the white-balance trap again** (EditHistory.swift:787-
    /// 801): absent means generation 1 wherever a tone slider is nonzero. A
    /// photograph with no sidecar that received `highlights` alone would
    /// render the source's numbers under the older bands. It is filled only
    /// when the target has no tone values of its own for it to reinterpret;
    /// a legacy target that does keeps its absent marker, as the product's
    /// sync leaves it.
    static func committed(stored: Data?, source: [String: Any], keys: [String]) -> Data? {
        var keys = keys
        let toneGated: Set<String> = ["highlights", "shadows", "whites", "blacks", "layers"]
        if keys.contains(where: toneGated.contains), !keys.contains("process") {
            let object = stored.flatMap { try? JSONSerialization.jsonObject(with: $0) as? [String: Any] }
            let own = stored.flatMap { try? JSONDecoder().decode(DevelopState.self, from: $0) }
            if object?["process"] == nil, !(own?.usesToneBands ?? false) { keys.append("process") }
        }
        return SyncSettings.patched(stored: stored, sourceObject: source, keys: keys)
    }

    /// The same photograph however it is spelled - a target equal to the
    /// source is skipped and said so, never written over itself.
    static func isSame(_ a: String, _ b: String) -> Bool {
        func canonical(_ p: String) -> String {
            URL(fileURLWithPath: p).standardizedFileURL.resolvingSymlinksInPath().path
        }
        return canonical(a) == canonical(b)
    }

    /// The text of a per-target failure: the message, not Swift's
    /// `run("...")` spelling of the enum it travelled in.
    static func describe(_ error: Error) -> String {
        switch error {
        case AgentCLI.Failure.usage(let m), AgentCLI.Failure.run(let m): return m
        case AgentCLI.Failure.unknownKeys(let keys):
            return "unknown keys \(keys.joined(separator: ", "))"
        default: return error.localizedDescription
        }
    }

    /// What happened to each target. Failures are per target: one card that
    /// will not take a write must not cost the other two hundred.
    struct Report {
        private(set) var results: [[String: Any]] = []
        private(set) var written = 0, failed = 0, skipped = 0

        mutating func wrote(_ path: String, to kind: String, file: String,
                            mattes: [[String: Any]]) {
            results.append(["path": path, "wrote": kind, "file": file, "mattes": mattes])
            written += 1
        }

        mutating func skip(_ path: String, because reason: String) {
            results.append(["path": path, "skipped": reason])
            skipped += 1
        }

        mutating func fail(_ path: String, _ error: String) {
            results.append(["path": path, "error": error])
            failed += 1
        }

        func json(source: String, sourceState: String, mode: Mode, groups: [Group],
                  keys: [String]) -> [String: Any] {
            ["source": source, "sourceState": sourceState, "mode": mode.rawValue,
             "groups": groups.map(\.name), "keys": keys, "results": results,
             "written": written, "failed": failed, "skipped": skipped]
        }
    }
}
