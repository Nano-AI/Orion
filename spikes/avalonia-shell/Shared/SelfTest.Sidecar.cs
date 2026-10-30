using System.Text;
using System.Text.Json.Nodes;
using System.Text.RegularExpressions;

namespace OrionShell;

/// The sidecar (GitHub issue 23): Fixtures/ are sidecars the Swift app wrote, read here
/// and written back byte for byte; then a save through Autosave and a reopen
/// on a copy of the open photo in a temporary folder, never beside the photo.
static partial class SelfTest {
    static string Fixtures => Path.GetFullPath(Path.Combine(AppContext.BaseDirectory, "..", "..", "..", "Fixtures"));
    static byte[] FixtureEdit(string fixture) => Sidecar.Read(Path.Combine(Fixtures, Path.ChangeExtension(fixture, ".ARW")))!.Develop!;

    static partial void SidecarChecks() {
        GoldenChecks();
        var engine = EngineModel.Shared;
        if (engine.Handle == 0) return;   // Run already failed for want of a photo
        string dir = Directory.CreateTempSubdirectory("orion-sidecar-").FullName;
        try {
            string photo = Path.Combine(dir, "photo" + Path.GetExtension(engine.Photos[engine.Current]));
            File.CreateSymbolicLink(photo, engine.Photos[engine.Current]);
            EngineChecks(photo);
            AutosaveChecks(photo);
        } finally {
            Directory.Delete(dir, recursive: true);
        }
    }

