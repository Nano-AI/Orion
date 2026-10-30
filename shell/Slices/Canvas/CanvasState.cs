using Avalonia;
using Avalonia.Controls;
using Avalonia.Media;
using Avalonia.Threading;

namespace OrionShell.CanvasSlice;

/// A port of the Swift app's Viewport.swift: zoom as a multiple of fit, center in
/// normalized image coordinates so it survives a resize. Past Swift, after
/// Apple's Designing Fluid Interfaces (WWDC 2018): a hand moves a raw zoom and
/// center, what shows is the raw state rubber-banded at the limits (§9), and a
/// release springs it back inside carrying the hand's velocity (§4–6).
sealed class Viewport {
    const double MaxZoom = 64;
    double _log;                    // ln of the raw zoom
    Point _raw = new(0.5, 0.5);     // the raw center
    bool _locked, _hand;

    /// What is on screen: the raw state, banded.
    public double Zoom { get; private set; } = 1;
    public Point Center { get; private set; } = new(0.5, 0.5);
    /// Drawable pixels per image pixel at zoom 1; the canvas sets it as it draws.
    public double FitScale = 1;
    /// How much of the view the frame covers at fit, per axis; the canvas sets it (Measure).
    public Size Coverage { get; private set; } = new(1, 1);

    /// Held at fit while the crop tool is open, so the crop handles stay on the pixels.
    public bool Locked { get => _locked; set { _locked = value; if (value) Reset(); } }
    public bool IsFit => Math.Abs(Zoom - 1) < 0.001;
    public bool PastFit => Zoom > 1.001;
    public int Percent => (int)Math.Round(FitScale * Zoom * 100);
    /// A hand or a spring is moving it: the full-size render waits for rest (#318).
    public bool Live => _hand || Motion.IsRunning(this);
    /// Where the hand has put the center, before the rubber band.
    public Point Raw => _raw;
    /// Visible fraction of the image on each axis, at the zoom on screen.
    public Size Visible => VisibleAt(Zoom);
    Size VisibleAt(double zoom) => new(Math.Min(1, 1 / (zoom * Coverage.Width)), Math.Min(1, 1 / (zoom * Coverage.Height)));

    /// Apple's projection: where a flick at `v` units a second comes to rest, decelerating 0.998 a millisecond.
    public static double Projected(double v) => v / 1000 * 0.998 / (1 - 0.998);
    /// Apple's rubber band: `over` past a limit shows as this, never more than `dim`.
    public static double Band(double over, double dim) => over * dim * 0.55 / (dim + 0.55 * Math.Abs(over));
    static double Axis(double c, double visible, bool band) {
        double lo = visible >= 1 ? 0.5 : visible / 2, hi = visible >= 1 ? 0.5 : 1 - visible / 2;
        return !band ? Math.Clamp(c, lo, hi) : c < lo ? lo + Band(c - lo, visible) : c > hi ? hi + Band(c - hi, visible) : c;
    }
    /// Below fit the picture shrinks to half at most; past 64× it grows by as much.
    static double Shown(double log) => log < 0 ? Band(log, Math.Log(2)) : log > Math.Log(MaxZoom) ? Math.Log(MaxZoom) + Band(log - Math.Log(MaxZoom), Math.Log(2)) : log;

    void Show() {
        Zoom = Math.Exp(Shown(_log));
        var v = Visible;
        Center = new Point(Axis(_raw.X, v.Width, true), Axis(_raw.Y, v.Height, true));
        CanvasState.Touch();
    }

    void Jump(double log, Point center) {
        Motion.Stop(this);
        _hand = false;
        _log = log;
        var v = VisibleAt(Math.Exp(log));
        _raw = new Point(Axis(center.X, v.Width, false), Axis(center.Y, v.Height, false));
        Show();
    }

    public void Reset() => Jump(0, new Point(0.5, 0.5));

    /// The keys' 0 and 9: a jump, never animated (a keyboard action, many times a day).
    public void ToggleFitAndActual() {
        if (_locked) return;
        if (IsFit) Jump(Math.Log(Math.Max(1, 1 / Math.Max(FitScale, 0.0001))), Center); else Reset();
    }

    /// A hand takes hold: a spring in flight stops where it is, and the hand carries on from there.
    public void Begin() { if (_locked) return; Motion.Stop(this); _hand = true; }

