using System.Diagnostics;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Headless;
using Avalonia.Input;
using Avalonia.Media;
using Avalonia.Media.Imaging;
using Avalonia.VisualTree;
using OrionShell.CanvasSlice;
using OrionShell.Develop;

namespace OrionShell;

/// Lightroom's clipping warning and its interactive histogram: the overlay on the
/// canvas (J, the flags, Alt on a tone slider) and a drag across the histogram.
static partial class SelfTest {
    static partial void ClippingChecks() {
        var engine = EngineModel.Shared;
        if (engine.Handle == 0) return;
        float ev = engine.Get("exposure_ev"), blacks = engine.Get("blacks");
        try {
            OverlayPixelChecks(engine);
            engine.Set("exposure_ev", ev);
            engine.Set("blacks", blacks);
            ClippingKeyChecks(engine);
        } finally {
            (Clipping.Shadows, Clipping.Highlights, Clipping.Held) = (false, false, false);
            engine.Set("exposure_ev", ev);
            engine.Set("blacks", blacks);
        }
    }

    /// The overlay on the drawn canvas, pixel by pixel against the picture drawn without it:
    /// where it paints, the picture had a channel in the histogram's end bin (by the bins' own
    /// rule, not the overlay's table), and where the picture clipped, it paints. Not quite every
    /// pixel: the window is read at 2x, and the picture and the mask are each filtered up to it.
    static void OverlayPixelChecks(EngineModel engine) {
        var canvas = new CanvasPanel();
        var window = Show(canvas);
        // A slow GPU's copies (WARP's): each lands 200 ms after it is begun, and a draw meanwhile shows the one before.
        var (begin, take, since) = (canvas.Copied.BeginCopy, canvas.Copied.TakeCopy, new Stopwatch());
        canvas.Copied.BeginCopy = (s, graph, level) => { since.Restart(); return begin(s, graph, level); };
        canvas.Copied.TakeCopy = (Session s, bool wait, ref WriteableBitmap? bitmap) => !wait && since.ElapsedMilliseconds < 200 ? 0 : take(s, wait, ref bitmap);
        try {
            engine.Set("exposure_ev", 5);   // blown: most of the frame in the last bin
            var plain = Landed(window, canvas);
            Commands.Run("show-clipping");
            var (share, precise, found) = Agreement(plain, Landed(window, canvas), Palette.ClipHigh, high: true);
            Check(share > 0.05 && precise > 0.97 && found > 0.9,
                  $"J paints the blown pixels red on the canvas ({share:P1} of the frame red, {precise:P1} of it blown, {found:P1} of the blown red)");
            Commands.Run("show-clipping");
            Check(!Clipping.Any && Agreement(plain, Landed(window, canvas), Palette.ClipHigh, high: true).Share < 0.002, "J again takes the overlay off");

            engine.Set("exposure_ev", -5);
            engine.Set("blacks", -1);
            plain = Landed(window, canvas);
            Commands.Run("show-shadow-clipping");
            var shown = Landed(window, canvas);
            (share, precise, found) = Agreement(plain, shown, Palette.ClipLow, high: false);
            Check(Clipping.Shadows && !Clipping.Highlights && share > 0.05 && precise > 0.97 && found > 0.9
                  && Agreement(plain, shown, Palette.ClipHigh, high: true).Share < 0.002,
                  $"the shadow flag's command paints crushed pixels blue and only those ({share:P1} blue, {precise:P1} of it crushed, {found:P1} of the crushed blue)");
            Commands.Run("show-shadow-clipping");
        } finally {
            window.Close();
        }
    }

    /// The drawn picture once the canvas holds the copy of the newest render, which a slow GPU lands after the draw that asked.
    static (byte[] Px, List<int> At) Landed(Window window, CanvasPanel canvas) {
        var waited = Stopwatch.StartNew();
        var picture = PictureAt(window, canvas);
        while (!canvas.Copied.Fresh && waited.ElapsedMilliseconds < 5000) {
            Pump(5);
            picture = PictureAt(window, canvas);
        }
        return picture;
    }

    /// Every other pixel of the drawn picture in the window at 2x, as offsets into its bytes.
    static (byte[] Px, List<int> At) PictureAt(Window window, Control canvas) {
        var (px, stride, c) = Pixels(window, canvas);
        var frame = CanvasState.DrawnFrame(canvas.Bounds.Size);
        var r = new Rect(c.X + frame.X * 2 + 4, c.Y + frame.Y * 2 + 4, frame.Width * 2 - 8, frame.Height * 2 - 8);
        var at = new List<int>();
        for (int y = (int)r.Y; y < (int)r.Bottom; y += 2)
            for (int x = (int)r.X; x < (int)r.Right; x += 2) at.Add(y * stride + x * 4);
        return (px, at);
    }

