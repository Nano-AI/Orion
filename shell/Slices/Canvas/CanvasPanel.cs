using Avalonia;
using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.Interactivity;
using Avalonia.Layout;
using Avalonia.Media;
using Avalonia.Media.Imaging;
using Avalonia.Platform;

namespace OrionShell.CanvasSlice;

/// A port of the Swift app's ImageCanvas.swift and the picture slot of OrionApp+Canvas.swift:
/// the photograph at the viewport's zoom and pan, the compare split, and what a
/// press on the picture does — pan, the targeted tool's drag, the loupe's hover —
/// and what the wheel and the trackpad do: zoom about the pointer, pan.
/// Order 0, so the overlays (crop, mask, spots, compare) stack over it.
[Panel("canvas", Region.Canvas, 0)]
public sealed class CanvasPanel : Panel {
    readonly StackPanel _welcome = Welcome();
    readonly TextBlock _note = new() { FontSize = Faces.Label, Foreground = Palette.Dim, TextAlignment = TextAlignment.Center,
                                       TextWrapping = TextWrapping.Wrap, MaxWidth = 360 };
    readonly StackPanel _choices = new() { Orientation = Orientation.Horizontal, Spacing = 6, HorizontalAlignment = HorizontalAlignment.Center };
    readonly StackPanel _state;
    readonly Drawn _picture;
    readonly SharedPicture _shared = new();   // under _picture: the canvas draws over it
    readonly Halvings _shown = new(), _original = new();
    int _version;   // the shown bitmap is reused render after render: same object, new pixels
    Point? _anchor;
    int? _band;
    readonly List<(ulong T, double V)> _x = [], _y = [];
    Interactive? _host;

    public CanvasPanel() {
        Margin = new Thickness(CanvasState.Inset);
        Background = Brushes.Transparent;   // the whole canvas takes a press, as the MTKView does
        _picture = new Drawn(Draw);
        // Try again reopens this photo; Skip moves on. Neither takes the arrow keys, which keep browsing.
        _choices.Children.AddRange([
            new StateButton(null, "Try again", () => { var e = EngineModel.Shared; if (e.Current >= 0) e.Open(e.Current); }) { Focusable = false },
            new StateButton(null, "Skip", () => Commands.Run(EngineModel.Shared.Current < EngineModel.Shared.Photos.Count - 1
                                                             ? "next-photo" : "previous-photo")) { Focusable = false },
        ]);
        _state = new StackPanel { Spacing = 10, HorizontalAlignment = HorizontalAlignment.Center, VerticalAlignment = VerticalAlignment.Center,
                                  Children = { _note, _choices } };
        Children.AddRange([_shared, _picture, _welcome, _state]);
        _shared.Arrived += _picture.InvalidateVisual;
        CanvasState.Changed += Refresh;
        CompareState.Moved += _picture.InvalidateVisual;   // a divider drag repaints the picture and nothing else
        EngineModel.Shared.Shown.Changed += () => { _version++; Refresh(); };   // the picture: only through this seam
        EngineModel.Shared.PropertyChanged += (_, e) => {
            if (e.PropertyName is nameof(EngineModel.Current) or nameof(EngineModel.Timing)) Refresh();
        };
        if (CanvasState.Alone("canvas")) CanvasScenes.Canvas();
        Refresh();
    }

    void Refresh() {
        var scene = CanvasState.Now;
        _welcome.IsVisible = scene == CanvasState.Scene.Welcome;
        _state.IsVisible = scene is CanvasState.Scene.Loading or CanvasState.Scene.Failed;
        _choices.IsVisible = scene == CanvasState.Scene.Failed;
        var engine = EngineModel.Shared;
        if (_state.IsVisible) {
            _note.Text = scene == CanvasState.Scene.Loading ? $"Decoding {engine.CurrentName}" : Failure(engine);
            _note.Foreground = scene == CanvasState.Scene.Loading ? Palette.Dim : Palette.Text;
        }
        _picture.InvalidateVisual();   // same bitmap, new pixels
    }

