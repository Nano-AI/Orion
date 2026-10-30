using System.Text;
using System.Text.Json.Nodes;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Headless;
using Avalonia.Input;
using Avalonia.Interactivity;
using Avalonia.LogicalTree;
using Avalonia.Threading;
using OrionShell.CanvasSlice;
using OrionShell.Masks;
using OrionShell.Sheets;

namespace OrionShell;

/// GitHub issue 23, items 4-5: mattes saved beside the photo, read back on open
/// (or said to be missing), made by Vision or the sky detector; the mask
/// colour pick and kind change; and Sync, through its alert, onto sidecars.
/// Everything writes in a temporary folder beside symlinks to the open photo.
static partial class SelfTest {
    static partial void MatteChecks() {
        PngChecks();
        var engine = EngineModel.Shared;
        if (engine.Handle == 0) return;   // Run already failed for want of a photo
        var list = engine.Photos.ToList();
        int current = engine.Current;
        string source = engine.Photos[engine.Current], ext = Path.GetExtension(source);
        string dir = Directory.CreateTempSubdirectory("orion-matte-").FullName;
        try {
            string a = Path.Combine(dir, "a" + ext), b = Path.Combine(dir, "b" + ext);
            File.CreateSymbolicLink(a, source);
            File.CreateSymbolicLink(b, source);
            RestoreChecks(a);
            engine.Load([a, b]);
            OpenAndWait(engine, 0);
            if (engine.Session?.Path != a) { Check(false, "the matte checks open their copy"); return; }
            SelectChecks(engine, a);
            PickAndKindChecks(engine);
            SyncChecks(engine, a, b);
        } finally {
            engine.Load(list);
            OpenAndWait(engine, current);
            Directory.Delete(dir, recursive: true);
        }
    }

    static void OpenAndWait(EngineModel engine, int index) {
        if (index < 0 || index >= engine.Photos.Count) return;
        engine.Open(index);
        for (int i = 0; i < 6000 && engine.Opening; i++) { Dispatcher.UIThread.RunJobs(); Thread.Sleep(1); }
    }

    static void PngChecks() {
        var swift = MatteStore.Decode(File.ReadAllBytes(Path.Combine(Fixtures, "matte-swift.png")));
        // 497220 lit samples, all 0 or 1: counted by an independent decoder (Python's zlib, by hand).
        Check(swift is { Width: 1024, Height: 684 } m && m.Alpha.Count(v => v > 0.5f) == 497220 && m.Alpha.All(v => v is 0 or 1),
              "a sky matte the Swift app wrote (16-bit gray, ImageIO's Sub, Up and Paeth filters, iCCP) decodes sample for sample");
        var ramp = Enumerable.Range(0, 37 * 5).Select(i => (i % 37) / 36f).ToArray();
        ramp[3] = 1.2f;   // a producer's "covered" past 1 saturates rather than wrapping to a hole
        var back = MatteStore.Decode(MatteStore.Encode(ramp, 37, 5));
        Check(back is { Width: 37, Height: 5 } r && r.Alpha[3] == 1 && Enumerable.Range(0, ramp.Length).All(i => i == 3 || Math.Abs(r.Alpha[i] - ramp[i]) <= 0.5f / 65535),
              "a matte round-trips through the PNG to within half a 16-bit step, clamped to 0…1");
        if (swift is { } s) {
            var again = MatteStore.Decode(MatteStore.Encode(s.Alpha, s.Width, s.Height));
            Check(again != null && again.Value.Alpha.SequenceEqual(s.Alpha), "the Swift matte, rewritten here, reads back sample for sample");
        }
        Check(MatteStore.Decode("not a png"u8.ToArray()) == null, "bytes that are not a PNG are no matte");
    }

    /// Engine+Mask.swift's restoreMattes: a saved matte reaches the picture on open; a missing file is recorded.
    static void RestoreChecks(string photo) {
        using var probe = Session.Open(photo);
        if (probe == null || Native.orion_engine_max_matte_size(probe.Handle, out uint mw, out uint mh) != 0) { Check(false, "max matte size"); return; }
        int w = (int)mw, h = (int)mh;
        var alpha = new float[w * h];
        for (int i = 0; i < alpha.Length; i++) alpha[i] = i % w < w / 2 ? 1 : 0;   // the frame's left half
        string id = MatteStore.Write(alpha, w, h, photo);
        Check(File.Exists(MatteStore.PathFor(photo, id)) && Path.GetFileName(MatteStore.PathFor(photo, id)) == $"a.orion-matte-{id}.png"
              && !Directory.EnumerateFiles(Path.GetDirectoryName(photo)!, "*.orion-tmp").Any(),
              "a matte lands beside the photo as NAME.orion-matte-UUID.png, with no temporary file left");

        double Brightness(string matteId) {
            var state = DevelopJson.Defaults();
            state["maskSpace"] = 1;
            var row = DevelopJson.DefaultMask();
            (row["kind"], row["matteId"], row["matteSource"]) = (4, matteId, "Subject");
            state["maskComponents"] = new JsonArray(row);
            ((JsonObject)((JsonArray)state["layers"]!)[0]!)["exposureEv"] = 2f;
            new Sidecar { Develop = DevelopJson.Encode(state) }.Write(photo);
            using var s = Session.OpenEdited(photo);
            if (s == null) return -1;
            bool missing = MatteStore.IsMissing(s, matteId);
            s.Render(preview: false);
            double luma = MeanLuma(s.Read(Graph.Full));
            return missing ? -luma : luma;   // negative: the row was recorded missing
        }
        double restored = Brightness(id), absent = Brightness("00000000-0000-0000-0000-000000000000");
        Check(restored > 0 && absent < 0 && restored > -absent * 1.15,
              $"a sidecar's matte is read back on open and brightens what it covers; a missing file is recorded ({restored:0.0} vs {-absent:0.0})");
        File.Delete(Sidecar.PathFor(photo));
    }