    /// The share of `shown` painted `brush` (either byte order), the share of that the plain
    /// picture clipped at that end, and the share of the clipped that it paints.
    static (double Share, double Precise, double Found) Agreement((byte[] Px, List<int> At) plain, (byte[] Px, List<int> At) shown, IBrush brush, bool high) {
        var ink = ((ISolidColorBrush)brush).Color;
        static bool Near(byte a, byte b) => Math.Abs(a - b) <= 3;
        long painted = 0, clipped = 0, both = 0;
        foreach (int i in shown.At) {
            var s = shown.Px;
            bool paint = Near(s[i + 1], ink.G) && ((Near(s[i], ink.R) && Near(s[i + 2], ink.B)) || (Near(s[i + 2], ink.R) && Near(s[i], ink.B)));
            bool clip = false;
            for (int k = 0; k < 3; k++) {
                int bin = Math.Min(127, (int)(plain.Px[i + k] / 255f * 128));
                clip |= high ? bin == 127 : bin == 0;
            }
            painted += paint ? 1 : 0;
            clipped += clip ? 1 : 0;
            both += paint && clip ? 1 : 0;
        }
        return ((double)painted / Math.Max(shown.At.Count, 1), (double)both / Math.Max(painted, 1), (double)both / Math.Max(clipped, 1));
    }

    /// In the whole window: the J key, Alt on a tone slider's drag, and the histogram's drag.
    static void ClippingKeyChecks(EngineModel engine) {
        var window = Shell();
        try {
            Type(window, Key.J, PhysicalKey.J);
            bool both = Clipping.Shadows && Clipping.Highlights;
            Type(window, Key.J, PhysicalKey.J);
            Check(both && !Clipping.Any, "the J key shows both ends of the clipping overlay, and hides them again");

            var host = window.FindControl<StackPanel>("ToolsHost")!;
            AnalogTrack Track(string name) => host.GetVisualDescendants().OfType<AdjustmentSlider>()
                .First(s => s.GetVisualDescendants().OfType<TextBlock>().Any(t => t.Text == name)).GetVisualDescendants().OfType<AnalogTrack>().First();
            bool AltDrag(AnalogTrack track, RawInputModifiers keys) {
                track.BringIntoView();
                Settle(window);
                var a = At(window, track, new Point(track.Bounds.Width / 2, 9.5));
                window.MouseMove(a);
                Settle(window);
                float before = track.Value;
                window.MouseDown(a, MouseButton.Left, keys);
                window.MouseMove(a + new Vector(6, 0), RawInputModifiers.LeftMouseButton | keys);
                bool during = Clipping.Held;
                window.MouseUp(a + new Vector(6, 0), MouseButton.Left, keys);
                Settle(window);
                if (track.Value != before) Commands.Run("undo");
                return during && !Clipping.Held;
            }
            bool exposure = AltDrag(Track("Exposure"), RawInputModifiers.Alt), whites = AltDrag(Track("Whites"), RawInputModifiers.Alt);
            bool plain = !AltDrag(Track("Exposure"), RawInputModifiers.None), contrast = !AltDrag(Track("Contrast"), RawInputModifiers.Alt);
            Check(exposure && whites && plain && contrast,
                  $"Alt on an Exposure or Whites drag shows the clipping for the drag only, not a plain drag nor Contrast's (exposure {exposure}, whites {whites}, plain {plain}, contrast {contrast})");

            HistogramDragChecks(window, engine);
        } finally {
            window.Close();
        }
    }

    static void HistogramDragChecks(MainWindow window, EngineModel engine) {
        engine.Open(engine.Current);
        // A session the cache no longer keeps decodes again, and the histogram ignores the pointer until it opens.
        var opening = Stopwatch.StartNew();
        while (engine.Opening && opening.ElapsedMilliseconds < 10000) Pump(5);
        EditHistory.Restart();   // a clean history: one step is the drag's
        Settle(window);
        if (window.GetVisualDescendants().OfType<HistogramDrag>().SingleOrDefault() is not { } drag) {
            Check(false, "the histogram takes a drag");
            return;
        }
        string? said = null;
        drag.Said += s => said = s;
        double w = drag.Bounds.Width, h = drag.Bounds.Height;
        window.MouseMove(At(window, drag, new Point(w * 0.05, h / 2)));
        Settle(window);
        string? left = said;
        window.MouseMove(At(window, drag, new Point(w * 0.95, h / 2)));
        Settle(window);
        Check(left == "Blacks" && said == "Whites", $"the histogram names the region under the pointer ('{left}' at the left end, '{said}' at the right)");

        float ev = engine.Get("exposure_ev"), whites = engine.Get("whites");
        Drag(window, drag, new Point(w * 0.5, h / 2), new Point(w * 0.6, h / 2));
        float moved = engine.Get("exposure_ev") - ev;
        bool oneStep = EditHistory.CanUndo;
        Commands.Run("undo");
        Check(Math.Abs(moved - 1) < 0.05 && engine.Get("whites") == whites && oneStep && engine.Get("exposure_ev") == ev && !EditHistory.CanUndo,
              $"a drag of a tenth of the histogram in its middle moves Exposure a tenth of its 10 EV ({moved:+0.000} EV) as one undo step");
    }
}
