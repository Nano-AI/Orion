using System.Text;
using System.Xml.Linq;
using OrionShell.Library;

namespace OrionShell;

/// Decision #405: `photo.ARW`, `photo.dng` and `photo.DNG` in one folder keep three
/// sidecars, and the one they used to share keeps every foreign node exactly.
static partial class SelfTest {
    static void SidecarNameChecks() {
        string dir = Directory.CreateTempSubdirectory("orion-sidecar-names-").FullName;
        string P(string name) => Path.Combine(dir, name);
        try {
            const string declaration = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
            string original = declaration + File.ReadAllText(Path.Combine(Fixtures, "foreign", "lightroom-style.xmp"), Encoding.UTF8);
            File.WriteAllText(P("photo.xmp"), original);
            File.WriteAllBytes(P("photo.ARW"), []);
            File.WriteAllBytes(P("photo.dng"), []);
            bool caseBlind = File.Exists(P("photo.DNG"));   // macOS and Windows: one file, so two photos
            if (!caseBlind) File.WriteAllBytes(P("photo.DNG"), []);
            var photos = new (string Name, string Sidecar, int Rating, string Label)[] {
                ("photo.ARW", "photo.xmp", 4, "Green"), ("photo.dng", "photo.dng.xmp", 2, "Blue"), ("photo.DNG", "photo.DNG.xmp", 5, "Yellow") }
                .Where(p => !caseBlind || p.Name != "photo.DNG").ToList();
            foreach (var p in photos)
                Sidecar.Merge(P(p.Name), s => (s.Rating, s.Label, s.Develop) = (p.Rating, p.Label, Encoding.UTF8.GetBytes(p.Name)));
            Check(photos.All(p => Sidecar.PathFor(P(p.Name)) == P(p.Sidecar) && Sidecar.Read(P(p.Name)) is { } s && s.Rating == p.Rating
                                  && s.Label == p.Label && s.Source == p.Name && s.Develop!.SequenceEqual(Encoding.UTF8.GetBytes(p.Name))),
                  $"photo.ARW, photo.dng{(caseBlind ? "" : " and photo.DNG")} round-trip rating, label and edit in their own sidecars (#405)");
            string merged = File.ReadAllText(P("photo.xmp"));
            Check(Canonical(merged) == Canonical(original) && merged.StartsWith(declaration) && merged.EndsWith("?>\n"),
                  "the shared photo.xmp keeps every foreign node byte for byte, Orion's fields aside, its whitespace and final newline included");

            // A raw of the same name escapes to its full name, with a notice, and never touches the other's.
            byte[] shared = File.ReadAllBytes(P("photo.xmp"));
            File.WriteAllBytes(P("photo.NEF"), []);
            string? said = null;
            void Hear(string m) => said = m;
            Sidecar.Noticed += Hear;
            try { Sidecar.Merge(P("photo.NEF"), s => s.Rating = 1); } finally { Sidecar.Noticed -= Hear; }
            Check(Sidecar.PathFor(P("photo.NEF")) == P("photo.NEF.xmp") && Sidecar.Read(P("photo.NEF")) is { Rating: 1 } && said != null
                  && File.ReadAllBytes(P("photo.xmp")).SequenceEqual(shared) && Sidecar.Read(P("photo.ARW")) is { Rating: 4 }
                  && LibraryFiles.Siblings(P("photo.NEF")).Contains(P("photo.NEF.xmp")) && !LibraryFiles.Siblings(P("photo.NEF")).Contains(P("photo.xmp")),
                  $"photo.NEF beside photo.ARW's photo.xmp writes photo.NEF.xmp and says so ({said}); the Trash takes only its own");
            File.WriteAllText(P("other.xmp"), original.Replace("crs:Version=", "crs:RawFileName=\"other.NEF\" crs:Version="));
            Check(Sidecar.Read(P("other.ARW")) == null, "a photo.xmp whose crs:RawFileName names another photo is not this one's");
            File.WriteAllBytes(P("case.ARW"), []);
            File.WriteAllText(P("case.xmp"), original.Replace("crs:Version=", "crs:RawFileName=\"CASE.arw\" crs:Version="));
            Check(Sidecar.Read(P("case.ARW")) is { Rating: 2 }, "a crs:RawFileName differing only in case, with no such second file, is this photo's");

            // Migration: a DNG's edit Orion kept in photo.xmp before #405 is still read, and moves on the first write.
            foreach (var f in new[] { "lone.dng", "pair.dng", "pair.ARW", "named.dng", "named.ARW" }) File.WriteAllBytes(P(f), []);
            foreach (var f in new[] { "lone.xmp", "pair.xmp" }) File.WriteAllText(P(f), original);
            File.WriteAllText(P("named.xmp"), original.Replace("crs:Version=", "xmlns:orion=\"http://orion.photo/ns/1.0/\" orion:Source=\"named.dng\" crs:Version="));
            bool lone = Sidecar.Read(P("lone.dng")) is { Rating: 2, Label: "Red" } && Sidecar.Merge(P("lone.dng"), s => s.Rating = 5);
            Check(lone && File.ReadAllText(P("lone.xmp")) == original && Sidecar.Read(P("lone.dng")) is { Rating: 5, Label: "Red", Source: "lone.dng" }
                  && Canonical(File.ReadAllText(P("lone.dng.xmp"))) == Canonical(original),
                  "an unnamed legacy photo.xmp beside a lone photo.dng is read, and the first write lands in photo.dng.xmp with every node, photo.xmp unchanged");
            Check(Sidecar.Read(P("pair.dng")) == null && Sidecar.Read(P("pair.ARW")) is { Rating: 2 },
                  "an unnamed photo.xmp beside photo.dng and photo.ARW is the raw's, not the DNG's");
            Check(Sidecar.Read(P("named.dng")) is { Rating: 2 } && Sidecar.Read(P("named.ARW")) == null
                  && LibraryFiles.Siblings(P("named.dng")).Contains(P("named.xmp")) && !LibraryFiles.Siblings(P("pair.dng")).Contains(P("pair.xmp")),
                  "a photo.xmp naming photo.dng is the DNG's beside an ARW too; the Trash takes a legacy file only when it is unambiguously this photo's");
        } finally {
            Directory.Delete(dir, recursive: true);
        }
    }

    /// Every node, whitespace text included, serialized without formatting, after Orion's
    /// fields and the namespace declarations it may add are taken out.
    static string Canonical(string text) {
        var doc = ForeignNodes(text, LoadOptions.PreserveWhitespace);
        return string.Concat(doc.Nodes().Select(n => n.ToString(SaveOptions.DisableFormatting)));
    }
}
