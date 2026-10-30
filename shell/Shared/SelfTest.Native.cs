using System.Runtime.InteropServices;
using Avalonia;

namespace OrionShell;

/// Stage 1b's checks (#315): each seam reaches the pixels, the preview reads
/// at the photo's shape, and releasing render memory gives memory back.
static partial class SelfTest {
    static partial void NativeChecks() {
        var engine = EngineModel.Shared;
        if (engine.Session is not { } session) return;   // SelfTest.Run already failed for want of a photo

        // A photo in a folder and under a file name that are not ASCII opens (#376): the shim
        // takes UTF-8, which Windows reads as the ANSI code page unless app.manifest says UTF-8.
        string unicode = Directory.CreateTempSubdirectory("orion-Zo\u00eb-").FullName;
        try {
            string named = Path.Combine(unicode, "caf\u00e9 \u65e5\u672c" + Path.GetExtension(engine.Photos[engine.Current]));
            File.Copy(engine.Photos[engine.Current], named);
            nint unicodeShim = Native.shim_open_mode(named, 1);
            Check(unicodeShim != 0, $"a photo under a non-ASCII folder and file name opens ({Path.GetFileName(unicode)}/{Path.GetFileName(named)})");
            if (unicodeShim != 0) Native.shim_close(unicodeShim);
        } finally {
            Directory.Delete(unicode, recursive: true);
        }

        // Curves: the panel's edit changes the render, identity puts it back.
        double flat = MeanLuma(engine.Picture);
        (float, float)[] lift = [(0, 0), (0.4f, 0.7f), (1, 1)];
        engine.Apply(s => s.SetCurve(0, lift));
        double lifted = MeanLuma(engine.Picture);
        engine.Apply(s => s.SetCurve(0, [(0, 0), (1, 1)]));
        double back = MeanLuma(engine.Picture);
        Check(lifted > flat + 5 && Math.Abs(back - flat) < 0.5,
              $"a master curve lift brightens the render and identity restores it ({flat:0.0} → {lifted:0.0} → {back:0.0})");
        for (int c = 0; c < 4; c++) {
            (float, float)[] drawn = [(0, 0.05f * c), (0.5f, 0.5f), (1, 1 - 0.05f * c)];
            session.SetCurve(c, drawn);
            Check(session.Curve(c).SequenceEqual(drawn), $"curve channel {c} round-trips");
            session.SetCurve(c, [(0, 0), (1, 1)]);
        }
        engine.Apply(s => s.SetCurve(3, [(0, 0.3f), (1, 1)]));
        var (r, _, b) = MeanRgb(engine.Picture);
        engine.Apply(s => s.SetCurve(3, [(0, 0), (1, 1)]));
        var (r0, _, b0) = MeanRgb(engine.Picture);
        Check(b > b0 + 5 && Math.Abs(r - r0) < 0.5, $"a blue curve lifts blue and leaves red ({b0:0.0} → {b:0.0} blue, red {r0:0.0} → {r:0.0})");

        // Auto: the sliders move (reread) and so does the picture.
        string[] owned = ["exposure_ev", "whites", "blacks", "fusion", "clarity"];
        var before = owned.Select(f => engine.Get(f)).ToArray();
        int rereads = 0;
        void Reread() => rereads++;
        engine.ValuesChanged += Reread;
        double plain = MeanLuma(engine.Picture);
        engine.Apply(s => s.AutoEnhance(), reread: true);
        engine.ValuesChanged -= Reread;
        Check(rereads == 1 && owned.Select(f => engine.Get(f)).Where((v, i) => v != before[i]).Any()
                           && MeanLuma(engine.Picture) != plain,
              "Auto writes the sliders it owns, rereads them, and changes the render");
        for (int i = 0; i < owned.Length; i++) session.Set(owned[i], before[i]);
        engine.Render(false);

        // Masks: a radial row and a brush row each carry their layer's exposure to the picture.
        Check(Session.MaskFields.Length == Native.shim_mask_field_count(),
              $"SetMaskGroup writes all {Native.shim_mask_field_count()} OrionMaskComponent fields");
        double unmasked = MeanLuma(engine.Picture);
        session.Set("local_exposure_ev", 2, 0);
        engine.Apply(s => s.SetMaskGroup([new MaskShape { Kind = 2, RadiusX = 0.5f, RadiusY = 0.5f }]));
        double radial = MeanLuma(engine.Picture);
        var brush = new MaskShape { Kind = 3, BrushRadius = 0.2f, BrushFlow = 1, BrushHardness = 1 };
        brush.BrushStroke.AddRange([0.5f, 0.5f, 0.6f, 0.5f]);
        engine.Apply(s => s.SetMaskGroup([brush]));
        double painted = MeanLuma(engine.Picture);
        brush.BrushStroke.Clear();
        engine.Apply(s => s.SetMaskGroup([brush]));
        double wiped = MeanLuma(engine.Picture);
        session.Set("local_exposure_ev", 0, 0);
        engine.Apply(s => s.SetMaskGroup([]));
        Check(radial > unmasked + 5 && painted > unmasked + 1 && Math.Abs(wiped - unmasked) < 0.5
              && Math.Abs(MeanLuma(engine.Picture) - unmasked) < 0.5 && session.Get("mask_count") == 0,
              $"a radial mask and a brush stroke reach the render; wiping and emptying restore it ({unmasked:0.0} → {radial:0.0}, {painted:0.0} → {wiped:0.0})");
        Check(Throws<ArgumentException>(() => session.SetMaskGroup(Enumerable.Range(0, 9).Select(_ => new MaskShape()).ToList()), "at most 8"),
              "a ninth mask row is refused");

        // A second session, never rendered full size: its preview reads at the photo's shape.
        using var fresh = Session.Open(session.Path);
        if (fresh == null) { Check(false, "a second session opens the photo"); return; }
        var graph = fresh.Render(preview: true);
        var shown = fresh.Read(graph);
        var p = fresh.PreviewSize;
        var full = fresh.ImageSize;
        Check(graph == Graph.Preview && shown?.PixelSize == p && p.Width < full.Width
              && Math.Abs((double)p.Width / p.Height - (double)full.Width / full.Height) < 0.01,
              $"a preview read before any full render is {shown?.PixelSize} at the photo's {full} shape (PreviewSize {p})");

        // Release: a full render's memory comes back, and a render brings the picture back.
        // Footprint, not RSS: Metal's textures never show in RSS (measured: 664 → 386
        // MB across a full render that took footprint 2814 → 4166). Metal frees them
        // a few hundred ms after the release, so wait for it, up to 3 s. Three
        // quarters of what the render added must come back: without the release,
        // 422 of 1351 MB came back on its own within the 3 s (measured).
        long opened = Footprint();
        fresh.Render(preview: false);
        long rendered = Footprint(), released = rendered, target = opened + (rendered - opened) / 4;
        fresh.ReleaseRenderMemory();
        for (var wait = System.Diagnostics.Stopwatch.StartNew(); wait.ElapsedMilliseconds < 3000 && released > target; Thread.Sleep(50))
            released = Footprint();
        Check(rendered - opened > 256L << 20 && released <= target,
              $"ReleaseRenderMemory gives back {(rendered - released) >> 20} MB of a full render's {(rendered - opened) >> 20} ({opened >> 20} → {rendered >> 20} → {released >> 20} MB footprint)");
        Check(fresh.Read(Graph.Full) == null && fresh.Render(preview: false) == Graph.Full && fresh.Read(Graph.Full) != null,
              "after a release the full picture is gone until the next render brings it back");
    }

