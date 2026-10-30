using Avalonia;
using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.Media;
using Avalonia.Media.Imaging;
using Avalonia.Rendering;
using Avalonia.Threading;

namespace OrionShell.CanvasSlice;

/// Engine+Compare.swift's split, free of its axis: the original shows on one
/// side of a line through `Center` (a point in the drawn frame, 0…1) at `Angle`
/// degrees on screen; 0 is Swift's vertical split with the original on the
/// left, 90 its horizontal one with the original on top.
///
/// The original is the as-shot look at the current geometry, never a render
/// per frame of the divider. Two pictures, neither made on the UI thread's
/// clock: the photo's first render after its first open, which is the as-shot
/// look by construction, kept small (preview size) per photo; and a sharp one
/// from a second engine on a worker, made once per photo and geometry, kept
/// across toggles. The engine opened for it closes as soon as it has rendered.
static class CompareState {
    /// The divider stops at multiples of 45°: it holds within Catch of one while
    /// dragged, and a release projected within Reach of one settles on it.
    const double Detent = 45, Catch = 4, Reach = 8;
    static readonly string[] Geometry = ["rotate_quarters", "straighten_deg", "perspective_vertical",
        "perspective_horizontal", "perspective_aspect", "crop_x", "crop_y", "crop_w", "crop_h",
        "crop_preview", "preview_x", "preview_y", "preview_size"];
    static readonly Spring _angle = new(0);
    static double _to;
    static readonly object Turning = new();

    public static bool Comparing { get; private set; }
    /// The split is on screen: comparing, with an original to show. Until the
    /// original exists only the toolbar says compare is on; the canvas never
    /// names the edit "Original".
    public static bool Showing => Comparing && Original != null;
    public static Point Center { get; private set; } = new(0.5, 0.5);
    public static double Angle => _angle.Value;
    /// Where the divider crosses its own axis, Swift's split; 1 when off.
    public static double Split => !Comparing ? 1 : Vertical ? Center.X : Center.Y;
    /// Nearer vertical than horizontal (where it is going, while it turns).
    /// Setting it puts the divider at Swift's 0° or 90°: swap-split, a command
    /// with a key, so it jumps (a key never animates).
    public static bool Vertical {
        get => Math.Abs(Math.Cos(_to * Math.PI / 180)) >= Math.Abs(Math.Sin(_to * Math.PI / 180));
        set { Hold(); TurnTo(value ? 0 : 90, free: true); }
    }
    public static WriteableBitmap? Original { get; private set; }

    /// comparisonReference: while an agent's proposal is up, the original side is
    /// the committed edit at the displayed framing, not the photo as shot. Null
    /// is ordinary Compare.
    public static System.Text.Json.Nodes.JsonObject? Reference {
        get => _reference;
        set {
            _reference = value;
            _referenceKey = value?.ToJsonString() ?? "";
            if (Comparing) Make();
        }
    }
    static System.Text.Json.Nodes.JsonObject? _reference;
    static string _referenceKey = "";
    /// The divider moved or turned, or the original changed: the canvas and the
    /// overlay repaint, and nothing else does (CanvasState.Changed reaches every overlay).
    public static event Action? Moved;

    /// Per photo, the as-shot picture its first render made, and the geometry it was made at.
    static readonly Dictionary<string, (string Key, WriteableBitmap Picture)> Kept = new();
    static readonly HashSet<string> Seen = new();
    static string? _capture;
    /// The sharp original for one photo and geometry; Picture null until a worker makes it.
    static (string Path, string Key, WriteableBitmap? Picture)? _sharp;
    /// One second engine at a time: it holds ~1.5 GB while it renders.
    static bool _working, _pending;

