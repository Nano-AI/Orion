// `agent matte` and `agent sync`'s pure halves: parsing, the group-to-key
// map, the patch and the commit blob. The drivers open real photographs and
// are smoke-tested by hand, as `ViewportTests+Agent.swift` says of the rest.

import Foundation

extension ViewportTests {

    /// Expects `args` to be refused as a usage error whose message names
    /// `mentions`.
    private static func expectUsage(_ args: [String], mentions: String, _ name: String) {
        do {
            _ = try AgentCLI.parse(args)
            report(false, name, "parsed")
        } catch AgentCLI.Failure.usage(let message) {
            report(message.contains(mentions), name, message)
        } catch {
            report(false, name, "wrong error \(error)")
        }
    }

    static func testAgentParsesMatte() {
        let command = try? AgentCLI.parse(
            ["--agent", "matte", "/a.arw", "--kind", "person", "--state", "/s.json"])
        report(command == .matte(raw: "/a.arw", kind: "person", state: "/s.json"),
               "parses matte with --kind and --state", "\(String(describing: command))")
        expectUsage(["--agent", "matte", "/a.arw"], mentions: "subject|person|sky",
                    "matte without --kind is a usage error naming the three kinds")
        expectUsage(["--agent", "matte", "/a.arw", "--kind", "dog"], mentions: "subject|person|sky",
                    "an unknown --kind is a usage error")
        expectUsage(["--agent", "matte", "/a.arw", "--kind", "sky", "--out", "/o"],
                    mentions: "out", "matte does not take --out")
    }

