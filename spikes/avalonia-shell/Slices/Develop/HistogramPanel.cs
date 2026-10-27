using Avalonia;
using Avalonia.Controls;
using Avalonia.Layout;
using Avalonia.Media;
using Avalonia.Media.Imaging;
using Avalonia.Platform;
using System.Runtime.InteropServices;
using Path = Avalonia.Controls.Shapes.Path;

namespace OrionShell;

/// The engine's per-channel histogram of the full render, 128 bins each, red
/// then green then blue (Engine.histogram). Not refreshed mid-drag: it reads
/// the whole output texture, and it is a readout nobody watches while a slider
/// moves (Engine+Render.swift scheduleHistogram).
static class Levels {
    public const int BinCount = 128;
    public static uint[] Bins { get; private set; } = [];
    public static event Action? Updated;

    static Levels() {
        EngineModel.Shared.Rendered += Refresh;
        Refresh();
    }

    // ponytail: synchronous on every settled render; Swift waits 160 ms. Debounce if typed values or resets feel slow
    // (a timer would also need flushing before a headless --screenshot).
    static void Refresh() {
        if (EngineModel.Shared.Interacting) return;
        var bins = new uint[BinCount * 3];
        nint engine = EngineModel.Shared.Handle;
        Bins = engine != 0 && DevelopFacade.orion_engine_histogram(engine, bins, BinCount) == 0 ? bins : [];
        Updated?.Invoke();
    }

    /// The share of pixels in the extreme bin, worst channel: a blown red is
    /// clipped, and averaging it with two clean channels reports a third of it.
    public static double Clipped(uint[] bins, bool atStart) {
        int n = bins.Length / 3;
        if (n <= 1) return 0;
        double worst = 0;
        for (int c = 0; c < 3; c++) {
            double total = 0;
            for (int i = 0; i < n; i++) total += bins[c * n + i];
            if (total > 0) worst = Math.Max(worst, bins[atStart ? c * n : c * n + n - 1] / total);
        }
        return worst;
    }
}

/// A port of app/Histogram.swift: the instrument above the tab bar. Flags that
/// fly only when a channel clips, the curves in a recessed plate, and an
/// engraved rail marked in quarters of the display range (not stops: see the
/// Swift header).
[Panel("develop-histogram", Region.Tools, 0)]
public sealed class HistogramPanel : StackPanel {
    /// Below this share of the frame a flag is reporting noise (UNSOURCED.md §19).
    const double ClipThreshold = 0.001;
    const double PlateHeight = 84;

    readonly Grid _flags = new() { ColumnDefinitions = new("Auto,*,Auto") };
    readonly HistogramPlate _plate = new() { Height = PlateHeight };

    public HistogramPanel() {
        Spacing = 4;
        Margin = new Thickness(0, 12, 0, 10);   // Swift: .padding(.top, 12) .padding(.bottom, 10)
        var levels = Engraved.Label("Levels", Palette.Faint, 9);
        levels.HorizontalAlignment = HorizontalAlignment.Center;
        Grid.SetColumn(levels, 1);
        _flags.Children.Add(levels);
        Children.Add(_flags);
        Children.Add(new Border { BorderBrush = Palette.Line, BorderThickness = new Thickness(1), Background = Palette.FilmBase,
                                  Padding = new Thickness(1), Child = _plate });
        Children.Add(new Rail());
        var names = new Grid { ColumnDefinitions = new("*,*,*") };
        string[] words = ["Black", "Mid", "White"];
        for (int i = 0; i < 3; i++) {
            var label = Engraved.Label(words[i], Palette.Faint, 9);
            label.HorizontalAlignment = new[] { HorizontalAlignment.Left, HorizontalAlignment.Center, HorizontalAlignment.Right }[i];
            Grid.SetColumn(label, i);
            names.Children.Add(label);
        }
        Children.Add(names);
        Levels.Updated += Refresh;
        Refresh();
    }

    void Refresh() {
        var bins = Levels.Bins;
        IsVisible = bins.Length > 0;   // Swift: only once the engine has a histogram
        _plate.Bins = bins;
        _plate.InvalidateVisual();
        _flags.Children.RemoveAll(_flags.Children.OfType<Border>().ToList());
        AddFlag(Levels.Clipped(bins, true), true);
        AddFlag(Levels.Clipped(bins, false), false);
    }

    /// A flag holds its 44 pt slot lit or not, so the row does not reflow.
    void AddFlag(double share, bool pointsLeft) {
        bool lit = share >= ClipThreshold;
        var flag = new StackPanel { Orientation = Orientation.Horizontal, Spacing = 3,
                                    HorizontalAlignment = pointsLeft ? HorizontalAlignment.Left : HorizontalAlignment.Right };
        var triangle = new Path {
            Data = Geometry.Parse(pointsLeft ? "M4,0 L0,3.5 L4,7 Z" : "M0,0 L4,3.5 L0,7 Z"),
            Fill = lit ? Palette.Star : Palette.Faint, Opacity = lit ? 1 : 0.35, VerticalAlignment = VerticalAlignment.Center,
        };
        var percent = Engraved.Readout(PercentText(share), Palette.Star, 9);
        if (!pointsLeft && lit) flag.Children.Add(percent);
        flag.Children.Add(triangle);
        if (pointsLeft && lit) flag.Children.Add(percent);
        ToolTip.SetTip(flag, pointsLeft ? $"Shadow clipping: {PercentText(share)} of pixels at black"
                                        : $"Highlight clipping: {PercentText(share)} of pixels at white");
        var slot = new Border { MinWidth = 44, Child = flag };
        Grid.SetColumn(slot, pointsLeft ? 0 : 2);
        _flags.Children.Add(slot);
    }

