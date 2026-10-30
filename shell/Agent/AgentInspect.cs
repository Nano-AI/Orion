using System.Runtime.InteropServices;
using System.Text.Json.Nodes;
using SkiaSharp;

namespace OrionShell.Agent;

/// The inspection verbs: a region of the developed picture at native
/// resolution, the numbers a region measures, and the face boxes (the Swift
/// app's AgentInspect, AgentFaces and RegionStats). ⚠ Why a region at all: a
/// 42 MP frame at the 2048 px proxy cap is a twentieth of its width, so
/// sharpening, skin texture and a mask's edge are all below what a whole-frame
/// proxy can show, and a model looking at one will call them fine.
static partial class AgentInspect {
    /// The full render, cropped to `region` and scaled down only when the crop's
    /// long edge is over `max`: a crop that already fits comes back at native size.
    public static JsonObject ProxyRegion(Session s, AgentCli.Region region, uint max, string path) {
        var rgba = s.Rgba(Graph.Full, out int w, out int h) ?? throw new AgentCli.Failure("no photo open");
        // Rounded out to whole pixels and clamped: a region of 0.0001 is still a pixel.
        int x0 = Math.Min(w - 1, Math.Max(0, (int)Math.Floor(region.X * w)));
        int y0 = Math.Min(h - 1, Math.Max(0, (int)Math.Floor(region.Y * h)));
        int cw = Math.Min(w - x0, Math.Max(1, (int)Math.Round(region.W * w)));
        int ch = Math.Min(h - y0, Math.Max(1, (int)Math.Round(region.H * h)));
        using var full = Bitmap(rgba, w, h);
        using var crop = new SKBitmap(cw, ch, SKColorType.Rgba8888, SKAlphaType.Opaque);
        if (!full.ExtractSubset(crop, new SKRectI(x0, y0, x0 + cw, y0 + ch))) throw new AgentCli.Failure($"could not crop {cw}x{ch} at {x0},{y0}");
        var image = crop;
        SKBitmap? scaled = null;
        if (max > 0 && Math.Max(cw, ch) > max) {
            double k = (double)max / Math.Max(cw, ch);
            scaled = crop.Resize(new SKImageInfo(Math.Max(1, (int)Math.Round(cw * k)), Math.Max(1, (int)Math.Round(ch * k))),
                                 new SKSamplingOptions(SKCubicResampler.Mitchell));
            image = scaled ?? throw new AgentCli.Failure("could not scale the crop");
        }
        using (scaled) {
            using var data = image.Encode(SKEncodedImageFormat.Jpeg, 85) ?? throw new AgentCli.Failure($"could not encode {path} as a JPEG");
            AgentCli.WriteAtomically(path, data.ToArray());
            return new JsonObject { ["path"] = path, ["width"] = image.Width, ["height"] = image.Height, ["bytes"] = data.Size };
        }
    }

    /// Every field the region measures, and `frame`: the rectangle carried into
    /// frame space as the ellipse covering it, ready for a kind-2 radial.
    public static JsonObject RegionJson(Session s, AgentCli.Region r) {
        var rgba = s.Rgba(Graph.Full, out int w, out int h) ?? throw new AgentCli.Failure("could not read the region - is a photo open");
        int x0 = Math.Max(0, Math.Min(w - 1, (int)(r.X * w))), y0 = Math.Max(0, Math.Min(h - 1, (int)(r.Y * h)));
        int rw = Math.Max(1, Math.Min(w - x0, (int)(r.W * w))), rh = Math.Max(1, Math.Min(h - y0, (int)(r.H * h)));
        var rgb = new float[rw * rh * 3];
        for (int y = 0; y < rh; y++)
            for (int x = 0; x < rw; x++)
                for (int c = 0; c < 3; c++) rgb[(y * rw + x) * 3 + c] = rgba[((y0 + y) * w + x0 + x) * 4 + c] / 255f;
        var st = RegionStats.Of(rgb, rw, rh);
        var e = FrameEllipse(s, r.X, r.Y, r.W, r.H);
        return new JsonObject {
            ["frame"] = new JsonObject { ["centerX"] = e.Cx, ["centerY"] = e.Cy, ["radiusX"] = e.Rx, ["radiusY"] = e.Ry },
            ["x"] = r.X, ["y"] = r.Y, ["w"] = r.W, ["h"] = r.H,
            ["luma"] = st.Luma, ["saturation"] = st.Saturation, ["hue"] = st.Hue, ["hueStrength"] = st.HueStrength,
            ["red"] = st.Red, ["green"] = st.Green, ["blue"] = st.Blue,
            ["clippedHigh"] = st.ClippedHigh, ["clippedLow"] = st.ClippedLow, ["shading"] = st.Shading, ["noise"] = st.Noise,
        };
    }