    /// Zooms about a normalized view point, keeping the image point under it still
    /// wherever the picture fills that axis: Swift's formula, solved from the
    /// picture map so that it also holds from a letterboxed start.
    public void ZoomBy(double factor, Point anchor) {
        if (_locked || factor <= 0) return;
        var b = Coverage;
        static double Unit(double c, double a, double bz) {
            double q = Math.Min(1, bz), v = Math.Min(1, 1 / bz);
            return c - v / 2 + (a - (1 - q) / 2) * v / q;
        }
        var u = new Point(Unit(Center.X, anchor.X, b.Width * Zoom), Unit(Center.Y, anchor.Y, b.Height * Zoom));
        _log += Math.Log(factor);
        double z = Math.Exp(Shown(_log)), bx = b.Width * z, by = b.Height * z;
        var v1 = VisibleAt(z);
        // A zoom never rubber-bands the pan; only a hand dragging past an edge does.
        _raw = new Point(Axis(bx >= 1 ? u.X - (anchor.X - 0.5) / bx : 0.5, v1.Width, false),
                         Axis(by >= 1 ? u.Y - (anchor.Y - 0.5) / by : 0.5, v1.Height, false));
        Show();
    }

    /// Pans by a delta in normalized view coordinates; past an edge it rubber-bands.
    public void Pan(Vector delta) {
        if (_locked) return;
        var v = Visible;
        _raw = new Point(_raw.X - delta.X * v.Width, _raw.Y - delta.Y * v.Height);
        Show();
    }

    /// The hand let go: springs back inside the limits, critically damped, from
    /// where it is and at the hand's speed; a flick (`velocity`, center units a
    /// second) travels on to where it would have come to rest.
    public void Settle(Vector velocity = default) {
        _hand = false;
        if (_locked) return;
        if (Motion.Reduced) velocity = default;   // Reduce Motion: no throw, only the way back inside
        double toLog = Math.Clamp(_log, 0, Math.Log(MaxZoom));
        var v = VisibleAt(Math.Exp(toLog));
        var to = new Point(Axis(_raw.X + Projected(velocity.X), v.Width, false), Axis(_raw.Y + Projected(velocity.Y), v.Height, false));
        Spring log = new(_log), x = new(_raw.X, velocity.X), y = new(_raw.Y, velocity.Y);
        Motion.Run(this, dt => {
            bool moving = log.Step(toLog, dt, 0.35, 1e-4) | x.Step(to.X, dt, 0.35, 1e-5) | y.Step(to.Y, dt, 0.35, 1e-5);
            (_log, _raw) = (log.Value, new Point(x.Value, y.Value));
            if (!moving) Motion.Stop(this);   // so the last Touch sees it at rest
            Show();
            return moving;
        });
    }

    /// Fit or 1:1 from a click (the navigator's pills, a double-click): a spring
    /// about `anchor` (normalized view), so the point clicked stays put.
    public void AnimateZoom(double zoom, Point anchor) {
        if (_locked) return;
        Motion.Stop(this);
        double to = Math.Log(Math.Clamp(zoom, 1, MaxZoom));
        Spring log = new(_log);
        Motion.Run(this, dt => {
            bool moving = log.Step(to, dt, 0.35, 1e-4);
            if (!moving) Motion.Stop(this);
            ZoomBy(Math.Exp(log.Value - _log), anchor);
            return moving;
        });
    }

    /// Sets Coverage for a view of this aspect.
    public void Measure(double imageAspect, double viewAspect) {
        if (imageAspect > 0 && viewAspect > 0) Coverage = FitCoverage(imageAspect, viewAspect);
    }

    static Size FitCoverage(double imageAspect, double viewAspect) =>
        imageAspect > viewAspect ? new Size(1, viewAspect / imageAspect) : new Size(imageAspect / viewAspect, 1);

    /// How much of the view the image covers, per axis.
    public Size QuadScale(double imageAspect, double viewAspect) {
        var b = FitCoverage(imageAspect, viewAspect);
        return new Size(Math.Min(1, b.Width * Zoom), Math.Min(1, b.Height * Zoom));
    }

    /// Visible fraction of the image on each axis.
    public Size VisibleFraction(double imageAspect, double viewAspect) {
        var b = FitCoverage(imageAspect, viewAspect);
        return new Size(Math.Min(1, 1 / (Zoom * b.Width)), Math.Min(1, 1 / (Zoom * b.Height)));
    }
}

