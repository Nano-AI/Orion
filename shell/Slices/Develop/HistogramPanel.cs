using Avalonia;
using Avalonia.Animation;
using Avalonia.Animation.Easings;
using Avalonia.Controls;
using Avalonia.Layout;
using Avalonia.Media;
using Avalonia.Media.Imaging;
using Avalonia.Platform;
using Avalonia.Styling;
using Avalonia.Threading;
using System.Runtime.InteropServices;
using OrionShell.Chrome;
using Path = Avalonia.Controls.Shapes.Path;

namespace OrionShell.Develop;

/// The engine's per-channel histogram of the full render, 128 bins each, red
/// then green then blue (Engine.histogram). Not refreshed mid-drag: it reads
/// the whole output texture, and it is a readout nobody watches while a slider
/// moves (Engine+Render.swift scheduleHistogram). Empty from the moment the
/// photo changes until the full graph has rendered it: a swap at fit renders
/// only the preview graph, and the full one follows once the hand rests (#324).
static class Levels {
    public const int BinCount = 128;
    public static uint[] Bins { get; private set; } = [];
    public static event Action? Updated;

    static Levels() {
        EngineModel.Shared.Rendered += Refresh;
        // The last photo's curve is not this one's: say nothing until it is measured.
        EngineModel.Shared.PropertyChanged += (_, e) => {
            if (e.PropertyName == nameof(EngineModel.Current) && Bins.Length > 0) { Bins = []; Updated?.Invoke(); }
        };
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

/// A port of the Swift app's Histogram.swift as Main.dc.html draws it: the curves in a
/// recessed plate; under it the clipping at each end, the shadow share on the
/// left and the highlight share on the right, star-coloured only when a channel
/// really clips; then what the camera recorded (CaptureLine).
///
/// Every part keeps its height whether or not there is anything to show, so a
/// photo swap never moves the tabs and sliders below. While the plate waits
/// for the full render it stays empty; if that takes longer than a swap
/// usually does, a quiet pulse says it is coming. The curves fade in.
[Panel("develop-histogram", Region.Tools, 0)]
public sealed class HistogramPanel : StackPanel {
    /// Below this share of the frame a flag is reporting noise (UNSOURCED.md §19).
    const double ClipThreshold = 0.001;
    const double PlateHeight = 92, FlagsHeight = 18;
    /// Most swaps fill the plate in about 300 ms (#324); a pulse for those would flash on every arrow press.
    static readonly TimeSpan PulseAfter = TimeSpan.FromMilliseconds(500);
    /// Opacity only; a little under one cycle a second, nowhere near a slow sway.
    // ponytail: runs under Reduce Motion too (opacity is the kind it keeps); hold it still once u5's Motion.Reduced lands.
    static readonly Animation Pulse = new() {
        Duration = TimeSpan.FromMilliseconds(650), IterationCount = IterationCount.Infinite,
        PlaybackDirection = PlaybackDirection.Alternate, Easing = new SineEaseInOut(),
        Children = {
            new KeyFrame { Cue = new Cue(0), Setters = { new Setter(OpacityProperty, 0.3) } },
            new KeyFrame { Cue = new Cue(1), Setters = { new Setter(OpacityProperty, 0.75) } },
        },
    };

    readonly HistogramPlate _plate = new() { Height = PlateHeight };
    /// What the plate shows while it waits: a flat fill, never a made-up curve.
    readonly Border _waiting = new() { Background = Palette.Raised, CornerRadius = new CornerRadius(Radius.Sm), IsVisible = false };
    readonly Border _shadows = new(), _highlights = new() { HorizontalAlignment = HorizontalAlignment.Right };
    readonly CaptureLine _capture = new();
    /// Bumped by each emptying, so only the latest one's wait can start the pulse.
    int _emptied;
    bool _arrived;
    CancellationTokenSource? _pulsing;

    public HistogramPanel() {
        Spacing = Space.Gap;
        Margin = new Thickness(0, Space.Md);
        _plate.Transitions = [new DoubleTransition { Property = OpacityProperty, Duration = TimeSpan.FromMilliseconds(100), Easing = new CubicEaseOut() }];
        var plate = new Border { BorderBrush = Palette.Line, BorderThickness = new Thickness(1), Background = Palette.FilmBase,
                                 CornerRadius = new CornerRadius(Radius.Sm), Padding = new Thickness(1), Child = new Panel { Children = { _waiting, _plate } } };
        Avalonia.Automation.AutomationProperties.SetName(plate, "Histogram");
        Children.Add(plate);
        var flags = new Grid { ColumnDefinitions = new("Auto,*,Auto"), Height = FlagsHeight };
        Grid.SetColumn(_highlights, 2);
        flags.Children.AddRange([_shadows, _highlights]);
        Children.Add(flags);
        Children.Add(_capture);
        Levels.Updated += Refresh;
        EngineModel.Shared.PropertyChanged += (_, _) => { if (_pulsing != null && !Waiting) StopPulse(); };   // a photo that would not open
        EngineModel.Shared.Opened += ShowCapture;
        ShowCapture();
        Refresh();
    }

    /// The waiting pulse is running (the selftest's handle on it).
    public bool Pulsing => _pulsing != null;

    /// A photo is open or opening and its histogram has not been measured yet.
    static bool Waiting => Levels.Bins.Length == 0 && (EngineModel.Shared.Handle != 0 || EngineModel.Shared.Opening);

    void ShowCapture() {
        var engine = EngineModel.Shared;
        _capture.Show(CaptureInfo.Read(engine.Handle), engine.Current >= 0 ? engine.Photos[engine.Current] : null);
    }

    void Refresh() {
        var bins = Levels.Bins;
        bool had = _plate.Bins.Length > 0, has = bins.Length > 0;
        _plate.Bins = bins;
        _plate.InvalidateVisual();
        // Gone at once; an arrival after a swap fades in. The panel's first curve (a new
        // window, a --screenshot) is simply there.
        if (!has) Fade(0, animate: false);
        else if (!had) Fade(1, animate: _arrived);
        _arrived |= has;
        _shadows.Child = has ? Flag(Levels.Clipped(bins, true), true) : null;
        _highlights.Child = has ? Flag(Levels.Clipped(bins, false), false) : null;
        // The 500 ms count from the latest swap, so browsing at speed never pulses; the tick decides whether anything is coming.
        // A task, not a DispatcherTimer: the headless selftest pumps jobs but no timers (as EngineModel's rest render).
        int emptied = ++_emptied;
        if (has) StopPulse();
        else if (_pulsing == null) Task.Delay(PulseAfter).ContinueWith(_ => Dispatcher.UIThread.Post(() => {
            if (emptied == _emptied && Waiting && _pulsing == null) StartPulse();
        }));
    }

    void Fade(double opacity, bool animate) {
        var fade = _plate.Transitions;
        if (!animate) _plate.Transitions = null;
        _plate.Opacity = opacity;
        _plate.Transitions = fade;
    }

    void StartPulse() {
        _waiting.IsVisible = true;
        Avalonia.Automation.AutomationProperties.SetName(_waiting, "Measuring the histogram");
        _pulsing = new CancellationTokenSource();
        _ = Pulse.RunAsync(_waiting, _pulsing.Token);
    }

    void StopPulse() {
        _pulsing?.Cancel();
        _pulsing = null;
        _waiting.IsVisible = false;
    }

    /// The share at one end, with a triangle pointing off that end. A reading,
    /// not a button: the engine has no clipping overlay to show on the photo.
    static Control Flag(double share, bool pointsLeft) {
        bool lit = share >= ClipThreshold;
        var ink = lit ? Palette.Warn : Palette.Dim;
        var flag = new StackPanel { Orientation = Orientation.Horizontal, Spacing = Space.Gap, MinWidth = 44, Background = Brushes.Transparent,
                                    HorizontalAlignment = pointsLeft ? HorizontalAlignment.Left : HorizontalAlignment.Right };
        var triangle = new Path { Data = Geometry.Parse(pointsLeft ? "M7,0 L7,8 L0,4 Z" : "M0,0 L0,8 L7,4 Z"), Fill = ink,
                                  VerticalAlignment = VerticalAlignment.Center };
        var percent = Faces.Figure(PercentText(share), Faces.Label, ink);
        flag.Children.AddRange(pointsLeft ? [triangle, percent] : [percent, triangle]);
        string said = pointsLeft ? $"Shadows clipped: {PercentText(share)} of pixels at black"
                                 : $"Highlights clipped: {PercentText(share)} of pixels at white";
        ToolTip.SetTip(flag, said);
        Avalonia.Automation.AutomationProperties.SetName(flag, said);
        return flag;
    }

    static string PercentText(double share) =>
        share <= 0 ? "0%" : share < 0.0001 ? "<0.01%" : (share * 100).ToString("0.00", System.Globalization.CultureInfo.InvariantCulture) + "%";
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
