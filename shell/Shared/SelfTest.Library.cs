using System.Text;
using System.Text.Json.Nodes;
using System.Text.RegularExpressions;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Interactivity;
using Avalonia.LogicalTree;
using OrionShell.CanvasSlice;
using OrionShell.Chrome;
using OrionShell.Develop;
using OrionShell.Library;
using OrionShell.Sheets;

namespace OrionShell;

/// GitHub issue 23, items 2-3: the selection, marks in the sidecar, saved
/// versions, and the history steps the stubs owed. Every write goes to a
/// temporary folder holding a link to the open photo, never beside it.
static partial class SelfTest {
    static void LibraryChecks() {
        SelectionLogic();
        CaptureTooltip();
        MergeEligibility();
        StackGroupChecks();
        var engine = EngineModel.Shared;
        if (engine.Handle == 0) return;   // Run already failed for want of a photo
        StackDismissal(engine);
        StackMemoryWarning(engine);
        string dir = Directory.CreateTempSubdirectory("orion-library-").FullName;
        try {
            string photo = Path.Combine(dir, "photo" + Path.GetExtension(engine.Photos[engine.Current]));
            File.CreateSymbolicLink(photo, engine.Photos[engine.Current]);
            MarksInSidecar(photo);
            Versions(photo);
            VersionRestore(engine);
            GallerySizeRemembered(dir);
        } finally {
            Directory.Delete(dir, recursive: true);
        }
        SpotAndWheelHistory(engine);
        MaskIdsInHistory(engine);
        GroupCulling(engine);
        MarksOffTheUiThread(engine);
    }

    /// Each history entry holds its mask rows with their names and matte ids
    /// (#207): put back by position, a removal's undo slid every id up a row.
    static void MaskIdsInHistory(EngineModel engine) {
        var group = Masks.MaskGroup.Shared;
        if (engine.Session is not { } session) return;
        group.Clear();
        string Ids() => string.Join(",", group.Shapes.Select(m => $"{m.Name}:{m.MatteId}"));
        group.Add(1, m => (m.Name, m.MatteId) = ("A", "id-a"));
        group.Add(2, m => (m.Name, m.MatteId) = ("B", "id-b"));
        group.Add(1, m => (m.Name, m.MatteId) = ("C", "id-c"));
        EditHistory.Undo();
        bool undone = Ids() == "A:id-a,B:id-b";
        EditHistory.Redo();
        Check(undone && Ids() == "A:id-a,B:id-b,C:id-c" && session.Capture(group.Shapes)["maskComponents"]![2]!["matteId"]!.GetValue<string>() == "id-c",
              $"an added row undone and redone comes back with its own name and matte id ('{Ids()}')");
        group.Remove(0);
        EditHistory.Undo();
        Check(Ids() == "A:id-a,B:id-b,C:id-c" && session.ReadMaskGroup().Count == 3, $"a removed row undone comes back with its id, the others unshifted ('{Ids()}')");
        var version = new SnapshotStore.Snapshot(Guid.NewGuid(), "bare", DateTimeOffset.Now, DevelopJson.Defaults());
        SnapshotStore.Assign(version);
        bool bare = group.Shapes.Count == 0;
        EditHistory.Undo();
        Check(bare && Ids() == "A:id-a,B:id-b,C:id-c", $"undoing a version restore brings back the masks it replaced, ids and all ('{Ids()}')");
        group.Clear();
    }

    /// The listing shows at once and its marks fill in from a background read;
    /// a folder opened again rereads its sidecars rather than keeping stale marks.
    static void MarksOffTheUiThread(EngineModel engine) {
        var library = LibraryModel.Shared;
        var list = engine.Photos.ToList();
        int current = engine.Current;
        string dir = Directory.CreateTempSubdirectory("orion-marks-").FullName;
        try {
            string a = Path.Combine(dir, "a.ARW"), b = Path.Combine(dir, "b.ARW");
            File.CreateSymbolicLink(a, list[current]);
            File.CreateSymbolicLink(b, list[current]);
            new Sidecar { Rating = 4 }.Write(b);
            engine.Load([a, b]);
            bool deferred = library.Photos[1].Rating == 0;
            library.MarksRead.Wait();
            Avalonia.Threading.Dispatcher.UIThread.RunJobs();
            Check(deferred && library.Photos[1].Rating == 4, "a folder lists before its sidecars are read, and the marks fill in");
            new Sidecar { Rating = 2 }.Write(b);
            engine.Load([a, b]);
            _ = library.Photos;
            library.MarksRead.Wait();
            Avalonia.Threading.Dispatcher.UIThread.RunJobs();
            Check(library.Photos[1].Rating == 2, "opening the folder again reads a mark changed on disk since");
        } finally {
            engine.Load(list);
            engine.Open(current);
            Directory.Delete(dir, recursive: true);
        }
    }

