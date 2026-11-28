using System.Text;
using System.Text.Json.Nodes;
using System.Text.RegularExpressions;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Headless;
using Avalonia.Input;
using Avalonia.Interactivity;
using Avalonia.LogicalTree;
using Avalonia.VisualTree;
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
        StackSheetPreview(engine);
        StackSheetResize(engine);
        string dir = Directory.CreateTempSubdirectory("orion-library-").FullName;
        try {
            string photo = Path.Combine(dir, "photo" + Path.GetExtension(engine.Photos[engine.Current]));
            File.CreateSymbolicLink(photo, engine.Photos[engine.Current]);
            MarksInSidecar(photo);
            PickInSidecar(photo);
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
        StripWheel(engine);
        FilteredSteps(engine);
    }

    /// The arrow keys walk the photos the strip's filter shows, and stop at its ends.
    static void FilteredSteps(EngineModel engine) {
        var library = LibraryModel.Shared;
        var list = engine.Photos.ToList();
        int current = engine.Current;
        string dir = Directory.CreateTempSubdirectory("orion-steps-").FullName;
        try {
            var links = Enumerable.Range(0, 4).Select(i => Path.Combine(dir, $"f{i}{Path.GetExtension(list[current])}")).ToList();
            foreach (var link in links) File.CreateSymbolicLink(link, list[current]);
            engine.Load(links);
            library.SetRating([library.Photos[0], library.Photos[3]], 2);
            library.Current = LibraryModel.Filter.Rated;
            bool fromNone = library.Neighbour(-1, 1) == library.Photos[0] && library.Neighbour(-1, -1) == library.Photos[0];
            OpenAndWait(engine, 0);
            int After(string command) { Commands.Run(command); return engine.Current; }
            int first = After("previous-photo"), next = After("next-photo"), end = After("next-photo");
            library.SetRating(library.Photos[3], 0);   // the open photo leaves the filter
            int back = After("previous-photo");
            library.Current = LibraryModel.Filter.Rejected;   // shows none
            Check(fromNone && first == 0 && next == 3 && end == 3 && back == 0 && After("next-photo") == 0,
                  $"next and previous walk only the photos the filter shows (from none, the first {fromNone}; previous at the first {first}; " +
                  $"0 to {next}, then {end} at the end; filtered out, back to {back}; none shown, {engine.Current})");
        } finally {
            library.Current = LibraryModel.Filter.All;
            engine.Load(list);
            OpenAndWait(engine, current);
            Directory.Delete(dir, recursive: true);
        }
    }

    /// Issue 55: a wheel over the strip moves the roll, whatever else is going on. Four links to
    /// the open photo overflow a 420 px strip, which sits at its window's foot, as the app's does.
    static void StripWheel(EngineModel engine) {
        var list = engine.Photos.ToList();
        int current = engine.Current;
        string dir = Directory.CreateTempSubdirectory("orion-strip-wheel-").FullName;
        try {
            var links = Enumerable.Range(1, 4).Select(i => Path.Combine(dir, $"f{i}{Path.GetExtension(list[current])}")).ToList();
            foreach (var link in links) File.CreateSymbolicLink(link, list[current]);
            engine.Load(links);
            var library = LibraryModel.Shared;
            library.Current = LibraryModel.Filter.All;   // the frames are counted below: all of them, in name order
            library.Sort(LibraryModel.Order.Name);
            OpenAndWait(engine, 0);
            var strip = new FilmstripPanel();
            ToolTip.SetServiceEnabled(strip, false);   // no hover opens a tooltip before the tooltip's own check
            var window = Show(new Border { Width = 420, Height = Layout.Filmstrip, VerticalAlignment = Avalonia.Layout.VerticalAlignment.Bottom, Child = strip });
            var film = strip.GetVisualDescendants().OfType<ScrollViewer>().Single();
            Point Middle(Visual v) => v.TranslatePoint(new Point(v.Bounds.Width / 2, v.Bounds.Height / 2), window)!.Value;
            double Wheel(Visual over, Vector delta) {
                film.Offset = film.Offset.WithX(0);
                window.MouseWheel(Middle(over), delta);
                return film.Offset.X;
            }
            Border FrameAt(int i) {
                window.UpdateLayout();
                return strip.GetVisualDescendants().OfType<Border>().Where(b => b.Focusable && b.Cursor != null).ElementAt(i);
            }
            bool Shows(Border frame) => frame.TranslatePoint(default, film)!.Value.X is var x && x >= 0 && x + frame.Bounds.Width <= film.Viewport.Width;

            double scrolled = Wheel(film, new Vector(0, -1));
            library.SetRating(library.Photos[1], library.Photos[1].Rating);   // a mark lands, as each batch of sidecar reads does
            window.UpdateLayout();
            double kept = film.Offset.X;
            Wheel(film, new Vector(0, -1));
            library.Sort(LibraryModel.Order.Name);
            bool bySort = Shows(FrameAt(0));
            Wheel(film, new Vector(0, -1));
            library.Current = LibraryModel.Filter.Unrated;   // the links carry no sidecar: all four still show
            bool byFilter = Shows(FrameAt(0));
            library.Current = LibraryModel.Filter.All;
            Check(scrolled > 0 && kept == scrolled && bySort && byFilter,
                  $"a mark landing keeps the roll where the wheel put it ({scrolled:0} → {kept:0} px), and a sort or a filter chosen by hand brings the open frame back into view (sort {bySort}, filter {byFilter})");

            double notch = Wheel(film, new Vector(0, -1)), tilted = Wheel(film, new Vector(0.05, -1)), swiped = Wheel(film, new Vector(-0.6, 0.05));
            Check(notch > 0 && tilted == notch && swiped > 0,
                  $"a wheel notch with a little sideways delta moves the roll as far as a straight one ({tilted:0} of {notch:0} px), and a sideways swipe still scrolls it ({swiped:0} px)");

            var bar = strip.GetVisualDescendants().OfType<StackSuggestion>().Single();
            bar.IsVisible = true;   // by hand: CI's generated DNG has no exposure to group frames by
            window.UpdateLayout();
            double overBar = Wheel(bar, new Vector(0, -1));
            bar.IsVisible = false;
            Check(overBar > 0, $"a wheel over the stack suggestion, on the film's right end, moves the roll ({overBar:0} px)");

            // The hover itself, not ToolTip.SetIsOpen: so opened, a tip placed at the pointer landed clear of the frame.
            ToolTip.SetServiceEnabled(strip, true);
            film.Offset = film.Offset.WithX(0);
            var at = Middle(film);
            window.MouseMove(at);
            var cell = (window.InputHitTest(at) as Visual)?.GetSelfAndVisualAncestors().OfType<Border>().FirstOrDefault(b => b.Focusable && b.Cursor != null);
            Frames(() => cell != null && ToolTip.GetIsOpen(cell), 10);   // the hover's 400 ms runs on the main loop's timers
            bool tip = cell != null && ToolTip.GetIsOpen(cell);
            AvaloniaHeadlessPlatform.ForceRenderTimerTick();   // an overlay popup takes its place on the next frame
            window.UpdateLayout();
            Rect? Box(Visual? v) => v?.TranslatePoint(default, window) is { } p ? new Rect(p, v.Bounds.Size) : null;
            // Headless popups are overlays in the window, so the tip is among its visuals.
            Rect? tipBox = Box(window.GetVisualDescendants().OfType<ToolTip>().FirstOrDefault(t => t.IsEffectivelyVisible)), cellBox = Box(cell);
            bool clear = tipBox is { } tipAt && cellBox is { } cellAt && !tipAt.Intersects(cellAt);
            double underTip = Wheel(film, new Vector(0, -1));
            Check(tip && clear && underTip > 0,
                  $"a frame's tooltip, the strip at the window's foot, keeps clear of the frame and the wheel over it moves the roll (open {tip}, at {tipBox} by the frame at {cellBox}, {underTip:0} px)");
            if (cell != null) ToolTip.SetIsOpen(cell, false);

            // Last: nothing after it waits for the glide to land.
            film.Offset = film.Offset.WithX(0);
            var third = FrameAt(2);
            bool hidden = !Shows(third);
            OpenAndWait(engine, 2);
            Frames(() => Shows(third), 10);
            Check(hidden && Shows(third), $"opening a frame out of view still brings the roll to it (hidden {hidden}, then shown {Shows(third)})");
            window.Close();
        } finally {
            engine.Load(list);
            // Not waited for, as MarksOffTheUiThread does not: the checks after these have never run with a photo open, and running them is a change of its own.
            engine.Open(current);
            Directory.Delete(dir, recursive: true);
        }
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
            var want = new Sidecar { Rating = 4, Label = "Red", Develop = edit, Source = Path.GetFileName(photo) }.Text();
            Check(!p.Rejected && File.ReadAllText(Sidecar.PathFor(photo), Encoding.UTF8) == want,
                  "a rating un-rejects and lands in Orion's own bytes, the label, the edit and whose it is kept");
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
        Check(menu[0] == "Pick (2 photos)" && menu[1].EndsWith(" (2 photos)") && menu[2] == "1 star (2 photos)", "the strip's cull menu counts the group it acts on");
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
