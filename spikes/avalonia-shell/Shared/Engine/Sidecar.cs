using System.Globalization;
using System.Text;
using System.Xml;
using System.Xml.Linq;

namespace OrionShell;

/// The XMP sidecar beside each raw: a port of app/Sidecar.swift, byte for byte
/// on the way out, so a file either app wrote opens the same in the other
/// (decision #9: the sidecar is the source of truth, not a catalog).
///
/// `xmp:Rating` -1..5 (-1 is rejected) and `xmp:Label` are the Adobe fields
/// other editors read; `orion:Develop` is the edit, base64 of DevelopJson.
///
/// ⚠ `IMG.xmp` is the same file Lightroom, Camera Raw, Capture One and darktable
/// keep their own work in, so Orion reads and writes only its three fields
/// (decision #370). A file Orion wrote itself is rewritten byte for byte as
/// before; any other is parsed, those three fields changed in the tree, and
/// every other node kept. One it cannot parse or place them in is never written.
public sealed class Sidecar {
    public int Rating;
    public bool Rejected;
    public string? Label;
    /// The develop JSON, or null when the file carries none.
    public byte[]? Develop;
    /// Why the file is there and cannot be read (permissions, an I/O error,
    /// XML that is not well-formed or has no place for Orion's fields, an
    /// `orion:Develop` that is not base64), or null. Nothing ever writes over such a sidecar.
    public string? Unreadable;

    public static string PathFor(string photo) => Path.ChangeExtension(photo, ".xmp");

    /// Null only when there is no sidecar. One that exists and will not read
    /// comes back with Unreadable set, never as "none".
    public static Sidecar? Read(string photo) {
        byte[] bytes;
        try {
            bytes = File.ReadAllBytes(PathFor(photo));
        } catch (Exception e) when (e is FileNotFoundException or DirectoryNotFoundException) {
            return null;
        } catch (Exception e) when (e is IOException or UnauthorizedAccessException or System.Security.SecurityException) {
            return new Sidecar { Unreadable = e.Message };
        }
        return Decode(bytes, out string why) is { } text ? Parse(text) : new Sidecar { Unreadable = why };
    }

    static readonly UTF8Encoding StrictUtf8 = new(false, true);
    const int MaxBytes = 4 << 20;

    /// The text of a sidecar, or null and why. UTF-8 only, strictly: a lenient
    /// decode would turn a bad byte into U+FFFD, or read UTF-16 by its BOM, and
    /// the merge would write that back as UTF-8 over another tool's file. A
    /// UTF-8 BOM is dropped; Swift's `Sidecar.contents` makes the same calls.
    static string? Decode(byte[] bytes, out string why) {
        why = bytes.Length > MaxBytes ? "larger than 4 MB, which no sidecar is"
            : bytes is [0xFE, 0xFF, ..] or [0xFF, 0xFE, ..] ? "it is UTF-16, not UTF-8" : "";
        if (why != "") return null;
        int skip = bytes is [0xEF, 0xBB, 0xBF, ..] ? 3 : 0;
        try {
            return StrictUtf8.GetString(bytes, skip, bytes.Length - skip);
        } catch (DecoderFallbackException) {
            why = "it is not UTF-8";
            return null;
        }
    }

    /// Orion's three fields out of any XMP packet, by namespace rather than by
    /// prefix (an older Adobe file says `xap:` for `xmp:`), as an attribute or
    /// an element of the same name: XMP allows either (XMP Part 1, serialization).
    public static Sidecar Parse(string text) {
        if (Load(text, out string why) is not { } x) return new Sidecar { Unreadable = why };
        var s = new Sidecar();
        if (Get(x.Descriptions, XmpRating) is { } r && double.TryParse(r, NumberStyles.Float, CultureInfo.InvariantCulture, out double rating)
            && double.IsFinite(rating)) {
            s.Rejected = rating < 0;
            s.Rating = (int)Math.Clamp(rating, 0, 5);
        }
        s.Label = Get(x.Descriptions, XmpLabel);
        if (Get(x.Descriptions, OrionDevelop) is { } encoded) {
            try { s.Develop = Convert.FromBase64String(encoded); } catch (FormatException) { s.Unreadable = "orion:Develop is not base64"; }
        }
        return s;
    }

