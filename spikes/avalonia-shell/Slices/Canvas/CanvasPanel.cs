using Avalonia;
using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.Layout;
using Avalonia.Media;
using Avalonia.Media.Imaging;

namespace OrionShell.CanvasSlice;

/// A port of app/ImageCanvas.swift and the picture slot of OrionApp+Canvas.swift:
/// the photograph at the viewport's zoom and pan, the compare split, and what a
/// press on the picture does — pan, the targeted tool's drag, the loupe's hover.
/// Order 0, so the overlays (crop, mask, spots, compare) stack over it.
[Panel("canvas", Region.Canvas, 0)]
public sealed class CanvasPanel : Panel {
    readonly StackPanel _welcome = Welcome();
    readonly Drawn _picture;
    Point? _anchor;
    int? _band;

    public CanvasPanel() {
        Margin = new Thickness(CanvasState.Inset);
        Background = Brushes.Transparent;   // the whole canvas takes a press, as the MTKView does
        _picture = new Drawn(Draw);
        Children.AddRange([_picture, _welcome]);
        CanvasState.Changed += Refresh;
        if (CanvasState.Alone("canvas")) CanvasScenes.Canvas();
        Refresh();
    }

    void Refresh() {
        _welcome.IsVisible = EngineModel.Shared.Handle == 0;
        _picture.InvalidateVisual();   // same bitmap, new pixels
    }

    protected override void OnAttachedToVisualTree(VisualTreeAttachmentEventArgs e) {
        base.OnAttachedToVisualTree(e);
        CanvasState.FollowShell(TopLevel.GetTopLevel(this));
    }

    void Draw(DrawingContext c) {
        var engine = EngineModel.Shared;
        double a = CanvasState.ImageAspect;
        var size = Bounds.Size;
        if (engine.Handle == 0 || engine.Picture is not { } picture || a <= 0 || size.Width <= 0 || size.Height <= 0) return;

        var vp = CanvasState.Viewport;
        double va = size.Width / size.Height;
        var visible = vp.VisibleFraction(a, va);
        var dest = CanvasLayout.DrawnRect(vp.QuadScale(a, va), size);
        double scaling = TopLevel.GetTopLevel(this)?.RenderScaling ?? 1;
        vp.FitScale = CanvasLayout.FrameRect(a, size).Width * scaling / Math.Max(CanvasNative.ImageSize.Width, 1);
        // Past 4× a pixel is a square, not a blur; below it, filtered.
        var mode = vp.FitScale * vp.Zoom > 4 ? BitmapInterpolationMode.None : BitmapInterpolationMode.HighQuality;

        using var turned = c.PushGeometryClip(TurnedFrame(dest));
        using (c.PushRenderOptions(new RenderOptions { BitmapInterpolationMode = mode })) {
            c.DrawImage(picture, Source(picture, visible, vp.Center), dest);
            if (CompareState.Comparing && CompareState.Original is { } original) {
                // The split lives in view space: it holds still while the picture pans under it.
                double s = CompareState.Split;
                var before = CompareState.Vertical ? new Rect(dest.X, dest.Y, dest.Width * s, dest.Height)
                                                   : new Rect(dest.X, dest.Y, dest.Width, dest.Height * s);
                using (c.PushClip(before)) c.DrawImage(original, Source(original, visible, vp.Center), dest);
            }
        }
    }

    /// While the crop tool previews, the turned frame on the canvas: outside it the
    /// engine's canvas is empty, and the surround shows there as it does in Swift.
    static Geometry TurnedFrame(Rect dest) {
        if (!CropEdit.Previewing) return new RectangleGeometry(dest);
        var (origin, size) = CropEdit.Canvas;
        var corners = CanvasLayout.TurnedFrame(CanvasNative.FrameAspect, EngineModel.Shared.Get("straighten_deg"))
            .Select(p => new Point(dest.X + (p.X - origin.X) / size * dest.Width, dest.Y + (p.Y - origin.Y) / size * dest.Height));
        return new PolylineGeometry(corners.ToList(), true);
    }

    /// The visible window about the viewport's center, in the bitmap's own pixels.
    static Rect Source(Bitmap b, Size visible, Point center) {
        var s = b.Size;
        return new Rect((center.X - visible.Width / 2) * s.Width, (center.Y - visible.Height / 2) * s.Height,
                        visible.Width * s.Width, visible.Height * s.Height);
    }

    Size Visible {
        get {
            double a = CanvasState.ImageAspect, va = Bounds.Width / Math.Max(Bounds.Height, 1);
            return a > 0 ? CanvasState.Viewport.VisibleFraction(a, va) : new Size(1, 1);
        }
    }