// ponytail: three springs live in the shell: this stepper and its keyed frame clock (Motion, below), u5's
// owner-bound OrionShell.Spring (Shared/Design), and u4's closed-form SpringBack (MaskLayout). Fold onto u5's.
/// Apple's spring with damping 1 (Designing Fluid Interfaces): no overshoot
/// from rest, described by its response in seconds. It starts from the value on
/// screen with the velocity it already has, so a new target never jumps.
sealed class Spring(double value, double velocity = 0) {
    public double Value = value, Velocity = velocity;

    /// One frame of x(t) = (x₀ + (v₀ + ωx₀)t)·e^(−ωt) about `target`, ω = 2π/response.
    /// False once within `eps` and still, landing exactly on the target.
    public bool Step(double target, double dt, double response, double eps) {
        double w = 2 * Math.PI / response, x0 = Value - target, b = Velocity + w * x0, e = Math.Exp(-w * dt);
        Value = target + (x0 + b * dt) * e;
        Velocity = (Velocity - w * b * dt) * e;
        if (Math.Abs(Value - target) > eps || Math.Abs(Velocity) > eps * 10) return true;
        (Value, Velocity) = (target, 0);
        return false;
    }
}

/// The display's frame clock (TopLevel.RequestAnimationFrame, Avalonia's
/// CADisplayLink): steps each running motion once a frame until it settles.
/// One motion per key; running a key again replaces it, which is how a new
/// gesture interrupts a spring. With no window, or Reduce Motion on, a motion
/// runs to its end at once.
static class Motion {
    static readonly Dictionary<object, Func<double, bool>> Running = new();
    static TopLevel? _clock;
    static TimeSpan? _last;
    static bool _asked, _draining;

    public static bool Reduced => OrionShell.Motion.Reduced;   // the shell's one reader (u5): macOS and Windows
    public static bool IsRunning(object key) => Running.ContainsKey(key);
    public static void Stop(object key) => Running.Remove(key);

    /// The canvas's window, once it has one.
    public static void Attach(TopLevel? clock) {
        (_clock, _asked, _last) = (clock, false, null);
        if (Running.Count > 0) Ask();
    }

    /// That window closed: what was running finishes at once rather than waiting on a dead clock.
    public static void Detach(TopLevel? clock) { if (clock == _clock) Attach(null); }

    /// Units a second over the last 80 ms of a hand's samples, as a release hands
    /// over to a spring; zero once the hand has rested 50 ms before letting go.
    public static double Velocity(List<(ulong T, double V)> samples, ulong now) {
        if (samples.Count < 2 || now - samples[^1].T > 50) return 0;
        var first = samples.LastOrDefault(s => samples[^1].T - s.T >= 80, samples[0]);
        double dt = (samples[^1].T - first.T) / 1000.0;
        return dt > 0 ? (samples[^1].V - first.V) / dt : 0;
    }

    public static void Run(object key, Func<double, bool> frame) {
        Running[key] = frame;
        Ask();
    }

    static bool Current(object key, Func<double, bool> frame) => Running.GetValueOrDefault(key) == frame;

    static void Ask() {
        if (_clock == null || Reduced) {   // a second a frame: done in one or two
            // A frame may start another motion (the canvas's rest settles the
            // viewport): this loop runs it, rather than Ask recursing until the
            // stack overflows, as it did under Reduce Motion on CI (#376).
            if (_draining) return;
            _draining = true;
            try {
                while (Running.Count > 0)
                    foreach (var (key, frame) in Running.ToArray()) {
                        for (int i = 0; i < 10 && Current(key, frame) && frame(1); i++) { }
                        if (Current(key, frame)) Running.Remove(key);
                    }
            } finally { _draining = false; }
            return;
        }
        if (_asked) return;
        _asked = true;
        _clock.RequestAnimationFrame(Tick);
    }

    static void Tick(TimeSpan now) {
        _asked = false;
        double dt = _last is { } last ? Math.Clamp((now - last).TotalSeconds, 0, 1 / 30.0) : 1 / 60.0;
        _last = now;
        foreach (var (key, frame) in Running.ToArray())
            if (Current(key, frame) && !frame(dt) && Current(key, frame)) Running.Remove(key);
        if (Running.Count > 0) Ask(); else _last = null;
    }
}