    static void GoldenChecks() {
        var files = Directory.EnumerateFiles(Fixtures, "*.xmp").ToList();
        Check(files.Count >= 3, $"{files.Count} Swift-written sidecars in Fixtures/");
        foreach (var file in files) {
            var s = Sidecar.Read(Path.ChangeExtension(file, ".ARW"))!;
            Check(s.Text() == File.ReadAllText(file, Encoding.UTF8), $"{Path.GetFileName(file)}: rewritten byte for byte");
            var state = DevelopJson.Decode(s.Develop!)!;
            var again = DevelopJson.Decode(DevelopJson.Encode(state));
            Check(again != null && JsonNode.DeepEquals(state, again), $"{Path.GetFileName(file)}: decode ∘ encode is a fixpoint");
        }

        var legacy = DevelopJson.Decode(FixtureEdit("sidecar-legacy.xmp"))!;
        Check(DevelopJson.Float(legacy["temperatureK"]) == 2780 && DevelopJson.Float(legacy["vignetteFieldAngle"]) == 55
              && DevelopJson.Float(legacy["tint"]) == 1.0384247e-07f && DevelopJson.Int(legacy["process"]) == 2
              && DevelopJson.Float(legacy["denoiseDetail"]) == 0 && ((JsonArray)legacy["layers"]!).Count == 1,
              "legacy sidecar: its values read, keys it predates take their defaults, no tone bands keeps process 2");
        var matte = DevelopJson.Decode(FixtureEdit("sidecar-matte.xmp"))!;
        var row = (JsonObject)((JsonArray)matte["maskComponents"]!)[0]!;
        Check(DevelopJson.Int(matte["maskSpace"]) == 0 && DevelopJson.Int(matte["process"]) == 1 && DevelopJson.Int(row["kind"]) == 4
              && (string?)row["matteId"] == "df311765-ecc4-4340-950f-67377ec54630" && (string?)row["matteSource"] == "Subject"
              && DevelopJson.Float(((JsonObject)((JsonArray)matte["layers"]!)[0]!)["exposureEv"]) == 1.5f,
              "matte sidecar: masks without a marker are display-space, whites without one are process 1, the matte keeps its id");
        var spot = DevelopJson.Decode(FixtureEdit("sidecar-spot.xmp"))!;
        var s0 = (JsonObject)((JsonArray)spot["spots"]!)[0]!;
        Check(DevelopJson.Float(s0["srcX"]) == 0.6f && s0["heal"]!.GetValue<bool>() == false && DevelopJson.Float(spot["cropW"]) == 0.7f,
              "spot sidecar: the spot and the crop read");

        // The legacy keys Swift still reads.
        var old = DevelopJson.Decode(Encoding.UTF8.GetBytes("""
            {"denoiseColour":2,"localExposureEv":0.5,"localTint":0.25,"maskKind":2,"maskCentreX":0.25,"maskInvert":true,
             "hueShift":[1,2],"rotateQuarters":1.5,"curve":{"master":[]}}
            """))!;
        var lifted = (JsonObject)((JsonArray)old["maskComponents"]!)[0]!;
        var first = (JsonObject)((JsonArray)old["layers"]!)[0]!;
        Check(DevelopJson.Float(old["denoiseColor"]) == 2 && DevelopJson.Float(first["exposureEv"]) == 0.5f
              && DevelopJson.Float(first["tint"]) == -0.25f && DevelopJson.Int(old["localTintAxis"]) == 1
              && DevelopJson.Int(lifted["kind"]) == 2 && DevelopJson.Float(lifted["centerX"]) == 0.25f && lifted["invert"]!.GetValue<bool>()
              && ((JsonArray)old["hueShift"]!).Count == 8 && DevelopJson.Int(old["rotateQuarters"]) == 0
              && ((JsonArray)old["curve"]!["red"]!).Count == 2,
              "legacy keys: British spellings, the scalar layer, the single mask, the flipped tint; malformed values keep defaults");
        Check(DevelopJson.Decode(Encoding.UTF8.GetBytes("not json")) == null && DevelopJson.Decode("[]"u8.ToArray()) is { } blank
              && JsonNode.DeepEquals(blank, DevelopJson.Defaults()), "bytes that are not JSON fail; JSON that is not an object is every default");

        // The keys are Swift's: a field added to DevelopState that this port does not know fails here.
        var swift = File.ReadAllText(Path.Combine(Directory.GetCurrentDirectory(), "..", "app", "EditHistory.swift"));
        var rosters = Regex.Matches(swift, @"static let fieldRoster: Set<String> = \[(.*?)\]", RegexOptions.Singleline)
            .Select(m => Regex.Matches(m.Groups[1].Value, "\"(\\w+)\"").Select(k => k.Groups[1].Value).ToHashSet()).ToList();
        var develop = rosters.FirstOrDefault(r => r.Contains("temperatureK")) ?? [];
        var mask = rosters.FirstOrDefault(r => r.Contains("centerX")) ?? [];
        var local = Regex.Match(swift, @"struct LocalAdjustState[^{]*\{(.*?)\n\}", RegexOptions.Singleline).Groups[1].Value;
        Check(develop.SetEquals(DevelopJson.Defaults().Select(p => p.Key)), $"DevelopJson has DevelopState's {develop.Count} keys");
        Check(mask.SetEquals(DevelopJson.DefaultMask().Select(p => p.Key).Concat(["matteId", "matteSource", "name"])),
              $"a mask row has MaskComponentState's {mask.Count} keys");
        Check(Regex.Matches(local, @"var (\w+)").Select(m => m.Groups[1].Value).ToHashSet().SetEquals(DevelopJson.DefaultLayer().Select(p => p.Key)),
              "a layer has LocalAdjustState's keys");

        // Every number DevelopState holds is an OrionAdjustments field of the same name, at its length.
        var fields = EngineModel.Fields.ToDictionary(f => f.Name, f => f.Length);
        var unmapped = DevelopJson.Defaults().Where(p => p.Key is not ("maskSpace" or "localTintAxis"))
            .Where(p => p.Value is JsonValue v ? v.GetValueKind() == System.Text.Json.JsonValueKind.Number && fields.GetValueOrDefault(Session.Snake(p.Key)) != 1
                      : p.Value is JsonArray a && a.FirstOrDefault() is JsonValue && fields.GetValueOrDefault(Session.Snake(p.Key)) != a.Count)
            .Select(p => p.Key)
            .Concat(DevelopJson.DefaultLayer().Select(p => "local_" + Session.Snake(p.Key)).Where(f => fields.GetValueOrDefault(f) != 8)).ToList();
        Check(unmapped.Count == 0, "every DevelopState number maps onto OrionAdjustments" + (unmapped.Count > 0 ? $"; not: {string.Join(", ", unmapped)}" : ""));

        // The envelope: a label survives escaping, and writing the edit keeps the rating.
        var label = new Sidecar { Rating = 3, Label = "R&D <\"x\">" };
        string dir = Directory.CreateTempSubdirectory("orion-envelope-").FullName;
        try {
            string photo = Path.Combine(dir, "a.ARW");
            Check(label.Write(photo) && Sidecar.Merge(photo, s => s.Develop = [123, 125]) && Sidecar.Read(photo) is
                      { Rating: 3, Rejected: false, Label: "R&D <\"x\">", Develop: [123, 125] }
                  && !File.Exists(Sidecar.PathFor(photo) + ".orion-tmp"),
                  "writing the edit keeps the rating and the escaped label, and leaves no temporary file");
            File.WriteAllText(Sidecar.PathFor(photo), "<x xmp:Rating=\"-1\" orion:Develop=\"!!\"/>");
            Check(Sidecar.Read(photo) is { Rejected: true, Rating: 0, DevelopUnreadable: true }
                  && !Sidecar.Merge(photo, s => s.Develop = [1]) && File.ReadAllText(Sidecar.PathFor(photo)).Contains("!!"),
                  "-1 reads as rejected; an edit that is not base64 is never written over");
        } finally {
            Directory.Delete(dir, recursive: true);
        }
    }

