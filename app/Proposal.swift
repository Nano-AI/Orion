import Foundation

/// Pure logic for two small features that share one shape: Orion telling the
/// outside world what it is doing.
///
/// **Orion publishes the photo it is on** — `current.json` beside the index,
/// so the MCP server (docs/superpowers/specs/2026-09-13-agent-mcp-design.md)
/// can read which photo is open instead of a model having to be told.
///
/// **The proposal view** — when `<stem>.proposed.json` appears beside the
/// open photo (the output of the MCP's `apply`), Orion previews it live in
/// compare, with Approve and Reject. `ProposalWatcher` is the app-only half:
/// the filesystem watch and the actual `Engine`/`Autosave` calls. Everything
/// here is data in, data out — the path a proposal lives at, what changed
/// between two states, and the phase machine that turns a filesystem event
/// into the list of actions the app performs, so `ProposalWatcher` is a thin
/// interpreter over the policy written down here rather than a second place
/// it could drift from.
enum Proposal {

    // MARK: Where a proposal lives

    /// `<raw stem>.proposed.json`, beside the raw. The same convention
    /// `mcp/server.ts`'s `proposedPath` uses (`raw.replace(/\.[^.]+$/,
    /// ".proposed.json")`) — written out by hand here rather than through
    /// `URL.appendingPathExtension`, whose handling of a dotted extension
    /// string is not a contract worth trusting.
    static func proposedURL(for raw: URL) -> URL {
        let stem = raw.deletingPathExtension().lastPathComponent
        return raw.deletingLastPathComponent()
                  .appendingPathComponent("\(stem).proposed.json")
    }

    // MARK: What changed

    /// The top-level `DevelopState` keys that differ between two encoded
    /// states. Shaped like `AgentCLI.mergeEdits`'s `changed`, but computed
    /// from two whole states rather than an edit patch — the footer has
    /// nothing on disk but the states themselves, never the patch that
    /// produced one.
    static func changedKeys(base: Data, proposed: Data) -> [String] {
        guard let baseObject = try? JSONSerialization.jsonObject(with: base) as? [String: Any],
              let proposedObject = try? JSONSerialization.jsonObject(with: proposed) as? [String: Any]
        else { return [] }
        return proposedObject.keys
            .filter { !jsonEqual(baseObject[$0], proposedObject[$0]) }
            .sorted()
    }

    private static func jsonEqual(_ a: Any?, _ b: Any?) -> Bool {
        switch (a, b) {
        case (nil, nil): return true
        case let (x as NSObject, y as NSObject): return x.isEqual(y)
        default: return false
        }
    }

    /// The footer's line: up to five keys, named, then a `+N` for the rest —
    /// so "Proposal:" always says something a photographer can read in one
    /// glance rather than a wall of field names.
    static func summary(keys: [String]) -> String {
        guard !keys.isEmpty else { return "Proposal" }
        let shown = keys.prefix(5)
        let rest = keys.count - shown.count
        let names = shown.joined(separator: ", ")
        return rest > 0 ? "Proposal: \(names) +\(rest)" : "Proposal: \(names)"
    }

    // MARK: current.json — the photo Orion is on

    struct CurrentPhoto: Codable, Equatable {
        var photo: String?
        var folder: String?
        var updated: String
    }

    /// `~/Library/Application Support/Orion/current.json`'s content — see
    /// `Editor.publishCurrentPhoto` for the actual write, which resolves
    /// Application Support and calls this. `now` is a parameter so the
    /// encoding is testable without the clock moving between assertion and
    /// call.
    static func currentJSON(photo: URL?, now: Date = Date()) -> Data {
        let doc = CurrentPhoto(photo: photo?.path,
                                folder: photo?.deletingLastPathComponent().path,
                                updated: iso8601.string(from: now))
        return (try? JSONEncoder().encode(doc)) ?? Data()
    }

    /// `current.json`'s path given Application Support's own URL — kept
    /// separate from resolving that URL (a real filesystem call) so the path
    /// arithmetic is testable, the same split `PhotoIndex.defaultURL` doesn't
    /// need because it has no pure half to isolate.
    static func currentFileURL(applicationSupport: URL) -> URL {
        applicationSupport.appendingPathComponent("Orion", isDirectory: true)
                          .appendingPathComponent("current.json")
    }