    /// Vision's face boxes on the developed picture at 1024 px, in display space
    /// and carried into frame space. ⚠ Vision jitters by about 0.01 between runs.
    public static JsonObject Faces(string raw, string? state) {
        using var s = AgentCli.OpenEngine(raw, Sidecar.Read(raw)?.Develop, state);
        var rgba = s.Rgba(Graph.Full, out int w, out int h) ?? throw new AgentCli.Failure("no photo open");
        double k = Math.Min(1.0, 1024.0 / Math.Max(w, h));
        int sw = Math.Max(1, (int)(w * k)), sh = Math.Max(1, (int)(h * k));
        using var full = Bitmap(rgba, w, h);
        using var small = full.Resize(new SKImageInfo(sw, sh, SKColorType.Rgba8888, SKAlphaType.Opaque), new SKSamplingOptions(SKCubicResampler.Mitchell))
                          ?? throw new AgentCli.Failure("could not render for the detector");
        var pixels = small.Bytes;
        for (int i = 3; i < pixels.Length; i += 4) pixels[i] = 255;   // opaque: a turned frame's transparent corners read as content
        var boxes = new float[4 * 64];
        var why = new byte[512];
        int n = shim_vision_faces(pixels, sw, sh, boxes, 64, why, why.Length);
        if (n < 0) throw new AgentCli.Failure("the face detector failed - " + System.Text.Encoding.UTF8.GetString(why).TrimEnd('\0'));
        var faces = new JsonArray();
        for (int i = 0; i < Math.Min(n, 64); i++) {
            // Vision's origin is bottom-left; display space is top-left.
            double bx = boxes[4 * i], by = boxes[4 * i + 1], bw = boxes[4 * i + 2], bh = boxes[4 * i + 3];
            double dy = 1 - by - bh;
            var e = FrameEllipse(s, bx, dy, bw, bh);
            faces.Add(new JsonObject { ["x"] = bx, ["y"] = dy, ["w"] = bw, ["h"] = bh,
                                       ["centerX"] = e.Cx, ["centerY"] = e.Cy, ["radiusX"] = e.Rx, ["radiusY"] = e.Ry });
        }
        return new JsonObject { ["path"] = raw, ["faces"] = faces };
    }

    /// FrameDisplayMap.frameEllipse: the display rectangle's centre and its two
    /// half-axes carried into frame space through the engine's display map.
    static (double Cx, double Cy, double Rx, double Ry) FrameEllipse(Session s, double x, double y, double w, double h) {
        var toDisplay = new float[9];
        var toFrame = new float[9];
        if (orion_engine_display_map(s.Handle, toDisplay, toFrame) != 0) toFrame = [1, 0, 0, 0, 1, 0, 0, 0, 1];
        (double X, double Y) Frame(double px, double py) {
            double d = toFrame[6] * px + toFrame[7] * py + toFrame[8];
            d = Math.Abs(d) > 1e-9 ? d : 1e-9;
            return ((toFrame[0] * px + toFrame[1] * py + toFrame[2]) / d, (toFrame[3] * px + toFrame[4] * py + toFrame[5]) / d);
        }
        var c = Frame(x + w / 2, y + h / 2);
        var ex = Frame(x + w, y + h / 2);
        var ey = Frame(x + w / 2, y + h);
        return (c.X, c.Y, Math.Max(Math.Abs(ex.X - c.X), Math.Abs(ey.X - c.X)), Math.Max(Math.Abs(ex.Y - c.Y), Math.Abs(ey.Y - c.Y)));
    }

    static unsafe SKBitmap Bitmap(byte[] rgba, int w, int h) {
        var bitmap = new SKBitmap(w, h, SKColorType.Rgba8888, SKAlphaType.Opaque);
        Marshal.Copy(rgba, 0, bitmap.GetPixels(), rgba.Length);
        return bitmap;
    }

