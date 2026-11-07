using Avalonia;
using Avalonia.Headless;
using Avalonia.Input;
using Avalonia.VisualTree;
using OrionShell.CanvasSlice;
using OrionShell.Library;

namespace OrionShell;

/// Lightroom's zoom presets and pick flags.
static partial class SelfTest {
    /// The presets as ratios on screen, Ctrl+= and Ctrl+- stepping through them by the keyboard,
    /// and the image point under the pointer held still.
    static partial void ZoomPresetChecks() {
        var engine = EngineModel.Shared;
        if (engine.Handle == 0) return;
        var window = Shell();
        var vp = CanvasState.Viewport;
        try {
            var canvas = window.GetVisualDescendants().OfType<CanvasPanel>().Single();
            vp.Reset();
            Settle(window);
            double fit = vp.Ratio;
            Commands.Run("zoom-1-1");
            double one = vp.Ratio;
            Commands.Run("zoom-1-4");
            double quarter = vp.Ratio;
            // A frame whose fit is already past 1:4 (a small raw on a large view) holds at fit.
            Check(fit < 1 && Math.Abs(one - 1) < 1e-9 && Math.Abs(quarter - Math.Max(0.25, fit)) < 1e-9,
                  $"zoom-1-1 and zoom-1-4 show one and a quarter screen pixel per image pixel, never less than fit (fit {fit:0.000}, {one:0.000}, {quarter:0.000})");

            vp.Reset();
            var size = canvas.Bounds.Size;
            var p = new Point(size.Width * 0.3, size.Height * 0.4);
            window.MouseMove(At(window, canvas, p));
            Settle(window);
            var steps = new List<double>();
            var under = new List<Point>();   // the image point under the pointer, each step
            void Step(Key key, PhysicalKey physical) {
                Type(window, key, physical, OperatingSystem.IsMacOS() ? RawInputModifiers.Meta : RawInputModifiers.Control);
                steps.Add(vp.Ratio);
                under.Add(CanvasState.Map(size).ToUnit(p));
            }
            var ins = Viewport.Ratios.Where(r => r > fit * 1.001).ToList();
            List<double> want = [.. ins, ins[^1], .. ins.AsEnumerable().Reverse().Skip(1), fit];
            for (int i = 0; i <= ins.Count; i++) Step(Key.OemPlus, PhysicalKey.Equal);
            for (int i = 0; i < ins.Count; i++) Step(Key.OemMinus, PhysicalKey.Minus);
            bool stepped = steps.Count == want.Count && steps.Zip(want).All(s => Math.Abs(s.First - s.Second) < 1e-9);
            Check(stepped && vp.IsFit, $"Ctrl+= steps through the presets past fit to 4:1 and stops, Ctrl+- steps back to fit ({string.Join(" ", steps.Select(s => s.ToString("0.###")))})");
            // From 1:1 on the picture fills the view both ways; below it, a letterboxed axis stays centred.
            int up = want.IndexOf(1), top = ins.Count - 1, down = want.LastIndexOf(1);
            Point Under(int i) => under.ElementAtOrDefault(i);
            Check(stepped && up >= 0 && PictureMap.Dist(Under(up), Under(top)) < 1e-6 && PictureMap.Dist(Under(up), Under(down)) < 1e-6,
                  $"the zoom keys hold the image point under the pointer ({Under(up)} at 1:1, {Under(top)} at 4:1, {Under(down)} back at 1:1)");
        } finally {
            vp.Reset();
            window.Close();
        }
    }

    /// P, X and U on the open photo, the Picked filter, and the pick through a real sidecar:
    /// Orion's own field, beside a rating it keeps, and gone when a reject or U clears it.
    static void PickInSidecar(string photo) {
        var library = LibraryModel.Shared;
        var open = library.Open!;
        var marks = (open.Rating, open.Rejected, open.Picked);
        Commands.Run("pick");
        bool picked = open.Picked && !open.Rejected;
        library.Current = LibraryModel.Filter.Picked;
        bool shown = library.Visible.Contains(open);
        Commands.Run("flag-reject");
        bool rejected = open.Rejected && !open.Picked && !library.Visible.Contains(open);
        Commands.Run("unflag");
        bool cleared = !open.Rejected && !open.Picked;
        library.Current = LibraryModel.Filter.All;
        (open.Rating, open.Rejected, open.Picked) = marks;
        Check(picked && shown && rejected && cleared, $"P picks, the Picked filter shows it, X rejects in its place, U clears (pick {picked}, shown {shown}, reject {rejected}, clear {cleared})");

        var p = LibraryModel.Listed(photo, 0);
        library.Writes = true;
        try {
            library.SetRating(p, 3);
            library.SetFlag([p], LibraryModel.Flag.Picked);
            string text = File.ReadAllText(Sidecar.PathFor(photo));
            var listed = LibraryModel.Listed(photo, 0);
            Check(text.Contains("orion:Pick=\"1\"") && text.Contains("xmp:Rating=\"3\"") && listed.Picked && listed.Rating == 3 && !listed.Rejected,
                  "a pick is orion:Pick in the sidecar beside the rating, and a listing reads it back");
            library.SetFlag([p], LibraryModel.Flag.Rejected);
            listed = LibraryModel.Listed(photo, 0);
            bool swapped = listed.Rejected && !listed.Picked && !File.ReadAllText(Sidecar.PathFor(photo)).Contains("orion:Pick");
            library.SetFlag([p], LibraryModel.Flag.Picked);
            library.SetFlag([p], LibraryModel.Flag.None);
            listed = LibraryModel.Listed(photo, 0);
            Check(swapped && !listed.Picked && !listed.Rejected && !File.ReadAllText(Sidecar.PathFor(photo)).Contains("orion:Pick"),
                  "a reject drops the pick from the sidecar, and unflag drops either");
        } finally {
            library.Writes = false;
            File.Delete(Sidecar.PathFor(photo));
        }
    }
}