    static CompareState() {
        var e = EngineModel.Shared;
        // The panels that touch this are built before anything can be edited, so a
        // photo already open is still as shot: its picture is the as-shot look.
        if (e.Handle != 0 && Seen.Add(e.Photos[e.Current]) && e.Shown.Bitmap is WriteableBitmap shown)
            Keep(e.Photos[e.Current], GeometryKey(), Duplicate(shown));
        e.Opened += () => {
            Clear();
            if (Seen.Add(e.Photos[e.Current])) _capture = e.Photos[e.Current];
        };
        e.Rendered += () => {
            Capture();
            // `refreshOriginal`, from every render: any route into a geometry change
            // re-takes the original, once, after the writes that caused it settle.
            if (!Comparing || _pending) return;
            _pending = true;
            Dispatcher.UIThread.Post(() => { _pending = false; if (Comparing && _sharp?.Key != GeometryKey()) Make(); });
        };
    }

    /// The toolbar's Compare button (OrionApp+Chrome.swift) and `\`: the divider
    /// comes back where it was left, at the angle it was left at.
    public static void Toggle() { if (Comparing) Clear(); else On(); }

    public static void SetSplit(double split) {
        if (split >= 0.999) { Clear(); return; }
        Center = Vertical ? new Point(Math.Clamp(split, 0, 1), Center.Y) : new Point(Center.X, Math.Clamp(split, 0, 1));
        On();
    }

    static void On() {
        Comparing = true;
        Make();
        CanvasState.Touch();
    }

    public static void Clear() {
        Comparing = false;
        Original = null;
        Motion.Stop(Turning);
        CanvasState.Touch();
        Moved?.Invoke();
    }

    /// The divider's point, in drawn-frame units, kept clear of the frame's edges as Swift's 0.02.
    public static void MoveTo(Point center) {
        Center = new Point(Math.Clamp(center.X, 0.02, 0.98), Math.Clamp(center.Y, 0.02, 0.98));
        Moved?.Invoke();
    }

    /// A hand on the divider: a turn in flight stops where it is.
    public static void Hold() => Motion.Stop(Turning);
    public static bool Spinning => Motion.IsRunning(Turning);

    /// The angle under the hand, held at a detent within Catch of it unless `free`.
    public static void TurnTo(double degrees, bool free) {
        double d = Detent * Math.Round(degrees / Detent);
        _angle.Value = _to = !free && Math.Abs(degrees - d) < Catch ? d : degrees;
        _angle.Velocity = 0;
        Moved?.Invoke();
    }

    /// The hand let go turning at `velocity` degrees a second: it carries on to
    /// where that projects (Apple's 0.998), onto a detent within Reach of there.
    /// `free` (⌘ or Ctrl held): it stays exactly where it was let go.
    public static void Release(double velocity, bool free) {
        if (free || Motion.Reduced) { Spin(Angle, 0); return; }
        double to = Angle + Viewport.Projected(velocity), d = Detent * Math.Round(to / Detent);
        Spin(Math.Abs(to - d) < Reach ? d : to, velocity);
    }

    static void Spin(double to, double velocity) {
        (_to, _angle.Velocity) = (to, velocity);
        Motion.Run(Turning, dt => {
            bool moving = _angle.Step(_to, dt, 0.4, 0.01);   // Apple's rotation response
            Moved?.Invoke();
            return moving;
        });
    }

    /// Where the divider is in the view, given the drawn frame: its point, the
    /// normal pointing into the edited side, and the line's own direction.
    public static (Point P, Vector N, Vector T) Line(Rect frame) {
        double r = Angle * Math.PI / 180;
        return (new Point(frame.X + frame.Width * Center.X, frame.Y + frame.Height * Center.Y),
                new Vector(Math.Cos(r), Math.Sin(r)), new Vector(-Math.Sin(r), Math.Cos(r)));
    }

    /// The original's side of the frame: a quad reaching past every edge, which the
    /// canvas's own clip trims to the picture.
    public static Geometry OriginalSide(Rect frame) {
        var (p, n, t) = Line(frame);
        double l = 2 * (frame.Width + frame.Height) + 1;
        return new PolylineGeometry([p + t * l, p + t * l - n * l, p - t * l - n * l, p - t * l], true);
    }

