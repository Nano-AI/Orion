// Decision #370: `IMG.xmp` is shared with Lightroom, Capture One and darktable,
// and Orion writes only its three fields into it.

import Foundation

extension ViewportTests {

    /// The same fixtures and the same checks as the shell's
    /// `SelfTest.ForeignXmp.cs`, through the Swift writer. The fixtures are
    /// hand-written after XMP Part 1; none is copied from those products.
    static func testForeignXmpSurvivesOrionsWrites() {
        let fixtures = URL(fileURLWithPath: #filePath).deletingLastPathComponent()
            .appendingPathComponent("../spikes/avalonia-shell/Fixtures/foreign")
        let dir = FileManager.default.temporaryDirectory
            .appendingPathComponent("orion-foreign-xmp-\(UUID().uuidString)")
        try? FileManager.default.createDirectory(at: dir, withIntermediateDirectories: true)
        defer { try? FileManager.default.removeItem(at: dir) }
        let photo = dir.appendingPathComponent("photo.ARW")
        let xmp = Sidecar.url(for: photo)
        func onDisk() -> String { (try? String(contentsOf: xmp, encoding: .utf8)) ?? "" }
        let edit = Data(#"{"exposureEv":1}"#.utf8)

        let cases: [(String, Int, String?, String)] = [
            ("lightroom-style.xmp", 2, "Red", #"xmp:Rating="4""#),
            ("captureone-style.xmp", 3, "Green", "<xmp:Rating>4</xmp:Rating>"),
            ("darktable-style.xmp", 1, nil, #"xmp:Rating="4""#),
        ]
        for (file, rating, label, inPlace) in cases {
            guard let original = try? String(contentsOf: fixtures.appendingPathComponent(file),
                                             encoding: .utf8) else {
                report(false, "\(file): the fixture reads")
                continue
            }
            try? original.write(to: xmp, atomically: true, encoding: .utf8)
            let before = Sidecar.read(for: photo)
            report(before?.unreadable == nil && before?.develop == nil
                   && before?.rating == rating && before?.label == label,
                   "\(file): Orion reads its rating and label",
                   "\(String(describing: before))")
            let wrote = Sidecar.merge(into: photo) { $0.rating = 4; $0.develop = Data([1, 2, 3]) }
                && Sidecar.merge(into: photo) { $0.develop = edit }
            let after = onDisk()
            let back = Sidecar.read(for: photo)
            report(wrote && back?.unreadable == nil && back?.rejected == false && back?.rating == 4
                   && back?.label == label && back?.develop == edit && after.contains(inPlace),
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

        let bad: [(String, String)] = [
            (#"<x:xmpmeta xmlns:x="adobe:ns:meta/"><rdf:RDF"#, "XML that is not well-formed"),
            ("<x:xmpmeta xmlns:x=\"adobe:ns:meta/\"><rdf:RDF xmlns:rdf=\"\(rdf)\"/></x:xmpmeta>",
             "an rdf:RDF with no rdf:Description"),
            ("<x:xmpmeta xmlns:x=\"adobe:ns:meta/\"><rdf:RDF xmlns:rdf=\"\(rdf)\"><rdf:Description rdf:about=\"\" "
             + "xmlns:xmp=\"http://ns.adobe.com/xap/1.0/\"><xmp:Rating><rdf:Bag/></xmp:Rating></rdf:Description>"
             + "</rdf:RDF></x:xmpmeta>", "an xmp:Rating that is a structure"),
        ]
        for (text, why) in bad {
            try? text.write(to: xmp, atomically: true, encoding: .utf8)
            var five = Sidecar()
            five.rating = 5
            report(Sidecar.read(for: photo)?.unreadable != nil
                   && !Sidecar.merge(into: photo) { $0.rating = 5 }
                   && !five.write(for: photo) && onDisk() == text,
                   "\(why) reads as unreadable and is never written over")
        }
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