    /// SubjectMatte.Select, the panel's Detect menu: the model, the file, the row, the picture, the sidecar.
    static void SelectChecks(EngineModel engine, string photo) {
        var group = MaskGroup.Shared;
        group.Clear();
        int before = Directory.EnumerateFiles(Path.GetDirectoryName(photo)!, "a.orion-matte-*.png").Count();
        double plain = MeanLuma(engine.Picture);
        string? said = SubjectMatte.Select(SubjectMatte.Kind.Subject, wait: true).GetAwaiter().GetResult();
        Console.WriteLine($"selftest: Subject: {said ?? "found"}");
        if (said != null) {
            Check(said.StartsWith("Nothing was found") && group.Shapes.Count == 0, $"a Subject refusal is said and adds no row ('{said}')");
        } else {
            var row = group.Current!;
            var state = engine.Session!.Capture(group.Shapes);
            var saved = (JsonObject)((JsonArray)state["maskComponents"]!)[0]!;
            Check(row is { Kind: 4, MatteSource: "Subject" } && row.MatteId != null && File.Exists(MatteStore.PathFor(photo, row.MatteId))
                  && !MatteStore.IsMissing(engine.Session, row.MatteId) && (string?)saved["matteId"] == row.MatteId
                  && Directory.EnumerateFiles(Path.GetDirectoryName(photo)!, "a.orion-matte-*.png").Count() == before + 1,
                  "Subject writes its matte beside the photo first, then a row naming it, which the sidecar carries");
            engine.Set("local_exposure_ev", 2, 0);
            // A subject is part of the frame: +2 EV through it moves the whole picture's mean by points, not percent.
            Check(MeanLuma(engine.Picture) > plain + 2, $"the selection reaches the picture ({plain:0.0} → {MeanLuma(engine.Picture):0.0})");
            // Rows move, the raster follows: a radial in front, the matte now in slot 1.
            group.Add(2, m => (m.RadiusX, m.RadiusY) = (0.01f, 0.01f));
            group.MoveLayer(1, -1);
            double moved = MeanLuma(engine.Picture);
            group.MoveLayer(0, 1);
            Check(Math.Abs(MeanLuma(engine.Picture) - moved) < 2 && group.Shapes[0].Kind == 4,
                  "a reordered matte row keeps its own raster");
        }
        string? person = SubjectMatte.Select(SubjectMatte.Kind.Person, wait: true).GetAwaiter().GetResult();
        string? sky = SubjectMatte.Select(SubjectMatte.Kind.Sky, wait: true).GetAwaiter().GetResult();
        Console.WriteLine($"selftest: Person: {person ?? "found"}; Sky: {sky ?? "found"}");
        Check(person is null or { Length: > 10 } && sky is null or { Length: > 10 } && !SubjectMatte.Running,
              "Person and Sky either fill a row or say why not");
        group.Clear();
        Check(group.Shapes.Count == 0 && engine.Session!.ReadMaskGroup().Count == 0, "clearing the masks clears the mattes' rows");
    }

    /// Engine+Mask.swift's pickMaskColor (a click on the canvas with the picker armed) and setMaskKind.
    static void PickAndKindChecks(EngineModel engine) {
        var group = MaskGroup.Shared;
        group.Add(6);
        var window = new Window { Width = 900, Height = 600, Content = new CanvasPanel() };
        window.Show();
        window.UpdateLayout();
        Dispatcher.UIThread.RunJobs();
        engine.Tool = CanvasTool.MaskColor;
        var middle = new Point(450, 300);
        window.MouseDown(middle, MouseButton.Left);
        window.MouseUp(middle, MouseButton.Left);
        Dispatcher.UIThread.RunJobs();
        var m = group.Current!;
        Check(engine.Tool == CanvasTool.None && group.ColorSwatch is { Length: 3 } && (m.ColorR, m.ColorG, m.ColorB) != (0.18f, 0.18f, 0.18f)
              && engine.Get("mask_components.color_r", 0) == m.ColorR,
              $"the colour picker takes the scene colour under the click into the row and disarms ({m.ColorR:0.000}, {m.ColorG:0.000}, {m.ColorB:0.000})");
        window.Close();

        (m.MatteId, m.MatteSource) = ("m-1", "Sky");
        group.SetKind(group.Selected, 2);
        Check(group.Current is { Kind: 2, MatteId: null, MatteSource: null } && engine.Get("mask_components.kind", 0) == 2,
              "a row changed to radial drops its matte reference");
        group.Clear();
    }