    static string GeometryKey() => string.Join(",", Geometry.Select(f => EngineModel.Shared.Get(f))) + _referenceKey;

    /// Shows the best original there is for this photo and geometry, and starts
    /// the sharp one if nobody has.
    static void Make() {
        var e = EngineModel.Shared;
        if (!Comparing || e.Handle == 0 || e.Current < 0) return;
        string path = e.Photos[e.Current], key = GeometryKey();
        WriteableBitmap? dropped = null;
        if (_sharp is not { } s || s.Path != path || s.Key != key) (dropped, _sharp) = (_sharp?.Picture, (path, key, null));
        bool kept = Kept.TryGetValue(path, out var k) && k.Key == key;
        if (_sharp.Value.Picture == null && !_working)
            Sharpen(path, key, Geometry.Select(f => (f, e.Get(f))).ToArray(), small: !kept, (System.Text.Json.Nodes.JsonObject?)_reference?.DeepClone());
        Original = _sharp.Value.Picture ?? (kept ? k.Picture : null);
        dropped?.Dispose();   // ~97 MB of pixels, freed now: the memory a finalizer would hold is the laptop's
        Moved?.Invoke();
    }

    /// A second engine on a worker: the photo as shot, at this geometry, full
    /// size. `small`: a preview render first, posted the moment it exists, so a
    /// photo with nothing kept still shows its split in a decode's time.
    /// A foreground thread, so a process quitting mid-render waits for it to close its engine.
    static void Sharpen(string path, string key, (string Field, float Value)[] geometry, bool small,
                        System.Text.Json.Nodes.JsonObject? reference = null) {
        _working = true;
        new Thread(() => {
            WriteableBitmap? sharp = null;
            try {
                using var s = Session.Open(path);
                if (s != null) {
                    if (reference != null) { s.ToFrameSpace(reference); s.Apply(reference); }   // the committed edit, then this framing
                    foreach (var (f, v) in geometry) s.Set(f, v);
                    if (small && s.Copy(s.Render(preview: true)) is { } preview) Dispatcher.UIThread.Post(() => { Keep(path, key, preview); Arrived(path, key); });
                    sharp = s.Copy(s.Render(preview: false));
                }
            } catch (Exception e) {
                Console.Error.WriteLine($"compare: the original of {Path.GetFileName(path)} failed: {e.Message}");
            }
            Dispatcher.UIThread.Post(() => {
                _working = false;
                bool current = _sharp is { } s && s.Path == path && s.Key == key;
                if (current && sharp == null) return;   // failed: the next toggle tries again
                if (current) _sharp = (path, key, sharp); else sharp?.Dispose();   // the geometry moved on while it rendered
                if (Comparing) Make();   // shows it, or starts the one wanted now
            });
        }) { IsBackground = false, Name = "compare original" }.Start();
    }

    static void Arrived(string path, string key) {
        var e = EngineModel.Shared;
        if (Comparing && e.Current >= 0 && e.Photos[e.Current] == path && _sharp?.Key == key) Make();
    }

    /// A photo's first render after its first open is the as-shot look: keep a copy.
    static void Capture() {
        var e = EngineModel.Shared;
        if (_capture == null || e.Current < 0 || e.Photos[e.Current] != _capture) return;
        if (e.Shown.Bitmap is WriteableBitmap b) Keep(_capture, GeometryKey(), Duplicate(b));
        _capture = null;
    }

    /// ponytail: the 16 most recent photos (preview size, ~6 MB each); an LRU when a session sees more.
    static void Keep(string path, string key, WriteableBitmap picture) {
        Kept.Remove(path);
        Kept[path] = (key, picture);
        if (Kept.Count <= 16) return;
        var oldest = Kept.Keys.First();
        if (Kept[oldest].Picture != Original) Kept[oldest].Picture.Dispose();
        Kept.Remove(oldest);
    }