    static readonly XNamespace Rdf = "http://www.w3.org/1999/02/22-rdf-syntax-ns#";
    static readonly XName XmpRating = XName.Get("Rating", "http://ns.adobe.com/xap/1.0/"),
        XmpLabel = XName.Get("Label", "http://ns.adobe.com/xap/1.0/"), OrionDevelop = XName.Get("Develop", "http://orion.photo/ns/1.0/");

    /// The document and the top-level rdf:Description elements of its one
    /// rdf:RDF (XMP Part 1, serialization), or null and why Orion cannot safely put its
    /// fields among them: not well-formed, a DTD, no or several rdf:RDF, no
    /// rdf:Description, or one of its fields held as a structure, not a value.
    static (XDocument Doc, List<XElement> Descriptions)? Load(string text, out string why) {
        XDocument doc;
        try {
            using var reader = XmlReader.Create(new StringReader(text),
                new XmlReaderSettings { DtdProcessing = DtdProcessing.Prohibit, XmlResolver = null });
            doc = XDocument.Load(reader, LoadOptions.PreserveWhitespace);
        } catch (XmlException e) {
            why = $"not well-formed XML: {e.Message}";
            return null;
        }
        var rdf = doc.Descendants(Rdf + "RDF").ToList();
        var descriptions = rdf.Count == 1 ? rdf[0].Elements(Rdf + "Description").ToList() : [];
        var odd = descriptions.SelectMany(d => d.Elements())
            .Where(e => e.Name == XmpRating || e.Name == XmpLabel || e.Name == OrionDevelop)
            .FirstOrDefault(e => e.HasElements || e.Attributes().Any(a => !a.IsNamespaceDeclaration));
        why = rdf.Count != 1 ? $"{rdf.Count} rdf:RDF elements where XMP has one"
            : descriptions.Count == 0 ? "no rdf:Description to hold Orion's fields"
            : odd != null ? $"{odd.Name.LocalName} held as a structure, not a value" : "";
        return why == "" ? (doc, descriptions) : null;
    }

    /// The first value found, attribute or element.
    static string? Get(List<XElement> descriptions, XName name) =>
        descriptions.Select(d => d.Attribute(name)?.Value ?? d.Element(name)?.Value.Trim()).FirstOrDefault(v => v != null);

    public static string Escape(string s) => s.Replace("&", "&amp;").Replace("<", "&lt;").Replace(">", "&gt;").Replace("\"", "&quot;");

    /// Changes these fields over whatever is on disk and keeps the rest: the
    /// develop writer must never cost a rating, nor a rating the edit.
    public static bool Merge(string photo, Action<Sidecar> change) {
        var s = Read(photo) ?? new Sidecar();
        if (s.Unreadable != null) return Fail(photo, $"it cannot be read ({s.Unreadable}), and is left untouched");
        change(s);
        return s.Write(photo);
    }

    /// Sidecar.write's text exactly: the BOM inside the xpacket, LF, no final newline.
    public string Text() {
        var attributes = new List<string> { $"xmp:Rating=\"{(Rejected ? -1 : Rating)}\"" };
        if (!string.IsNullOrEmpty(Label)) attributes.Add($"xmp:Label=\"{Escape(Label)}\"");
        if (Develop != null) attributes.Add($"orion:Develop=\"{Convert.ToBase64String(Develop)}\"");
        return "<?xpacket begin=\"﻿\" id=\"W5M0MpCehiHzreSzNTczkc9d\"?>\n" +
               "<x:xmpmeta xmlns:x=\"adobe:ns:meta/\" x:xmptk=\"Orion\">\n" +
               " <rdf:RDF xmlns:rdf=\"http://www.w3.org/1999/02/22-rdf-syntax-ns#\">\n" +
               "  <rdf:Description rdf:about=\"\"\n" +
               "    xmlns:xmp=\"http://ns.adobe.com/xap/1.0/\"\n" +
               "    xmlns:orion=\"http://orion.photo/ns/1.0/\"\n" +
               "    " + string.Join("\n    ", attributes) + "/>\n" +
               " </rdf:RDF>\n" +
               "</x:xmpmeta>\n" +
               "<?xpacket end=\"w\"?>";
    }