    /// SyncSettings.swift: key-level patches onto sidecars, none opened, then the open photo by the edit path.
    static void SyncChecks(EngineModel engine, string a, string b) {
        for (int g = 0; g < PresetStore.Groups.Length; g++) {
            var fields = PresetStore.Groups[g].Fields.ToHashSet();
            var keys = SyncSettings.Keys(g).Where(k => k != "curve").Select(Session.Snake).ToHashSet();
            Check(fields.SetEquals(keys) && (g == 3) == SyncSettings.Keys(g).Contains("curve"),
                  $"Sync's keys for {PresetStore.Groups[g].Title} are the preset's fields");
        }
        var s = engine.Session!;
        float asShotK = s.Get("temperature_k");
        engine.Set("exposure_ev", 0.5f);
        engine.Set("temperature_k", asShotK + 900);
        var copied = PresetStore.Capture("Copied", [.. Enumerable.Range(0, PresetStore.Groups.Length)])!;
        var state = s.Capture(MaskGroup.Shared.Shapes);
        engine.Set("exposure_ev", 0);

        // b has a sidecar with a rating and an edit the light group does not own.
        new Sidecar { Rating = 3, Develop = Encoding.UTF8.GetBytes("""{"tint":7,"exposureEv":-1}""") }.Write(b);
        var dialog = SyncSettings.Dialog(copied, state, new HashSet<int> { 1 });
        Button Pressing(string title) => dialog.GetLogicalDescendants().OfType<StateButton>().First(x => x.Label == title);
        Check(dialog.Title == "Sync to 2 photos?" && SyncSettings.Warning(new HashSet<int> { 1, 0 }, 2)
                  == "Writes White balance, Light to all 2 photos in view. This cannot be undone for photos other than the one open.",
              "the sync asks the counted question and names the groups");
        Pressing("Cancel").RaiseEvent(new RoutedEventArgs(Button.ClickEvent));
        Check(Encoding.UTF8.GetString(Sidecar.Read(b)!.Develop!) == """{"tint":7,"exposureEv":-1}""", "Cancel writes nothing");
        Pressing("Sync").RaiseEvent(new RoutedEventArgs(Button.ClickEvent));
        var written = JsonNode.Parse(Sidecar.Read(b)!.Develop!)!.AsObject();
        Check(Sidecar.Read(b)!.Rating == 3 && DevelopJson.Float(written["tint"]) == 7 && DevelopJson.Float(written["exposureEv"]) == 0.5f
              && written["temperatureK"] is null && written.Count == 8 && written.Select(p => p.Key).SequenceEqual(written.Select(p => p.Key).Order(StringComparer.Ordinal)),
              "Sync writes only the light keys over b's sidecar, keys sorted, keeping its rating and its tint");
        Check(engine.Get("exposure_ev") == 0.5f && engine.Status == "Synced 1 other photo.", $"the open photo takes the groups too, and it is said ('{engine.Status}')");
        using (var opened = Session.OpenEdited(b))
            Check(opened?.Get("exposure_ev") == 0.5f && opened.Get("tint") == 7, "b opens with the synced light and its own tint");

        // No sidecar: the camera's white balance stands, not 5500 K and not the copy's (WB was not ticked).
        File.Delete(Sidecar.PathFor(b));
        var outcome = SyncSettings.Run(copied, state, new HashSet<int> { 1 });
        using (var fresh = Session.OpenEdited(b))
            Check(outcome.Written == 1 && fresh?.Get("temperature_k") == asShotK && fresh.Get("exposure_ev") == 0.5f,
                  "a photo with no sidecar keeps its as-shot white balance under a light sync");

        // A write that fails is counted and named; an edit that is not JSON is never overwritten.
        var failed = SyncSettings.Sync(state, new HashSet<int> { 1 }, [a, b], _ => null, (_, _) => false);
        Check(failed.Written == 0 && failed.Complaint == "2 photos could not be written, starting with a" + Path.GetExtension(a) + ".",
              "a sync that did not land says which photos");
        new Sidecar { Develop = "not json"u8.ToArray() }.Write(b);
        Check(SyncSettings.Run(copied, state, new HashSet<int> { 1 }).Failed.SequenceEqual([b])
              && Encoding.UTF8.GetString(Sidecar.Read(b)!.Develop!) == "not json" && engine.Status.EndsWith("could not be written."),
              "an edit that is not a JSON object is left alone and reported");
        engine.Set("temperature_k", asShotK);
    }
}
