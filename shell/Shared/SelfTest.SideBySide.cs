using Avalonia;
using Avalonia.Controls;
using Avalonia.Media.Imaging;
using Avalonia.Threading;
using OrionShell.CanvasSlice;
using OrionShell.Library;

namespace OrionShell;

/// Decision #400: the side-by-side compare through its commands and the real
/// engine. The same raw on both sides (two links to it, as ParityChecks does),
/// so wherever the two views agree the two renders must agree pixel for pixel,
/// and a link that drifted by a pixel shows as a difference, not a number.
static partial class SelfTest {
    static partial void SideBySideChecks() {
        var engine = EngineModel.Shared;
        if (engine.Handle == 0) return;
        var list = engine.Photos.ToList();
        int at = engine.Current;
        string dir = Directory.CreateTempSubdirectory("orion-compare-").FullName;
        Window? window = null;
        try {
            string ext = Path.GetExtension(list[at]), a = Path.Combine(dir, "a" + ext), b = Path.Combine(dir, "b" + ext);
            File.CreateSymbolicLink(a, list[at]);
            File.CreateSymbolicLink(b, list[at]);
            engine.Load([a, b]);
            engine.Open(0);
            Arrived(engine);
            window = Show(new SideBySidePanel());
            engine.Render(preview: false);
            bool hadFull = engine.Session?.Size(Graph.Full) is { Width: > 0 };
            Check(Commands.Run("compare-side-by-side") == "ran compare-side-by-side" && SideBySide.On, "C opens the side-by-side compare");
            var after = engine.Session?.Size(Graph.Full);
            Check(hadFull && after is { Width: 0 }, $"at fit the select's full graph is given back: both panes show previews (rendered {hadFull}, after {after})");
            var (l, r) = (SideBySide.Left, SideBySide.Right);
            Pump(() => r.Session != null && l.Preview != null && r.Preview != null, 60);
            window.UpdateLayout();
            l.View.ZoomBy(3, new Point(0.7, 0.3));   // the select's hand: the candidate follows
            l.View.Pan(new Vector(0.05, -0.03));
            window.UpdateLayout();
            Pump(() => l.Rendered != null && r.Rendered != null);
            Native.orion_engine_gpu_memory(l.Session?.Handle ?? 0, out ulong gpu);   // the process's one device: both engines
            double diff = Differ(l.Region, r.Region);
            Check(r.View.Zoom == l.View.Zoom && r.View.Center == l.View.Center && l.View.PastFit && l.RegionUv == r.RegionUv && diff < 0.5,
                  $"linked, a zoom and pan on one side shows the same region on both ({l.RegionUv} and {r.RegionUv}, " +
                  $"mean difference {diff:0.00}; GPU {gpu >> 20} MB, both engines)");
            Commands.Run("compare-link");
            var held = l.View.Center;
            r.View.Pan(new Vector(0.1, 0));
            Check(!SideBySide.Linked && l.View.Center == held && r.View.Center != held, "unlinked (L), a pan moves only its own side");
            SideBySide.Activate(r);
            int before = r.Photo?.Rating ?? -1, other = l.Photo?.Rating ?? -1;
            Commands.Run("rate-3");
            Check(r.Photo?.Rating == 3 && l.Photo?.Rating == other, "a rating key marks the active side alone");
            if (r.Photo != null) LibraryModel.Shared.SetRating(r.Photo, before);
            Commands.Run("compare-side-by-side");
            Check(!SideBySide.On && r.Session == null && engine.Shown.Any, "C again leaves it, the candidate handed back and the canvas's picture up");
        } finally {
            SideBySide.Exit();
            window?.Close();
            engine.Load(list);
            engine.Open(at);
            Arrived(engine);
            Directory.Delete(dir, recursive: true);
        }
    }

    /// Mean absolute difference per channel of two renders of one size; 255 when they cannot be compared.
    static unsafe double Differ(Bitmap? x, Bitmap? y) {
        if (x is not WriteableBitmap p || y is not WriteableBitmap q || p.PixelSize != q.PixelSize) return 255;
        using var fp = p.Lock();
        using var fq = q.Lock();
        double sum = 0;
        long n = 0;
        for (int row = 0; row < fp.Size.Height; row += 4)
            for (int x0 = 0; x0 < fp.Size.Width * 4; x0 += 4 * 4, n += 3)
                for (int k = 0; k < 3; k++)
                    sum += Math.Abs(((byte*)fp.Address)[(long)row * fp.RowBytes + x0 + k] - ((byte*)fq.Address)[(long)row * fq.RowBytes + x0 + k]);
        return n == 0 ? 255 : sum / n;
    }
}
