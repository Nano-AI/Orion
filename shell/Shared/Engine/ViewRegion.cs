using Avalonia;

namespace OrionShell;

/// What a render past fit asks for (#22, decision #401): the part of the picture
/// in view and a margin around it, so a pan after a rest moves pixels already
/// rendered at full detail instead of uncovering the preview, and whether a
/// region already rendered still shows a view at full detail, so a pan inside
/// it renders nothing. Chromium's prepaint margin around the viewport is the
/// established form of the margin (impl-side painting, chromium.org).
static class ViewRegion {
    /// The margin on each side, as a fraction of the view's own size. A region
    /// renders without holding the UI thread (EngineModel), so its GPU time
    /// only delays the sharp picture; that time follows the window (Engine.cpp's
    /// halo and buckets): at 100% on a 42 MP ARW in a 3077 × 1258 view, a
    /// quarter each side took 215 ms against 110 for the view alone, an eighth
    /// ~1.4× the view's window. Under a moving hand the next region waits for
    /// the last, so an eighth (385 px each way there) lands soonest; at rest
    /// a quarter, so the next pan starts on rendered pixels.
    public const double HandMargin = 0.125, RestMargin = 0.25;
    /// Dawn's default maxTextureDimension2D: the region's output never passes it.
    const int MaxSide = 8192;

    /// `view` grown by `margin` on each side at the same pixels per unit, clamped
    /// to the picture and to MaxSide.
    public static (Rect Uv, PixelSize Pixels) Around((Rect Uv, PixelSize Pixels) view, double margin) {
        var (uv, px) = view;
        if (uv.Width <= 0 || uv.Height <= 0 || px.Width <= 0 || px.Height <= 0) return view;
        double sx = px.Width / uv.Width, sy = px.Height / uv.Height;   // device pixels per unit of picture
        double mx = Math.Clamp((MaxSide / sx - uv.Width) / 2, 0, uv.Width * margin);
        double my = Math.Clamp((MaxSide / sy - uv.Height) / 2, 0, uv.Height * margin);
        double x0 = Math.Max(0, uv.X - mx), y0 = Math.Max(0, uv.Y - my);
        double x1 = Math.Min(1, uv.Right + mx), y1 = Math.Min(1, uv.Bottom + my);
        var grown = new Rect(x0, y0, x1 - x0, y1 - y0);
        // Never smaller than the view, never past MaxSide unless the view itself is.
        static int Side(double want, int seen) => Math.Clamp((int)Math.Round(want), seen, Math.Max(seen, MaxSide));
        return (grown, new PixelSize(Side(grown.Width * sx, px.Width), Side(grown.Height * sy, px.Height)));
    }

    /// A region rendered as `rendered` shows `view` as sharply as a render of
    /// `view` would: it contains it, at the same pixels per unit (within 1%,
    /// what rounding a size to whole pixels moves).
    public static bool Covers((Rect Uv, PixelSize Pixels) rendered, (Rect Uv, PixelSize Pixels) view) {
        var (r, v) = (rendered.Uv, view.Uv);
        const double Slack = 1e-6;
        return SameScale(rendered, view) && v.X >= r.X - Slack && v.Y >= r.Y - Slack && v.Right <= r.Right + Slack && v.Bottom <= r.Bottom + Slack;
    }

    /// The same pixels per unit of picture, within 1%: a pan of `rendered`'s view, not a zoom.
    public static bool SameScale((Rect Uv, PixelSize Pixels) rendered, (Rect Uv, PixelSize Pixels) view) {
        var (r, rp) = rendered;
        var (v, vp) = view;
        if (r.Width <= 0 || r.Height <= 0 || v.Width <= 0 || v.Height <= 0) return false;
        double scaleX = rp.Width / r.Width / (vp.Width / v.Width), scaleY = rp.Height / r.Height / (vp.Height / v.Height);
        return Math.Abs(scaleX - 1) < 0.01 && Math.Abs(scaleY - 1) < 0.01;
    }
}
