// `Proposal`'s pure logic: the on-disk path, the changed-keys summary, the
// `current.json` encoding, and the phase machine `ProposalWatcher` interprets.
// `ProposalWatcher` itself opens no file and touches no `Engine` here — it is
// app-target only, exercised by hand (see the session report for how the live
// preview was verified).

import Foundation

extension ViewportTests {

    static func testProposalProposedURLBesideTheRaw() {
        let raw = URL(fileURLWithPath: "/shoot/_PIC8095.ARW")
        let got = Proposal.proposedURL(for: raw)
        report(got.path == "/shoot/_PIC8095.proposed.json",
               "the proposed file sits beside the raw, stem plus .proposed.json",
               got.path)
    }

    static func testProposalChangedKeysNamesWhatDiffers() {
        let base = DevelopState()
        var proposed = DevelopState()
        proposed.exposureEv = 1.0
        proposed.contrast = 1.6
        guard let baseData = try? JSONEncoder().encode(base),
              let proposedData = try? JSONEncoder().encode(proposed) else {
            report(false, "both states should encode")
            return
        }
        let keys = Proposal.changedKeys(base: baseData, proposed: proposedData)
        report(keys == ["contrast", "exposureEv"],
               "names exactly the fields that differ, sorted", "\(keys)")
    }

    static func testProposalChangedKeysIsEmptyForIdenticalStates() {
        guard let data = try? JSONEncoder().encode(DevelopState()) else {
            report(false, "DevelopState() should encode")
            return
        }
        let keys = Proposal.changedKeys(base: data, proposed: data)
        report(keys.isEmpty, "two identical states change nothing", "\(keys)")
    }

    static func testProposalSummaryTruncatesAtFive() {
        let keys = ["blacks", "contrast", "exposureEv", "highlights", "shadows", "vibrance", "whites"]
        let got = Proposal.summary(keys: keys)
        report(got == "Proposal: blacks, contrast, exposureEv, highlights, shadows +2",
               "shows the first five keys and counts the rest", got)
    }

    static func testProposalSummaryListsFewKeysInFull() {
        let got = Proposal.summary(keys: ["exposureEv", "contrast"])
        report(got == "Proposal: exposureEv, contrast",
               "two keys need no +N tail", got)
    }

    static func testProposalCurrentJSONEncodesAnOpenPhoto() {
        let photo = URL(fileURLWithPath: "/shoot/_PIC8095.ARW")
        let now = Date(timeIntervalSince1970: 1_700_000_000)
        let data = Proposal.currentJSON(photo: photo, now: now)
        guard let doc = try? JSONDecoder().decode(Proposal.CurrentPhoto.self, from: data) else {
            report(false, "currentJSON should decode back")
            return
        }
        report(doc.photo == "/shoot/_PIC8095.ARW", "photo is the absolute path", doc.photo ?? "nil")
        report(doc.folder == "/shoot", "folder is the photo's directory", doc.folder ?? "nil")
        report(ISO8601DateFormatter().date(from: doc.updated) != nil,
               "updated parses as ISO 8601", doc.updated)
    }

    static func testProposalCurrentJSONEncodesNoPhotoAsNull() {
        let data = Proposal.currentJSON(photo: nil)
        guard let doc = try? JSONDecoder().decode(Proposal.CurrentPhoto.self, from: data) else {
            report(false, "currentJSON should decode back")
            return
        }
        report(doc.photo == nil, "no open photo encodes photo as null")
        report(doc.folder == nil, "no open photo encodes folder as null")
    }

    static func testProposalCurrentFileURLSitsUnderOrion() {
        let support = URL(fileURLWithPath: "/Users/x/Library/Application Support")
        let got = Proposal.currentFileURL(applicationSupport: support)
        report(got.path == "/Users/x/Library/Application Support/Orion/current.json",
               "current.json sits beside index.sqlite3, under an Orion folder", got.path)
    }

    /// Appeared and changed both arm the same four actions, in order, and put
    /// the phase into `.previewing` with the keys named — `ProposalWatcher`
    /// tells the two apart only to decide whether it is re-reading the same
    /// baseline or a fresh one; the interpreter's job is identical either way.
    static func testProposalAppearedEntersPreviewing() {
        let (phase, actions) = Proposal.transition(.idle, on: .appeared(keys: ["exposureEv"]))
        report(phase == .previewing(keys: ["exposureEv"]),
               "appearing enters previewing with its keys", "\(phase)")
        report(actions == [.captureOriginal, .stopAutosave, .restoreProposed, .setCompare],
               "captures, stops autosave, restores, then splits the compare", "\(actions)")
    }