    /// What failed, as far as the shell can tell without the engine's word for it.
    static string Failure(EngineModel engine) {
        string path = engine.Photos[engine.Current], name = engine.CurrentName;
        if (engine.Timing.StartsWith("render failed: ")) return $"Couldn't render {name}: {engine.Timing["render failed: ".Length..]}";
        if (!File.Exists(path)) return $"Couldn't find {name}: it was moved or deleted.";
        if (Chrome.CaptureInfo.ReadFile(path) == null) return $"Couldn't read {name}: it isn't a raw file the decoder understands.";
        return $"Couldn't open {name}: the engine could not decode it.";
    }

    protected override void OnAttachedToVisualTree(VisualTreeAttachmentEventArgs e) {
        base.OnAttachedToVisualTree(e);
        var top = TopLevel.GetTopLevel(this);
        CanvasState.FollowShell(top);
        Motion.Attach(top);
        // The wheel and the pinch work wherever the pointer is on the canvas, the
        // compare divider and the mask handles included: those are this panel's
        // siblings, so their events bubble to the region and never through here.
        _host = Parent as Interactive ?? this;
        _host.AddHandler(PointerWheelChangedEvent, Wheel);
        _host.AddHandler(PointerTouchPadGestureMagnifyEvent, Magnify);
    }

    protected override void OnDetachedFromVisualTree(VisualTreeAttachmentEventArgs e) {
        base.OnDetachedFromVisualTree(e);
        _host?.RemoveHandler(PointerWheelChangedEvent, Wheel);
        _host?.RemoveHandler(PointerTouchPadGestureMagnifyEvent, Magnify);
        _host = null;
        Motion.Detach(e.RootVisual as TopLevel);
    }

    void Draw(DrawingContext c) {
        var engine = EngineModel.Shared;
        double a = CanvasState.ImageAspect;
        var size = Bounds.Size;
        if (CanvasState.Now != CanvasState.Scene.Picture || !engine.Shown.Any || a <= 0 || size.Width <= 0 || size.Height <= 0) {
            _shared.Place(null, default, default, 0);
            return;
        }

        var vp = CanvasState.Viewport;
        vp.Measure(a, size.Width / size.Height);
        var whole = CanvasState.Whole(size);
        double scaling = TopLevel.GetTopLevel(this)?.RenderScaling ?? 1;
        vp.FitScale = CanvasLayout.FrameRect(a, size).Width * scaling / Math.Max(CanvasNative.ImageSize.Width, 1);
        // Past 4× a pixel is a square, not a blur; below it, filtered.
        var mode = vp.FitScale * vp.Zoom > 4 ? BitmapInterpolationMode.None : BitmapInterpolationMode.HighQuality;
        // Past fit the engine renders only what is on screen (#22): say what that is,
        // the part of the picture in view and the device pixels it covers.
        var seen = CanvasState.DrawnFrame(size);
        if (seen.Width > 0 && seen.Height > 0 && !CropEdit.Previewing) {
            var view = (new Rect((seen.X - whole.X) / whole.Width, (seen.Y - whole.Y) / whole.Height,
                                 seen.Width / whole.Width, seen.Height / whole.Height),
                        new PixelSize((int)Math.Round(seen.Width * scaling), (int)Math.Round(seen.Height * scaling)));
            if (engine.View != view) Avalonia.Threading.Dispatcher.UIThread.Post(() => engine.View = view);
        }
        // A region sits where its part of the picture is, and moves with it until the next.
        var uv = engine.Shown.Graph == Graph.Region ? engine.Shown.Uv : new Rect(0, 0, 1, 1);
        var dest = new Rect(whole.X + uv.X * whole.Width, whole.Y + uv.Y * whole.Height,
                            uv.Width * whole.Width, uv.Height * whole.Height);
        double pixels = dest.Width * scaling;   // the shown picture's width on screen, in device pixels
        // The render straight from the GPU when the shared surface holds it (#317), else the CPU copy.
        // ponytail: the crop tool's turned clip takes the CPU copy (CompositionVisual.Clip is internal); a mask over the visual lifts that.
        bool shared = _shared.Place(CropEdit.Previewing ? null : engine.Shown, dest, mode, pixels);

        // The whole picture into its place, clipped to the view (a rubber band shows
        // the surround past its edge), or to the turned frame while the crop tool previews.
        using var clip = c.PushGeometryClip(CropEdit.Previewing ? TurnedFrame(whole) : new RectangleGeometry(new Rect(size)));
        using (c.PushRenderOptions(new RenderOptions { BitmapInterpolationMode = mode })) {
            // Around a region, the preview: what a pan uncovers until the next region lands.
            if (dest != whole && engine.Session?.Picture is { } under) {
                var outside = new CombinedGeometry(GeometryCombineMode.Exclude, new RectangleGeometry(whole), new RectangleGeometry(dest));
                using (c.PushGeometryClip(outside)) c.DrawImage(under, new Rect(under.Size), whole);
            }
            if (!shared && engine.Shown.Bitmap is { } picture) {
                var shown = _shown.For(picture, _version, pixels);
                c.DrawImage(shown, new Rect(shown.Size), dest);
            }
            if (CompareState.Showing && CompareState.Original is { } original) {
                // The split lives in view space: it holds still while the picture pans under it.
                var held = _original.For(original, 0, pixels);
                using (c.PushGeometryClip(CompareState.OriginalSide(CanvasState.DrawnFrame(size))))
                    c.DrawImage(held, new Rect(held.Size), whole);
            }
        }
    }

