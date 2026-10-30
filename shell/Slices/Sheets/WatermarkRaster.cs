using System.Runtime.InteropServices;
using SkiaSharp;

namespace OrionShell.Sheets;

/// A port of the Swift app's WatermarkRaster.swift: where the mark goes (Place, pure
/// geometry) and the 8-bit coverage mask the engine blends the one gray
/// through (Mask). The engine never learns what the mark was (#284).
/// Text is drawn with SkiaSharp, which Avalonia already ships; an SVG through
/// the shim's NSImage (shim_svg_coverage).
static unsafe partial class WatermarkRaster {
    /// A placed mark in pixels, origin top-left, y down; Angle counter-clockwise as the viewer sees it.
    public readonly record struct Placed(double Cx, double Cy, double W, double H, double Angle) {
        /// The axis-aligned box the turned mark occupies.
        public (double X, double Y, double W, double H) Bounds {
            get {
                double c = Math.Abs(Math.Cos(Angle)), s = Math.Abs(Math.Sin(Angle));
                double w = W * c + H * s, h = W * s + H * c;
                return (Cx - w / 2, Cy - h / 2, w, h);
            }
        }
    }

    /// WatermarkLayout.place. Shrunk to fit rather than allowed off the frame:
    /// a name with its last letters missing reads as a mistake, not a signature.
    public static Placed Place(double markAspect, double W, double H, Watermark m) {
        if (W <= 0 || H <= 0 || markAspect <= 0) return default;
        if (!m.Diagonal) {
            double margin = m.Margin * Math.Min(W, H);
            double roomW = Math.Max(1, W - 2 * margin), roomH = Math.Max(1, H - 2 * margin);
            double h = m.Size * Math.Min(W, H), w = h * markAspect;
            double fit = Math.Min(1, Math.Min(roomW / w, roomH / h));
            (w, h) = (w * fit, h * fit);
            var (fx, fy) = m.Fraction;
            return new(margin + w / 2 + fx * (roomW - w), margin + h / 2 + fy * (roomH - h), w, h, 0);
        }
        double angle = Math.Atan2(H, W);
        double dw = m.Span * Math.Sqrt(W * W + H * H), dh = dw / markAspect;
        // The turned box, not the mark, has to fit.
        double co = Math.Cos(angle), si = Math.Sin(angle);
        double f = Math.Min(1, Math.Min(W / (dw * co + dh * si), H / (dw * si + dh * co)));
        return new(W / 2, H / 2, dw * f, dh * f, angle);
    }

    /// Coverage over the whole width×height frame, rows top first, opacity baked
    /// in: what OrionExportOptions.watermark_mask takes. Null when there is
    /// nothing to draw, so an export never pays for a pass that changes nothing.
    public static byte[]? Mask(Watermark mark, int width, int height) {
        if (width <= 0 || height <= 0 || !mark.IsDrawable || mark.Opacity <= 0) return null;
        double aspect;
        SKFont? font = null;
        SKRect ink = default;
        try {
            if (mark.IsText) {
                font = new SKFont(SKTypeface.FromFamilyName(mark.Family, mark.Bold ? SKFontStyle.Bold : SKFontStyle.Normal), 200);
                // The ink, not the typographic box: a margin measured from the ascender
                // would leave a caps-only name visibly off its corner.
                font.MeasureText(mark.Words, out ink);
                if (ink.Width <= 0 || ink.Height <= 0) return null;
                aspect = ink.Width / ink.Height;
            } else {
                double sw, sh;
                fixed (byte* svg = mark.Svg)
                    if (shim_svg_size(svg, mark.Svg.Length, out sw, out sh) == 0) return null;
                aspect = sw / sh;
            }

            var placed = Place(aspect, width, height, mark);
            var (bx0, by0, bw0, bh0) = placed.Bounds;
            // The box, whole pixels outward, inside the frame.
            int bx = Math.Max(0, (int)Math.Floor(bx0)), by = Math.Max(0, (int)Math.Floor(by0));
            int bw = Math.Min(width, (int)Math.Ceiling(bx0 + bw0)) - bx, bh = Math.Min(height, (int)Math.Ceiling(by0 + bh0)) - by;
            if (bw <= 0 || bh <= 0) return null;

            var tile = new byte[bw * bh];
            if (font != null) {
                using var bitmap = new SKBitmap(new SKImageInfo(bw, bh, SKColorType.Alpha8, SKAlphaType.Premul));
                using (var canvas = new SKCanvas(bitmap)) {
                    canvas.Clear(SKColors.Transparent);
                    canvas.Translate((float)(placed.Cx - bx), (float)(placed.Cy - by));
                    canvas.RotateRadians((float)-placed.Angle);   // y down: counter-clockwise is negative
                    float s = (float)(placed.H / ink.Height);
                    canvas.Scale(s, s);
                    using var paint = new SKPaint { Color = SKColors.Black, IsAntialias = true };
                    canvas.DrawText(mark.Words, -ink.MidX, -ink.MidY, font, paint);
                }
                for (int y = 0; y < bh; y++) Marshal.Copy(bitmap.GetPixels() + y * bitmap.RowBytes, tile, y * bw, bw);
            } else {
                fixed (byte* svg = mark.Svg) fixed (byte* t = tile)
                    if (shim_svg_coverage(svg, mark.Svg.Length, t, bw, bh, placed.Cx - bx, placed.Cy - by,
                                          placed.W, placed.H, placed.Angle) == 0) return null;
            }

            var bytes = new byte[width * height];
            double opacity = Math.Clamp(mark.Opacity, 0, 1);
            for (int y = 0; y < bh; y++)
                for (int x = 0; x < bw; x++)
                    bytes[(by + y) * width + bx + x] = (byte)Math.Round(tile[y * bw + x] * opacity);
            return bytes;
        } finally {
            font?.Dispose();
        }
    }

    /// Engine.withWatermark: `call` with the options pointed at the photographer's
    /// mark for exactly its length, or unmarked when the mark is off or empty.
    /// `size` is the output's, which is the mask's.
    public static int Marked(SheetsNative.ExportOptions options, (uint W, uint H) size, Func<SheetsNative.ExportOptions, int> call) =>
        With(Watermark.Shared.Enabled ? Mask(Watermark.Shared, (int)size.W, (int)size.H) : null, options, size, call);

    public static int With(byte[]? mask, SheetsNative.ExportOptions options, (uint W, uint H) size, Func<SheetsNative.ExportOptions, int> call) {
        if (mask == null || mask.Length != (long)size.W * size.H) return call(options);
        fixed (byte* p = mask)
            return call(options with {
                WatermarkMask = (nint)p, WatermarkWidth = size.W, WatermarkHeight = size.H,
                WatermarkR = Watermark.Gray, WatermarkG = Watermark.Gray, WatermarkB = Watermark.Gray,
            });
    }

    [LibraryImport(Native.Lib)] private static partial int shim_svg_size(byte* bytes, int length, out double w, out double h);
    [LibraryImport(Native.Lib)]
    private static partial int shim_svg_coverage(byte* bytes, int length, byte* alpha, int bw, int bh,
                                                 double cx, double cy, double w, double h, double angle);
}
