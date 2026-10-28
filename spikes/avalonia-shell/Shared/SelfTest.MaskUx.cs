using Avalonia;
using Avalonia.Controls;
using Avalonia.Headless;
using Avalonia.Input;
using Avalonia.Media.Imaging;
using Avalonia.Threading;
using OrionShell.CanvasSlice;
using OrionShell.Develop;
using OrionShell.Masks;

namespace OrionShell;

/// Stream u4's checks: masks are seen and edited on the photo. Every gesture
/// goes through the headless window's real pointer path (hit test, capture,
/// the overlay's handlers), and every claim is read off the rendered picture.
static partial class SelfTest {
    static partial void MaskUxChecks() {
        var engine = EngineModel.Shared;
        // Earlier checks may leave the list mid-step (a trash, a browse): open the first photo again.
        if (engine.Session == null && engine.Photos.Count > 0) {
            engine.Open(0);
            for (int i = 0; i < 6000 && engine.Opening; i++) { Dispatcher.UIThread.RunJobs(); Thread.Sleep(1); }
        }
        if (engine.Session == null) { Check(false, $"mask checks need an open photo ({engine.Photos.Count} photos, current {engine.Current})"); return; }
        var group = MaskGroup.Shared;
        group.Clear();
        CarriedChecks();

        var overlay = new MaskOverlay();
        var window = new Window { Width = 1100, Height = 800, Content = new Grid { Children = { new CanvasPanel(), overlay } } };
        window.Show();
        window.UpdateLayout();
        Dispatcher.UIThread.RunJobs();
        CanvasState.SetTab("Mask");

        Point At(double x, double y) => overlay.TranslatePoint(CanvasState.Map(overlay.Bounds.Size).FramePoint(new Point(x, y)), window)!.Value;
        void Drag(Point a, Point b, RawInputModifiers mods = RawInputModifiers.None) {
            window.MouseDown(a, MouseButton.Left, mods);
            for (int i = 1; i <= 12; i++) window.MouseMove(a + (b - a) * (i / 12.0), mods | RawInputModifiers.LeftMouseButton);
            window.MouseUp(b, MouseButton.Left, mods);
            Dispatcher.UIThread.RunJobs();
        }
        static bool Near(double a, double b, double tol = 0.01) => Math.Abs(a - b) <= tol;

        // A drag with Radial armed draws a radial centred where it began, sized by the drag.
        group.Drawing = 2;
        Drag(At(0.30, 0.40), At(0.42, 0.52));
        var radial = group.Current;
        Check(group.Shapes.Count == 1 && radial?.Kind == 2 && Near(radial.CenterX, 0.30) && Near(radial.CenterY, 0.40)
              && Near(radial.RadiusX, 0.12, 0.015) && Near(radial.RadiusY, 0.12, 0.015) && group.Drawing == 0,
              $"a drag with Radial armed draws a radial where it began, sized by the drag, then disarms " +
              $"({radial?.CenterX:0.000}, {radial?.CenterY:0.000}) r {radial?.RadiusX:0.000}×{radial?.RadiusY:0.000}");

        // The overlay is on by default: the mask's inside renders red, and only in the Mask tab.
        double redOn = Redness(engine.Picture, 0.30, 0.40);
        group.Overlay = false;
        double redOff = Redness(engine.Picture, 0.30, 0.40);
        group.Overlay = true;
        CanvasState.SetTab("Light");
        double redAway = Redness(engine.Picture, 0.30, 0.40);
        CanvasState.SetTab("Mask");
        Check(redOn > redOff + 15 && Math.Abs(redAway - redOff) < 1 && Redness(engine.Picture, 0.30, 0.40) > redOff + 15,
              $"the overlay paints the selected mask by default, only on the Mask tab (redness {redOff:0.0} off, {redOn:0.0} on, {redAway:0.0} on Light)");
        Check(engine.Session!.Get("mask_count") == 1 && !EngineModel.Fields.Any(f => f.Name.StartsWith("mask_overlay")),
              "the overlay is not an adjustment: no field carries it, so history and export never see it");

        // Raising the mask's exposure brightens inside it and nowhere else.
        group.Overlay = false;
        // Turning the overlay off renders the preview graph at fit; the full one follows once it rests.
        for (int i = 0; i < 3000 && engine.Shown.Graph != Graph.Full; i++) { Dispatcher.UIThread.RunJobs(); Thread.Sleep(1); }
        Check(engine.Shown.Graph == Graph.Full, "once the overlay rests, the full graph follows its preview render");
        double in0 = Luma(engine.Picture, 0.30, 0.40), out0 = Luma(engine.Picture, 0.80, 0.75);
        engine.Set("local_exposure_ev", 1.5f, group.SelectedLayer);
        double in1 = Luma(engine.Picture, 0.30, 0.40), out1 = Luma(engine.Picture, 0.80, 0.75);
        Check(in1 > in0 + 10 && Math.Abs(out1 - out0) < 1,
              $"+1.5 EV on the radial brightens inside it only (inside {in0:0.0} → {in1:0.0}, outside {out0:0.0} → {out1:0.0})");

        // Dragging the centre handle carries the effect with it; undo brings it back.
        double there0 = Luma(engine.Picture, 0.65, 0.45);
        Drag(At(0.30, 0.40), At(0.65, 0.45));
        double here = Luma(engine.Picture, 0.30, 0.40), there = Luma(engine.Picture, 0.65, 0.45);
        Check(Near(group.Current!.CenterX, 0.65) && Near(group.Current.CenterY, 0.45) && there > there0 + 10 && Math.Abs(here - in0) < 2,
              $"dragging the centre handle moves the effect (old centre {in0:0.0} → {in1:0.0} → {here:0.0}, new {there0:0.0} → {there:0.0})");
        EditHistory.Undo();
        Check(Near(group.Current!.CenterX, 0.30) && Near(group.Current.CenterY, 0.40) && Math.Abs(Luma(engine.Picture, 0.30, 0.40) - in1) < 1,
              $"undo puts the mask and its effect back ({group.Current.CenterX:0.000}, {group.Current.CenterY:0.000})");

        // A brush mask paints coverage, and Alt-painting the same path erases it.
        group.Drawing = 3;
        Drag(At(0.10, 0.80), At(0.40, 0.80));
        int brushLayer = group.SelectedLayer;
        double bare = Luma(engine.Picture, 0.25, 0.80);
        engine.Set("local_exposure_ev", 1.5f, brushLayer);
        double painted = Luma(engine.Picture, 0.25, 0.80);
        Drag(At(0.10, 0.80), At(0.40, 0.80), RawInputModifiers.Alt);
        Drag(At(0.10, 0.80), At(0.40, 0.80), RawInputModifiers.Alt);
        double erased = Luma(engine.Picture, 0.25, 0.80);
        Check(group.Current?.Kind == 3 && brushLayer == 1 && painted > bare + 10 && erased - bare < (painted - bare) * 0.2
              && group.Current.BrushErase.Contains(1),
              $"a brush stroke brightens its mask and Alt-painting over it erases the coverage ({bare:0.0} → {painted:0.0} → {erased:0.0})");

        // Undo writes the stroke's old revision back, and the shim lays the dabs it kept for it.
        EditHistory.Undo();
        double unerased = Luma(engine.Picture, 0.25, 0.80);
        Check(Math.Abs(unerased - painted) < 1 && group.Current?.BrushErase.Contains(1) == false,
              $"undoing the erase brings the paint back ({erased:0.0} → {unerased:0.0}, painted {painted:0.0})");

        // Removing the first mask leaves the brush its own adjustments, now in slot 0.
        group.RemoveLayer(0);
        Check(group.Shapes.Count == 1 && engine.Get("local_exposure_ev", 0) == 1.5f && engine.Get("local_exposure_ev", 1) == 0,
              "deleting a mask carries every later mask's adjustments down with it");

        // The Mask tab's keys (CommandTable): `]` sizes the brush instead of turning the photo, O toggles the overlay, Escape disarms a draw.
        float r0 = group.Current!.BrushRadius, turn = engine.Get("rotate_quarters");
        bool overlay0 = group.Overlay;   // the exposure check above turned it off
        var ran = new List<string> { Commands.Run("rotate-right") };
        float r1 = group.Current!.BrushRadius, turned = engine.Get("rotate_quarters");
        ran.Add(Commands.Run("rotate-left"));
        ran.Add(Commands.Run("mask-overlay"));
        bool flipped = group.Overlay != overlay0;
        ran.Add(Commands.Run("mask-overlay"));
        group.Drawing = 2;
        ran.Add(Commands.Run("cancel-crop"));
        Check(r1 > r0 && turned == turn && Math.Abs(group.Current!.BrushRadius - r0) < 1e-5 && flipped && group.Overlay == overlay0 && group.Drawing == 0,
              $"on the Mask tab ] grows the brush and leaves the photo's turn alone, O toggles the overlay, Escape disarms a draw " +
              $"(kind {group.Current.Kind}, radius {r0:0.000} → {r1:0.000} → {group.Current.BrushRadius:0.000}, turn {turn} → {turned}, " +
              $"overlay {overlay0} → flipped {flipped} → {group.Overlay}, drawing {group.Drawing}, tab {CanvasState.Tab}; {string.Join(", ", ran)})");

        group.Clear();
        for (int l = 0; l < MaskGroup.Max; l++) engine.Session!.Set("local_exposure_ev", 0, l);
        group.Overlay = true;
        engine.Render(false);
        window.Close();
        CanvasState.SetTab(null);
    }

