using System.Runtime.InteropServices;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.Media;
using Avalonia.Media.Imaging;
using Avalonia.Platform;
using Avalonia.Rendering;
using Avalonia.Threading;

namespace OrionShell.CanvasSlice;

/// Engine+Compare.swift's split: the held as-shot render and the geometry that
/// invalidates it. The original renders in a second engine opened on the same
/// photo, which is the as-shot state plus the default look by construction.
/// ponytail: a second engine is a second decode and its own GPU memory; a facade
/// call that renders a held state on the first engine removes it.
static class CompareState {
    public static double Split { get; private set; } = 1;
    public static bool Vertical { get; set; } = true;
    public static bool Comparing => Split < 0.999;
    public static WriteableBitmap? Original { get; private set; }

    static readonly string[] Geometry = ["rotate_quarters", "straighten_deg", "perspective_vertical",
        "perspective_horizontal", "perspective_aspect", "crop_x", "crop_y", "crop_w", "crop_h",
        "crop_preview", "preview_x", "preview_y", "preview_size"];
    static nint _shim;
    static string _heldAt = "";
    static bool _pending;

    static CompareState() {
        EngineModel.Shared.Opened += Clear;
        // `refreshOriginal`, from every render: any route into a geometry change
        // re-takes the original, once, after the writes that caused it settle.
        EngineModel.Shared.Rendered += () => {
            if (!Comparing || _pending || GeometryKey() == _heldAt) return;
            _pending = true;
            Dispatcher.UIThread.Post(() => { _pending = false; Refresh(); });
        };
    }

    /// The toolbar's Compare button (OrionApp+Chrome.swift) calls this.
    public static void Toggle() { if (Comparing) Clear(); else SetSplit(0.5); }

    public static void SetSplit(double split) {
        Split = Math.Clamp(split, 0, 1);
        Refresh();
        CanvasState.Touch();
    }

    public static void Clear() {
        Split = 1;
        Original = null;
        if (_shim != 0) Native.shim_close(_shim);
        (_shim, _heldAt) = (0, "");
        CanvasState.Touch();
    }

    static string GeometryKey() => string.Join(",", Geometry.Select(f => EngineModel.Shared.Get(f)));

    static void Refresh() {
        var e = EngineModel.Shared;
        if (!Comparing || e.Handle == 0) return;
        string key = GeometryKey();
        if (key == _heldAt && Original != null) return;
        if (_shim == 0) _shim = Native.shim_open(e.Photos[e.Current]);
        if (_shim == 0) return;
        foreach (var f in Geometry) Native.shim_set(_shim, f, 0, e.Get(f));
        _heldAt = key;
        Original = Read(_shim);
        CanvasState.Touch();
    }

    static WriteableBitmap? Read(nint shim) {
        int which = Native.shim_render(shim, 0, out _);
        int w, h;
        unsafe { if (which == 0 || Native.shim_read(shim, which, null, 0, out w, out h) == 0) return null; }
        var bitmap = new WriteableBitmap(new PixelSize(w, h), new Vector(96, 96), PixelFormat.Rgba8888, AlphaFormat.Opaque);
        var pixels = new byte[w * h * 4];
        unsafe { fixed (byte* p = pixels) Native.shim_read(shim, which, p, pixels.Length, out _, out _); }
        using var fb = bitmap.Lock();
        for (int y = 0; y < h; y++) Marshal.Copy(pixels, y * w * 4, fb.Address + y * fb.RowBytes, w * 4);
        return bitmap;
    }
}

/// A port of app/CompareOverlay.swift: the draggable divider with both sides
/// named. The split itself is drawn by the canvas, as ImageCanvas's shader does.
[Panel("canvas-compare", Region.Canvas, 40)]
public sealed class CompareOverlay : Panel, ICustomHitTest {
    const double GrabWidth = 28;
    readonly Border _before = Label(), _after = Label(), _grip;
    Control _arrows = Arrows(CompareState.Vertical);
    readonly Drawn _line;
    double? _startSplit;
    bool _gripVertical;
    Point _press;

    public CompareOverlay() {
        Margin = new Thickness(CanvasState.Inset);
        _grip = new Border {
            Width = 24, Height = 24, CornerRadius = new CornerRadius(12), Background = Ink.White(), IsHitTestVisible = false,
            BoxShadow = new BoxShadows(new BoxShadow { Blur = 4, Color = Color.FromArgb(102, 0, 0, 0) }),
        };
        _gripVertical = CompareState.Vertical;
        _line = new Drawn(Draw);
        // The arrows are the grip's sibling, not its child: a Path inside this Border
        // rendered at twice its offset (Avalonia 12.1.4, headless), in the frame's corner.
        Children.AddRange([_line, _before, _after, _grip, _arrows]);
        CanvasState.Changed += Place;
        if (CanvasState.Alone("canvas-compare")) CanvasScenes.Compare();
        Place();
    }