    static unsafe (double R, double G, double B) MeanRgb(Avalonia.Media.Imaging.Bitmap? picture) {
        if (picture is not Avalonia.Media.Imaging.WriteableBitmap bitmap) return default;
        using var fb = bitmap.Lock();
        double r = 0, g = 0, b = 0;
        long n = 0;
        for (int y = 0; y < fb.Size.Height; y += 16) {
            byte* row = (byte*)fb.Address + (long)y * fb.RowBytes;
            for (int x = 0; x < fb.Size.Width; x += 16, n++) { r += row[x * 4]; g += row[x * 4 + 1]; b += row[x * 4 + 2]; }
        }
        return n == 0 ? default : (r / n, g / n, b / n);
    }

    /// The process's physical footprint (what Activity Monitor calls Memory),
    /// which counts Metal's private textures where RSS does not: ri_phys_footprint
    /// of rusage_info_v0, 16 bytes of uuid and seven uint64s in.
    static unsafe long Footprint() {
        // No phys_footprint off macOS; the working set holds what CI's CPU GPU drivers allocate (#376).
        if (!OperatingSystem.IsMacOS()) return Environment.WorkingSet;
        byte* info = stackalloc byte[512];
        return proc_pid_rusage(Environment.ProcessId, 0, info) == 0 ? *(long*)(info + 72) : 0;
    }

    [DllImport("libSystem")]
    static extern unsafe int proc_pid_rusage(int pid, int flavor, byte* buffer);
}