    /// While the crop tool previews, the turned frame on the canvas: outside it the
    /// engine's canvas is empty, and the surround shows there as it does in Swift.
    static Geometry TurnedFrame(Rect dest) {
        var (origin, size) = CropEdit.Canvas;
        var corners = CanvasLayout.TurnedFrame(CanvasNative.FrameAspect, EngineModel.Shared.Get("straighten_deg"))
            .Select(p => new Point(dest.X + (p.X - origin.X) / size * dest.Width, dest.Y + (p.Y - origin.Y) / size * dest.Height));
        return new PolylineGeometry(corners.ToList(), true);
    }

    /// `imageUV`: the displayed-picture point under `p`, or null off the picture.
    Point? Uv(Point p) {
        var u = CanvasState.Map(Bounds.Size).ToUnit(p);
        return u.X is >= 0 and <= 1 && u.Y is >= 0 and <= 1 ? u : null;
    }

    /// A view point as a fraction of the view: what zoom keeps still.
    Point Anchor(Point p) => new(p.X / Math.Max(Bounds.Width, 1), p.Y / Math.Max(Bounds.Height, 1));

    /// A hand on the viewport, which first learns this view's shape.
    Viewport Hold() {
        Motion.Stop(this);   // a wheel's pending rest: the new hand outlives it
        var vp = CanvasState.Viewport;
        vp.Measure(CanvasState.ImageAspect, Bounds.Width / Math.Max(Bounds.Height, 1));
        vp.Begin();
        return vp;
    }

    /// After a wheel or pinch event: settle now if AppKit says the fingers are off
    /// (no latency on the way back), else 80 ms of frames after the last event,
    /// which is how long a pinch's or a scroll's events can pause and still be one gesture.
    void Rest() {
        if (CanvasNative.GestureEnded() == true) { CanvasState.Viewport.Settle(); return; }
        double waited = 0;
        Motion.Run(this, dt => {
            if ((waited += dt) < 0.08) return true;
            CanvasState.Viewport.Settle();
            return false;
        });
    }

