import Foundation

/// `Orion --agent matte <raw> --kind subject|person|sky [--state <json>]` -
/// the agent's Subject, Person and Sky buttons. A model cannot paint a kind-4
/// row (`AgentComposite` refuses one without a matte on disk), so before this
/// verb an agent could only ever reference a selection the photographer had
/// already made by hand.
///
/// ⚠ **It produces a file and a row, and changes nothing else.** The matte goes
/// beside the photograph through `MatteStore.write`, the product's own write
/// (DevelopPanels+Mask.swift:74), and the row comes back as JSON for
/// `propose_edit`'s `maskComponents`; neither the sidecar nor the proposed file
/// is touched. A matte nothing names yet is an orphan, and it survives exactly
/// as long as one the panel wrote and never committed: until the sweep on the
/// next open, unless a proposal names it by then (`Proposal.pinnedMattes`).
extension AgentCLI {

    /// `--kind`'s value as the producer the panel runs.
    @MainActor
    static func matteKind(_ name: String) -> SubjectMatte.Kind? {
        switch name {
        case "subject": return .subject
        case "person": return .person
        case "sky": return .sky
        default: return nil
        }
    }

    /// The share of the matte that is selected - alpha above one half, the
    /// same threshold the producer's own "nothing found" test counts
    /// (SubjectMatte.swift:204).
    static func coverage(_ alpha: [Float]) -> Double {
        guard !alpha.isEmpty else { return 0 }
        return Double(alpha.reduce(0) { $1 > 0.5 ? $0 + 1 : $0 }) / Double(alpha.count)
    }

    /// A complete kind-4 row naming the matte, round-tripped through
    /// `JSONEncoder` from a real `MaskComponentState` the way
    /// `AgentVocabulary.compositeKeysJSON` builds its examples, so it is
    /// exactly the shape `AgentComposite` accepts and cannot drift from the
    /// struct. `startsLayer` because a row on its own is a layer on its own.
    static func matteComponentJSON(id: String, label: String) throws -> [String: Any] {
        var row = MaskComponentState()
        row.kind = 4
        row.matteId = id
        row.matteSource = label
        row.startsLayer = true
        guard let object = try JSONSerialization.jsonObject(
            with: JSONEncoder().encode(row)) as? [String: Any] else {
            throw Failure.run("the mask row is not a JSON object")
        }
        return object
    }

    @MainActor
    static func runMatte(raw: String, kind name: String, state: String?) throws -> [String: Any] {
        guard let kind = matteKind(name) else {
            throw Failure.usage("matte needs --kind \(matteKinds.joined(separator: "|"))")
        }
        let url = URL(fileURLWithPath: raw)
        // The same open `faces` makes: sidecar, then `--state`, then the
        // mattes they name - so the producer sees the picture being edited.
        let engine = try openEngine(url: url, sidecarDevelop: Sidecar.read(for: url)?.develop,
                                    state: state)
        let matte: (alpha: [Float], width: Int, height: Int)
        do {
            matte = try SubjectMatte.generateBlocking(engine: engine, kind: kind)
        } catch let failure as SubjectMatte.Failure {
            // ⚠ An empty answer from the model is an answer, not a failure -
            // how `faces` treats an empty array. `.vision` is also how the sky
            // detector says "no sky" (SubjectMatte.swift:145).
            switch failure {
            case .nothingFound, .vision:
                return ["path": raw, "found": false, "kind": name,
                        "reason": failure.localizedDescription]
            case .noPhoto, .couldNotRender:
                throw Failure.run(failure.localizedDescription)
            }
        }
        let id = try MatteStore.write(matte.alpha, width: matte.width, height: matte.height,
                                      photo: url)
        return ["path": raw, "found": true, "kind": name, "matteId": id,
                "matteSource": kind.label, "width": matte.width, "height": matte.height,
                "coverage": coverage(matte.alpha),
                "component": try matteComponentJSON(id: id, label: kind.label)]
    }
}
