import Foundation

/// XMP sidecar — the source of truth for everything Orion knows about a photo.
///
/// One `.xmp` next to each raw file. This is deliberate and load-bearing:
/// sidecars are portable, survive the folder being moved or backed up, are
/// readable by Lightroom and darktable, and mean deleting Orion does not delete
/// your work. A catalog database would be faster to query and would own your
/// edits; this does not.
///
/// The format follows the Adobe XMP convention that other editors already read:
/// `xmp:Rating` from -1 to 5, where -1 means rejected, and `xmp:Label` for a
/// color label. Orion's own settings go in a private namespace so they neither
/// confuse other software nor get clobbered by it.
///
/// ⚠ **`IMG.xmp` is shared.** Lightroom Classic, Camera Raw, Capture One and
/// darktable keep their own develop settings, keywords, titles and history in
/// the same file, and every write here used to rebuild it from Orion's three
/// fields — so the first save deleted all of it. Now (decision #370) Orion
/// reads and writes only `xmp:Rating`, `xmp:Label` and `orion:Develop`, with
/// Foundation's XML parser. A file Orion wrote itself is still rewritten byte
/// for byte from `text()`; any other is changed in the tree and every other
/// node kept (`merged`). One it cannot parse, or cannot place its fields in,
/// reads as `unreadable` and is never written. The shell's `Sidecar.cs` is the
/// same algorithm, step for step.
struct Sidecar: Sendable {
    var rating: Int = 0
    var rejected: Bool = false
    var label: String?
    /// Develop settings, encoded as JSON in Orion's namespace.
    var develop: Data?
    /// Why the file is there and Orion cannot read it — not UTF-8, not
    /// well-formed XML, no `rdf:Description` to hold Orion's fields — or nil.
    /// `merge` and `write` refuse such a file rather than replace it.
    var unreadable: String?

    static func url(for photo: URL) -> URL {
        photo.deletingPathExtension().appendingPathExtension("xmp")
    }

    // MARK: Reading

    /// Nil only when there is no sidecar. One that exists and will not read
    /// comes back with `unreadable` set and no values.
    static func read(for photo: URL) -> Sidecar? {
        switch contents(of: url(for: photo)) {
        case .absent: return nil
        case .unreadable(let why): return Sidecar(unreadable: why)
        case .text(let text): return parse(text)
        }
    }

    private enum Contents { case absent, unreadable(String), text(String) }

    /// The file's text, without a leading UTF-8 byte-order mark (the parser and
    /// the byte-exact comparison both want it gone). UTF-8 only, strictly: a
    /// repairing decode would write U+FFFD back over another tool's bytes.
    /// `Sidecar.cs` `Decode` makes the same calls.
    private static func contents(of path: URL) -> Contents {
        guard let data = FileManager.default.contents(atPath: path.path) else {
            return FileManager.default.fileExists(atPath: path.path)
                ? .unreadable("it could not be read") : .absent
        }
        if data.count > 4 << 20 { return .unreadable("larger than 4 MB, which no sidecar is") }
        if data.starts(with: [0xFE, 0xFF]) || data.starts(with: [0xFF, 0xFE]) {
            return .unreadable("it is UTF-16, not UTF-8")
        }
        guard let text = String(data: data, encoding: .utf8) else {
            return .unreadable("it is not UTF-8")
        }
        return .text(text.hasPrefix("\u{FEFF}") ? String(text.dropFirst()) : text)
    }

    /// Orion's three fields out of any XMP packet, by namespace rather than by
    /// prefix (an older Adobe file says `xap:` for `xmp:`), as an attribute or
    /// an element of the same name: XMP allows either (XMP Part 1, serialization), and
    /// editors write them interchangeably.
    static func parse(_ text: String) -> Sidecar {
        let loaded = load(text)
        guard let descriptions = loaded.descriptions else { return Sidecar(unreadable: loaded.why) }
        var result = Sidecar()
        if let rating = value(xmpNS, "Rating", in: descriptions)
               .flatMap({ Double($0.trimmingCharacters(in: .whitespacesAndNewlines)) }),
           rating.isFinite {
            // -1 is the convention for rejected, which every editor understands.
            result.rejected = rating < 0
            result.rating = Int(min(max(rating, 0), 5))
        }
        result.label = value(xmpNS, "Label", in: descriptions)
        if let encoded = value(orionNS, "Develop", in: descriptions) {
            // Not base64 is unreadable, as in the shell: read as "no edit", the
            // next rating merge would delete it.
            result.develop = Data(base64Encoded: encoded)
            if result.develop == nil { result.unreadable = "orion:Develop is not base64" }
        }
        return result
    }

    private static let rdfNS = "http://www.w3.org/1999/02/22-rdf-syntax-ns#"
    private static let xmpNS = "http://ns.adobe.com/xap/1.0/"
    private static let orionNS = "http://orion.photo/ns/1.0/"

