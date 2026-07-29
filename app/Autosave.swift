import Foundation

/// Writes the open photo's edits to its sidecar without being asked.
///
/// `saveDevelop` used to run in exactly one place: switching photos. Edit a
/// frame, quit — ⌘Q, a crash, a logout — reopen, and every adjustment since the
/// last switch was gone, silently. For the common case of opening one file,
/// working on it and quitting, that was *all* of the work. The sidecar
/// machinery was complete and correct the whole time; nothing called it at the
/// moment that mattered.
///
/// Two triggers, because they fail differently. Coalesced writes catch the
/// crash, the logout and the flat battery; `flush()` on `willTerminate` catches
/// the ⌘Q that arrives inside the coalescing window.
///
/// **The queued state travels with the URL it belongs to.** That is the whole
/// reason this is a type rather than a timer next to `saveDevelop()`. Reading
/// the engine when the timer fires would race the ~50 ms decode in
/// `Editor.load` — for that window `current` is already the arriving photo
/// while the engine still holds the outgoing photo's adjustments, so a write
/// landing there would file one photo's edits under another's name. Carrying
/// the pair makes that unrepresentable rather than merely unlikely.
///
/// Last note wins, and that matters: `Engine.captureOriginal` applies a neutral
/// state and then the real one, back to back on the main thread, so a queue
/// that appended rather than replaced would persist the neutral.
///
/// No SwiftUI and no AppKit here on purpose — the lifecycle notifications are
/// the shell's job (`Editor`), which keeps this compilable into the viewport
/// test binary where the invariant is actually checked.
final class Autosave {

    /// How a queued write gets deferred. The app hands in a real timer; the
    /// tests hand in a closure they fire by hand, so the coalescing is checked
    /// without putting a sleep in the suite.
    typealias Deferral = (@escaping () -> Void) -> Void

    private let deferral: Deferral
    private let write: (URL, DevelopState) -> Void

    /// The photo being edited, and what its sidecar is already known to hold.
    private var target: URL?
    private var saved: DevelopState?

    /// The write that is owed, with the photo it is owed to.
    private var pending: (url: URL, state: DevelopState)?
    private var scheduled = false

    init(deferral: @escaping Deferral = Autosave.afterSettling,
         write: @escaping (URL, DevelopState) -> Void = Autosave.toSidecar) {
        self.deferral = deferral
        self.write = write
    }

    /// 900 ms. Long enough that a slider drag becomes one write rather than
    /// sixty; short enough that a crash costs the last gesture and not the
    /// session. Measured from the *first* unsaved change, not the last, so a
    /// long continuous drag still checkpoints instead of holding everything
    /// until the mouse comes up.
    static func afterSettling(_ body: @escaping () -> Void) {
        Timer.scheduledTimer(withTimeInterval: 0.9, repeats: false) { _ in body() }
    }

    static func toSidecar(_ url: URL, _ state: DevelopState) {
        guard let encoded = try? JSONEncoder().encode(state) else { return }
        Sidecar.merge(into: url) { $0.develop = encoded }
    }

    // MARK: The photo in hand

    /// A photo is open and settled. `saved` is what its sidecar already holds,
    /// so restoring a photo does not immediately write back what was just read.
    ///
    /// Anything still owed to the previous photo is written first.
    func begin(url: URL, saved: DevelopState) {
        flush()
        target = url
        self.saved = saved
    }

    /// Nothing is open, or a photo is mid-load. Notes are dropped until the
    /// next `begin`: the engine pushes renders while it opens a file, and until
    /// the sidecar has been restored those describe the outgoing photo.
    func stop() {
        flush()
        target = nil
        saved = nil
    }

    // MARK: Changes

    /// The engine's adjustments changed. Cheap enough to call on every render:
    /// it copies a struct of floats and compares it against the last one saved.
    func note(_ state: DevelopState) {
        guard let target, state != saved else { return }
        pending = (target, state)

        guard !scheduled else { return }
        scheduled = true
        deferral { [weak self] in self?.flush() }
    }

    /// Writes what is owed, now. Quit, app switch, and photo switch all land
    /// here. A no-op when nothing changed, so it is safe to call often.
    func flush() {
        // Clear the arm even when there is nothing to write, so the invariant
        // holds both ways: something owed always has a deferral outstanding.
        // An out-of-band flush would otherwise leave the flag set, and the next
        // edit would wait on the *old* deferral instead of its own.
        scheduled = false

        guard let job = pending else { return }
        pending = nil
        write(job.url, job.state)

        // Only the photo still in hand gets its baseline moved. A late write
        // for a photo already left must not teach us anything about this one.
        if job.url == target { saved = job.state }
    }

    /// For tests and for the panel: whether a write is owed right now.
    var isDirty: Bool { pending != nil }
}