    /// ImageCanvas.swift `handleScroll`: ⌘ (Ctrl) or a mouse wheel zooms about
    /// the pointer; a trackpad's two fingers pan, the content under them 1:1.
    void Wheel(object? sender, PointerWheelEventArgs e) {
        var d = e.Delta;
        // Avalonia's AppKit view hands over NSEvent.scrollingDelta ÷ 50 from a trackpad
        // and ÷ 5 from a wheel (libAvaloniaNative, AvnView mouseEvent); undone, Swift's
        // 0.01 a point applies as it is. Elsewhere a notch is 1.0, read as 10 points,
        // and a fraction or a sideways delta means a precision touchpad.
        bool precise = CanvasNative.PreciseScroll() ?? (d.X != 0 || d.Y != Math.Round(d.Y));
        double points = precise ? 50 : OperatingSystem.IsMacOS() ? 5 : 10;
        var vp = Hold();
        if (!precise || e.KeyModifiers.HasFlag(KeyModifiers.Meta) || e.KeyModifiers.HasFlag(KeyModifiers.Control))
            vp.ZoomBy(Math.Exp(0.01 * d.Y * points), Anchor(e.GetPosition(this)));
        else
            vp.Pan(new Vector(d.X * points / Math.Max(Bounds.Width, 1), d.Y * points / Math.Max(Bounds.Height, 1)));
        Rest();
        e.Handled = true;
    }

    /// ImageCanvas.swift `handleMagnify`: the trackpad pinch (NSEvent magnify,
    /// raised by Avalonia as PointerTouchPadGestureMagnify), 1:1 about the pointer.
    void Magnify(object? sender, PointerDeltaEventArgs e) {
        Hold().ZoomBy(1 + Math.Max(e.Delta.X, -0.9), Anchor(e.GetPosition(this)));
        Rest();
        e.Handled = true;
    }

    protected override void OnPointerPressed(PointerPressedEventArgs e) {
        var engine = EngineModel.Shared;
        var p = e.GetPosition(this);
        if (e.GetCurrentPoint(this).Properties.IsRightButtonPressed) { CanvasState.Viewport.Reset(); return; }

        // The mask colour picker consumes the click and disarms: a pick, not a mode.
        if (engine.Tool == CanvasTool.MaskColor) {
            // Engine+Mask.swift's pickMaskColor: the scene colour under the click, not the edited one.
            if (Uv(p) is { } at && CanvasNative.Sample(at) is { } picked) Masks.MaskGroup.Shared.PickColor(picked.Scene, picked.Display);
            engine.Tool = CanvasTool.None;
            return;
        }
        if (engine.Tool == CanvasTool.Targeted && Uv(p) is { } uv && CanvasNative.Sample(uv) is { } s
            && ColorLoupe.Hue(s.Scene) is { } hue) {
            _band = ColorLoupe.Band(hue);
            Develop.TargetedAdjust.Begin((Develop.HueBand)_band);   // the mixer follows the pick
            engine.BeginInteraction();
        } else if (e.ClickCount == 2) {
            // A double-click: 1:1 about the point clicked, or back to fit, by spring.
            var vp = CanvasState.Viewport;
            vp.AnimateZoom(vp.PastFit ? 1 : 1 / Math.Max(vp.FitScale, 0.0001), Anchor(p));
            return;
        } else {
            Hold();
            _x.Clear();
            _y.Clear();
        }
        _anchor = p;
        e.Pointer.Capture(this);
    }

    protected override void OnPointerMoved(PointerEventArgs e) {
        var p = e.GetPosition(this);
        ColorLoupe.Track(p, Bounds.Size);
        if (_anchor is not { } anchor) return;
        double dx = (p.X - anchor.X) / Math.Max(Bounds.Width, 1), dy = (p.Y - anchor.Y) / Math.Max(Bounds.Height, 1);
        _anchor = p;

        // Armed, a vertical drag adjusts the band under the cursor; up increases.
        if (_band is { } band) {
            // The mode the Color panel chose: hue, saturation (the default) or luminance.
            var engine = EngineModel.Shared;
            string field = Develop.TargetedAdjust.Field(Develop.TargetedAdjust.Current);
            engine.Set(field, Math.Clamp(engine.Get(field, band) - (float)dy * 2, -1, 1), band);
            return;
        }
        // The picture follows the hand 1:1, and past an edge with growing resistance.
        var vp = CanvasState.Viewport;
        vp.Pan(new Vector(dx, dy));
        _x.Add((e.Timestamp, vp.Raw.X));
        _y.Add((e.Timestamp, vp.Raw.Y));
    }

    protected override void OnPointerReleased(PointerReleasedEventArgs e) => Let(e.Timestamp, e.Pointer);

