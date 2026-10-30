using System.Globalization;
using System.Text;

namespace OrionShell;

/// The XMP sidecar beside each raw: a port of app/Sidecar.swift, byte for byte
/// on the way out, so a file either app wrote opens the same in the other
/// (decision #9: the sidecar is the source of truth, not a catalog).
///
/// `xmp:Rating` -1..5 (-1 is rejected) and `xmp:Label` are the Adobe fields
/// other editors read; `orion:Develop` is the edit, base64 of DevelopJson.
/// String matching rather than an XML parser, as Swift's is: the bar is the
/// three fields Orion writes and what Lightroom and darktable write for them.
public sealed class Sidecar {
    public int Rating;
    public bool Rejected;
    public string? Label;
    /// The develop JSON, or null when the file carries none.
    public byte[]? Develop;
    /// Why the file is there and cannot be read (permissions, an I/O error,
    /// an `orion:Develop` that is not base64), or null. Nothing ever writes
    /// over such a sidecar: Swift reads a bad blob as "no edit" and would.
    public string? Unreadable;

    public static string PathFor(string photo) => Path.ChangeExtension(photo, ".xmp");

    /// Null only when there is no sidecar. One that exists and will not read
    /// comes back with Unreadable set, never as "none".
    public static Sidecar? Read(string photo) {
        string text;
        try {
            text = File.ReadAllText(PathFor(photo), Encoding.UTF8);
        } catch (Exception e) when (e is FileNotFoundException or DirectoryNotFoundException) {
            return null;
        } catch (Exception e) when (e is IOException or UnauthorizedAccessException or System.Security.SecurityException) {
            return new Sidecar { Unreadable = e.Message };
        }
        var s = new Sidecar();
        if (Value("xmp:Rating", text) is { } r && int.TryParse(r, NumberStyles.AllowLeadingSign, CultureInfo.InvariantCulture, out int rating)) {
            s.Rejected = rating < 0;
            s.Rating = Math.Max(0, rating);
        }
        s.Label = Value("xmp:Label", text);
        if (Value("orion:Develop", text) is { } encoded) {
            try { s.Develop = Convert.FromBase64String(encoded); } catch (FormatException) { s.Unreadable = "orion:Develop is not base64"; }
        }
        return s;
    }

    /// An attribute, or an element of the same name: editors write either.
    static string? Value(string key, string text) {
        int at = text.IndexOf(key + "=\"", StringComparison.Ordinal);
        if (at >= 0) {
            int start = at + key.Length + 2, end = text.IndexOf('"', start);
            if (end >= 0) return Unescape(text[start..end]);
        }
        int open = text.IndexOf($"<{key}>", StringComparison.Ordinal), close = text.IndexOf($"</{key}>", StringComparison.Ordinal);
        if (open >= 0 && close >= 0 && open + key.Length + 2 <= close)
            return Unescape(text[(open + key.Length + 2)..close].Trim());
        return null;
    }

    /// Ampersand last, or `&amp;lt;` would come back as `<`.
    public static string Unescape(string s) => s.Replace("&lt;", "<").Replace("&gt;", ">").Replace("&quot;", "\"")
        .Replace("&apos;", "'").Replace("&amp;", "&");

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

    /// Atomic: a temporary file beside it, then a rename over it, so a crash
    /// mid-write leaves the old sidecar rather than half of a new one.
    /// False, never a throw, when it did not land: Autosave keeps the edit and retries.
    public bool Write(string photo) {
        string path = PathFor(photo), temp = path + ".orion-tmp";
        try {
            File.WriteAllText(temp, Text(), new UTF8Encoding(false));
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
