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
struct Sidecar: Sendable {
    var rating: Int = 0
    var rejected: Bool = false
    var label: String?
    /// Develop settings, encoded as JSON in Orion's namespace.
    var develop: Data?

    static func url(for photo: URL) -> URL {
        photo.deletingPathExtension().appendingPathExtension("xmp")
    }

    // MARK: Reading

    static func read(for photo: URL) -> Sidecar? {
        let path = url(for: photo)
        guard let text = try? String(contentsOf: path, encoding: .utf8) else { return nil }

        var result = Sidecar()

        if let rating = value(of: "xmp:Rating", in: text).flatMap(Int.init) {
            // -1 is the convention for rejected, which every editor understands.
            result.rejected = rating < 0
            result.rating = max(0, rating)
        }
        result.label = value(of: "xmp:Label", in: text)

        if let encoded = value(of: "orion:Develop", in: text) {
            result.develop = Data(base64Encoded: encoded)
        }

        return result
    }

    /// Reads an attribute or an element with the same name. Editors write
    /// these interchangeably, so accepting both is what makes the sidecar
    /// actually interoperable rather than nominally so.
    ///
    /// ⚠️ This is string matching, not an XML parser. It is deliberate — the
    /// interop bar today is "read what Lightroom and darktable write for these
    /// six fields", and a parser is a dependency and a schema. It will not
    /// survive a namespace prefix it has not seen or an attribute split across
    /// lines. If the bar rises to "read arbitrary XMP", replace it rather than
    /// patching it.
    private static func value(of key: String, in text: String) -> String? {
        if let r = text.range(of: "\(key)=\"") {
            let rest = text[r.upperBound...]
            if let end = rest.firstIndex(of: "\"") {
                return unescape(String(rest[..<end]))
            }
        }
        if let open = text.range(of: "<\(key)>"),
           let close = text.range(of: "</\(key)>"),
           open.upperBound <= close.lowerBound {
            return unescape(String(text[open.upperBound..<close.lowerBound])
                .trimmingCharacters(in: .whitespacesAndNewlines))
        }
        return nil
    }

    /// The inverse of `escape`, and it has to exist: `Library.persist` reads,
    /// modifies and rewrites the whole sidecar on every rating change, so a
    /// value that is escaped on write and not unescaped on read gains a layer
    /// per save. A label of `R&D` becomes `R&amp;D`, then `R&amp;amp;D`.
    /// Ampersand last, or `&lt;` written as `&amp;lt;` would come back as `<`.
    static func unescape(_ s: String) -> String {
        s.replacingOccurrences(of: "&lt;", with: "<")
         .replacingOccurrences(of: "&gt;", with: ">")
         .replacingOccurrences(of: "&quot;", with: "\"")
         .replacingOccurrences(of: "&apos;", with: "'")
         .replacingOccurrences(of: "&amp;", with: "&")
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
        change(&sidecar)
        return sidecar.write(for: photo)
    }

    /// ⚠ **Returns false rather than only logging.** This used to swallow the
    /// error into `NSLog` and return, which reads as success to every caller —
    /// and the caller that mattered was `Autosave.flush`, which then moved its
    /// baseline forward and never tried again. A read-only volume, a full disk
    /// or a folder the sandbox lost access to therefore cost the whole session's
    /// edits, with the only trace in a log nobody has open.
    @discardableResult
    func write(for photo: URL) -> Bool {
        let effectiveRating = rejected ? -1 : rating

        var attributes = ["xmp:Rating=\"\(effectiveRating)\""]
        if let label, !label.isEmpty {
            attributes.append("xmp:Label=\"\(escape(label))\"")
        }
        if let develop {
            attributes.append("orion:Develop=\"\(develop.base64EncodedString())\"")
        }

        let xml = """
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

        let path = Sidecar.url(for: photo)
        do {
            // Atomic: a half-written sidecar during a crash would lose ratings
            // for a file that is otherwise fine.
            try xml.write(to: path, atomically: true, encoding: .utf8)
            return true
        } catch {
            let why = "could not write the sidecar for "
                    + "\(photo.lastPathComponent) — \(error.localizedDescription)"
            NSLog("Orion: \(why)")
            FileHandle.standardError.write(Data("orion: \(why)\n".utf8))
            return false
        }
    }

    /// The escape half, reachable from the tests. `escape` is an instance
    /// method on the writer and the round trip is what needs proving.
    static func escapeForTests(_ s: String) -> String { escapeXml(s) }

    private func escape(_ s: String) -> String { Sidecar.escapeXml(s) }

    private static func escapeXml(_ s: String) -> String {
        s.replacingOccurrences(of: "&", with: "&amp;")
         .replacingOccurrences(of: "<", with: "&lt;")
         .replacingOccurrences(of: ">", with: "&gt;")
         .replacingOccurrences(of: "\"", with: "&quot;")
    }
}
