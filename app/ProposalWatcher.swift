import Foundation

/// Watches the open photo's folder for `<stem>.proposed.json` appearing,
/// changing or disappearing, and drives `Engine`/`Autosave` through
/// `Proposal`'s phase machine — the interpreter for the policy table in
/// `Proposal.swift`. App-target only: `DispatchSourceFileSystemObject` and
/// `Engine` have no place in `orion-viewport-tests`.
///
/// ⚠ **A singleton, not an `Editor` stored property.** `Editor`'s stored
/// properties all live in `OrionApp.swift`, which this feature does not
/// touch — see the session's file-ownership note. `.shared` observed from
/// the footer works the same way `engine` itself does: SwiftUI's `body`
/// re-renders when a property it reads changes, whether that property lives
/// on a view's own `@State` or on a class instance held anywhere else.
@Observable
final class ProposalWatcher {
    static let shared = ProposalWatcher()
    private init() {}

    private(set) var phase: Proposal.Phase = .idle

    /// True while a proposal is previewing — the develop panels' lockout
    /// reads this (OrionApp+Tools.swift, OrionApp+Chrome.swift) rather than
    /// `phase` directly, so a manual slider drag during a preview has one
    /// place to check rather than a pattern match at every call site.
    /// Autosave is off for the whole time this is true (`.stopAutosave` runs
    /// before anything else `.appeared`/`.changed` do — see the ⚠ in
    /// `Proposal.transition`), so an edit that slipped past this lockout
    /// would be neither saved nor undoable: Reject would discard it
    /// silently. Locking the panels is the honest fix; merging a manual
    /// edit with a live proposal is not attempted.
    var isLive: Bool { if case .previewing = phase { true } else { false } }

    private weak var engine: Engine?
    private weak var autosave: Autosave?
    private var photo: URL?
    /// The state Orion had committed before the live proposal started —
    /// captured once, on the first `appeared`, and used for every
    /// `captureOriginal`/restore this preview does, because by the time a
    /// second `changed` event arrives `engine.state` is the *previous*
    /// proposal, not the true baseline.
    private var committedState: DevelopState?
    private var lastSeenProposed: Data?

    private var fd: Int32 = -1
    private var source: DispatchSourceFileSystemObject?
    private var debounce: DispatchWorkItem?

    private var proposedURL: URL? { photo.map(Proposal.proposedURL(for:)) }

    // MARK: Wiring — called from `Editor.load` and `Editor.runTrash`,
    // wherever `autosave.begin`/`autosave.stop` mark the photo changing.

    /// The photo changed, or closed (`photo == nil`). Leaves whatever was
    /// live for the outgoing photo on disk — reject-without-delete — then
    /// starts watching the new one.
    func attach(photo: URL?, engine: Engine, autosave: Autosave) {
        if case .previewing = phase { fire(.switchedAway) }

        stopWatching()
        self.photo = photo
        self.engine = engine
        self.autosave = autosave
        committedState = nil
        lastSeenProposed = nil
        guard let photo else { return }

        watch(directory: photo.deletingLastPathComponent())
        // A proposal already on disk when the photo opens counts as having
        // just appeared — the MCP may have written it while Orion was
        // elsewhere.
        check()
    }

    // MARK: Footer buttons

    func approve() { fire(.approved) }
    func reject() { fire(.rejected) }

    // MARK: The directory watch

    private func watch(directory: URL) {
        fd = open(directory.path, O_EVTONLY)
        guard fd >= 0 else { return }
        let s = DispatchSource.makeFileSystemObjectSource(
            fileDescriptor: fd, eventMask: [.write, .rename, .delete], queue: .main)
        s.setEventHandler { [weak self] in self?.debounced() }
        let handle = fd
        s.setCancelHandler { close(handle) }
        s.resume()
        source = s
    }

    private func stopWatching() {
        source?.cancel()
        source = nil
        fd = -1
        debounce?.cancel()
        debounce = nil
    }

