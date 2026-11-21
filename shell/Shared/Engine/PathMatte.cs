using SkiaSharp;

namespace OrionShell;

/// A path row's matte (research/masking.md §5b): its closed outline of cubic
/// Bézier segments, filled by the non-zero winding rule and antialiased by
/// Skia, then blurred by a Gaussian for the feather, at the engine's matte
/// size in frame coordinates. Filled again from the points whenever they
/// change, so the sidecar holds the points and no file holds the matte.
public static class PathMatte {
    /// MaskShape.PathPoints per point: the anchor, the handle in, the handle out, each x then y.
    public const int Stride = 6;
    /// Feather 1 is a Gaussian of this standard deviation, as a share of the matte's shorter side.
    public const float MaxSigma = 0.04f;

    /// What MatteStore holds in a slot for a path row: equal exactly when the matte would be.
    public static string? Key(MaskShape m) =>
        m.PathPoints.Count < Stride ? null : $"path {m.Feather} {string.Join(' ', m.PathPoints)}";

    /// The matte at this session's size, or null when the engine will not say its size. Its alpha is
    /// this thread's buffer, good until the thread's next fill: orion_engine_set_mask_matte copies it.
    public static (float[] Alpha, int Width, int Height)? Fill(Session s, MaskShape m) =>
        Native.orion_engine_max_matte_size(s.Handle, out uint w, out uint h) != 0 ? null
            : (Fill(m.PathPoints, m.Feather, (int)w, (int)h), (int)w, (int)h);

    // A drag fills on every tick, and both are on the large-object heap. Per thread, because the
    // neighbour cache restores sessions, and so fills, off the UI thread.
    [ThreadStatic] static SKBitmap? _bitmap;
    [ThreadStatic] static float[]? _alpha;

    static float[] Fill(IReadOnlyList<float> points, float feather, int width, int height) {
        int n = points.Count / Stride;
        SKPoint At(int point, int part) =>
            new(points[point % n * Stride + part * 2] * width, points[point % n * Stride + part * 2 + 1] * height);
        using var path = new SKPath { FillType = SKPathFillType.Winding };
        path.MoveTo(At(0, 0));
        for (int i = 0; i < n; i++) path.CubicTo(At(i, 2), At(i + 1, 1), At(i + 1, 0));
        path.Close();

        if (_bitmap is not { } bitmap || bitmap.Width != width || bitmap.Height != height) {
            // Both made before the old pair is let go, so a throw leaves the thread's pair whole and live.
            var (made, floats) = (new SKBitmap(new SKImageInfo(width, height, SKColorType.Alpha8, SKAlphaType.Premul)), new float[width * height]);
            _bitmap?.Dispose();
            (_bitmap, _alpha, bitmap) = (made, floats, made);
        }
        using (var canvas = new SKCanvas(bitmap)) {
            canvas.Clear(SKColors.Transparent);
            float sigma = Math.Clamp(feather, 0, 1) * MaxSigma * Math.Min(width, height);
            using var blur = sigma > 0 ? SKMaskFilter.CreateBlur(SKBlurStyle.Normal, sigma) : null;
            using var paint = new SKPaint { Color = SKColors.Black, IsAntialias = true, Style = SKPaintStyle.Fill, MaskFilter = blur };
            canvas.DrawPath(path, paint);
        }
        var alpha = _alpha!;
        var bytes = bitmap.GetPixelSpan();
        for (int y = 0; y < height; y++)
            for (int x = 0; x < width; x++) alpha[y * width + x] = bytes[y * bitmap.RowBytes + x] / 255f;
        return alpha;
    }
}