    /// The pure part: which old mask's adjustments each new mask keeps.
    static void CarriedChecks() {
        object a = new(), b = new(), c = new(), d = new();
        Check(MaskGroup.Carried<object>([[a], [b, c], [d]], [[b, c], [d]]).SequenceEqual([1, 2]), "a removed mask shifts the rest down a slot");
        Check(MaskGroup.Carried<object>([[a], [b]], [[b], [a]]).SequenceEqual([1, 0]), "a moved mask takes its adjustments along");
        Check(MaskGroup.Carried<object>([[a, b]], [[a], [b]]).SequenceEqual([0, -1]), "a split's second half starts clean");
        Check(MaskGroup.Carried<object>([[a], [b]], [[a, b]]).SequenceEqual([0]), "a merge keeps the upper mask's adjustments");
        Check(MaskGroup.Carried<object>([[a]], [[a], [c]]).SequenceEqual([0, -1]), "a new mask starts at zero");
    }

    /// Mean of R − (G + B) / 2 in a small box about a frame point (where masks
    /// live, #202), found on the displayed picture through the engine's own map:
    /// a portrait photo's frame is its sensor turned.
    static double Redness(Bitmap? picture, double x, double y) => Box(picture, x, y, (r, g, b) => r - (g + b) / 2);
    static double Luma(Bitmap? picture, double x, double y) => Box(picture, x, y, (r, g, b) => 0.2126 * r + 0.7152 * g + 0.0722 * b);

    static unsafe double Box(Bitmap? picture, double x, double y, Func<double, double, double, double> f) {
        if (picture is not WriteableBitmap bitmap) return 0;
        var d = CanvasNative.DisplayMap().Display(new Point(x, y));
        using var fb = bitmap.Lock();
        int cx = (int)(d.X * fb.Size.Width), cy = (int)(d.Y * fb.Size.Height), half = Math.Max(2, fb.Size.Width / 100);
        double sum = 0;
        int n = 0;
        for (int py = Math.Max(cy - half, 0); py < Math.Min(cy + half, fb.Size.Height); py++) {
            byte* row = (byte*)fb.Address + (long)py * fb.RowBytes;
            for (int px = Math.Max(cx - half, 0); px < Math.Min(cx + half, fb.Size.Width); px++, n++)
                sum += f(row[px * 4], row[px * 4 + 1], row[px * 4 + 2]);
        }
        return n == 0 ? 0 : sum / n;
    }
}