/// What the canvas panels share (OrionApp+Canvas.swift's `canvas` and its three
/// geometry helpers): the viewport, the tool tab on show, the colour under the
/// cursor. Every overlay repaints on Changed.
static class CanvasState {
    /// OrionApp+Canvas.swift pads the picture 20 on every side; no token is 20.
    public const double Inset = 20;
    public static readonly Viewport Viewport = new();
    public static event Action? Changed;
    public static void Touch() {
        // Past fit the photo renders full size (#318), once the hand rests: a
        // full render inside a pinch would hold the fingers still.
        if (!Viewport.Live) EngineModel.Shared.ZoomedPastFit = Viewport.PastFit;
        Changed?.Invoke();
    }

    /// The tool tab the shell is showing. Overlays belong to theirs (the crop
    /// overlay to Crop, spots to Detail, the mask to Mask).
    public static string? Tab { get; private set; }

    /// Under the cursor while a picking tool is armed (TargetedAdjust's hover*).
    public static Point? HoverPoint { get; private set; }
    public static Color HoverColor { get; private set; }
    public static string HoverCaption { get; private set; } = "";

    /// What the canvas has to show (Controls.dc.html's browsing states).
    public enum Scene { Welcome, Picture, Loading, Failed }

    /// The photo the shown picture was made for: Current at the moment it was
    /// shown. A picture always arrives after the list has moved to its photo, so
    /// a picture left over from the previous photo never counts as this one's.
    public static int ShownFor { get; private set; } = -1;

    /// Nothing to open; a picture for this photo; this photo still decoding (a
    /// calm blank, never the previous photo); or this photo failed to open or render.
    public static Scene SceneFor(int count, int current, nint handle, int shownFor, string timing) =>
        count == 0 || current < 0 ? Scene.Welcome
        : shownFor == current ? Scene.Picture   // this photo's kept picture draws while its session decodes (#318)
        : handle == 0 ? (timing == CouldNotOpen ? Scene.Failed : Scene.Loading)
        : timing.StartsWith("render failed") ? Scene.Failed : Scene.Loading;

    /// EngineModel.Show's Timing when the session source returned no session.
    const string CouldNotOpen = "could not open";

    public static Scene Now {
        get { var e = EngineModel.Shared; return SceneFor(e.Photos.Count, e.Current, e.Handle, ShownFor, e.Timing); }
    }

    static CanvasState() {
        var engine = EngineModel.Shared;
        ShownFor = engine.Shown.Any ? engine.Current : -1;
        engine.Shown.Changed += () => ShownFor = engine.Shown.Any ? engine.Current : -1;
        engine.Opened += () => { Viewport.Reset(); ClearHover(); };
        engine.Rendered += Touch;
        engine.PropertyChanged += (_, e) => {
            if (e.PropertyName != nameof(EngineModel.Tool)) return;
            if (!WantsLoupe) ClearHover();
            Touch();
        };
    }

    /// A panel shown alone with --panel: it acts as if its tab were selected.
    public static bool Alone(string panel) => App.Run.Panel == panel;

    public static bool WantsLoupe => EngineModel.Shared.Tool is CanvasTool.Targeted or CanvasTool.MaskColor;

    /// OrionApp+Canvas.swift `.onChange(of: tab)`: anything armed belongs to the
    /// panel that armed it; the crop tab previews the whole frame, locks the
    /// view and drops compare, whose held original is at the old geometry.
    public static void SetTab(string? tab) {
        if (tab == Tab) return;
        Tab = tab;
        EngineModel.Shared.Tool = CanvasTool.None;
        ClearHover();
        Viewport.Locked = tab == "Crop";
        CropEdit.Preview(tab == "Crop");
        if (tab == "Crop") CompareState.Clear();
        Touch();
    }

    /// Follows the shell's tool tab by watching which registered tab panels it hosts.
    /// ponytail: reads MainWindow's ToolsHost by name; a Tab on EngineModel removes this.
    public static void FollowShell(TopLevel? top) {
        // A --panel window is built in code and has no name scope: no shell, no tabs to follow.
        if (top is not Window window || NameScope.GetNameScope(window)?.Find<StackPanel>("ToolsHost") is not { } host) return;
        void Read() => SetTab(host.Children
            .Select(c => Panels.All.FirstOrDefault(e => e.Type == c.GetType())?.Tab)
            .FirstOrDefault(t => t != null));
        host.Children.CollectionChanged += (_, _) => Dispatcher.UIThread.Post(Read);   // after Clear+AddRange
        Read();
    }