    static string PercentText(double share) =>
        share <= 0 ? "0%" : share < 0.0001 ? "<0.01%" : (share * 100).ToString("0.00", System.Globalization.CultureInfo.InvariantCulture) + "%";

    /// Index marks at the quarters: the named ones (ends, middle) longer and dimmer-than-text,
    /// the two between shorter and fainter. Inset half a line at the ends.
    sealed class Rail : Control {
        public Rail() => Height = 6;
        public override void Render(DrawingContext c) {
            for (int i = 0; i <= 4; i++) {
                double x = 0.5 + (Bounds.Width - 1) * i / 4;
                c.DrawLine(new Pen(i % 2 == 0 ? Palette.Dim : Palette.Faint, 1), new Point(x, 0), new Point(x, i % 2 == 0 ? 6 : 3));
            }
        }
    }
}

/// The curves in their recess: filled channels with screen blending over film
/// base, so where all three agree the plot goes white. Rasterised here because
/// a DrawingContext has no per-draw screen blend; 2 px per point, like Retina.
sealed class HistogramPlate : Control {
    const double Scale = 2;
    public uint[] Bins = [];

    public override void Render(DrawingContext context) {
        int w = (int)(Bounds.Width * Scale), h = (int)(Bounds.Height * Scale), n = Bins.Length / 3;
        if (w <= 0 || h <= 0 || n <= 2) return;
        var pixels = new byte[w * h * 4];
        var ground = Rgb(Palette.FilmBase);
        Color[] inks = [((ISolidColorBrush)Palette.ChanR).Color, ((ISolidColorBrush)Palette.ChanG).Color, ((ISolidColorBrush)Palette.ChanB).Color];
        double ceiling = Math.Max(Ceiling(n), 1);
        var heights = new double[3];
        for (int x = 0; x < w; x++) {
            // The path joins bin i at x = width * i / (n - 1): interpolate between the two either side.
            double f = (x + 0.5) / w * (n - 1);
            int i0 = Math.Min((int)f, n - 2);
            for (int c = 0; c < 3; c++) {
                double a = Plotted(Bins[c * n + i0] / ceiling), b = Plotted(Bins[c * n + i0 + 1] / ceiling);
                heights[c] = h * (1 - (a + (b - a) * (f - i0)));
            }
            // Quarter divisions, white at 8%, under the curves.
            bool gridline = Enumerable.Range(1, 3).Any(q => x == (int)(w * q / 4.0));
            var under = gridline ? Mix(ground, (1, 1, 1), 0.08) : ground;
            for (int y = 0; y < h; y++) {
                var px = under;
                for (int c = 0; c < 3; c++) {
                    double cover = Math.Clamp(y + 1 - heights[c], 0, 1) * 0.85;
                    if (cover > 0) px = Mix(px, Screen(px, Rgb(inks[c])), cover);
                }
                int o = (y * w + x) * 4;
                (pixels[o], pixels[o + 1], pixels[o + 2], pixels[o + 3]) = (Byte(px.R), Byte(px.G), Byte(px.B), 255);
            }
        }
        var bitmap = new WriteableBitmap(new PixelSize(w, h), new Vector(96, 96), PixelFormat.Rgba8888, AlphaFormat.Opaque);
        using (var fb = bitmap.Lock())
            for (int y = 0; y < h; y++) Marshal.Copy(pixels, y * w * 4, fb.Address + y * fb.RowBytes, w * 4);
        // 96 dpi, so the bitmap's size and its pixels agree and the whole of it is stretched in.
        context.DrawImage(bitmap, new Rect(Bounds.Size));
    }

    /// The tallest bin the curve has to fit, ignoring the two clipped ends:
    /// the flags report those as numbers, so the curve need not carry them.
    double Ceiling(int n) {
        uint peak = 0;
        for (int c = 0; c < 3; c++)
            for (int i = 1; i < n - 1; i++) peak = Math.Max(peak, Bins[c * n + i]);
        return peak;
    }

    /// Square root: a linear axis draws all but the tallest few bins as the floor.
    static double Plotted(double fraction) => Math.Min(Math.Sqrt(Math.Max(fraction, 0)), 1);

    static (double R, double G, double B) Rgb(IBrush brush) => Rgb(((ISolidColorBrush)brush).Color);
    static (double R, double G, double B) Rgb(Color c) => (c.R / 255.0, c.G / 255.0, c.B / 255.0);
    static (double R, double G, double B) Screen((double R, double G, double B) a, (double R, double G, double B) b) =>
        (1 - (1 - a.R) * (1 - b.R), 1 - (1 - a.G) * (1 - b.G), 1 - (1 - a.B) * (1 - b.B));
    static (double R, double G, double B) Mix((double R, double G, double B) a, (double R, double G, double B) b, double t) =>
        (a.R + (b.R - a.R) * t, a.G + (b.G - a.G) * t, a.B + (b.B - a.B) * t);
    static byte Byte(double v) => (byte)Math.Clamp(Math.Round(v * 255), 0, 255);
}