    /// PhotoSelection.swift's rules, as ViewportTests+Selection pins them.
    static void SelectionLogic() {
        string[] v = ["a", "b", "c", "d", "e"];
        var s = new PhotoSelection();
        s.Focus("b");
        Check(!s.IsExplicit && s.Targets(v).SequenceEqual(v) && s.Summary(v) == "", "one selected photo is no selection: a batch takes everything in view");
        Check(s.Click("d", PhotoSelection.Modifiers.Command, v) == null && s.IsExplicit && s.Targets(v).SequenceEqual(["b", "d"]) && s.Summary(v) == "2 selected",
              "a command-click adds without moving the canvas");
        s.Click("b", PhotoSelection.Modifiers.Command, v);
        Check(s.Contains("b"), "the photo on the canvas cannot be deselected");
        Check(s.Click("e", PhotoSelection.Modifiers.Shift, v) == null && s.Targets(v).SequenceEqual(["b", "c", "d", "e"]),
              "a shift-click selects the range from the anchor, the canvas photo kept");
        s.Confine(["a", "b", "c"]);
        Check(s.Targets(["a", "b", "c"]).SequenceEqual(["b", "c"]), "a filter drops what it hides from the selection");
        s.SelectAll(v);
        Check(s.Count == 5, "select all takes everything in view");
        Check(s.Click("a", PhotoSelection.Modifiers.None, v) == "a" && !s.IsExplicit && s.Current == "a", "a plain click focuses and collapses");
    }

    static void CaptureTooltip() {
        var info = new CaptureInfo("Sony", "", 35, 4, 1 / 250f, 800, null, 0, 0);
        Check(CaptureInfo.Tooltip("a.ARW", info) == "a.ARW\nf/4 · 1/250 s · ISO 800" && CaptureInfo.Tooltip("a.ARW", null) == "a.ARW",
              "the browsing tooltip is the name, then aperture · shutter · ISO (CaptureInfo.tooltip)");
    }

    static void MergeEligibility() {
        static HdrMergePanel.Candidate C(string camera) => new("x.ARW", camera, 100, 0.01f, 8);
        Check(HdrMergePanel.Ineligibility([C("A")]) == "Select at least two exposures to merge."
              && HdrMergePanel.Ineligibility([C("B"), C("A")]) == "These photos come from different cameras: A, B."
              && HdrMergePanel.Ineligibility([C("A"), C("A"), C("")]) == null, "the merge refuses one frame or two cameras (HdrMergeFlow.ineligibility)");
    }

    /// Library.persist: merged into the sidecar in Swift's bytes, the edit kept; read back when listed; never headless.
    static void MarksInSidecar(string photo) {
        var library = LibraryModel.Shared;
        byte[] edit = DevelopJson.Encode(DevelopJson.Defaults());
        Sidecar.Merge(photo, s => (s.Develop, s.Label) = (edit, "Red"));
        var p = LibraryModel.Listed(photo, 0);
        library.SetRating(p, 3);
        Check(Sidecar.Read(photo)!.Rating == 0, "headless, a rating never reaches the sidecar");
        library.Writes = true;
        try {
            library.SetRejected(p, true);
            var rejected = Sidecar.Read(photo)!;
            Check(rejected.Rejected && p.Rating == 0 && File.ReadAllText(Sidecar.PathFor(photo)).Contains("xmp:Rating=\"-1\""),
                  "a rejection is xmp:Rating -1 in the sidecar and clears the rating");
            library.SetRating(p, 4);
            var want = new Sidecar { Rating = 4, Label = "Red", Develop = edit }.Text();
            Check(!p.Rejected && File.ReadAllText(Sidecar.PathFor(photo), Encoding.UTF8) == want,
                  "a rating un-rejects and lands as Swift writes it, the label and the edit kept");
            var listed = LibraryModel.Listed(photo, 0);
            Check(listed.Rating == 4 && !listed.Rejected && listed.Label == "Red", "a folder lists each photo's marks from its sidecar");
            File.WriteAllText(Sidecar.PathFor(photo), "<x orion:Develop=\"%%%\"/>");
            library.SetRating(p, 2);
            Check(File.ReadAllText(Sidecar.PathFor(photo)) == "<x orion:Develop=\"%%%\"/>" && EngineModel.Shared.Status.Contains("could not be saved"),
                  "a sidecar that will not read is never written over, and the status line says the rating did not save");
            EngineModel.Shared.Say("");
        } finally {
            library.Writes = false;
            File.Delete(Sidecar.PathFor(photo));
        }
    }

