using Avalonia;
using Avalonia.Controls;
using Avalonia.Media;
using Avalonia.Threading;

namespace OrionShell.CanvasSlice;

/// A port of app/Viewport.swift: zoom as a multiple of fit, center in
/// normalized image coordinates so it survives a resize.
sealed class Viewport {
    public double Zoom { get; private set; } = 1;
    public Point Center { get; private set; } = new(0.5, 0.5);
    /// Drawable pixels per image pixel at zoom 1; the canvas sets it as it draws.
    public double FitScale = 1;
    bool _locked;

    /// Held at fit while the crop tool is open, so the crop handles stay on the pixels.
    public bool Locked { get => _locked; set { _locked = value; if (value) Reset(); } }
    public bool IsFit => Math.Abs(Zoom - 1) < 0.001;
    public int Percent => (int)Math.Round(FitScale * Zoom * 100);

    public void Reset() { (Zoom, Center) = (1, new Point(0.5, 0.5)); CanvasState.Touch(); }

    public void ToggleFitAndActual() {
        if (_locked) return;
        if (IsFit) Zoom = Math.Max(1, 1 / Math.Max(FitScale, 0.0001)); else Reset();
        ClampCenter();
    }

    /// Zooms about a normalized view point, keeping the pixel under it still.
    public void ZoomBy(double factor, Point anchor, Size visible) {
        if (_locked) return;
        double old = Zoom;
        Zoom = Math.Clamp(Zoom * factor, 1, 64);
        if (Zoom == old) return;
        double shrink = 1 - old / Zoom;
        Center = new Point(Center.X + (anchor.X - 0.5) * visible.Width * shrink, Center.Y + (anchor.Y - 0.5) * visible.Height * shrink);
        ClampCenter();
    }

    /// Pans by a delta in normalized view coordinates.
    public void Pan(Vector delta, Size visible) {
        if (_locked) return;
        Center = new Point(Center.X - delta.X * visible.Width, Center.Y - delta.Y * visible.Height);
        ClampCenter();
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

    void ClampCenter() { Center = new Point(Math.Clamp(Center.X, 0, 1), Math.Clamp(Center.Y, 0, 1)); CanvasState.Touch(); }

    /// Final clamp so the viewport never shows outside the frame.
    public void Clamp(Size visible) {
        Center = new Point(visible.Width >= 1 ? 0.5 : Math.Clamp(Center.X, visible.Width / 2, 1 - visible.Width / 2),
                           visible.Height >= 1 ? 0.5 : Math.Clamp(Center.Y, visible.Height / 2, 1 - visible.Height / 2));
        CanvasState.Touch();
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
        EngineModel.Shared.ZoomedPastFit = !Viewport.IsFit;   // past fit the photo renders full size (#318)
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
        ShownFor = engine.Shown.Bitmap != null ? engine.Current : -1;
        engine.Shown.Changed += () => ShownFor = engine.Shown.Bitmap != null ? engine.Current : -1;
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

    /// `drawnFrame`: where the picture is right now, zoom included.
    public static Rect DrawnFrame(Size size) => ImageAspect > 0 && size.Height > 0
        ? CanvasLayout.DrawnRect(Viewport.QuadScale(ImageAspect, size.Width / size.Height), size) : default;

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