    protected override void OnPointerCaptureLost(PointerCaptureLostEventArgs e) => Let(0, null);

    /// The hand let go: a flick carries on at its speed (Apple's projection), a
    /// placed drag stays, and past an edge it springs back.
    void Let(ulong now, IPointer? pointer) {
        if (_anchor == null) return;
        _anchor = null;
        pointer?.Capture(null);
        if (_band != null) {
            _band = null;
            Develop.TargetedAdjust.End();
            EngineModel.Shared.EndInteraction();
            return;
        }
        CanvasState.Viewport.Settle(now == 0 ? default : new Vector(Motion.Velocity(_x, now), Motion.Velocity(_y, now)));
    }

    protected override void OnPointerExited(PointerEventArgs e) => CanvasState.ClearHover();

    /// Nothing open yet: the name, one line, and the way in.
    static StackPanel Welcome() => new() {
        HorizontalAlignment = HorizontalAlignment.Center, VerticalAlignment = VerticalAlignment.Center, Spacing = 6,
        Children = {
            new TextBlock { Text = "Orion", FontSize = 28, FontWeight = FontWeight.SemiBold, HorizontalAlignment = HorizontalAlignment.Center },
            new TextBlock { Text = "A darkroom for raw files.", Foreground = Palette.Dim, HorizontalAlignment = HorizontalAlignment.Center },
            new StateButton("folder", "Open a raw file", () => Commands.Run("open-photo")) {
                Height = 28, Margin = new Thickness(0, 18, 0, 0), HorizontalAlignment = HorizontalAlignment.Center,
            },
            new TextBlock { Text = "ARW, DNG, NEF, CR2, CR3, RAF, ORF, RW2. Sony is the tested one.", FontSize = Faces.Mark,
                            Foreground = Palette.Faint, Margin = new Thickness(0, 8, 0, 0), HorizontalAlignment = HorizontalAlignment.Center },
        },
    };
}

/// CanvasReduce.swift's chain, on the CPU: a picture halved by 2 × 2 boxes as
/// many times as the screen allows, once per picture, never per frame. Measured
/// in the window with compare on over a 24 MP render: drawn from the render
/// itself a divider frame took 33 ms, Skia sampling two 24 MP bitmaps down every
/// frame; drawn from the halvings, 16.7 ms, the display's own rate.
/// ponytail: halved on the UI thread, ~40 ms once per full render; a worker if that shows.
sealed class Halvings {
    Bitmap? _source;
    int _version;
    readonly List<WriteableBitmap> _levels = [];

    /// `source`, or its smallest halving still at least `pixels` wide.
    public Bitmap For(Bitmap source, int version, double pixels) {
        if (source != _source || version != _version) {
            foreach (var level in _levels) level.Dispose();   // 6 MB and less each, freed now rather than by a finalizer
            _levels.Clear();
            (_source, _version) = (source, version);
        }
        Bitmap b = source;
        for (int i = 0; b.PixelSize.Width / 2 >= pixels && b is WriteableBitmap w; i++) {
            if (i == _levels.Count) _levels.Add(Halve(w));
            b = _levels[i];
        }
        return b;
    }

    static unsafe WriteableBitmap Halve(WriteableBitmap source) {
        using var s = source.Lock();
        int w = s.Size.Width / 2, h = s.Size.Height / 2, ss = s.RowBytes;
        var half = new WriteableBitmap(new PixelSize(w, h), s.Dpi, s.Format, AlphaFormat.Opaque);
        using var d = half.Lock();
        nint from = s.Address, to = d.Address;
        int ds = d.RowBytes;
        Parallel.For(0, h, y => {
            byte* a = (byte*)from + 2L * y * ss, b = a + ss, o = (byte*)to + (long)y * ds;
            for (int x = 0; x < w; x++, a += 8, b += 8, o += 4)
                for (int k = 0; k < 4; k++) o[k] = (byte)((a[k] + a[k + 4] + b[k] + b[k + 4] + 2) >> 2);
        });
        return half;
    }
}