    [LibraryImport(Native.Lib)]
    private static partial int orion_engine_display_map(nint engine, [Out] float[] toDisplay, [Out] float[] toFrame);

    [LibraryImport(Native.Lib)]
    private static partial int shim_vision_faces([In] byte[] rgba, int width, int height, [Out] float[] boxes, int capacity,
                                                 [Out] byte[] why, int whyCapacity);
}

/// The numbers a patch of the picture measures (decision #278), one pass over
/// interleaved RGB in 0…1. ⚠ `shading`'s twelfth is chosen, not sourced
/// (research/UNSOURCED.md §19); `noise` is the RMS of luma against its own 3x3
/// box mean, research/denoise-masked.md §3, compared before and after on one
/// patch, never across patches.
static class RegionStats {
    public sealed record Stats(double Luma, double Saturation, double Red, double Green, double Blue, double Hue, double HueStrength,
                               double ClippedHigh, double ClippedLow, double Shading, double Noise);

    public static Stats Of(float[] rgb, int width, int height) {
        int n = width * height;
        double luma = 0, sat = 0, red = 0, green = 0, blue = 0, hx = 0, hy = 0, chromaTotal = 0, hue = 0, strength = 0;
        var lumas = new double[n];
        int high = 0, low = 0;
        for (int i = 0; i < n; i++) {
            double r = Math.Clamp(rgb[i * 3], 0, 1), g = Math.Clamp(rgb[i * 3 + 1], 0, 1), b = Math.Clamp(rgb[i * 3 + 2], 0, 1);
            double mx = Math.Max(r, Math.Max(g, b)), mn = Math.Min(r, Math.Min(g, b)), chroma = mx - mn;
            red += r; green += g; blue += b;
            sat += mx > 0.001 ? chroma / mx : 0;
            double y = 0.2126 * r + 0.7152 * g + 0.0722 * b;
            lumas[i] = y;
            luma += y;
            if (mx >= 254.0 / 255.0) high++;
            if (mx <= 1.0 / 255.0) low++;
            if (chroma > 1e-6) {
                double hh = (mx == r ? (g - b) / chroma : mx == g ? 2 + (b - r) / chroma : 4 + (r - g) / chroma) * 60;
                if (hh < 0) hh += 360;
                hx += chroma * Math.Cos(hh * Math.PI / 180);
                hy += chroma * Math.Sin(hh * Math.PI / 180);
                chromaTotal += chroma;
            }
        }
        if (n == 0) return new(0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
        if (chromaTotal > 0) {
            strength = Math.Sqrt(hx * hx + hy * hy) / chromaTotal;
            hue = Math.Atan2(hy, hx) * 180 / Math.PI;
            if (hue < 0) hue += 360;
        }
        // Shading: block means over a grid, then their spread over their mean.
        double shading = 0;
        int block = Math.Max(2, Math.Min(width, height) / 12), bw = width / block, bh = height / block;
        if (bw * bh >= 4) {
            var means = new double[bw * bh];
            for (int by = 0; by < bh; by++)
                for (int bx = 0; bx < bw; bx++) {
                    double sum = 0;
                    for (int y = by * block; y < (by + 1) * block; y++)
                        for (int x = bx * block; x < (bx + 1) * block; x++) sum += lumas[y * width + x];
                    means[by * bw + bx] = sum / (block * block);
                }
            double m = means.Average();
            if (m > 1e-6) shading = Math.Sqrt(means.Sum(v => (v - m) * (v - m)) / means.Length) / m;
        }
        // Noise: interior pixels only; padding a border would read the patch's edge as grain.
        double noise = 0;
        if (width >= 3 && height >= 3) {
            double sum = 0;
            for (int y = 1; y < height - 1; y++)
                for (int x = 1; x < width - 1; x++) {
                    double box = 0;
                    for (int dy = -1; dy <= 1; dy++)
                        for (int dx = -1; dx <= 1; dx++) box += lumas[(y + dy) * width + x + dx];
                    double d = lumas[y * width + x] - box / 9;
                    sum += d * d;
                }
            noise = Math.Sqrt(sum / ((width - 2) * (height - 2)));
        }
        return new(luma / n, sat / n, red / n, green / n, blue / n, hue, strength, (double)high / n, (double)low / n, shading, noise);
    }
}
