using System.Text;
using System.Xml.Linq;

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
            foreach (var (file, rating, label, inPlace) in new[] {
                         ("lightroom-style.xmp", 2, (string?)"Red", "xmp:Rating=\"4\""),
                         ("captureone-style.xmp", 3, "Green", "<xmp:Rating>4</xmp:Rating>"),
                         ("darktable-style.xmp", 1, null, "xmp:Rating=\"4\"") }) {
                string original = File.ReadAllText(Path.Combine(Fixtures, "foreign", file), Encoding.UTF8);
                File.WriteAllText(xmp, original);
                Check(Sidecar.Read(photo) is { Unreadable: null, Develop: null } before && before.Rating == rating && before.Label == label,
                      $"{file}: Orion reads its rating and label");
                bool wrote = Sidecar.Merge(photo, s => (s.Rating, s.Develop) = (4, [1, 2, 3]))
                             && Sidecar.Merge(photo, s => s.Develop = "{\"exposureEv\":1}"u8.ToArray());
                string after = File.ReadAllText(xmp, Encoding.UTF8);
                Check(wrote && Sidecar.Read(photo) is { Unreadable: null, Rejected: false, Rating: 4 } back && back.Label == label
                      && back.Develop!.SequenceEqual("{\"exposureEv\":1}"u8.ToArray()) && after.Contains(inPlace)
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

            foreach (var (bad, why) in new[] {
                         ("<x:xmpmeta xmlns:x=\"adobe:ns:meta/\"><rdf:RDF", "XML that is not well-formed"),
                         ($"<x:xmpmeta xmlns:x=\"adobe:ns:meta/\"><rdf:RDF xmlns:rdf=\"{RdfNs}\"/></x:xmpmeta>", "an rdf:RDF with no rdf:Description"),
                         ($"<x:xmpmeta xmlns:x=\"adobe:ns:meta/\"><rdf:RDF xmlns:rdf=\"{RdfNs}\"><rdf:Description rdf:about=\"\" "
                          + "xmlns:xmp=\"http://ns.adobe.com/xap/1.0/\"><xmp:Rating><rdf:Bag/></xmp:Rating></rdf:Description></rdf:RDF></x:xmpmeta>",
                          "an xmp:Rating that is a structure") }) {
                File.WriteAllText(xmp, bad);
                Check(Sidecar.Read(photo) is { Unreadable: not null } && !Sidecar.Merge(photo, s => s.Rating = 5)
                      && !new Sidecar { Rating = 5 }.Write(photo) && File.ReadAllText(xmp) == bad,
                      $"{why} reads as unreadable and is never written over");
            }
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