    static func testAgentParsesSync() {
        let bare = try? AgentCLI.parse(["--agent", "sync", "/a.arw", "--targets", "/t.json"])
        report(bare == .sync(source: "/a.arw", targets: "/t.json",
                             groups: AgentSync.Group.defaults, state: nil, mode: .propose),
               "sync defaults to the product's groups and to propose",
               "\(String(describing: bare))")
        let full = try? AgentCLI.parse(
            ["--agent", "sync", "/a.arw", "--targets", "/t.json", "--groups",
             "whiteBalance,masks,masks", "--mode", "commit", "--state", "/s.json"])
        report(full == .sync(source: "/a.arw", targets: "/t.json",
                             groups: [.preset(.whiteBalance), .masks], state: "/s.json",
                             mode: .commit),
               "parses groups in order, repeats dropped, and --mode commit",
               "\(String(describing: full))")
        expectUsage(["--agent", "sync", "/a.arw"], mentions: "--targets",
                    "sync without --targets is a usage error")
        expectUsage(["--agent", "sync", "/a.arw", "--targets", "/t", "--groups", "light,tone"],
                    mentions: "tone", "an unknown group is a usage error naming it")
        expectUsage(["--agent", "sync", "/a.arw", "--targets", "/t", "--groups", "colour"],
                    mentions: "colour", "the legacy colour spelling is not a group name")
        expectUsage(["--agent", "sync", "/a.arw", "--targets", "/t", "--mode", "apply"],
                    mentions: "propose or commit", "a bad --mode is a usage error")

        func targets(_ text: String) -> [String]? {
            try? AgentSync.parseTargets(Data(text.utf8))
        }
        report(targets(#"["/a.arw", "/b.arw"]"#) == ["/a.arw", "/b.arw"], "reads a targets array")
        report(targets("[]") == nil, "an empty targets array is refused")
        report(targets(#"["a.arw"]"#) == nil, "a relative target is refused")
        report(targets(#"["/a.arw", 3]"#) == nil, "a non-string target is refused")
        report((try? AgentSync.parseTargets(nil)) == nil, "an unreadable targets file is refused")
    }

    /// ⚠ The six are `SyncSettings.keys(for:)` itself, so a field added to a
    /// product group can never be left out of the agent's sync.
    static func testAgentSyncGroupsOwnTheProductKeys() {
        for group in PresetGroup.allCases {
            report(AgentSync.Group.preset(group).keys == SyncSettings.keys(for: group),
                   "agent group \(group.rawValue) owns exactly the product's keys")
            report(AgentSync.Group(name: group.rawValue) == .preset(group),
                   "\(group.rawValue) parses by its raw value")
        }
        report(AgentSync.Group.masks.keys
                == ["maskComponents", "layers", "maskRefine", "maskSpace", "localTintAxis"],
               "masks owns the mask rows, the layers, the refine and their two markers",
               "\(AgentSync.Group.masks.keys)")
        report(Set(AgentSync.Group.defaults) == Set(PresetGroup.defaultSelection.map {
                   AgentSync.Group.preset($0) }),
               "the default groups are the product's default selection")
        let roster = DevelopState.fieldRoster
        let unknown = AgentSync.Group.all.flatMap(\.keys).filter { !roster.contains($0) }
        report(unknown.isEmpty, "every key a group owns is a DevelopState key", "\(unknown)")
    }

    static func testAgentSyncPatchCopiesOnlyChosenKeys() {
        var source = DevelopState()
        source.exposureEv = 1.25
        source.temperatureK = 3200
        guard let data = try? JSONEncoder().encode(source),
              let object = try? JSONSerialization.jsonObject(with: data) as? [String: Any]
        else { return report(false, "source encodes") }
        let patch = AgentSync.patch(source: object,
                                    keys: AgentSync.keys(for: [.preset(.light)]))
        report((patch["exposureEv"] as? NSNumber)?.doubleValue == 1.25,
               "a light patch carries exposureEv")
        report(patch["temperatureK"] == nil, "and never temperatureK")
        report(Set(patch.keys) == Set(SyncSettings.keys(for: .light)),
               "and nothing outside the group", "\(patch.keys.sorted())")

        // The matte rows and their rewrite: a row's id becomes the target's.
        var row = MaskComponentState()
        row.kind = 4
        row.matteId = "source-id"
        row.matteSource = "Person"
        var radial = MaskComponentState()
        radial.kind = 2
        source.maskComponents = [radial, row]
        guard let masked = (try? JSONEncoder().encode(source))
                .flatMap({ try? JSONSerialization.jsonObject(with: $0) as? [String: Any] })
        else { return report(false, "masked source encodes") }
        let rows = AgentSync.matteRows(masked)
        report(rows.map(\.row) == [1] && rows.first?.matteSource == "Person",
               "matteRows finds the kind-4 row and its producer")
        report(AgentSync.producer(matteSource: "person") == "person"
                && AgentSync.producer(matteSource: "Brush") == nil
                && AgentSync.producer(matteSource: nil) == nil,
               "a producer is read case-insensitively and nothing else is one")
        let rewritten = AgentSync.rewritingMatte(masked, row: 1, id: "target-id", label: "Person")
        let after = rewritten["maskComponents"] as? [[String: Any]]
        report(after?[1]["matteId"] as? String == "target-id"
                && after?[0]["kind"] as? Int == 2,
               "the rewrite names the target's matte and leaves other rows alone")
    }

    /// ⚠ The white-balance trap SyncSettings' header describes: a photograph
    /// with no sidecar must not gain a white balance nobody chose. And the
    /// two markers it shares a shape with (`process`, `maskSpace`) must be
    /// written beside what they gate, or a no-sidecar target reads the
    /// source's numbers as legacy ones.
    static func testAgentSyncCommitLeavesAbsentWhiteBalanceAbsent() {
        var source = DevelopState()
        source.highlights = -0.5
        source.temperatureK = 3200
        var radial = MaskComponentState()
        radial.kind = 2
        source.maskComponents = [radial]
        guard let data = try? JSONEncoder().encode(source),
              let object = try? JSONSerialization.jsonObject(with: data) as? [String: Any]
        else { return report(false, "source encodes") }

        let keys = AgentSync.keys(for: [.preset(.light), .masks])
        let blob = AgentSync.committed(stored: nil, source: object, keys: keys)
        let written = blob.flatMap { try? JSONSerialization.jsonObject(with: $0) as? [String: Any] }
        report(written != nil && written?["temperatureK"] == nil && written?["tint"] == nil,
               "a commit onto no sidecar writes no white balance",
               "\(written?.keys.sorted() ?? [])")
        let decoded = blob.flatMap { try? JSONDecoder().decode(DevelopState.self, from: $0) }
        report(decoded?.process == source.process,
               "and the tone sliders keep the source's generation",
               "\(String(describing: decoded?.process))")
        report(decoded?.maskSpace == 1, "and the mask rows stay frame-space",
               "\(String(describing: decoded?.maskSpace))")

        // A target that already states its own generation keeps it.
        let stored = try? JSONSerialization.data(withJSONObject: ["process": 1, "exposureEv": 0.5])
        let kept = AgentSync.committed(stored: stored, source: object, keys: keys)
            .flatMap { try? JSONSerialization.jsonObject(with: $0) as? [String: Any] }
        report((kept?["process"] as? NSNumber)?.intValue == 1,
               "a target's own process is never overwritten")
        report((kept?["exposureEv"] as? NSNumber)?.doubleValue == source.exposureEv.asDouble,
               "and the chosen keys still land on it")
    }
}

private extension Float {
    var asDouble: Double { Double(self) }
}