    /// ~150 ms after the last directory event, so the several notifications
    /// one atomic rename produces settle into a single check.
    private func debounced() {
        debounce?.cancel()
        let work = DispatchWorkItem { [weak self] in self?.check() }
        debounce = work
        DispatchQueue.main.asyncAfter(deadline: .now() + 0.15, execute: work)
    }

    /// Reads the proposed file, if any, and turns what changed into an event
    /// for `Proposal.transition`. A directory event with no bearing on the
    /// proposed file — including the autosave feedback loop `Autosave.note`
    /// warns about, since a sidecar write inside the same folder fires this
    /// same watch — falls through the `default` and does nothing.
    private func check() {
        guard let url = proposedURL, let engine else { return }
        let onDisk = try? Data(contentsOf: url)

        switch (phase, onDisk) {
        case (.idle, .some(let data)):
            let base = engine.state
            committedState = base
            lastSeenProposed = data
            fire(.appeared(keys: Proposal.changedKeys(base: encode(base), proposed: data)),
                 proposedData: data)

        case (.previewing, .some(let data)) where data != lastSeenProposed:
            let base = committedState.map(encode) ?? data
            lastSeenProposed = data
            fire(.changed(keys: Proposal.changedKeys(base: base, proposed: data)),
                 proposedData: data)

        case (.previewing, .none):
            let sidecarDevelop = photo.flatMap(Sidecar.read(for:))?.develop
            let committedEncoded = committedState.map(encode)
            fire(.vanished(sidecarChanged: sidecarDevelop != nil
                                          && sidecarDevelop != committedEncoded))

        default:
            break   // idle with no file, or a directory event with no real change.
        }
    }

    private func encode(_ s: DevelopState) -> Data { (try? JSONEncoder().encode(s)) ?? Data() }

    // MARK: The interpreter

    private func fire(_ event: Proposal.Event, proposedData: Data? = nil) {
        let (next, actions) = Proposal.transition(phase, on: event)
        phase = next
        for action in actions { perform(action, proposedData: proposedData) }
    }

    /// One action from `Proposal.transition`, turned into the real call.
    ///
    /// ⚠ **`.restoreProposed` uses `engine.apply(_:)`, not `engine.restore
    /// (encoded:)`.** `restore(encoded:)` is the open-a-photograph call — it
    /// resets `engine.history` to a single entry, which would erase the
    /// photographer's real undo stack the moment a proposal appears and
    /// leave "ONE Cmd-Z reverts the whole proposal" untrue (undo would land
    /// on the just-reset entry, which already equals the proposal). `apply`
    /// assigns and renders without touching history — the same primitive
    /// `undo`/`redo` themselves use — so the true prior history survives
    /// underneath the preview, and `.recordHistory` on approve appends
    /// exactly one entry on top of it.
    private func perform(_ action: Proposal.Action, proposedData: Data?) {
        guard let engine else { return }
        switch action {
        case .captureOriginal:
            engine.captureOriginal()
        case .stopAutosave:
            autosave?.stop()
        case .restoreProposed:
            guard let proposedData,
                  let decoded = try? JSONDecoder().decode(DevelopState.self, from: proposedData)
            else { return }
            engine.apply(decoded)
        case .setCompare:
            engine.setCompare(split: 0.5)
        case .recordHistory(let label):
            engine.history.record(engine.state, label: label)
        case .beginAutosave:
            guard let photo, let committedState else { return }
            autosave?.begin(url: photo, saved: committedState)
        case .noteAndFlush:
            autosave?.note(engine.state)
            autosave?.flush()
        case .deleteProposedFile:
            if let url = proposedURL { try? FileManager.default.removeItem(at: url) }
            lastSeenProposed = nil
        case .restoreCommitted:
            if let committedState { engine.apply(committedState) }
        case .restoreSidecar:
            guard let photo, let develop = Sidecar.read(for: photo)?.develop,
                  let decoded = try? JSONDecoder().decode(DevelopState.self, from: develop)
            else { return }
            engine.apply(decoded)
        case .clearCompare:
            engine.clearCompare()
        }
    }
}