    /// SnapshotStore: Swift's file shape, replace by name, the automatic slot, three states.
    static void Versions(string photo) {
        var store = new SnapshotStore();
        store.Open(photo);
        Check(store.Snapshots.Count == 0 && !store.Unreadable, "no versions file reads as no versions");
        var warm = DevelopJson.Defaults();
        warm["temperatureK"] = 7200f;
        store.Save("  warmer ", warm);
        store.Save("warmer", warm);
        string text = File.ReadAllText(SnapshotStore.PathFor(photo));
        var back = SnapshotStore.Read(photo)!;
        Check(back.Count == 1 && back[0].Name == "warmer" && Regex.IsMatch(text, "\"created\":\"\\d{4}-\\d\\d-\\d\\dT\\d\\d:\\d\\d:\\d\\dZ\"")
              && Regex.IsMatch(text, "\"id\":\"[0-9A-F-]{36}\"") && text.Contains("\"automatic\":false")
              && DevelopJson.Float(back[0].State["temperatureK"]) == 7200,
              "a version is saved trimmed, replaced by name, in Swift's shape: uppercase id, whole-second ISO date, the state");

        var cool = DevelopJson.Defaults();
        cool["temperatureK"] = 4000f;
        SnapshotStore.Snapshot? assigned = null;
        store.Restore(store.Snapshots[0], cool, s => assigned = s);
        store.Restore(store.Snapshots[1], DevelopJson.Defaults(), s => assigned = s);
        Check(assigned?.Name == "warmer" && store.Snapshots.Count(s => s.Automatic) == 1 && store.Snapshots[0].Name == "Before restoring warmer",
              "restoring keeps the working edit first, in one automatic slot");
        store.Restore(store.Snapshots[1], warm, _ => { });
        Check(DevelopJson.Float(store.Snapshots[0].State["temperatureK"]) == DevelopJson.Float(DevelopJson.Defaults()["temperatureK"]),
              "restoring while the working edit is already a named version keeps no copy of it");
        store.Rename(store.Snapshots[0].Id, "kept");
        Check(store.Snapshots.All(s => !s.Automatic) && SnapshotStore.Read(photo)!.Any(s => s.Name == "kept"), "renaming the automatic version keeps it");
        store.Remove(store.Snapshots[0].Id);
        Check(SnapshotStore.Read(photo)!.Count == 1, "a deleted version leaves the file");
        Check(Throws<SnapshotStore.Refusal>(() => store.Save(" ", warm), "name"), "a version needs a name");

        File.WriteAllText(SnapshotStore.PathFor(photo), "{ not json");
        store.Open(photo);
        Check(store.Unreadable && store.Snapshots.Count == 0 && Throws<SnapshotStore.Refusal>(() => store.Save("x", warm), "could not be read")
              && File.ReadAllText(SnapshotStore.PathFor(photo)) == "{ not json", "an unreadable versions file shows nothing and is never written over");
        File.Delete(SnapshotStore.PathFor(photo));

        var raster = DevelopJson.Defaults();
        var mask = DevelopJson.DefaultMask();
        (mask["kind"], mask["matteId"], mask["matteSource"]) = (4, "gone", "Sky");
        raster["maskComponents"] = new JsonArray(mask);
        Check(SnapshotStore.MissingMattes(new(Guid.NewGuid(), "v", DateTimeOffset.Now, raster), photo).SequenceEqual(["Sky"]),
              "a version naming a matte that is not beside the photo says so");
    }

    /// Engine.restore(snapshot:): the whole state on the open photo, one ⌘Z away.
    static void VersionRestore(EngineModel engine) {
        float ev = engine.Get("exposure_ev");
        engine.Set("exposure_ev", 0.75f);
        var saved = new SnapshotStore.Snapshot(Guid.NewGuid(), "bright", DateTimeOffset.Now, engine.Session!.Capture(Masks.MaskGroup.Shared.Shapes));
        engine.Set("exposure_ev", -1);
        SnapshotStore.Assign(saved);
        bool restored = engine.Get("exposure_ev") == 0.75f && EditHistory.UndoLabel == "Version bright";
        EditHistory.Undo();
        Check(restored && engine.Get("exposure_ev") == -1, "restoring a version puts its state back as one step, \"Version bright\", that ⌘Z takes back");
        engine.Set("exposure_ev", ev);
    }