    /// The document and the top-level `rdf:Description` elements of its one
    /// `rdf:RDF` (XMP Part 1, serialization), or why Orion cannot safely put its fields
    /// among them: not well-formed, a DTD, no or several `rdf:RDF`, no
    /// `rdf:Description`, or one of its fields held as a structure, not a value.
    private static func load(_ text: String)
        -> (doc: XMLDocument?, descriptions: [XMLElement]?, why: String) {
        let doc: XMLDocument
        do {
            doc = try XMLDocument(xmlString: text, options: [.nodePreserveWhitespace,
                                                             .nodePreserveCDATA, .nodePreserveEmptyElements,
                                                             .nodeLoadExternalEntitiesNever])
        } catch {
            let first = error.localizedDescription.split(separator: "\n").first ?? ""
            return (nil, nil, "not well-formed XML: \(first)")
        }
        if doc.dtd != nil { return (nil, nil, "not well-formed XML: it has a DTD") }
        func below(_ e: XMLElement) -> [XMLElement] {
            (e.children ?? []).compactMap { $0 as? XMLElement }.flatMap { [$0] + below($0) }
        }
        let rdf = (doc.rootElement().map { [$0] + below($0) } ?? [])
            .filter { $0.localName == "RDF" && $0.uri == rdfNS }
        let descriptions = rdf.count == 1
            ? rdf[0].elements(forLocalName: "Description", uri: rdfNS) : []
        let fields = [(xmpNS, "Rating"), (xmpNS, "Label"), (orionNS, "Develop")]
        let odd = descriptions.flatMap { d in fields.flatMap { d.elements(forLocalName: $0.1, uri: $0.0) } }
            .first { e in (e.children ?? []).contains { $0.kind == .element } || !(e.attributes ?? []).isEmpty }
        let why = rdf.count != 1 ? "\(rdf.count) rdf:RDF elements where XMP has one"
            : descriptions.isEmpty ? "no rdf:Description to hold Orion's fields"
            : odd.map { "\($0.localName ?? "?") held as a structure, not a value" } ?? ""
        return why.isEmpty ? (doc, descriptions, "") : (nil, nil, why)
    }

    /// The first value found, attribute or element.
    private static func value(_ uri: String, _ name: String, in descriptions: [XMLElement]) -> String? {
        for d in descriptions {
            if let a = d.attribute(forLocalName: name, uri: uri) { return a.stringValue ?? "" }
            if let e = d.elements(forLocalName: name, uri: uri).first {
                return (e.stringValue ?? "").trimmingCharacters(in: .whitespacesAndNewlines)
            }
        }
        return nil
    }

    // MARK: Writing

    /// Writes these fields over whatever is already on disk, keeping the rest.
    ///
    /// The two writers touch different halves — culling writes the rating and
    /// the flag, editing writes the develop state — and each used to rebuild
    /// the whole file from its own half. Rating a photo therefore erased its
    /// edits, silently, on a keystroke.
    @discardableResult
    static func merge(into photo: URL, _ change: (inout Sidecar) -> Void) -> Bool {
        var sidecar = Sidecar.read(for: photo) ?? Sidecar()
        if let why = sidecar.unreadable {
            return refuse(photo, "it cannot be read (\(why)), and is left untouched")
        }
        change(&sidecar)
        return sidecar.write(for: photo)
    }

    /// The file Orion writes when the sidecar is its own: the BOM inside the
    /// xpacket, LF, no final newline. `Sidecar.cs` `Text()` is the same bytes.
    func text() -> String {
        let effectiveRating = rejected ? -1 : rating

        var attributes = ["xmp:Rating=\"\(effectiveRating)\""]
        if let label, !label.isEmpty {
            attributes.append("xmp:Label=\"\(Sidecar.escapeXml(label))\"")
        }
        if let develop {
            attributes.append("orion:Develop=\"\(develop.base64EncodedString())\"")
        }

        return """
        <?xpacket begin="\u{FEFF}" id="W5M0MpCehiHzreSzNTczkc9d"?>
        <x:xmpmeta xmlns:x="adobe:ns:meta/" x:xmptk="Orion">
         <rdf:RDF xmlns:rdf="http://www.w3.org/1999/02/22-rdf-syntax-ns#">
          <rdf:Description rdf:about=""
            xmlns:xmp="http://ns.adobe.com/xap/1.0/"
            xmlns:orion="http://orion.photo/ns/1.0/"
            \(attributes.joined(separator: "\n    "))/>
         </rdf:RDF>
        </x:xmpmeta>
        <?xpacket end="w"?>
        """
    }

