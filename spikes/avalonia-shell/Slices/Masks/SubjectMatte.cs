using System.Runtime.InteropServices;
using Avalonia.Media.Imaging;

namespace OrionShell.Masks;

/// Subject, Person and Sky selection: a port of app/SubjectMatte.swift, its
/// Engine+Render.swift `renderForAnalysis` and MatteGeometry.swift, and the
/// panel's `findMatte`. research/masking.md §5.
///
/// The models see the developed picture, display-referred, rendered with the
/// crop, straighten, rotation, perspective and overlay off, so the only thing
/// between it and the frame a kind-4 matte lives in is the camera's own EXIF
/// quarter turn, undone exactly. Subject and Person are Apple's Vision
/// (native/matte.mm), so macOS only; Sky is the classical detector, anywhere.
/// The matte is not regenerated when the edit changes: it is made on demand
/// and kept until asked again (decision #79).
public static class SubjectMatte {
    public enum Kind { Subject, Person, Sky }

    /// A refusal the photographer reads: "Nothing was found…", the model's own error.
    public sealed class Failure(string message) : Exception(message);

    /// One run in flight at a time (Swift's matteRunning).
    public static bool Running { get; private set; }

    /// The whole act, as the panel's Detect menu does it: the model, then the
    /// file beside the photo, then a new mask row naming it, one undo step.
    /// Null when it worked, else the sentence to show. Off the UI thread for
    /// the inference only; `wait` runs it inline (the selftest, `--command`).
    public static async Task<string?> Select(Kind kind, bool wait = false) {
        var engine = EngineModel.Shared;
        var group = MaskGroup.Shared;
        if (engine.Session is not { } s || Running) return null;
        if (group.Shapes.Count >= MaskGroup.Max) return $"A photo holds at most {MaskGroup.Max} mask shapes.";
        Running = true;
        try {
            var picture = RenderForAnalysis(s);
            engine.Render(preview: false);   // the canvas shows the edit again, not the neutral render
            if (picture is not { } p) return "Could not read the rendered picture.";
            var matte = wait ? Infer(p, kind) : await Task.Run(() => Infer(p, kind));
            if (engine.Session != s) return null;   // the photo changed under the model: nothing to fill
            // The file first: a write that fails fails the whole selection, so the
            // edit never names a matte that is not on disk (MatteStore.swift).
            string id = MatteStore.Write(matte.Alpha, matte.Width, matte.Height, s.Path);
            group.Add(4, m => (m.MatteId, m.MatteSource) = (id, kind.ToString()));
            return MatteStore.IsMissing(s, id) ? "That selection was too large for this photo." : null;
        } catch (Failure e) {
            return e.Message;
        } catch (Exception e) when (e is IOException or UnauthorizedAccessException) {
            return "That selection could not be saved with the photo.";
        } finally {
            Running = false;
        }
    }

    /// The developed picture with its geometry neutralised, as opaque RGBA at
    /// the analysis size, and the EXIF turn it was rendered under. The session's
    /// state is restored exactly; its last render is the neutral one, so the
    /// caller renders next.
    static (byte[] Rgba, int Width, int Height, int Turns)? RenderForAnalysis(Session s) {
        string[] neutral = ["crop_x", "crop_y", "crop_w", "crop_h", "straighten_deg", "rotate_quarters",
                            "perspective_vertical", "perspective_horizontal", "perspective_aspect", "crop_preview"];
        var held = neutral.Select(f => s.Get(f)).ToArray();
        try {
            foreach (var f in neutral) s.Set(f, f is "crop_w" or "crop_h" ? 1 : 0);
            // The overlay tints what it covers red, and a model would read that as the photograph.
            s.SetMaskOverlay(false, 0);
            if (s.Render(preview: false) != Graph.Full || s.Copy(Graph.Full) is not WriteableBitmap full) return null;
            if (Native.orion_engine_quarter_turns(s.Handle, out int turns) != 0
                || Native.orion_engine_max_matte_size(s.Handle, out uint mw, out uint mh) != 0) return null;
            int w = full.PixelSize.Width, h = full.PixelSize.Height;
            // MatteGeometry.analysisSize: the engine's own cap, turned to match the render.
            bool odd = ((turns % 4 + 4) % 4) % 2 != 0;
            int capW = (int)(odd ? mh : mw), capH = (int)(odd ? mw : mh);
            var (aw, ah) = w <= capW && h <= capH ? (w, h) : (capW, capH);
            return aw <= 0 || ah <= 0 ? null : (Shrink(full, aw, ah), aw, ah, turns);
        } finally {
            for (int i = 0; i < neutral.Length; i++) s.Set(neutral[i], held[i]);
            var (on, layer) = MaskGroup.Shared.OverlayWanted;
            s.SetMaskOverlay(on, layer);
        }
    }