    /// Each Swift sidecar, beside a copy of the photo: what opens is what was saved.
    static void EngineChecks(string photo) {
        foreach (var file in Directory.EnumerateFiles(Fixtures, "*.xmp")) {
            File.Copy(file, Sidecar.PathFor(photo), overwrite: true);
            using var s = Session.OpenEdited(photo);
            if (s == null) { Check(false, $"{Path.GetFileName(file)}: the copy opens"); continue; }
            var probe = new Autosave(defer: _ => { }, write: (_, _) => false);
            probe.Begin(photo, s.Document!);
            probe.Note(s.Capture());
            Check(!probe.IsDirty, $"{Path.GetFileName(file)}: opening it owes no write");
            var want = DevelopJson.Decode(Sidecar.Read(photo)!.Develop!)!;
            want["maskSpace"] = 1;   // converted on the way in
            var got = s.Capture();
            var diff = Differences(want, got, "").ToList();
            Check(s.Unreadable == null && diff.Count == 0,
                  $"{Path.GetFileName(file)}: opens with every value it saved" + (diff.Count > 0 ? $"; differs at {string.Join(", ", diff.Take(6))}" : ""));
        }

        File.WriteAllText(Sidecar.PathFor(photo), new Sidecar { Develop = "not json"u8.ToArray() }.Text());
        using (var bad = Session.OpenEdited(photo))
            Check(bad?.Unreadable != null && bad.Get("exposure_ev") == 0, "an edit that is not JSON opens the photo as shot and says so");
        File.Delete(Sidecar.PathFor(photo));
    }

    /// Numbers to 1e-4 (the legacy matte row goes through the engine's frame conversion), everything else exactly.
    static IEnumerable<string> Differences(JsonNode? a, JsonNode? b, string at) {
        if (a is JsonObject oa && b is JsonObject ob) {
            foreach (var key in oa.Select(p => p.Key).Union(ob.Select(p => p.Key)))
                foreach (var d in Differences(oa[key], ob[key], $"{at}.{key}")) yield return d;
        } else if (a is JsonArray aa && b is JsonArray ab) {
            if (aa.Count != ab.Count) { yield return $"{at} ({aa.Count} vs {ab.Count} items)"; yield break; }
            for (int i = 0; i < aa.Count; i++)
                foreach (var d in Differences(aa[i], ab[i], $"{at}[{i}]")) yield return d;
        } else if (DevelopJson.Float(a) is { } x && DevelopJson.Float(b) is { } y) {
            if (Math.Abs(x - y) > 1e-4) yield return $"{at} ({x} vs {y})";
        } else if (!JsonNode.DeepEquals(a, b)) yield return $"{at} ({a?.ToJsonString()} vs {b?.ToJsonString()})";
    }

