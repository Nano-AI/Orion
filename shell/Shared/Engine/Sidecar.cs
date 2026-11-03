using System.Globalization;
using System.Text;
using System.Xml;
using System.Xml.Linq;

namespace OrionShell;

/// The XMP sidecar beside each raw: a port of the Swift app's Sidecar.swift, byte for byte
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
///
/// Which file is a photo's (decision #405, Adobe's convention in full): `IMG.xmp`
/// for a proprietary raw, `IMG.DNG.xmp` for a format Adobe embeds XMP into (DNG,
/// JPEG, TIFF, HEIF, PSD), whose embedded packet is read as the baseline and
/// never written. Every sidecar Orion touches says whose it is (`orion:Source`,
/// the photo's own file name; Lightroom's `crs:RawFileName` is honoured the same
/// way), so `IMG.NEF` beside an `IMG.ARW` escapes to `IMG.NEF.xmp` instead of
/// writing over the other's edit. `Locate` is the one place that rule lives.
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
    /// Whose sidecar this is: `orion:Source`, the photo's file name, written on every write.
    public string? Source;
    /// Lightroom's and Camera Raw's `crs:RawFileName`, read as the same hint when there is no `orion:Source`.
    public string? RawFileName;

    /// Formats Adobe writes XMP into rather than beside, lower case without the dot.
    static readonly HashSet<string> Embeds = ["dng", "jpg", "jpeg", "tif", "tiff", "heic", "heif", "hif", "psd"];
    static string Ext(string photo) => Path.GetExtension(photo).TrimStart('.').ToLowerInvariant();

    /// Where a photo's sidecar is read from (null: it has none of its own), where
    /// a write goes, the one-line notice that write owes when it escapes to the
    /// full name, and the shared file already parsed, so a read never reads it twice.
    readonly record struct Place(string? Read, string Write, string? Notice, Sidecar? Loaded);

    /// The naming rule (#405), and the only copy of it. A raw opens `IMG.ARW.xmp` if
    /// it is there, else `IMG.xmp` if that names this photo or no photo, else none;
    /// it writes where it opened, or `IMG.ARW.xmp` beside an `IMG.xmp` that is
    /// another photo's. A format Adobe embeds into writes only `IMG.DNG.xmp`, and
    /// opens it, else a legacy `IMG.xmp` that is its own (Legacy), else its embedded XMP.
    static Place Locate(string photo) {
        string full = photo + ".xmp", shared = Path.ChangeExtension(photo, ".xmp");
        if (File.Exists(full)) return new(full, full, null, null);
        if (Embeds.Contains(Ext(photo))) {
            var legacy = ReadFile(shared);
            return legacy is { Unreadable: null } && Legacy(photo, legacy) ? new(shared, full, null, legacy) : new(null, full, null, null);
        }
        var loaded = ReadFile(shared);
        if (loaded == null) return new(null, shared, null, null);
        // One that will not parse cannot say whose it is: it stays this photo's, and Unreadable keeps it unwritten.
        string? owner = loaded.Unreadable == null ? loaded.Source ?? loaded.RawFileName : null;
        if (owner == null || Same(photo, owner)) return new(shared, shared, null, loaded);
        return new(null, full, $"{Path.GetFileName(shared)} is {owner}'s, so this photo's edit is in {Path.GetFileName(full)}", null);
    }

    /// `owner` names this file. A spelling that differs only in case is another file
    /// only when the folder holds both spellings (a case-sensitive file system with
    /// photo.ARW and photo.arw); otherwise it is this one, renamed or matched case-blind.
    static bool Same(string photo, string owner) {
        string name = Path.GetFileName(photo);
        if (owner == name) return true;
        if (!string.Equals(owner, name, StringComparison.OrdinalIgnoreCase)) return false;
        try {
            var listed = Directory.EnumerateFiles(Path.GetDirectoryName(Path.GetFullPath(photo))!).Select(Path.GetFileName).ToHashSet();
            return !(listed.Contains(owner) && listed.Contains(name));
        } catch (Exception e) when (e is IOException or UnauthorizedAccessException) { return false; }
    }

    /// Orion before #405 kept a DNG's edit in `IMG.xmp`. It is still that DNG's when it
    /// names it, or names no file and no other photo has the stem (Lightroom never
    /// writes `IMG.xmp` for a DNG, so then nothing else can have written it). Read
    /// from there until the first write lands in `IMG.DNG.xmp`; never written itself.
    static bool Legacy(string photo, Sidecar s) =>
        (s.Source ?? s.RawFileName) is { } owner ? Same(photo, owner) : !Rival(photo, embedded: true);

    /// Another photo in the folder shares this one's stem, case-blind, among the raws
    /// (`embedded`: raws and formats Adobe embeds into) and could claim its `IMG.xmp`.
    static bool Rival(string photo, bool embedded) {
        string stem = Path.GetFileNameWithoutExtension(photo), name = Path.GetFileName(photo);
        try {
            return Directory.EnumerateFiles(Path.GetDirectoryName(Path.GetFullPath(photo))!).Select(Path.GetFileName)
                .Any(n => n != name && string.Equals(Path.GetFileNameWithoutExtension(n), stem, StringComparison.OrdinalIgnoreCase)   // PHOTO.NEF shares photo.xmp where names are case-blind
                          && (embedded ? Program.RawExtensions.Contains(Ext(n!)) || Embeds.Contains(Ext(n!))
                                       : Program.RawExtensions.Contains(Ext(n!)) && !Embeds.Contains(Ext(n!))));
        } catch (Exception e) when (e is IOException or UnauthorizedAccessException) {
            return true;   // cannot tell: claim nothing
        }
    }

    /// The sidecar file this photo reads, or the one its first write will make.
    public static string PathFor(string photo) => Locate(photo) is var p && p.Read != null ? p.Read : p.Write;

    /// The sidecar that goes with the photo when it moves (the Trash): its own, but
    /// not an `IMG.xmp` that names no one while another raw of that name could
    /// read it too. Never another photo's.
    public static string? OwnPath(string photo) {
        var place = Locate(photo);
        if (place.Read == null || place.Loaded is not { Source: null, RawFileName: null }) return place.Read;
        return Rival(photo, Embeds.Contains(Ext(photo))) ? null : place.Read;
    }

    /// Null only when there is no sidecar. One that exists and will not read
    /// comes back with Unreadable set, never as "none". A format Adobe embeds
    /// into with no sidecar of its own reads its embedded XMP as the baseline.
    public static Sidecar? Read(string photo) {
        var place = Locate(photo);
        if (place.Loaded != null) return place.Loaded;
        if (place.Read != null) return ReadFile(place.Read);
        return Embeds.Contains(Ext(photo)) ? Embedded(photo) : null;
    }

    static Sidecar? ReadFile(string path) {
        byte[] bytes;
        try {
            bytes = File.ReadAllBytes(path);
        } catch (Exception e) when (e is FileNotFoundException or DirectoryNotFoundException) {
            return null;
        } catch (Exception e) when (e is IOException or UnauthorizedAccessException or System.Security.SecurityException) {
            return new Sidecar { Unreadable = e.Message };
        }
        return Decode(bytes, out string why) is { } text ? Parse(text) : new Sidecar { Unreadable = why };
    }

    /// The packet a DNG carries, through LibRaw's `xmpdata` (orion_read_xmp): Orion's
    /// fields from it, or null. Never written back, so one Orion cannot read is no baseline, not a refusal.
    static unsafe Sidecar? Embedded(string photo) {
        try {
            // One open of the file for a packet under 64 KB, which is every camera's; a larger one asks again at its size.
            var bytes = new byte[64 << 10];
            uint size;
            int status;
            fixed (byte* p = bytes) status = Native.orion_read_xmp(photo, p, (uint)bytes.Length, out size);
            if (size == 0 || size > MaxBytes) return null;
            if (status != 0) {
                bytes = new byte[size];
                fixed (byte* p = bytes) status = Native.orion_read_xmp(photo, p, size, out size);
            }
            if (status != 0 || size > bytes.Length) return null;
            bytes = bytes[..(int)size];
            if (Decode(bytes, out _) is not { } text || Parse(text) is not { Unreadable: null } s) return null;
            s.Source = s.RawFileName = null;   // the photo's own packet says nothing about whose sidecar is whose
            return s;
        } catch (Exception e) when (e is DllNotFoundException or EntryPointNotFoundException) {
            return null;
        }
    }

    /// Says that a write escaped to the full name: the status line in the app, stderr everywhere.
    public static event Action<string>? Noticed;

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
        s.Source = Get(x.Descriptions, OrionSource);
        s.RawFileName = Get(x.Descriptions, CrsRawFileName);
        if (Get(x.Descriptions, OrionDevelop) is { } encoded) {
            try { s.Develop = Convert.FromBase64String(encoded); } catch (FormatException) { s.Unreadable = "orion:Develop is not base64"; }
        }
        return s;
    }

    static readonly XNamespace Rdf = "http://www.w3.org/1999/02/22-rdf-syntax-ns#";
    static readonly XName XmpRating = XName.Get("Rating", "http://ns.adobe.com/xap/1.0/"),
        XmpLabel = XName.Get("Label", "http://ns.adobe.com/xap/1.0/"), OrionDevelop = XName.Get("Develop", "http://orion.photo/ns/1.0/"),
        OrionSource = XName.Get("Source", "http://orion.photo/ns/1.0/"),
        CrsRawFileName = XName.Get("RawFileName", "http://ns.adobe.com/camera-raw-settings/1.0/");

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
            .Where(e => e.Name == XmpRating || e.Name == XmpLabel || e.Name == OrionDevelop || e.Name == OrionSource)
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
        if (Source != null) attributes.Add($"orion:Source=\"{Escape(Source)}\"");
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
                     (OrionSource, "orion", Source),
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
        // Top-level nodes as they were, the whitespace between them and a final newline
        // included (#405: the foreign file keeps its bytes); the declaration, if there
        // was one, says UTF-8 because that is what is written.
        // ⚠ Not XNode.ToString: its writer's NewLineChars is Environment.NewLine, so on
        // Windows every "\n" of the foreign file's whitespace came back "\r\n" (#37).
        var sw = new StringWriter(CultureInfo.InvariantCulture);
        using (var w = XmlWriter.Create(sw, new XmlWriterSettings {
                   OmitXmlDeclaration = true, ConformanceLevel = ConformanceLevel.Fragment, NewLineChars = "\n" }))
            foreach (var n in x.Doc.Nodes()) n.WriteTo(w);
        return (x.Doc.Declaration != null ? "<?xml version=\"1.0\" encoding=\"UTF-8\"?>" : "") + sw;
    }

    /// Atomic: a temporary file beside it, then a rename over it, so a crash
    /// mid-write leaves the old sidecar rather than half of a new one.
    /// False, never a throw, when it did not land: Autosave keeps the edit and retries.
    /// A file Orion did not write keeps everything but Orion's fields (MergedInto).
    /// It goes where Locate says, and says whose it is (orion:Source).
    public bool Write(string photo) {
        var place = Locate(photo);
        string path = place.Write, temp = path + ".orion-tmp";
        Source = Path.GetFileName(photo);
        try {
            string text = Text();
            byte[]? bytes = null;
            try { bytes = File.ReadAllBytes(path); } catch (Exception e) when (e is FileNotFoundException or DirectoryNotFoundException) { }
            // A migrating DNG's first write starts from the legacy `IMG.xmp` it read, so every node of it carries over.
            if (bytes == null && place.Read is { } from && from != path)
                try { bytes = File.ReadAllBytes(from); } catch (Exception e) when (e is FileNotFoundException or DirectoryNotFoundException) { }
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
            if (place.Notice != null) {
                Console.Error.WriteLine($"orion: {place.Notice}");
                Noticed?.Invoke(place.Notice);
            }
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

public static unsafe partial class Native {
    /// A file's embedded XMP packet (orion.h); a null buffer asks the size.
    [System.Runtime.InteropServices.LibraryImport(Lib, StringMarshalling = System.Runtime.InteropServices.StringMarshalling.Utf8)]
    public static partial int orion_read_xmp(string path, byte* buffer, uint capacity, out uint size);
}