    /// `existing`, another tool's packet, with only Orion's three fields
    /// changed: each is set where it already is, removed when Orion has no
    /// value, and otherwise added as an attribute of the first
    /// `rdf:Description`, declaring `xmp`/`orion` there if its namespace is not
    /// in scope (a numbered prefix if that one is taken). Nil text, and why,
    /// when the file cannot be parsed or has no place for them.
    func merged(into existing: String) -> (text: String?, why: String) {
        let loaded = Sidecar.load(existing)
        guard let doc = loaded.doc, let descriptions = loaded.descriptions else {
            return (nil, loaded.why)
        }
        let fields: [(uri: String, prefix: String, name: String, value: String?)] = [
            (Sidecar.xmpNS, "xmp", "Rating", String(rejected ? -1 : rating)),
            (Sidecar.xmpNS, "xmp", "Label", label?.isEmpty == false ? label : nil),
            (Sidecar.orionNS, "orion", "Develop", develop?.base64EncodedString()),
        ]
        for field in fields {
            var found = false
            for d in descriptions {
                if let a = d.attribute(forLocalName: field.name, uri: field.uri) {
                    found = true
                    if let value = field.value { a.stringValue = value }
                    else { d.removeAttribute(forName: a.name ?? "") }
                }
                for e in d.elements(forLocalName: field.name, uri: field.uri) {
                    found = true
                    if let value = field.value { e.stringValue = value } else { e.detach() }
                }
            }
            guard let value = field.value, !found else { continue }
            let first = descriptions[0]
            var prefix = first.resolvePrefix(forNamespaceURI: field.uri) ?? ""
            if prefix.isEmpty {
                // Never rebind a prefix the file already uses, here or on an
                // ancestor (`resolveNamespace` walks them; checked by test):
                // `orion1`, `orion2`… instead, as the shell does.
                prefix = field.prefix
                var n = 1
                while first.resolveNamespace(forName: "\(prefix):x") != nil {
                    prefix = "\(field.prefix)\(n)"
                    n += 1
                }
                first.addNamespace(XMLNode.namespace(withName: prefix,
                                                     stringValue: field.uri) as! XMLNode)
            }
            first.addAttribute(XMLNode.attribute(withName: "\(prefix):\(field.name)",
                                                 uri: field.uri, stringValue: value) as! XMLNode)
        }
        // Top-level nodes one per line; a declaration, if there was one, says
        // UTF-8 because that is what is written. `Sidecar.cs` does the same.
        let declaration = existing.hasPrefix("<?xml ") ? "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n" : ""
        let nodes = (doc.children ?? []).map {
            $0.xmlString(options: [.nodePreserveWhitespace, .nodePreserveCDATA, .nodePreserveEmptyElements])
        }
        return (declaration + nodes.joined(separator: "\n"), "")
    }

    /// ⚠ **Returns false rather than only logging.** This used to swallow the
    /// error into `NSLog` and return, which reads as success to every caller —
    /// and the caller that mattered was `Autosave.flush`, which then moved its
    /// baseline forward and never tried again. A read-only volume, a full disk
    /// or a folder the sandbox lost access to therefore cost the whole session's
    /// edits, with the only trace in a log nobody has open.
    ///
    /// A file on disk that Orion did not write keeps everything but Orion's
    /// fields (`merged`); one it cannot read or parse is refused, not replaced.
    @discardableResult
    func write(for photo: URL) -> Bool {
        let path = Sidecar.url(for: photo)
        var xml = text()
        switch Sidecar.contents(of: path) {
        case .absent:
            break
        case .unreadable(let why):
            return Sidecar.refuse(photo, "\(why), and is left untouched")
        case .text(let existing):
            let old = Sidecar.parse(existing)
            if let why = old.unreadable {
                return Sidecar.refuse(photo, "it is not XMP Orion can change safely "
                                           + "(\(why)), and is left untouched")
            }
            // Orion's own file is exactly the text of what it reads as.
            if old.text() != existing {
                let merged = merged(into: existing)
                guard let text = merged.text else {
                    return Sidecar.refuse(photo, "it is not XMP Orion can change safely "
                                               + "(\(merged.why)), and is left untouched")
                }
                xml = text
            }
        }
        do {
            // Atomic: a half-written sidecar during a crash would lose ratings
            // for a file that is otherwise fine.
            try xml.write(to: path, atomically: true, encoding: .utf8)
            return true
        } catch {
            return Sidecar.refuse(photo, error.localizedDescription)
        }
    }

    private static func refuse(_ photo: URL, _ why: String) -> Bool {
        let line = "could not write the sidecar for \(photo.lastPathComponent) — \(why)"
        NSLog("Orion: \(line)")
        FileHandle.standardError.write(Data("orion: \(line)\n".utf8))
        return false
    }

    private static func escapeXml(_ s: String) -> String {
        s.replacingOccurrences(of: "&", with: "&amp;")
         .replacingOccurrences(of: "<", with: "&lt;")
         .replacingOccurrences(of: ">", with: "&gt;")
         .replacingOccurrences(of: "\"", with: "&quot;")
    }
}