    /// Autosave.swift's invariants, then a save and a reopen.
    static void AutosaveChecks(string photo) {
        Action? fire = null;
        var writes = new List<(string Path, JsonObject State)>();
        bool disk = true;
        var a = new Autosave(defer: body => fire = body, write: (p, s) => { if (disk) writes.Add((p, s)); return disk; });
        JsonObject State(float ev) => new() { ["exposureEv"] = ev };
        a.Begin("one", State(0));
        a.Note(State(0));
        Check(!a.IsDirty && fire == null, "noting the saved state owes nothing");
        a.Note(State(1));
        var first = fire;
        fire = null;
        a.Note(State(2));
        Check(a.IsDirty && first != null && fire == null, "a burst of changes is one deferred write");
        first!();
        Check(writes.Count == 1 && (float)writes[0].State["exposureEv"]! == 2 && !a.IsDirty, "the deferred write takes the last state");
        a.Note(State(3));
        a.Note(State(2));
        Check(!a.IsDirty && a.LastFailure == null, "returning to the saved state cancels the owed write");
        disk = false;
        a.Note(State(4));
        fire!();
        Check(a.IsDirty && a.LastFailure != null && a.LastFailure.Contains("one"), "a failed write is kept and named on the status line");
        a.Begin("two", State(0));
        Check(a.IsDirty && writes.Count == 1, "a switch still owes the failed write");
        disk = true;
        a.Flush();
        Check(!a.IsDirty && a.LastFailure == null && writes[^1].Path == "one" && (float)writes[^1].State["exposureEv"]! == 4,
              "the retry lands under the photo it belongs to, and the complaint clears");

        // The real thing: a rated sidecar with no edit, an edit through Autosave, a reopen.
        new Sidecar { Rating = 4, Label = "Green" }.Write(photo);
        using (var s = Session.OpenEdited(photo)) {
            if (s == null) { Check(false, "the copy opens"); return; }
            var saver = new Autosave(defer: body => fire = body);
            saver.Begin(photo, s.Document!);
            s.Set("exposure_ev", 0.7f);
            s.Set("hue_shift", -0.3f, 5);
            s.Set("local_exposure_ev", 0.9f, 0);
            s.SetCurve(0, [(0, 0), (0.5f, 0.6f), (1, 1)]);
            s.SetMaskGroup([new MaskShape { Kind = 2, CenterX = 0.3f, Name = "Sky" }]);
            saver.Note(s.Capture([new MaskShape { Name = "Sky", MatteId = "m-1" }]));
            fire!();
            Check(!saver.IsDirty && Sidecar.Read(photo) is { Rating: 4, Label: "Green", Develop: not null }, "the edit is saved beside the rating and label");
        }
        using (var back = Session.OpenEdited(photo)) {
            var row = (JsonObject?)((JsonArray?)back?.Document?["maskComponents"])?.FirstOrDefault();
            Check(back is { Unreadable: null } && back.Get("exposure_ev") == 0.7f && back.Get("hue_shift", 5) == -0.3f
                  && back.Get("local_exposure_ev", 0) == 0.9f && back.Curve(0).SequenceEqual([(0f, 0f), (0.5f, 0.6f), (1f, 1f)])
                  && back.Get("mask_count") == 1 && back.Get("mask_components.center_x", 0) == 0.3f
                  && (string?)row?["name"] == "Sky" && (string?)row?["matteId"] == "m-1",
                  "reopened: exposure, color, layer, curve and mask come back, with the mask's name");
        }
    }
}
