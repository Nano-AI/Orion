using System.Text;
using System.Text.Json.Nodes;
using Avalonia.Controls;

namespace OrionShell;

/// The sidecar (GitHub issue 23): Fixtures/ are sidecars the Swift app wrote, read here
/// and written back byte for byte; then a save through Autosave and a reopen
/// on a copy of the open photo in a temporary folder, never beside the photo.
static partial class SelfTest {
    static string Fixtures => Path.GetFullPath(Path.Combine(AppContext.BaseDirectory, "..", "..", "..", "Fixtures"));
    static byte[] FixtureEdit(string fixture) => Sidecar.Read(Path.Combine(Fixtures, Path.ChangeExtension(fixture, ".ARW")))!.Develop!;

    // The sidecar's keys, frozen from the Swift app's EditHistory.swift at #380 (GoldenChecks).
    static readonly string[] DevelopKeys = [
        "blacks", "clarity", "contrast", "cropH", "cropW", "cropX", "cropY", "curve", "dehaze", "denoiseColor",
        "denoiseDetail", "denoiseLuma", "denoiseNlm", "denoiseSmoothness", "exposureEv", "fusion", "gradeBalance",
        "gradeHighlight", "gradeMidtone", "gradeShadow", "grainAmount", "grainSize", "highlightRecovery",
        "highlights", "hueShift", "layers", "lensCaBlue", "lensCaRed", "lensChoice", "lensDistortion", "lensVignette",
        "localTintAxis", "lumShift", "lutStrength", "maskComponents", "maskRefine", "maskSpace", "perspectiveAspect",
        "perspectiveHorizontal", "perspectiveVertical", "process", "rotateQuarters", "satShift", "saturation",
        "shadows", "sharpenAmount", "sharpenMasking", "sharpenRadius", "spots", "straightenDeg", "temperatureK",
        "texture", "tint", "vibrance", "vignetteAmount", "vignetteFieldAngle", "whites"];
    static readonly string[] MaskKeys = [
        "angle", "brushErase", "brushFlow", "brushHardness", "brushRadius", "brushStroke", "centerX", "centerY",
        "colorB", "colorG", "colorR", "colorSoft", "colorTol", "compose", "feather", "hidden", "invert", "kind",
        "length", "matteId", "matteSource", "name", "radiusX", "radiusY", "rangeHi", "rangeLo", "rangeSoft",
        "roundness", "startsLayer"];
    static readonly string[] LayerKeys = [
        "blacks", "contrast", "denoiseColor", "denoiseLuma", "exposureEv", "highlights", "saturation", "shadows",
        "tint", "warmth", "whites"];

    static partial void SidecarChecks() {
        GoldenChecks();
        ForeignXmpChecks();
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

        // The keys are the sidecar format's: a key added or dropped here changes
        // what every photographer's saved edits mean, so it must change these
        // lists in the same commit. Frozen from the Swift app's EditHistory.swift
        // (fieldRoster, LocalAdjustState) when it was retired (#380).
        Check(DevelopKeys.ToHashSet().SetEquals(DevelopJson.Defaults().Select(p => p.Key)), $"DevelopJson has DevelopState's {DevelopKeys.Length} keys");
        Check(MaskKeys.ToHashSet().SetEquals(DevelopJson.DefaultMask().Select(p => p.Key).Concat(["matteId", "matteSource", "name"])),
              $"a mask row has MaskComponentState's {MaskKeys.Length} keys");
        Check(LayerKeys.ToHashSet().SetEquals(DevelopJson.DefaultLayer().Select(p => p.Key)), "a layer has LocalAdjustState's keys");

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
            File.WriteAllText(Sidecar.PathFor(photo), new Sidecar { Rejected = true }.Text().Replace("xmp:Rating", "orion:Develop=\"!!\" xmp:Rating"));
            Check(Sidecar.Read(photo) is { Rejected: true, Rating: 0, Unreadable: not null }
                  && !Sidecar.Merge(photo, s => s.Develop = [1]) && !label.Write(photo) && File.ReadAllText(Sidecar.PathFor(photo)).Contains("!!"),
                  "-1 reads as rejected; an edit that is not base64 is never written over, by Merge or by Write");
            // Present but unreadable is not absent: a rename would replace it, so Merge must refuse.
            File.Delete(Sidecar.PathFor(photo));
            label.Write(photo);
            File.SetUnixFileMode(Sidecar.PathFor(photo), UnixFileMode.None);
            bool refused = Sidecar.Read(photo) is { Unreadable: not null } && !Sidecar.Merge(photo, s => s.Develop = [1]);
            File.SetUnixFileMode(Sidecar.PathFor(photo), UnixFileMode.UserRead | UnixFileMode.UserWrite);
            Check(refused && Sidecar.Read(photo) is { Rating: 3, Develop: null }, "a sidecar that exists but cannot be read is never written over");
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
            s.ToFrameSpace(want);   // a legacy mask converts on the way in, under this photo's own orientation
            var got = s.Capture();
            var diff = Differences(want, got, "").ToList();
            Check(s.Unreadable == null && diff.Count == 0,
                  $"{Path.GetFileName(file)}: opens with every value it saved" + (diff.Count > 0 ? $"; differs at {string.Join(", ", diff.Take(6))}" : ""));
        }