    public static void SetHover(Point at, Color color, string caption) {
        (HoverPoint, HoverColor, HoverCaption) = (at, color, caption);
        Touch();
    }

    public static void ClearHover() {
        if (HoverPoint == null) return;
        HoverPoint = null;
        Touch();
    }

    /// Several fields, one full render: the writes go to the preview graph and
    /// the end of the interaction renders once.
    /// ponytail: EngineModel renders per Set; a batch Set in Shared removes the preview renders.
    public static void Write(params (string Field, double Value)[] writes) {
        var e = EngineModel.Shared;
        if (e.Handle == 0) return;
        var changed = writes.Where(w => e.Get(w.Field) != (float)w.Value).ToArray();
        if (changed.Length == 0) return;
        bool batch = !e.Interacting && changed.Length > 1;   // one write, or inside a drag: as is
        if (batch) e.BeginInteraction();
        foreach (var (f, v) in changed) e.Set(f, (float)v);
        if (batch) e.EndInteraction();
    }

    /// The open image's width over height as rendered (crop preview included).
    public static double ImageAspect => CanvasNative.ImageSize is { Width: > 0, Height: > 0 } s ? (double)s.Width / s.Height
        : EngineModel.Shared.Shown.Bitmap is { } b ? b.Size.Width / b.Size.Height : 0;   // a kept picture while its session decodes

    /// `photoFrame`: the picture at fit, which is where the crop overlay draws.
    public static Rect PhotoFrame(Size size) => CanvasLayout.FrameRect(ImageAspect, size);

    /// `drawnFrame`: where the picture is on screen right now, zoom, pan and a
    /// rubber band included.
    public static Rect DrawnFrame(Size size) => ImageAspect > 0 && size.Height > 0 ? Whole(size).Intersect(new Rect(size)) : default;

    /// The whole picture's rectangle in the view: past fit, mostly off screen.
    public static Rect Whole(Size size) {
        var map = Map(size);
        return new Rect(map.ToView(new Point(0, 0)), map.ToView(new Point(1, 1)));
    }

    /// `pictureMap`: normalized picture ↔ view, zoom, pan and the frame map included.
    public static PictureMap Map(Size size) {
        double a = ImageAspect;
        if (a <= 0 || size.Width <= 0 || size.Height <= 0)
            return new PictureMap(default, new Size(1, 1), default, FrameDisplayMap.Identity);
        double va = size.Width / size.Height;
        return CanvasLayout.Map(Viewport.QuadScale(a, va), Viewport.VisibleFraction(a, va), Viewport.Center, size,
                                CanvasNative.DisplayMap());
    }
}

/// Draws through a callback. Panel.Render is sealed in Avalonia 12, so a
/// panel that both draws and hosts children draws through one of these, its
/// bottom child; `At` places a child at a point, as `.position` does.
sealed class Drawn(Action<DrawingContext> draw) : Control {
    public override void Render(DrawingContext c) => draw(c);

    /// A child's own size, to centre it with At. Not DesiredSize after an At: that
    /// counts the Margin At placed it by, so a label re-centred on every repaint
    /// walked left by half its last offset (the compare labels left the picture).
    public static Size Measured(Control child) {
        child.Margin = default;
        child.Measure(Size.Infinity);
        return child.DesiredSize;
    }

    public static void At(Control child, double x, double y) {
        child.HorizontalAlignment = Avalonia.Layout.HorizontalAlignment.Left;
        child.VerticalAlignment = Avalonia.Layout.VerticalAlignment.Top;
        child.Margin = new Thickness(x, y, 0, 0);
    }
}

/// The overlays' ink: a white line over a dark halo, legible on a blown sky and
/// a black shadow alike. Swift draws Color.white/.black; tokens.json has neither.
static class Ink {
    public static IBrush White(double opacity = 1) => new SolidColorBrush(Colors.White, opacity);
    public static IBrush Black(double opacity) => new SolidColorBrush(Colors.Black, opacity);
    public static readonly IBrush Halo = Black(0.55);
}
