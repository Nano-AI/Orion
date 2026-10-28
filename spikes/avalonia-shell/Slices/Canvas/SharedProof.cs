using System.Diagnostics;
using System.Runtime.InteropServices;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Controls.ApplicationLifetimes;
using Avalonia.Media.Imaging;
using Avalonia.Platform;
using Avalonia.Threading;
using Avalonia.VisualTree;

namespace OrionShell.CanvasSlice;

/// `--proof DIR`: the windowed proof that the canvas shows the shared surface (#317).
/// ⚠ Opens one window. Once the photo is on the shared surface, it drags exposure
/// for 45 frames through the shared surface, then 45 through the CPU copy; each
/// phase prints process CPU per frame (all threads) and the UI thread's time per
/// tick, then screenshots the window (screencapture -l) at the starting exposure.
/// The two shots' canvas centres must agree. Exits by itself: 0 when the shared
/// surface held every frame and the shots agree; 2 at 6 s, and 3 at 8 s if the UI thread is stuck.
static partial class SharedProof {
    const int Warmup = 5, Frames = 45;

    public static void Start(Window window, string dir) {
        DispatcherTimer.RunOnce(() => Quit(2, "timed out after 6 s"), TimeSpan.FromSeconds(6));
        // And off the UI thread, in case a GPU wait has it stuck: the window must never outstay.
        new Thread(() => { Thread.Sleep(8000); Console.Error.WriteLine("proof: killed after 8 s"); Environment.Exit(3); }) { IsBackground = true }.Start();
        window.Opened += async (_, _) => {
            int code = 1;
            try { code = await Run(window, dir); } catch (Exception x) { Console.Error.WriteLine(x); }
            Quit(code, null);
        };
    }

    static void Quit(int code, string? why) {
        if (why != null) Console.Error.WriteLine($"proof: {why}");
        ((IClassicDesktopStyleApplicationLifetime)Application.Current!.ApplicationLifetime!).Shutdown(code);
    }

    static async Task<int> Run(Window window, string dir) {
        var engine = EngineModel.Shared;
        // Decoded, rendered full (the 300 ms rest), and handed over; or given up on.
        await Until(() => engine.Session != null && engine.Shown.Graph == Graph.Full && SharedPicture.Showing || SharedPicture.Failed, 3);
        Console.WriteLine($"proof: {engine.CurrentName} · shared surface " +
                          (SharedPicture.Failed ? "failed (see above)" : SharedPicture.Showing ? "holds it" : "never held it"));
        if (!SharedPicture.Showing) return 1;
        var shared = await Phase(window, dir, "shared");
        engine.Shown.OnGpu = false;
        var copy = await Phase(window, dir, "copy");
        engine.Shown.OnGpu = true;
        double diff = Difference(window, shared.Shot, copy.Shot);
        Console.WriteLine($"proof: canvas centre, shared shot against copy shot: mean difference {diff:0.00} of 255 · {SharedPicture.Presented} hand-offs");
        return shared.Held == Frames && copy.Held == 0 && diff < 3 ? 0 : 1;
    }

    static async Task<(int Held, string Shot)> Phase(Window window, string dir, string name) {
        var engine = EngineModel.Shared;
        float start = engine.Get("exposure_ev");
        var ticks = new List<double>();
        int held = 0;
        TimeSpan cpu0 = default;
        var clock = new Stopwatch();
        engine.BeginInteraction();
        for (int i = 0; i < Warmup + Frames; i++) {
            await Frame(window);
            if (i == Warmup) { cpu0 = Process.GetCurrentProcess().TotalProcessorTime; clock.Start(); }
            var tick = Stopwatch.StartNew();
            engine.Set("exposure_ev", start + 0.5f * MathF.Sin(i * 0.3f));
            if (i < Warmup) continue;
            ticks.Add(tick.Elapsed.TotalMilliseconds);
            if (SharedPicture.Showing) held++;
        }
        await Frame(window);
        double cpu = (Process.GetCurrentProcess().TotalProcessorTime - cpu0).TotalMilliseconds;
        double seconds = clock.Elapsed.TotalSeconds;
        engine.Set("exposure_ev", start);   // both shots show the same picture
        engine.EndInteraction();
        for (int i = 0; i < 8; i++) await Frame(window);
        string shot = Shot(window, dir, name);
        ticks.Sort();
        Console.WriteLine($"proof {name,-6}: {Frames} frames in {seconds:0.00} s · process CPU {cpu / Frames:0.00} ms per frame, all threads · " +
                          $"Set() on the UI thread median {ticks[Frames / 2]:0.00} ms, p95 {ticks[Frames * 95 / 100]:0.00} · " +
                          $"shared surface held {held}/{Frames} · {shot}");
        return (held, shot);
    }

    static Task Frame(TopLevel top) {
        var done = new TaskCompletionSource();
        top.RequestAnimationFrame(_ => done.TrySetResult());
        return done.Task;
    }

    static async Task Until(Func<bool> done, double seconds) {
        var sw = Stopwatch.StartNew();
        while (!done() && sw.Elapsed.TotalSeconds < seconds) await Task.Delay(16);
    }

    static string Shot(Window window, string dir, string name) {
        if (window.TryGetPlatformHandle() is not IMacOSTopLevelPlatformHandle mac) return "";
        Directory.CreateDirectory(dir);
        string file = Path.Combine(dir, $"canvas-{name}.png");
        nint id = Send(mac.NSWindow, sel_registerName("windowNumber"));
        Process.Start("/usr/sbin/screencapture", ["-x", "-o", $"-l{id}", file])!.WaitForExit();
        return File.Exists(file) ? file : "";
    }

    /// Mean absolute difference, of 255, over a square at the canvas's centre. The
    /// shot is the window's frame, so the client area is its bottom part.
    static unsafe double Difference(Window window, string a, string b) {
        var canvas = window.GetVisualDescendants().OfType<CanvasPanel>().FirstOrDefault();
        if (canvas == null || a == "" || b == "") return double.MaxValue;
        var centre = canvas.TranslatePoint(new Point(canvas.Bounds.Width / 2, canvas.Bounds.Height / 2), window) ?? default;
        using var x = new Bitmap(a);
        using var y = new Bitmap(b);
        double scale = x.PixelSize.Width / window.ClientSize.Width;
        int r = (int)(Math.Min(canvas.Bounds.Width, canvas.Bounds.Height) * scale / 8);
        var rect = new PixelRect((int)(centre.X * scale) - r, (int)(x.PixelSize.Height - (window.ClientSize.Height - centre.Y) * scale) - r, 2 * r, 2 * r);
        if (x.PixelSize != y.PixelSize || r <= 0 || !new PixelRect(x.PixelSize).Contains(rect)) return double.MaxValue;
        var (pa, pb) = (new byte[rect.Width * rect.Height * 4], new byte[rect.Width * rect.Height * 4]);
        fixed (byte* p = pa) x.CopyPixels(rect, (nint)p, pa.Length, rect.Width * 4);
        fixed (byte* p = pb) y.CopyPixels(rect, (nint)p, pb.Length, rect.Width * 4);
        return pa.Zip(pb, (u, v) => Math.Abs(u - v)).Average();
    }

    const string ObjC = "/usr/lib/libobjc.A.dylib";
    [LibraryImport(ObjC, StringMarshalling = StringMarshalling.Utf8)] private static partial nint sel_registerName(string name);
    [LibraryImport(ObjC, EntryPoint = "objc_msgSend")] private static partial nint Send(nint receiver, nint selector);
}
