using System.Text;
using System.Xml.Linq;
using OrionShell.Library;

namespace OrionShell;

/// Decision #370: `IMG.xmp` is shared with Lightroom, Capture One and darktable,
/// and Orion writes only its three fields. Fixtures/foreign/ are hand-written
/// after XMP Part 1 (none copied from those products); app/ViewportTests+ForeignXmp.swift
/// runs the same fixtures and the same checks through the Swift writer.
static partial class SelfTest {
    static readonly XNamespace RdfNs = "http://www.w3.org/1999/02/22-rdf-syntax-ns#";

    static void ForeignXmpChecks() {
        string dir = Directory.CreateTempSubdirectory("orion-foreign-xmp-").FullName;
        try {
            string photo = Path.Combine(dir, "photo.ARW"), xmp = Sidecar.PathFor(photo);
            const string edit64 = "eyJleHBvc3VyZUV2IjoxfQ==";   // {"exposureEv":1}
            foreach (var (file, rating, label, develop, inPlace) in new[] {
                         ("lightroom-style.xmp", 2, (string?)"Red", false, new[] { "xmp:Rating=\"4\"" }),
                         ("captureone-style.xmp", 3, "Green", false, ["<xmp:Rating>4</xmp:Rating>"]),
                         ("darktable-style.xmp", 1, null, false, ["xmp:Rating=\"4\""]),
                         // Ancestors bind orion and orion1 elsewhere: Orion's goes in as orion2, nothing rebound.
                         ("prefix-collision.xmp", 2, null, false, ["xmlns:orion2=\"http://orion.photo/ns/1.0/\"", $"orion2:Develop=\"{edit64}\""]),
                         ("develop-element.xmp", 5, null, true, ["<xmp:Rating>4</xmp:Rating>", $"<orion:Develop>{edit64}</orion:Develop>"]),
                         // The same field in two rdf:Descriptions: every occurrence changes.
                         ("split-descriptions.xmp", 1, "Blue", false, ["xmp:Rating=\"4\"", "<xmp:Rating>4</xmp:Rating>"]) }) {
                string original = File.ReadAllText(Path.Combine(Fixtures, "foreign", file), Encoding.UTF8);
                File.WriteAllText(xmp, original);
                Check(Sidecar.Read(photo) is { Unreadable: null } before && before.Rating == rating && before.Label == label
                      && (before.Develop != null) == develop,
                      $"{file}: Orion reads its rating, label and edit");
                bool wrote = Sidecar.Merge(photo, s => (s.Rating, s.Develop) = (4, [1, 2, 3]))
                             && Sidecar.Merge(photo, s => s.Develop = "{\"exposureEv\":1}"u8.ToArray());
                string after = File.ReadAllText(xmp, Encoding.UTF8);
                Check(wrote && Sidecar.Read(photo) is { Unreadable: null, Rejected: false, Rating: 4 } back && back.Label == label
                      && back.Develop!.SequenceEqual("{\"exposureEv\":1}"u8.ToArray()) && inPlace.All(after.Contains)
                      && !File.Exists(xmp + ".orion-tmp"),
                      $"{file}: a rating and two edits land, the rating where it already was, and read back");
                Check(XNode.DeepEquals(ForeignNodes(original), ForeignNodes(after)), $"{file}: every foreign node is still there and equal");
            }

            // An older Adobe prefix for the same namespace is the same field.
            File.WriteAllText(xmp, $"<x:xmpmeta xmlns:x=\"adobe:ns:meta/\"><rdf:RDF xmlns:rdf=\"{RdfNs}\"><rdf:Description rdf:about=\"\" "
                                   + "xmlns:xap=\"http://ns.adobe.com/xap/1.0/\" xap:Rating=\"5\"/></rdf:RDF></x:xmpmeta>");
            Check(Sidecar.Read(photo) is { Rating: 5 } && Sidecar.Merge(photo, s => s.Rating = 1)
                  && File.ReadAllText(xmp).Contains("xap:Rating=\"1\"") && !File.ReadAllText(xmp).Contains("xmp:"),
                  "xap:Rating is xmp:Rating, and is changed under its own prefix");

            // Encodings: a UTF-8 BOM is UTF-8; UTF-16 and broken UTF-8 are refused, never re-encoded.
            string lightroom = File.ReadAllText(Path.Combine(Fixtures, "foreign", "lightroom-style.xmp"), Encoding.UTF8);
            byte[] utf8 = Encoding.UTF8.GetBytes(lightroom);
            File.WriteAllBytes(xmp, [0xEF, 0xBB, 0xBF, .. utf8]);
            Check(Sidecar.Read(photo) is { Unreadable: null, Rating: 2, Label: "Red" } && Sidecar.Merge(photo, s => s.Rating = 3)
                  && Sidecar.Read(photo) is { Rating: 3, Label: "Red" }, "a UTF-8 byte-order mark reads and writes as UTF-8");
            int title = utf8.AsSpan().IndexOf("Bridge"u8);

            foreach (var (bad, why) in new[] {
                         (Encoding.UTF8.GetBytes("<x:xmpmeta xmlns:x=\"adobe:ns:meta/\"><rdf:RDF"), "XML that is not well-formed"),
                         (Encoding.UTF8.GetBytes($"<x:xmpmeta xmlns:x=\"adobe:ns:meta/\"><rdf:RDF xmlns:rdf=\"{RdfNs}\"/></x:xmpmeta>"),
                          "an rdf:RDF with no rdf:Description"),
                         (Encoding.UTF8.GetBytes($"<x:xmpmeta xmlns:x=\"adobe:ns:meta/\"><rdf:RDF xmlns:rdf=\"{RdfNs}\"><rdf:Description rdf:about=\"\" "
                          + "xmlns:xmp=\"http://ns.adobe.com/xap/1.0/\"><xmp:Rating><rdf:Bag/></xmp:Rating></rdf:Description></rdf:RDF></x:xmpmeta>"),
                          "an xmp:Rating that is a structure"),
                         ([.. Encoding.Unicode.GetPreamble(), .. Encoding.Unicode.GetBytes(lightroom)], "a UTF-16 file"),
                         ([.. utf8[..title], 0xFF, .. utf8[title..]], "a file that is not valid UTF-8") }) {
                File.WriteAllBytes(xmp, bad);
                Check(Sidecar.Read(photo) is { Unreadable: not null } && !Sidecar.Merge(photo, s => s.Rating = 5)
                      && !new Sidecar { Rating = 5 }.Write(photo) && File.ReadAllBytes(xmp).SequenceEqual(bad),
                      $"{why} reads as unreadable and is never written over");
            }

            // A star pressed before the photo's marks were read writes the rating
            // alone: the Lightroom label it has not seen yet stays.
            File.WriteAllText(xmp, lightroom);
            var library = LibraryModel.Shared;
            var unread = new LibraryModel.Photo(photo, 0);
            bool writes = library.Writes;
            library.Writes = true;
            try { library.SetRating(unread, 5); } finally { library.Writes = writes; }
            Check(unread.Label == null && Sidecar.Read(photo) is { Rating: 5, Label: "Red" }
                  && XNode.DeepEquals(ForeignNodes(lightroom), ForeignNodes(File.ReadAllText(xmp))),
                  "a rating before the marks load keeps the file's label and every foreign node");
        } finally {
            Directory.Delete(dir, recursive: true);
        }
    }

    /// The document without Orion's three fields and without namespace
    /// declarations (Orion may add `xmlns:orion`; names stay expanded), whitespace dropped.
    static XDocument ForeignNodes(string text) {
        var doc = XDocument.Parse(text);
        XName[] orion = [XName.Get("Rating", "http://ns.adobe.com/xap/1.0/"), XName.Get("Label", "http://ns.adobe.com/xap/1.0/"),
                         XName.Get("Develop", "http://orion.photo/ns/1.0/")];
        foreach (var d in doc.Descendants(RdfNs + "Description").ToList()) {
            d.Attributes().Where(a => orion.Contains(a.Name)).Remove();
            d.Elements().Where(e => orion.Contains(e.Name)).Remove();
        }
        foreach (var e in doc.Descendants()) e.Attributes().Where(a => a.IsNamespaceDeclaration).Remove();
        return doc;
    }
}