    /// `imageUV`: the displayed-picture point under `p`, or null off the picture.
    Point? Uv(Point p) {
        var u = CanvasState.Map(Bounds.Size).ToUnit(p);
        return u.X is >= 0 and <= 1 && u.Y is >= 0 and <= 1 ? u : null;
    }

    protected override void OnPointerPressed(PointerPressedEventArgs e) {
        var engine = EngineModel.Shared;
        var p = e.GetPosition(this);
        if (e.GetCurrentPoint(this).Properties.IsRightButtonPressed) { CanvasState.Viewport.Reset(); return; }

        // The mask colour picker consumes the click and disarms: a pick, not a mode.
        if (engine.Tool == CanvasTool.MaskColor) {
            // stub(#315): Engine+Mask.swift owns this until the logic moves below the facade (pickMaskColor)
            engine.Tool = CanvasTool.None;
            return;
        }
        if (engine.Tool == CanvasTool.Targeted && Uv(p) is { } uv && CanvasNative.Sample(uv) is { } s
            && ColorLoupe.Hue(s.Scene) is { } hue) {
            _band = ColorLoupe.Band(hue);
            engine.BeginInteraction();
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
            // stub(#315): TargetedAdjust.swift owns this until the logic moves below the facade (the hue/sat/lum mode; saturation is its default)
            var engine = EngineModel.Shared;
            engine.Set("sat_shift", Math.Clamp(engine.Get("sat_shift", band) - (float)dy * 2, -1, 1), band);
            return;
        }
        var visible = Visible;
        CanvasState.Viewport.Pan(new Vector(dx, dy), visible);
        CanvasState.Viewport.Clamp(Visible);
    }

    protected override void OnPointerReleased(PointerReleasedEventArgs e) {
        _anchor = null;
        e.Pointer.Capture(null);
        if (_band == null) return;
        _band = null;
        EngineModel.Shared.EndInteraction();
    }

    protected override void OnPointerExited(PointerEventArgs e) => CanvasState.ClearHover();

    /// A mouse notch or ⌘-scroll zooms about the cursor; a trackpad scroll pans.
    /// ponytail: Avalonia reports no precise-delta flag, so a whole-line delta reads as a mouse.
    protected override void OnPointerWheelChanged(PointerWheelEventArgs e) {
        var d = e.Delta;
        var p = e.GetPosition(this);
        var vp = CanvasState.Viewport;
        bool zoom = e.KeyModifiers.HasFlag(KeyModifiers.Meta) || e.KeyModifiers.HasFlag(KeyModifiers.Control)
                    || (d.X == 0 && d.Y == Math.Round(d.Y));
        if (zoom) vp.ZoomBy(1 + d.Y * 0.1, new Point(p.X / Math.Max(Bounds.Width, 1), p.Y / Math.Max(Bounds.Height, 1)), Visible);
        else vp.Pan(new Vector(d.X * 10 / Math.Max(Bounds.Width, 1), d.Y * 10 / Math.Max(Bounds.Height, 1)), Visible);
        vp.Clamp(Visible);
        e.Handled = true;
    }

    /// Nothing open yet: the title, one line, and the way in.
    static StackPanel Welcome() {
        var serif = new FontFamily("New York, Georgia, Times New Roman, serif");
        var open = new Button {
            Content = "Open a raw file", FontSize = TypeSize.Base, Foreground = Palette.Accent, Background = Brushes.Transparent,
            Padding = new Thickness(16, 7), CornerRadius = new CornerRadius(Radius.Md), Margin = new Thickness(0, 28, 0, 0),
            BorderThickness = new Thickness(1), HorizontalAlignment = HorizontalAlignment.Center,
            BorderBrush = new SolidColorBrush(((ISolidColorBrush)Palette.Accent).Color, 0.5),
        };
        // stub(#315): OrionApp+Files.swift owns this until the logic moves below the facade (openFile)
        open.Click += (_, _) => { };
        return new StackPanel {
            HorizontalAlignment = HorizontalAlignment.Center, VerticalAlignment = VerticalAlignment.Center,
            Children = {
                new TextBlock { Text = "Orion", FontFamily = serif, FontSize = 52, HorizontalAlignment = HorizontalAlignment.Center },
                new TextBlock { Text = "A darkroom for raw files.", FontFamily = serif, FontSize = TypeSize.Md, FontStyle = FontStyle.Italic,
                                Foreground = Palette.Dim, Margin = new Thickness(0, 6, 0, 0), HorizontalAlignment = HorizontalAlignment.Center },
                open,
                new TextBlock { Text = "ARW, DNG, NEF, CR2, CR3, RAF, ORF, RW2 — Sony is the tested one.", FontSize = TypeSize.Xs,
                                Foreground = Palette.Faint, Margin = new Thickness(0, 14, 0, 0), HorizontalAlignment = HorizontalAlignment.Center },
            },
        };
    }
}