    /// GalleryView's @AppStorage("galleryCellSize"): the size a person chose comes back.
    static void GallerySizeRemembered(string dir) {
        ShellSettings.File = Path.Combine(dir, "settings.json");
        try {
            ShellSettings.Set(GalleryPanel.CellSizeKey, 260);
            var slider = new GalleryPanel().GetLogicalDescendants().OfType<Slider>().First();
            bool came = slider.Value == 260;
            slider.Value = 300;
            Check(came && ShellSettings.Get(GalleryPanel.CellSizeKey, 0) == 300, "the gallery's cell size is remembered across launches");
        } finally {
            ShellSettings.File = null;
        }
    }

    /// commitSpotEdit and the wheel's reset: each one step under its own name.
    static void SpotAndWheelHistory(EngineModel engine) {
        SpotModel.Spots.Add(new SpotPlacement(new Point(0.4, 0.4), new Point(0.5, 0.4), 0.02, true));
        EditHistory.Label("Spot");
        engine.Render(preview: false);
        bool recorded = EditHistory.UndoLabel == "Spot";
        EditHistory.Undo();
        bool gone = SpotModel.Spots.Count == 0;
        EditHistory.Redo();
        Check(recorded && gone && SpotModel.Spots.Count == 1, "placing a spot is a history step that undo and redo take and give back");
        EditHistory.Label("Clear");
        SpotModel.Spots.Clear();
        engine.Render(preview: false);

        engine.Set("grade_shadow", 0.3f, 0);
        engine.Set("grade_shadow", 0.1f, 2);
        var wheel = new ColorWheel("Shadows", "grade_shadow");
        wheel.GetLogicalDescendants().OfType<StateButton>().First().RaiseEvent(new RoutedEventArgs(Button.ClickEvent));
        bool reset = engine.Get("grade_shadow", 0) == 0 && engine.Get("grade_shadow", 2) == 0 && EditHistory.UndoLabel == "Reset Shadows";
        EditHistory.Undo();
        Check(reset && engine.Get("grade_shadow", 0) == 0.3f && engine.Get("grade_shadow", 2) == 0.1f,
              "a wheel's reset is one step, \"Reset Shadows\", that one ⌘Z undoes");
        for (int i = 0; i < 3; i++) engine.Set("grade_shadow", 0, i);
    }

    /// Culling a selection: the keys and the menus act on the group, the batch on its export targets.
    static void GroupCulling(EngineModel engine) {
        var library = LibraryModel.Shared;
        if (library.Photos.Count < 2) return;   // needs a folder: OrionShell <folder> --selftest
        var marks = library.Photos.Select(p => (p.Rating, p.Rejected)).ToList();
        var open = library.Open!;
        var other = library.Photos.First(p => p != open);
        library.ClickStrip(other, PhotoSelection.Modifiers.Command);
        Check(library.HasExplicitSelection && library.Open == open && library.Targets.Count == 2,
              "a command-click on the strip selects without opening");
        var menu = LibraryUi.CullItems(other).OfType<MenuItem>().Select(m => (string)m.Header!).ToList();
        Check(menu[0].EndsWith(" (2 photos)") && menu[1] == "1 star (2 photos)", "the strip's cull menu counts the group it acts on");
        Commands.Run("rate-2");
        library.SetRejected([other], true);
        Check(open.Rating == 2 && ExportRun.Targets().Contains(other), "a rating key rates the group; a selected reject is exported, being asked for");
        Commands.Run("deselect");
        Check(!library.HasExplicitSelection && !ExportRun.Targets().Contains(other), "deselect collapses to the open photo; the batch then leaves rejects out");
        Commands.Run("select-all");
        bool all = library.Targets.Count == library.Visible.Count();
        engine.Open(other.Index);
        Check(all && !library.HasExplicitSelection, "select all takes every photo in view, and moving the canvas collapses it");
        engine.Open(open.Index);
        for (int i = 0; i < marks.Count; i++) (library.Photos[i].Rating, library.Photos[i].Rejected) = marks[i];
    }
}