    /// Box-averaged down to `aw` × `ah`, alpha forced opaque: a turned frame's
    /// transparent corners would read to a model as content.
    static unsafe byte[] Shrink(WriteableBitmap full, int aw, int ah) {
        using var fb = full.Lock();
        int w = fb.Size.Width, h = fb.Size.Height;
        var out_ = new byte[aw * ah * 4];
        for (int y = 0; y < ah; y++) {
            int y0 = y * h / ah, y1 = Math.Max(y0 + 1, (y + 1) * h / ah);
            for (int x = 0; x < aw; x++) {
                int x0 = x * w / aw, x1 = Math.Max(x0 + 1, (x + 1) * w / aw);
                long r = 0, g = 0, b = 0;
                for (int sy = y0; sy < y1; sy++) {
                    byte* row = (byte*)fb.Address + (long)sy * fb.RowBytes;
                    for (int sx = x0; sx < x1; sx++) { r += row[sx * 4]; g += row[sx * 4 + 1]; b += row[sx * 4 + 2]; }
                }
                long n = (long)(y1 - y0) * (x1 - x0);
                int o = (y * aw + x) * 4;
                (out_[o], out_[o + 1], out_[o + 2], out_[o + 3]) = ((byte)(r / n), (byte)(g / n), (byte)(b / n), 255);
            }
        }
        return out_;
    }

    /// The model on the analysis picture, then back into frame coordinates. Any thread.
    static (float[] Alpha, int Width, int Height) Infer((byte[] Rgba, int Width, int Height, int Turns) p, Kind kind) {
        int n = p.Width * p.Height;
        float[] alpha;
        if (kind == Kind.Sky) {
            var rgb = new float[n * 3];
            for (int i = 0; i < n; i++)
                for (int c = 0; c < 3; c++) rgb[i * 3 + c] = p.Rgba[i * 4 + c] / 255f;
            var (found, noSky) = SkyDetector.Detect(rgb, p.Width, p.Height);
            alpha = found ?? throw new Failure(noSky!);
        } else {
            if (!OperatingSystem.IsMacOS()) throw new Failure($"{kind} detection is not available on this system.");
            alpha = new float[n];
            var why = new byte[512];
            if (Vision(p.Rgba, p.Width, p.Height, kind == Kind.Person ? 1 : 0, alpha, why) == 0)
                throw new Failure(System.Text.Encoding.UTF8.GetString(why, 0, Math.Max(0, Array.IndexOf(why, (byte)0))));
            // The person request answers "no people" with an all-zero mask, not an error, so a
            // selection under one part in 2000 of the frame is refused, saying what it measured.
            int lit = alpha.Count(v => v > 0.5f);
            if (lit * 2000L < n)
                throw new Failure(string.Format(System.Globalization.CultureInfo.InvariantCulture,
                    "Nothing was found to select: the model lit {0:0.000}% of the frame above half confidence (peak {1:0.00}); the floor is 0.05%.",
                    100.0 * lit / Math.Max(n, 1), alpha.Max()));
        }
        return UndoTurns(alpha, p.Width, p.Height, p.Turns);
    }

    /// MatteGeometry.undoTurns: a clockwise turn sent (x, y) to (h-1-y, x); this sends it back.
    public static (float[] Pixels, int Width, int Height) UndoTurns(float[] src, int width, int height, int turns) {
        int k = (turns % 4 + 4) % 4;
        if (k == 0) return (src, width, height);
        int outW = k % 2 != 0 ? height : width, outH = k % 2 != 0 ? width : height;
        var out_ = new float[outW * outH];
        for (int y = 0; y < height; y++)
            for (int x = 0; x < width; x++) {
                var (dx, dy) = k switch { 1 => (y, width - 1 - x), 2 => (width - 1 - x, height - 1 - y), _ => (height - 1 - y, x) };
                out_[dy * outW + dx] = src[y * width + x];
            }
        return (out_, outW, outH);
    }

    static unsafe int Vision(byte[] rgba, int width, int height, int kind, float[] alpha, byte[] why) {
        fixed (byte* r = rgba) fixed (float* a = alpha) fixed (byte* w = why)
            return MatteNative.shim_vision_matte(r, width, height, kind, a, w, why.Length);
    }
}

static unsafe partial class MatteNative {
    [LibraryImport(Native.Lib)]
    internal static partial int shim_vision_matte(byte* rgba, int width, int height, int kind, float* alpha, byte* why, int whyCapacity);
}