    static Control Arrows(bool vertical) {
        var arrows = Icons.Create(vertical ? "arrow.left.and.right" : "arrow.up.and.down", 10, Ink.Black(0.7));
        arrows.IsHitTestVisible = false;
        return arrows;
    }

    static Border Label() => new() {
        Background = Ink.Black(0.6), Padding = new Thickness(7, 3), IsHitTestVisible = false,
        Child = new TextBlock { FontSize = TypeSize.Xs, FontWeight = FontWeight.SemiBold, LetterSpacing = 0.9, Foreground = Ink.White() },
    };

    Rect Frame => CanvasState.DrawnFrame(Bounds.Size);
    double Split => CompareState.Split;
    bool Showing => CompareState.Comparing && Frame.Width > 0 && Frame.Height > 0;

    Point Divider => CompareState.Vertical
        ? new Point(Frame.X + Frame.Width * Split, Frame.Center.Y)
        : new Point(Frame.Center.X, Frame.Y + Frame.Height * Split);

    Rect Band {
        get {
            var d = Divider;
            return CompareState.Vertical ? new Rect(d.X - GrabWidth / 2, Frame.Y, GrabWidth, Frame.Height)
                                         : new Rect(Frame.X, d.Y - GrabWidth / 2, Frame.Width, GrabWidth);
        }
    }

    protected override void OnSizeChanged(SizeChangedEventArgs e) { base.OnSizeChanged(e); Place(); }

    void Place() {
        foreach (var child in Children) child.IsVisible = Showing;
        _line.InvalidateVisual();
        if (!Showing) return;
        var f = Frame;
        bool v = CompareState.Vertical;
        // Centered in each half, so a label never sits under the divider.
        Put(_before, "Original", v ? new Point(f.X + f.Width * Split * 0.5, f.Y + 22) : new Point(f.Center.X, f.Y + f.Height * Split * 0.5), Split);
        Put(_after, "Edited", v ? new Point(f.X + f.Width * (Split + (1 - Split) * 0.5), f.Y + 22)
                                 : new Point(f.Center.X, f.Y + f.Height * (Split + (1 - Split) * 0.5)), 1 - Split);
        if (_gripVertical != v) {
            _gripVertical = v;
            Children.Remove(_arrows);
            Children.Add(_arrows = Arrows(v));
        }
        Drawn.At(_grip, Divider.X - 12, Divider.Y - 12);
        Drawn.At(_arrows, Divider.X - 5, Divider.Y - 5);
    }

    // stub(#315): Proposal.swift owns this until the logic moves below the facade ("Current edit"/"Proposed")
    void Put(Border label, string text, Point at, double share) {
        ((TextBlock)label.Child!).Text = text.ToUpperInvariant();
        label.Measure(Size.Infinity);
        var size = label.DesiredSize;
        label.CornerRadius = new CornerRadius(size.Height / 2);
        Drawn.At(label, at.X - size.Width / 2, at.Y - size.Height / 2);
        // Hidden once its half is too narrow to hold it.
        label.Opacity = (CompareState.Vertical ? Frame.Width : Frame.Height) * share > 110 ? 1 : 0;
    }

    void Draw(DrawingContext c) {
        if (!Showing) return;
        var d = Divider;
        var f = Frame;
        if (CompareState.Vertical) c.FillRectangle(Ink.White(), new Rect(d.X - 0.75, f.Y, 1.5, f.Height));
        else c.FillRectangle(Ink.White(), new Rect(f.X, d.Y - 0.75, f.Width, 1.5));
    }

    public bool HitTest(Point point) => Showing && Band.Contains(point);

    protected override void OnPointerPressed(PointerPressedEventArgs e) {
        _press = e.GetPosition(this);
        _startSplit = Split;
        e.Pointer.Capture(this);
        e.Handled = true;
    }

    protected override void OnPointerMoved(PointerEventArgs e) {
        if (_startSplit is not { } from) return;
        var p = e.GetPosition(this);
        double delta = CompareState.Vertical ? (p.X - _press.X) / Math.Max(Frame.Width, 1) : (p.Y - _press.Y) / Math.Max(Frame.Height, 1);
        CompareState.SetSplit(Math.Clamp(from + delta, 0.02, 0.98));
    }

    protected override void OnPointerReleased(PointerReleasedEventArgs e) {
        _startSplit = null;
        e.Pointer.Capture(null);
    }
}