        File.WriteAllText(Sidecar.PathFor(photo), new Sidecar { Develop = "not json"u8.ToArray() }.Text());
        using (var bad = Session.OpenEdited(photo))
            Check(bad?.Unreadable != null && bad.Get("exposure_ev") == 0, "an edit that is not JSON opens the photo as shot and says so");

        // Corrupt in ways the JSON parser accepts: none may throw out of the open (the cache's prefetch
        // task would fault), each opens as shot, unreadable. The last one is otherwise valid.
        string[] corrupt = [
            """{"exposureEv":1,"exposureEv":2}""",
            """{"exposureEv":1,"maskComponents":[{"kind":2,"kind":3}]}""",
            """{"exposureEv":1,"layers":[{"tint":0.5,"tint":0.2}]}""",
            """{"exposureEv":1,"futureKnob":1,"futureKnob":2}""",
        ];
        foreach (var json in corrupt) {
            File.WriteAllText(Sidecar.PathFor(photo), new Sidecar { Develop = Encoding.UTF8.GetBytes(json) }.Text());
            Session? opened = null;
            Exception? thrown = null;
            try { opened = Session.OpenEdited(photo); } catch (Exception e) { thrown = e; }
            Check(thrown == null && opened is { Unreadable: not null } && opened.Get("exposure_ev") == 0,
                  $"a corrupt edit opens as shot and unreadable, never throwing ({json}{(thrown != null ? $": {thrown.GetType().Name}" : "")})");
            opened?.Dispose();
        }

        // A sidecar that exists and cannot be read opens unreadable, never as "no edit".
        File.WriteAllText(Sidecar.PathFor(photo), new Sidecar { Develop = "{\"exposureEv\":1}"u8.ToArray() }.Text());
        File.SetUnixFileMode(Sidecar.PathFor(photo), UnixFileMode.None);
        using (var locked = Session.OpenEdited(photo))
            Check(locked is { Unreadable: not null, Document: null }, "a sidecar it may not read opens unreadable, so autosave stays off");
        File.SetUnixFileMode(Sidecar.PathFor(photo), UnixFileMode.UserRead | UnixFileMode.UserWrite);

        // A newer build's key rides through a save; a legacy key is read and not written back.
        File.WriteAllText(Sidecar.PathFor(photo), new Sidecar { Develop = """{"exposureEv":0.5,"futureKnob":{"a":[1,2]},"denoiseColour":2}"""u8.ToArray() }.Text());
        using (var newer = Session.OpenEdited(photo)) {
            var saved = newer?.Capture();
            Check(saved != null && JsonNode.DeepEquals(saved["futureKnob"], JsonNode.Parse("""{"a":[1,2]}""")) && !saved.ContainsKey("denoiseColour")
                  && DevelopJson.Float(saved["denoiseColor"]) == 2 && newer!.Get("exposure_ev") == 0.5f,
                  "an unknown key from a newer build is kept on save; a legacy key is not written back");
        }
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
        a.Note(State(5));   // an edit on the next photo while the disk is still gone
        Check(a.IsDirty && a.LastFailure != null && a.LastFailure.Contains("one"), "an edit on another photo does not clear the complaint");
        disk = true;
        a.Flush();
        Check(!a.IsDirty && a.LastFailure == null && writes[^1].Path == "one" && (float)writes[^1].State["exposureEv"]! == 4,
              "the retry lands under the photo it belongs to, never replaced by the next photo's edit, and the complaint clears");
        a.Note(State(5));
        a.Flush();
        Check(writes[^1].Path == "two" && (float)writes[^1].State["exposureEv"]! == 5, "the next photo's edit then saves under its own name");