    static func testProposalChangedWhileIdleIsANoOp() {
        let (phase, actions) = Proposal.transition(.idle, on: .changed(keys: ["exposureEv"]))
        report(phase == .idle, "a rewrite with nothing previewing does not start a preview")
        report(actions.isEmpty, "and performs nothing", "\(actions)")
    }

    static func testProposalApprovedRecordsAndClearsToIdle() {
        let start = Proposal.Phase.previewing(keys: ["exposureEv", "contrast"])
        let (phase, actions) = Proposal.transition(start, on: .approved)
        report(phase == .idle, "approving returns to idle")
        report(actions == [.recordHistory(label: "Agent: exposureEv, contrast"),
                            .beginAutosave, .noteAndFlush, .deleteProposedFile, .clearCompare],
               "records one history entry naming the keys, writes the sidecar once, "
                   + "deletes the file, clears the split", "\(actions)")
    }

    static func testProposalRejectedRestoresCommitted() {
        let start = Proposal.Phase.previewing(keys: ["exposureEv"])
        let (phase, actions) = Proposal.transition(start, on: .rejected)
        report(phase == .idle, "rejecting returns to idle")
        report(actions == [.restoreCommitted, .deleteProposedFile, .beginAutosave, .clearCompare],
               "restores the committed state, deletes the file, resumes autosave, clears the split",
               "\(actions)")
    }

    /// The model called `approve_edit` in chat: the sidecar now holds
    /// something other than what Orion committed before the proposal
    /// appeared, so Orion adopts it and records one entry — never silently.
    static func testProposalVanishedWithSidecarChangeRestoresSidecar() {
        let start = Proposal.Phase.previewing(keys: ["exposureEv"])
        let (phase, actions) = Proposal.transition(start, on: .vanished(sidecarChanged: true))
        report(phase == .idle, "vanishing returns to idle")
        report(actions == [.restoreSidecar, .recordHistory(label: "Agent: approved in chat"),
                            .clearCompare, .beginAutosave],
               "adopts the sidecar, records it, clears the split, resumes autosave", "\(actions)")
    }

    /// The model called `reject_edit` in chat: the sidecar never changed, so
    /// Orion just returns to what it had — no history entry, nothing new to
    /// record.
    static func testProposalVanishedWithoutChangeRestoresCommitted() {
        let start = Proposal.Phase.previewing(keys: ["exposureEv"])
        let (phase, actions) = Proposal.transition(start, on: .vanished(sidecarChanged: false))
        report(phase == .idle, "vanishing returns to idle")
        report(actions == [.restoreCommitted, .clearCompare, .beginAutosave],
               "restores the committed state, clears the split, resumes autosave — no history entry",
               "\(actions)")
    }

    /// Switching photos leaves the proposed file on disk — reject-without-
    /// delete — because it belongs to the photo being left, not to a decision
    /// this session made about it.
    static func testProposalSwitchedAwayLeavesTheFileAlone() {
        let start = Proposal.Phase.previewing(keys: ["exposureEv"])
        let (phase, actions) = Proposal.transition(start, on: .switchedAway)
        report(phase == .idle, "leaving a live proposal returns to idle")
        report(actions == [.restoreCommitted, .clearCompare, .beginAutosave],
               "restores, clears the split, resumes autosave — and no deleteProposedFile",
               "\(actions)")
        report(!actions.contains(.deleteProposedFile),
               "the file is never deleted by switching away")
    }

    /// Approve, reject and vanish while nothing is previewing are no-ops —
    /// `ProposalWatcher` does not have to guard every call site against a
    /// stray event.
    static func testProposalEventsAreNoOpsWhenIdle() {
        for event in [Proposal.Event.approved, .rejected, .vanished(sidecarChanged: true)] {
            let (phase, actions) = Proposal.transition(.idle, on: event)
            report(phase == .idle, "\(event) on idle stays idle")
            report(actions.isEmpty, "\(event) on idle performs nothing", "\(actions)")
        }
    }
}
