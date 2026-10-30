// Decision #370: `IMG.xmp` is shared with Lightroom, Capture One and darktable,
// and Orion writes only its three fields into it.

import Foundation

extension ViewportTests {

    /// The same fixtures and the same checks as the shell's
    /// `SelfTest.ForeignXmp.cs`, through the Swift writer. The fixtures are
    /// hand-written after XMP Part 1; none is copied from those products.
    static func testForeignXmpSurvivesOrionsWrites() {
        let fixtures = URL(fileURLWithPath: #filePath).deletingLastPathComponent()
            .appendingPathComponent("../shell/Fixtures/foreign")
        let dir = FileManager.default.temporaryDirectory
            .appendingPathComponent("orion-foreign-xmp-\(UUID().uuidString)")
        try? FileManager.default.createDirectory(at: dir, withIntermediateDirectories: true)
        defer { try? FileManager.default.removeItem(at: dir) }
        let photo = dir.appendingPathComponent("photo.ARW")
        let xmp = Sidecar.url(for: photo)
        func onDisk() -> String { (try? String(contentsOf: xmp, encoding: .utf8)) ?? "" }
        let edit = Data(#"{"exposureEv":1}"#.utf8)
        let edit64 = "eyJleHBvc3VyZUV2IjoxfQ=="

        let cases: [(String, Int, String?, Bool, [String])] = [
            ("lightroom-style.xmp", 2, "Red", false, [#"xmp:Rating="4""#]),
            ("captureone-style.xmp", 3, "Green", false, ["<xmp:Rating>4</xmp:Rating>"]),
            ("darktable-style.xmp", 1, nil, false, [#"xmp:Rating="4""#]),
            // Ancestors bind orion and orion1 elsewhere: Orion's goes in as orion2, nothing rebound.
            ("prefix-collision.xmp", 2, nil, false,
             [#"xmlns:orion2="http://orion.photo/ns/1.0/""#, "orion2:Develop=\"\(edit64)\""]),
            ("develop-element.xmp", 5, nil, true,
             ["<xmp:Rating>4</xmp:Rating>", "<orion:Develop>\(edit64)</orion:Develop>"]),
            // The same field in two rdf:Descriptions: every occurrence changes.
            ("split-descriptions.xmp", 1, "Blue", false,
             [#"xmp:Rating="4""#, "<xmp:Rating>4</xmp:Rating>"]),
        ]
        for (file, rating, label, develop, inPlace) in cases {
            guard let original = try? String(contentsOf: fixtures.appendingPathComponent(file),
                                             encoding: .utf8) else {
                report(false, "\(file): the fixture reads")
                continue
            }
            try? original.write(to: xmp, atomically: true, encoding: .utf8)
            let before = Sidecar.read(for: photo)
            report(before?.unreadable == nil && (before?.develop != nil) == develop
                   && before?.rating == rating && before?.label == label,
                   "\(file): Orion reads its rating, label and edit",
                   "\(String(describing: before))")
            let wrote = Sidecar.merge(into: photo) { $0.rating = 4; $0.develop = Data([1, 2, 3]) }
                && Sidecar.merge(into: photo) { $0.develop = edit }
            let after = onDisk()
            let back = Sidecar.read(for: photo)
            report(wrote && back?.unreadable == nil && back?.rejected == false && back?.rating == 4
                   && back?.label == label && back?.develop == edit
                   && inPlace.allSatisfy { after.contains($0) },
                   "\(file): a rating and two edits land, the rating where it already was, and read back",
                   after)
            report(foreignNodes(original) == foreignNodes(after) && foreignNodes(after) != nil,
                   "\(file): every foreign node is still there and equal")
        }

        // An older Adobe prefix for the same namespace is the same field.
        let rdf = "http://www.w3.org/1999/02/22-rdf-syntax-ns#"
        try? """
        <x:xmpmeta xmlns:x="adobe:ns:meta/"><rdf:RDF xmlns:rdf="\(rdf)"><rdf:Description rdf:about="" \
        xmlns:xap="http://ns.adobe.com/xap/1.0/" xap:Rating="5"/></rdf:RDF></x:xmpmeta>
        """.write(to: xmp, atomically: true, encoding: .utf8)
        report(Sidecar.read(for: photo)?.rating == 5 && Sidecar.merge(into: photo) { $0.rating = 1 }
               && onDisk().contains(#"xap:Rating="1""#) && !onDisk().contains("xmp:"),
               "xap:Rating is xmp:Rating, and is changed under its own prefix", onDisk())

        // Encodings: a UTF-8 BOM is UTF-8; UTF-16 and broken UTF-8 are refused, never re-encoded.
        let lightroom = (try? String(contentsOf: fixtures.appendingPathComponent("lightroom-style.xmp"),
                                     encoding: .utf8)) ?? ""
        let utf8 = Data(lightroom.utf8)
        try? (Data([0xEF, 0xBB, 0xBF]) + utf8).write(to: xmp)
        let bom = Sidecar.read(for: photo)
        report(bom?.unreadable == nil && bom?.rating == 2 && bom?.label == "Red"
               && Sidecar.merge(into: photo) { $0.rating = 3 }
               && Sidecar.read(for: photo)?.rating == 3 && Sidecar.read(for: photo)?.label == "Red",
               "a UTF-8 byte-order mark reads and writes as UTF-8")
        let title = utf8.range(of: Data("Bridge".utf8))?.lowerBound ?? 0

        let structure = "<x:xmpmeta xmlns:x=\"adobe:ns:meta/\"><rdf:RDF xmlns:rdf=\"\(rdf)\">"
            + "<rdf:Description rdf:about=\"\" xmlns:xmp=\"http://ns.adobe.com/xap/1.0/\">"
            + "<xmp:Rating><rdf:Bag/></xmp:Rating></rdf:Description></rdf:RDF></x:xmpmeta>"
        let bad: [(Data, String)] = [
            (Data(#"<x:xmpmeta xmlns:x="adobe:ns:meta/"><rdf:RDF"#.utf8), "XML that is not well-formed"),
            (Data("<x:xmpmeta xmlns:x=\"adobe:ns:meta/\"><rdf:RDF xmlns:rdf=\"\(rdf)\"/></x:xmpmeta>".utf8),
             "an rdf:RDF with no rdf:Description"),
            (Data(structure.utf8), "an xmp:Rating that is a structure"),
            (Data([0xFF, 0xFE]) + (lightroom.data(using: .utf16LittleEndian) ?? Data()), "a UTF-16 file"),
            (utf8[..<title] + Data([0xFF]) + utf8[title...], "a file that is not valid UTF-8"),
        ]
        for (bytes, why) in bad {
            try? bytes.write(to: xmp)
            var five = Sidecar()
            five.rating = 5
            report(Sidecar.read(for: photo)?.unreadable != nil
                   && !Sidecar.merge(into: photo) { $0.rating = 5 }
                   && !five.write(for: photo) && (try? Data(contentsOf: xmp)) == bytes,
                   "\(why) reads as unreadable and is never written over")
        }

        // Not base64 is unreadable, as in the shell: a rating must not delete it.
        try? Data(Sidecar(rating: 2).text()
            .replacingOccurrences(of: "xmp:Rating", with: #"orion:Develop="!!" xmp:Rating"#).utf8).write(to: xmp)
        report(Sidecar.read(for: photo)?.unreadable != nil
               && !Sidecar.merge(into: photo) { $0.rating = 5 } && onDisk().contains("!!"),
               "an orion:Develop that is not base64 is never written over")
    }

    /// The document without Orion's three fields, namespace declarations
    /// (Orion may add `xmlns:orion`; names stay expanded) or whitespace, as one string.
    private static func foreignNodes(_ text: String) -> String? {
        guard let doc = try? XMLDocument(xmlString: text, options: []) else { return nil }
        let orion = ["http://ns.adobe.com/xap/1.0/ Rating", "http://ns.adobe.com/xap/1.0/ Label",
                     "http://orion.photo/ns/1.0/ Develop"]
        func key(_ n: XMLNode) -> String { "\(n.uri ?? "") \(n.localName ?? "")" }
        func canon(_ n: XMLNode) -> String {
            guard let e = n as? XMLElement else {
                let value = n.stringValue ?? ""
                if n.kind == .text {
                    return value.trimmingCharacters(in: .whitespacesAndNewlines)
                }
                return "<?\(n.kind.rawValue) \(n.name ?? "") \(value)?>"
            }
            let description = key(e) == "http://www.w3.org/1999/02/22-rdf-syntax-ns# Description"
            let attributes = (e.attributes ?? []).filter { !(description && orion.contains(key($0))) }
                .map { "\(key($0))=\($0.stringValue ?? "")" }
            let children = (e.children ?? []).filter { !(description && orion.contains(key($0))) }
                .map(canon)
            return "<\(key(e)) \(attributes.joined(separator: " "))>\(children.joined())</>"
        }
        return (doc.children ?? []).map(canon).joined(separator: "\n")
    }
}