    static unsafe WriteableBitmap Duplicate(WriteableBitmap b) {
        using var s = b.Lock();
        var copy = new WriteableBitmap(s.Size, s.Dpi, s.Format, Avalonia.Platform.AlphaFormat.Opaque);
        using var d = copy.Lock();
        long bytes = (long)s.RowBytes * s.Size.Height;
        Buffer.MemoryCopy((void*)s.Address, (void*)d.Address, (long)d.RowBytes * d.Size.Height, bytes);
        return copy;
    }
}

/// A port of the Swift app's CompareOverlay.swift: the divider with both sides named, now
/// movable and turnable. The split itself is drawn by the canvas, as
/// ImageCanvas's shader does. The line, the grip and the turn handle are drawn,
/// not laid out, so a drag repaints this layer and the canvas and nothing else.
///
/// Drag the grip or the line to move it (it keeps the point you took it by);
/// drag the small round handle on the line to turn it about the grip. It holds at
/// 0°, 45° and 90° (every 45°); hold ⌘ (Ctrl) for a free angle.
[Panel("canvas-compare", Region.Canvas, 40)]
public sealed class CompareOverlay : Panel, ICustomHitTest {
    const double Grip = 12, Knob = 8, Reach = 46, GrabWidth = 28;
    static readonly Geometry Arrows = Icons.Create("arrow.left.and.right", 10, Brushes.Black).Data!;
    static readonly Geometry Turn = Icons.Create("arrow.clockwise", 10, Brushes.Black).Data!;
    static readonly IPen Rule = new Pen(Ink.White(), 1.5);
    static readonly BoxShadows Shadow = new(new BoxShadow { Blur = 4, Color = Color.FromArgb(102, 0, 0, 0) });
    static readonly Cursor MoveCursor = new(StandardCursorType.SizeAll), TurnCursor = new(StandardCursorType.Hand);

    enum Grab { None, Move, Turn }
    readonly Border _before = Label(), _after = Label();
    readonly Drawn _layer;
    Grab _grab;
    Point _press, _from;
    double _fromAngle, _lastPointer, _turned;
    readonly List<(ulong T, double V)> _turns = [];

    public CompareOverlay() {
        Margin = new Thickness(CanvasState.Inset);
        _layer = new Drawn(Draw);
        Children.AddRange([_layer, _before, _after]);
        CanvasState.Changed += Place;   // the picture moved under it (zoom, pan, a new photo)
        CompareState.Moved += Place;
        if (CanvasState.Alone("canvas-compare")) CanvasScenes.Compare();
        Place();
    }

    static Border Label() => new() {
        Background = Pill.Ground, Padding = new Thickness(8, 3), CornerRadius = new CornerRadius(4), IsHitTestVisible = false,
        Child = new TextBlock { FontSize = Faces.Mark, Foreground = Palette.Text },
    };

    Rect Frame => CanvasState.DrawnFrame(Bounds.Size);
    bool Showing => CompareState.Showing && Frame.Width > 0 && Frame.Height > 0;

    /// The turn handle: on the line, Reach from the grip, on the side that is
    /// inside the frame (above the grip when it can be).
    Point KnobAt(Rect f) {
        var (p, _, t) = CompareState.Line(f);
        var up = p - t * Reach;
        return f.Deflate(Knob).Contains(up) || !f.Deflate(Knob).Contains(p + t * Reach) ? up : p + t * Reach;
    }

    protected override void OnSizeChanged(SizeChangedEventArgs e) { base.OnSizeChanged(e); Place(); }

    void Place() {
        _before.IsVisible = _after.IsVisible = Showing;
        _layer.InvalidateVisual();
        if (!Showing) return;
        var f = Frame;
        var (p, n, _) = CompareState.Line(f);
        // Proposal review names its sides for what they are: the committed edit and the agent's.
        Put(_before, CompareState.Reference != null ? "Current edit" : "Original", f, p, n, -1);
        Put(_after, CompareState.Reference != null ? "Proposed" : "Edited", f, p, n, 1);
    }

