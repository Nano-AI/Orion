using System.Runtime.InteropServices;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.Layout;
using Avalonia.Media;
using Avalonia.Media.Imaging;
using Avalonia.Platform;
using OrionShell.Masks;

namespace OrionShell.Sheets;

/// Landscape stacking's sky/ground split (#391): the reference frame's embedded preview with the
/// sky tinted, seeded by SkyDetector and corrected by hand with a round brush. The matte is upright,
/// as the preview is, 1 = sky, at the preview's size up to LongEdge; the engine scales it to the
/// frame and undoes the EXIF turn.
sealed class StackSplit : Control {
    public const int LongEdge = 384;
    const double TintOpacity = 0.45;

    readonly Bitmap _picture;
    readonly WriteableBitmap _tint;
    bool _painting;

    /// The reference frame this split was painted on.
    public string Path { get; }
    public float[] Matte { get; }
    public int MatteWidth { get; }
    public int MatteHeight { get; }
    /// Why the detector found no sky (the matte then starts empty), or null when it seeded it.
    public string? NoSky { get; }
    /// Add sky, or Remove sky.
    public bool Adding { get; set; } = true;
    /// The brush's radius as a fraction of the preview's long edge.
    public double Radius { get; set; } = 0.06;

    StackSplit(string path, Bitmap picture, float[] rgb, int w, int h) {
        (Path, _picture, MatteWidth, MatteHeight) = (path, picture, w, h);
        (Matte, NoSky) = Seed(rgb, w, h);
        _tint = new WriteableBitmap(new PixelSize(w, h), new Vector(96, 96), PixelFormat.Rgba8888, AlphaFormat.Premul);
        Tint();
        Cursor = new Cursor(StandardCursorType.Cross);
        HorizontalAlignment = HorizontalAlignment.Left;   // a portrait frame keeps its measured width, not the row's
    }

    /// The split for `path`'s embedded preview, seeded; null when the raw carries none.
    public static StackSplit? For(string path) {
        if (Library.Thumbnails.Load(path) is not { } picture) return null;
        var full = picture.PixelSize;
        double k = Math.Min(1.0, (double)LongEdge / Math.Max(full.Width, full.Height));
        int w = Math.Max(3, (int)Math.Round(full.Width * k)), h = Math.Max(3, (int)Math.Round(full.Height * k));
        // Drawn small, then read back as RGBA whatever the preview's own format.
        using var small = new RenderTargetBitmap(new PixelSize(w, h));
        using (var c = small.CreateDrawingContext())
        using (c.PushRenderOptions(new RenderOptions { BitmapInterpolationMode = BitmapInterpolationMode.HighQuality }))
            c.DrawImage(picture, new Rect(0, 0, w, h));
        using var rgba = new WriteableBitmap(new PixelSize(w, h), new Vector(96, 96), PixelFormat.Rgba8888, AlphaFormat.Opaque);
        var bytes = new byte[w * 4];
        var rgb = new float[w * h * 3];
        using (var fb = rgba.Lock()) {
            small.CopyPixels(fb);
            for (int y = 0; y < h; y++) {
                Marshal.Copy(fb.Address + y * fb.RowBytes, bytes, 0, bytes.Length);
                for (int x = 0; x < w; x++)
                    for (int ch = 0; ch < 3; ch++) rgb[(y * w + x) * 3 + ch] = bytes[x * 4 + ch] / 255f;
            }
        }
        return new StackSplit(path, picture, rgb, w, h);
    }

    /// The detector on a 3×3 median of the picture: the median takes single-pixel stars out, so a
    /// night sky reads as calm. All zero when it refuses, with its reason.
    public static (float[] Matte, string? NoSky) Seed(float[] rgb, int w, int h) {
        var (alpha, why) = SkyDetector.Detect(Median3(rgb, w, h), w, h);
        return (alpha ?? new float[w * h], why);
    }

    static float[] Median3(float[] rgb, int w, int h) {
        var out_ = new float[rgb.Length];
        Span<float> v = stackalloc float[9];
        for (int y = 0; y < h; y++)
            for (int x = 0; x < w; x++)
                for (int ch = 0; ch < 3; ch++) {
                    int n = 0;
                    for (int dy = -1; dy <= 1; dy++)
                        for (int dx = -1; dx <= 1; dx++)
                            v[n++] = rgb[(Math.Clamp(y + dy, 0, h - 1) * w + Math.Clamp(x + dx, 0, w - 1)) * 3 + ch];
                    v.Sort();
                    out_[(y * w + x) * 3 + ch] = v[4];
                }
        return out_;
    }