    private static let iso8601 = ISO8601DateFormatter()

    // MARK: The phase machine

    /// Whether a proposal is live on the canvas, and — while it is — the keys
    /// the footer names.
    enum Phase: Equatable {
        case idle
        case previewing(keys: [String])
    }

    /// What `ProposalWatcher` saw, or what a footer button did.
    enum Event: Equatable {
        case appeared(keys: [String])
        case changed(keys: [String])
        case approved
        case rejected
        /// The proposed file vanished on its own — `approve_edit` or
        /// `reject_edit` ran in chat. `sidecarChanged` is whether the
        /// sidecar's develop now differs from the state Orion committed
        /// before the proposal appeared: true means approve happened there.
        case vanished(sidecarChanged: Bool)
        /// The photo on the canvas changed while a proposal was live.
        case switchedAway
    }

    /// One step `ProposalWatcher` performs, in the order returned.
    enum Action: Equatable {
        case captureOriginal
        case stopAutosave
        /// Shows the just-seen proposed state on the canvas.
        case restoreProposed
        case setCompare
        case recordHistory(label: String)
        case beginAutosave
        case noteAndFlush
        case deleteProposedFile
        case restoreCommitted
        case restoreSidecar
        case clearCompare
    }

    /// The whole feature as a table: an event against the phase it applies to
    /// yields the next phase and the actions to perform, in order. An event
    /// that does not apply to the current phase (an `approved` with nothing
    /// previewing, say) is a no-op — the phase does not move and nothing is
    /// performed, rather than the interpreter having to guard every call site
    /// itself.
    static func transition(_ phase: Phase, on event: Event) -> (Phase, [Action]) {
        switch event {
        // ⚠ **`.stopAutosave` first, not second — a data-loss bug found in
        // review of 806a00d.** `.captureOriginal` (`Engine.captureOriginal`,
        // Engine+Compare.swift:59-99) renders twice — `apply(neutral)` then
        // `apply(current)` — and each `apply` goes through `pushAndRender`,
        // which fires `onEdit` into `autosave.note(state)`. With autosave
        // still armed, `note(neutral)` queues the *neutral* state as
        // pending (`neutral != saved`); `note(current)` is a no-op
        // (`current == saved`); so by the time `.stopAutosave` used to run
        // *after* `.captureOriginal`, `autosave.stop()`'s own `flush()`
        // wrote that neutral state — every adjustment zeroed — straight to
        // the photographer's sidecar, the instant a proposal was detected
        // and before anything was even shown. Stopping first disarms
        // `note` (`Autosave.note` guards on `target`) before either render
        // can queue anything, so the two renders inside `captureOriginal`
        // are inert as far as the sidecar is concerned.
        // `testProposalStopAutosaveRunsBeforeEverythingElse` pins the order.
        case let .appeared(keys):
            return (.previewing(keys: keys),
                    [.stopAutosave, .captureOriginal, .restoreProposed, .setCompare])

        case let .changed(keys):
            guard case .previewing = phase else { return (phase, []) }
            return (.previewing(keys: keys),
                    [.stopAutosave, .captureOriginal, .restoreProposed, .setCompare])

        case .approved:
            guard case let .previewing(keys) = phase else { return (phase, []) }
            return (.idle, [.recordHistory(label: "Agent: \(keys.joined(separator: ", "))"),
                             .beginAutosave, .noteAndFlush, .deleteProposedFile, .clearCompare])

        case .rejected:
            guard case .previewing = phase else { return (phase, []) }
            return (.idle, [.restoreCommitted, .deleteProposedFile, .beginAutosave, .clearCompare])

        case let .vanished(sidecarChanged):
            guard case .previewing = phase else { return (phase, []) }
            var actions: [Action] = sidecarChanged
                ? [.restoreSidecar, .recordHistory(label: "Agent: approved in chat")]
                : [.restoreCommitted]
            actions += [.clearCompare, .beginAutosave]
            return (.idle, actions)

        case .switchedAway:
            guard case .previewing = phase else { return (phase, []) }
            return (.idle, [.restoreCommitted, .clearCompare, .beginAutosave])
        }
    }
}