    void Put(Border label, string text, Rect f, Point p, Vector n, int side) {
        ((TextBlock)label.Child!).Text = text;
        var size = Drawn.Measured(label);
        var (at, room) = LabelAt(f, p, n, side, size);
        Drawn.At(label, at.X - size.Width / 2, at.Y - size.Height / 2);
        // Hidden once its side is too narrow to hold it.
        label.Opacity = room > 110 ? 1 : 0;
    }

    /// Where a side's name goes, and the room that side has for it. Swift's two
    /// rules (a vertical split names its halves along the top, centred in each; a
    /// horizontal one down the middle) blended by how vertical the divider is, so
    /// a turning divider carries its labels smoothly. A blend of two points on one
    /// side is on that side; a label that would still touch the line steps off it.
    internal static (Point At, double Room) LabelAt(Rect f, Point p, Vector n, int side, Size label) {
        double y = f.Y + 22, x = f.Center.X;
        var row = Part(f.Left, f.Right, t => (t - p.X) * n.X + (y - p.Y) * n.Y, side);
        var column = Part(f.Top, f.Bottom, t => (x - p.X) * n.X + (t - p.Y) * n.Y, side);
        double w = row == null ? 0 : column == null ? 1 : n.X * n.X;
        if (row == null && column == null) return (p, 0);
        var at = new Point(w * (row?.Mid ?? 0) + (1 - w) * x, w * y + (1 - w) * (column?.Mid ?? 0));
        double room = w * (row?.Length ?? 0) + (1 - w) * (column?.Length ?? 0);
        double clear = Math.Abs(n.X) * label.Width / 2 + Math.Abs(n.Y) * label.Height / 2 + 6, off = side * ((at - p) * n);
        return (off < clear ? at + n * side * (clear - off) : at, room);
    }

    /// Where `side * s` is positive along [a, b], s linear: its midpoint and length.
    static (double Mid, double Length)? Part(double a, double b, Func<double, double> s, int side) {
        double sa = side * s(a), sb = side * s(b);
        if (sa <= 0 && sb <= 0) return null;
        if (sa > 0 && sb > 0) return ((a + b) / 2, b - a);
        double z = a + (b - a) * sa / (sa - sb);
        return sa > 0 ? ((a + z) / 2, z - a) : ((z + b) / 2, b - z);
    }

    void Draw(DrawingContext c) {
        if (!Showing) return;
        var f = Frame;
        var (p, n, t) = CompareState.Line(f);
        double l = f.Width + f.Height;
        using (c.PushClip(f)) c.DrawLine(Rule, p - t * l, p + t * l);
        // The grip: grows a point while held, so the press shows the moment it lands.
        double r = Grip + (_grab == Grab.Move ? 1 : 0);
        c.DrawRectangle(Ink.White(), null, new RoundedRect(new Rect(p.X - r, p.Y - r, 2 * r, 2 * r), r), Shadow);
        Glyph(c, Arrows, p, 10, CompareState.Angle, Ink.Black(0.7));
        var k = KnobAt(f);
        double kr = Knob + (_grab == Grab.Turn ? 1 : 0);
        c.DrawRectangle(Ink.White(), null, new RoundedRect(new Rect(k.X - kr, k.Y - kr, 2 * kr, 2 * kr), kr), Shadow);
        Glyph(c, Turn, k, 9, 0, Ink.Black(0.7));
    }