    /// A disc of `radius` matte pixels around (x, y) set to 1 (add) or 0 (remove); nothing outside it changes.
    public static void Paint(float[] matte, int w, int h, float x, float y, float radius, bool add) {
        int x0 = Math.Max(0, (int)MathF.Floor(x - radius)), x1 = Math.Min(w - 1, (int)MathF.Ceiling(x + radius));
        int y0 = Math.Max(0, (int)MathF.Floor(y - radius)), y1 = Math.Min(h - 1, (int)MathF.Ceiling(y + radius));
        for (int py = y0; py <= y1; py++)
            for (int px = x0; px <= x1; px++) {
                float dx = px + 0.5f - x, dy = py + 0.5f - y;
                if (dx * dx + dy * dy <= radius * radius) matte[py * w + px] = add ? 1 : 0;
            }
    }

    /// The painter, its brush (Add sky · Remove sky, size) and the detector's refusal, if any.
    public Control Editor() {
        var brush = new Segmented(["Add sky", "Remove sky"], 0);
        brush.Changed += i => Adding = i == 0;
        var size = new Slider { Minimum = 0.01, Maximum = 0.2, Value = Radius, Foreground = Palette.Accent, MinHeight = 0, Width = 160,
                                VerticalAlignment = VerticalAlignment.Center };
        size.ValueChanged += (_, e) => Radius = e.NewValue;
        ToolTip.SetTip(size, "Brush size");
        Avalonia.Automation.AutomationProperties.SetName(size, "Brush size");
        var tools = new DockPanel { Children = { brush, size } };
        DockPanel.SetDock(brush, Dock.Left);
        size.HorizontalAlignment = HorizontalAlignment.Right;
        var editor = new Avalonia.Controls.StackPanel { Spacing = Space.Sm, Children = { this, tools } };
        if (NoSky != null) editor.Children.Add(SheetKit.Note("No horizon found — paint the sky.", Palette.Warn));
        return editor;
    }

    /// The sheet's width, or less for a portrait frame: never taller than MaxTall.
    protected override Size MeasureOverride(Size available) {
        const double MaxTall = 260;
        double w = double.IsFinite(available.Width) ? available.Width : MatteWidth, h = w * MatteHeight / MatteWidth;
        return h <= MaxTall ? new Size(w, h) : new Size(MaxTall * MatteWidth / MatteHeight, MaxTall);
    }

    public override void Render(DrawingContext context) {
        var r = new Rect(Bounds.Size);
        context.DrawImage(_picture, r);
        using (context.PushOpacity(TintOpacity)) context.DrawImage(_tint, r);
    }

    protected override void OnPointerPressed(PointerPressedEventArgs e) {
        _painting = true;
        e.Pointer.Capture(this);
        Stroke(e.GetPosition(this));
        e.Handled = true;
    }

    // ponytail: a disc per pointer event, no line between them; a very fast drag with a small brush
    // leaves beads. Interpolate from the last point if that shows.
    protected override void OnPointerMoved(PointerEventArgs e) { if (_painting) Stroke(e.GetPosition(this)); }

    protected override void OnPointerReleased(PointerReleasedEventArgs e) {
        _painting = false;
        e.Pointer.Capture(null);
    }

    protected override void OnPointerCaptureLost(PointerCaptureLostEventArgs e) => _painting = false;

    void Stroke(Point p) {
        if (Bounds.Width <= 0) return;
        float k = (float)(MatteWidth / Bounds.Width);
        Paint(Matte, MatteWidth, MatteHeight, (float)p.X * k, (float)p.Y * k, (float)(Radius * Math.Max(MatteWidth, MatteHeight)), Adding);
        Tint();
        InvalidateVisual();
    }

    /// The matte in red, premultiplied, alpha = matte: red as the mask overlay is (develop_linear.slang),
    /// so coverage reads one way across Orion, and so it shows on a blue sky, where the cyan accent vanished.
    void Tint() {
        var c = ((ISolidColorBrush)Palette.Reject).Color;
        var row = new byte[MatteWidth * 4];
        using var fb = _tint.Lock();
        for (int y = 0; y < MatteHeight; y++) {
            for (int x = 0; x < MatteWidth; x++) {
                float m = Math.Clamp(Matte[y * MatteWidth + x], 0, 1);
                (row[x * 4], row[x * 4 + 1], row[x * 4 + 2], row[x * 4 + 3]) = ((byte)(c.R * m), (byte)(c.G * m), (byte)(c.B * m), (byte)(255 * m));
            }
            Marshal.Copy(row, 0, fb.Address + y * fb.RowBytes, row.Length);
        }
    }
}