    /// `existing`, another tool's packet, with only Orion's three fields
    /// changed (Swift's `Sidecar.merged`, step for step): each is set where it
    /// already is, removed when Orion has no value, and otherwise added as an
    /// attribute of the first rdf:Description, declaring `xmp`/`orion` there if
    /// its namespace is not in scope (a numbered prefix if that one is taken).
    /// Null and why when the file cannot be parsed or has no place for them.
    public string? MergedInto(string existing, out string why) {
        if (Load(existing, out why) is not { } x) return null;
        foreach (var (name, prefix, value) in new[] {
                     (XmpRating, "xmp", (Rejected ? -1 : Rating).ToString(CultureInfo.InvariantCulture)),
                     (XmpLabel, "xmp", string.IsNullOrEmpty(Label) ? null : Label),
                     (OrionDevelop, "orion", Develop == null ? null : Convert.ToBase64String(Develop)) }) {
            var attributes = x.Descriptions.SelectMany(d => d.Attributes(name)).ToList();
            var elements = x.Descriptions.SelectMany(d => d.Elements(name)).ToList();
            foreach (var a in attributes) if (value == null) a.Remove(); else a.Value = value;
            foreach (var e in elements) if (value == null) e.Remove(); else e.Value = value;
            if (value == null || attributes.Count + elements.Count > 0) continue;
            var first = x.Descriptions[0];
            if (string.IsNullOrEmpty(first.GetPrefixOfNamespace(name.Namespace))) {
                // Never rebind a prefix the file already uses, here or on an ancestor:
                // `orion1`, `orion2`… instead, as Swift does.
                string fresh = prefix;
                for (int i = 1; first.GetNamespaceOfPrefix(fresh) != null; i++) fresh = prefix + i;
                first.SetAttributeValue(XNamespace.Xmlns + fresh, name.NamespaceName);
            }
            first.SetAttributeValue(name, value);
        }
        // Top-level nodes one per line, as Swift's XMLDocument writes them; the
        // declaration, if there was one, says UTF-8 because that is what is written.
        var nodes = x.Doc.Nodes().Where(n => n is not XText).Select(n => n.ToString(SaveOptions.DisableFormatting));
        return (x.Doc.Declaration != null ? "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n" : "") + string.Join("\n", nodes);
    }

    /// Atomic: a temporary file beside it, then a rename over it, so a crash
    /// mid-write leaves the old sidecar rather than half of a new one.
    /// False, never a throw, when it did not land: Autosave keeps the edit and retries.
    /// A file Orion did not write keeps everything but Orion's fields (MergedInto).
    public bool Write(string photo) {
        string path = PathFor(photo), temp = path + ".orion-tmp";
        try {
            string text = Text();
            byte[]? bytes = null;
            try { bytes = File.ReadAllBytes(path); } catch (Exception e) when (e is FileNotFoundException or DirectoryNotFoundException) { }
            if (bytes != null) {
                if (Decode(bytes, out string why) is not { } existing) return Fail(photo, $"{why}, and is left untouched");
                var old = Parse(existing);
                if (old.Unreadable != null)
                    return Fail(photo, $"it is not XMP Orion can change safely ({old.Unreadable}), and is left untouched");
                // Orion's own file is exactly the text of what it reads as: rewrite it whole.
                if (old.Text() != existing) {
                    if (MergedInto(existing, out why) is not { } merged)
                        return Fail(photo, $"it is not XMP Orion can change safely ({why}), and is left untouched");
                    text = merged;
                }
            }
            File.WriteAllText(temp, text, new UTF8Encoding(false));
            File.Move(temp, path, overwrite: true);
            return true;
        } catch (Exception e) when (e is IOException or UnauthorizedAccessException) {
            try { File.Delete(temp); } catch (Exception) { /* the write already failed; that is the report */ }
            return Fail(photo, e.Message);
        }
    }

    static bool Fail(string photo, string why) {
        Console.Error.WriteLine($"orion: could not write the sidecar for {Path.GetFileName(photo)} — {why}");
        return false;
    }
}