    /// A symbol centred on `at` by its own drawn bounds, `size` along its long
    /// side, turned `degrees`. The bounds, not the 24-point box Icons draws in:
    /// a Path stretched Uniform into a square sat the left-right arrows 2.8 points
    /// high in it, which is how the grip's glyph came to be off centre. The
    /// geometry is moved, not the context: under a 2× RenderTargetBitmap a
    /// PushTransform here put the arrows, and everything drawn after them, off
    /// to the lower right (probe: a dot pushed 5 pt landed 30 pt away).
    internal static void Glyph(DrawingContext c, Geometry g, Point at, double size, double degrees, IBrush ink) {
        var b = g.Bounds;
        double s = size / Math.Max(b.Width, b.Height);
        var placed = g.Clone();
        placed.Transform = new MatrixTransform(Matrix.CreateTranslation(-b.Center.X, -b.Center.Y) * Matrix.CreateScale(s, s)
            * Matrix.CreateRotation(degrees * Math.PI / 180) * Matrix.CreateTranslation(at.X, at.Y));
        c.DrawGeometry(null, new Pen(ink, 1.6, lineCap: PenLineCap.Round, lineJoin: PenLineJoin.Round), placed);
    }

    Grab HitAt(Point point) {
        if (!Showing) return Grab.None;
        var f = Frame;
        var (p, n, _) = CompareState.Line(f);
        if (PictureMap.Dist(point, KnobAt(f)) <= Knob + 4) return Grab.Turn;
        if (PictureMap.Dist(point, p) <= Grip + 4) return Grab.Move;
        return f.Contains(point) && Math.Abs((point - p) * n) <= GrabWidth / 2 ? Grab.Move : Grab.None;
    }

    public bool HitTest(Point point) => HitAt(point) != Grab.None;

    static double Degrees(Vector v) => Math.Atan2(v.Y, v.X) * 180 / Math.PI;

    protected override void OnPointerPressed(PointerPressedEventArgs e) {
        _press = e.GetPosition(this);
        _grab = HitAt(_press);
        if (_grab == Grab.None) return;
        CompareState.Hold();
        (_from, _fromAngle, _turned) = (CompareState.Center, CompareState.Angle, 0);
        _lastPointer = Degrees(_press - CompareState.Line(Frame).P);
        _turns.Clear();
        e.Pointer.Capture(this);
        e.Handled = true;
        _layer.InvalidateVisual();   // the pressed grip, on the press
    }

    protected override void OnPointerMoved(PointerEventArgs e) {
        var point = e.GetPosition(this);
        if (_grab == Grab.None) {
            Cursor = HitAt(point) == Grab.Turn ? TurnCursor : MoveCursor;
            return;
        }
        var f = Frame;
        if (_grab == Grab.Move) {
            // 1:1 from the point it was taken by.
            CompareState.MoveTo(new Point(_from.X + (point.X - _press.X) / Math.Max(f.Width, 1),
                                          _from.Y + (point.Y - _press.Y) / Math.Max(f.Height, 1)));
            return;
        }
        // Turned by how far the pointer has gone round the grip since the press,
        // unwrapped across ±180°, so the handle stays under the hand.
        double now = Degrees(point - CompareState.Line(f).P), step = now - _lastPointer;
        _turned += step - 360 * Math.Round(step / 360);
        _lastPointer = now;
        CompareState.TurnTo(_fromAngle + _turned, Free(e.KeyModifiers));
        _turns.Add((e.Timestamp, _fromAngle + _turned));
    }

    static bool Free(KeyModifiers m) => m.HasFlag(KeyModifiers.Meta) || m.HasFlag(KeyModifiers.Control);

    protected override void OnPointerReleased(PointerReleasedEventArgs e) {
        var grab = _grab;
        _grab = Grab.None;
        e.Pointer.Capture(null);
        if (grab == Grab.Turn) CompareState.Release(Motion.Velocity(_turns, e.Timestamp), Free(e.KeyModifiers));
        _layer.InvalidateVisual();
    }

    protected override void OnPointerCaptureLost(PointerCaptureLostEventArgs e) {
        if (_grab == Grab.Turn) CompareState.Release(0, false);
        _grab = Grab.None;
        _layer.InvalidateVisual();
    }
}