        // canLeavePhoto: while the open photo's edit will not write, nothing leaves it.
        var engine = EngineModel.Shared;
        string locked = Directory.CreateTempSubdirectory("orion-locked-").FullName;
        try {
            string owed = Path.Combine(locked, "owed.ARW");
            File.SetUnixFileMode(locked, UnixFileMode.UserRead | UnixFileMode.UserExecute);
            Autosave.Shared.Begin(owed, State(0));
            Autosave.Shared.Note(State(1));
            Autosave.Shared.Flush();
            int at = engine.Current;
            bool left = engine.Open(at) || engine.Next() || engine.Previous() || engine.Load(engine.Photos.ToList());
            Check(!left && engine.Current == at && Autosave.Shared.LastFailure != null && engine.Status == Autosave.Shared.LastFailure,
                  $"a photo whose edit will not save cannot be left, and the status line says why ('{engine.Status}')");
            // The drawn line, not the model: deleting the footer's failure branch was green everywhere (#125).
            var footer = new Chrome.StatusPanel();
            var host = Show(footer);
            var said = (TextBlock)((StackPanel)footer.Child!).Children[1];
            Check(footer.IsVisible && said.IsVisible && said.Text == Autosave.Shared.LastFailure && said.Foreground == Palette.Star,
                  $"the footer draws the failed write in the warning colour ('{said.Text}')");
            host.Close();
            // Trash, the departure the Swift probe drove that the switch above does not:
            // the open photo stays, nothing moves, and the failure is the answer.
            var moved = new List<string>();
            string? trashWhy = Library.LibraryFiles.Trash([engine.Photos[at]], p => { moved.Add(p); return null; });
            Check(trashWhy == Autosave.Shared.LastFailure && moved.Count == 0 && engine.Current == at && engine.Photos.Count > at,
                  "trashing the open photo while its edit will not save moves nothing and says why");
            File.SetUnixFileMode(locked, UnixFileMode.UserRead | UnixFileMode.UserWrite | UnixFileMode.UserExecute);
            Check(Autosave.Shared.FlushBeforeLeaving() && File.Exists(Sidecar.PathFor(owed)) && Autosave.Shared.LastFailure == null,
                  "once the folder is writable again the owed edit lands");
            engine.Say("");
        } finally {
            Autosave.Shared.Stop();
            File.SetUnixFileMode(locked, UnixFileMode.UserRead | UnixFileMode.UserWrite | UnixFileMode.UserExecute);
            Directory.Delete(locked, recursive: true);
        }

        // A failed render, drawn: the footer names it, and stops when the next render lands.
        // Needs an open photo (the footer says nothing of renders without one), so no handle is a failure, not a skip.
        {
            var footer = new Chrome.StatusPanel();
            var host = Show(footer);
            var said = (TextBlock)((StackPanel)footer.Child!).Children[1];
            string timing = engine.Timing;
            engine.SetTiming("render failed: probe");
            bool drawn = said.IsVisible && said.Text == "Render failed: probe" && said.Foreground == Palette.Star;
            engine.SetTiming(timing);
            bool cleared = said.Text != "Render failed: probe";
            host.Close();   // off screen it stops listening to the shared models
            engine.SetTiming("render failed: after close");
            bool detached = said.Text != "Render failed: after close";
            engine.SetTiming(timing);
            Check(engine.Handle != 0 && drawn && cleared && detached,
                  $"the footer draws a failed render, clears it after, and stops listening once closed (handle {engine.Handle != 0}, '{said.Text}')");
        }

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
