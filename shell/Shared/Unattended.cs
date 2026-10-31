using System.Diagnostics;
using System.Runtime.InteropServices;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Media.Imaging;
using Avalonia.Threading;

namespace OrionShell;

/// The headless harness: --selftest, --bench, --command, --screenshot and the
/// --panel posing window. Its own file so tools/check-wiring.py can count it as
/// harness: a mechanism only these call is #181's shape (decision #380).
sealed partial class App {
    /// Headless: --selftest, --bench, --command, --screenshot. Returns the exit code.
    public static int RunUnattended() {
        OpenPhotos();
        Pump(() => !Opening);
        try {
            if (Run.SelfTest) return SelfTest.Run() == 0 ? 0 : 1;
            var window = MakeWindow();
            if (window == null) return 2;
            window.Show();
            window.UpdateLayout();
            Dispatcher.UIThread.RunJobs();
            if (Run.Bench) Bench(window);
            int exit = 0;
            if (Run.Command is { } command) {
                // Inside the laid-out window, so a command that needs it (a tab, a sheet) has one.
                string said = Commands.Run(command);
                Dispatcher.UIThread.RunJobs();
                Console.WriteLine(said + (said.StartsWith("ran ") ? $" · photo {EngineModel.Shared.CurrentName}" : ""));
                exit = said.StartsWith("ran ") ? 0 : 1;
            }
            if (Run.Screenshot != null) {
                Pump(() => Library.StackSuggestion.Scanned.IsCompleted, 10);   // the folder's stack line, posted when its scan lands
                Dispatcher.UIThread.RunJobs();                                 // and shown
                EngineModel.Shared.Render(preview: false);   // a still is for looking at, not for speed
                window.UpdateLayout();
                Dispatcher.UIThread.RunJobs();
                Shoot(window, Run.Screenshot);
            }
            window.Close();
            return exit;
        } finally {
            EngineModel.Shared.Close();
            Drain();
        }
    }

    // The bench's only reach into the cache, so a baseline build swaps four lines.
    static bool Opening => EngineModel.Shared.Opening;
    static bool Busy => EngineModel.Shared.Cache.Busy;
    static string Kept => $"{EngineModel.Shared.Cache.Sessions} sessions + {EngineModel.Shared.Cache.Pictures} pictures kept";

    /// Runs the UI thread's queue until `done`: background decodes post their results there.
    static void Pump(Func<bool> done, double seconds = 60) {
        var sw = Stopwatch.StartNew();
        while (!done() && sw.Elapsed.TotalSeconds < seconds) {
            Dispatcher.UIThread.RunJobs();
            Thread.Sleep(1);
        }
    }

    /// One panel alone, at its region's size, for comparing with a Swift scene.
    /// A canvas overlay is drawn over the canvas so it has a picture under it.
    static Window PanelWindow(Panels.Entry entry) {
        Control body = Panels.Create(entry.Type);
        if (entry.Region == Region.Canvas && entry.Name != "canvas" && Panels.Find("canvas") is { } canvas)
            body = new Grid { Children = { Panels.Create(canvas.Type), body } };
        var window = new Window {
            Title = entry.Name,
            Background = entry.Region switch {
                Region.Tools or Region.Status or Region.Toolbar or Region.Sheet => Palette.Panel,
                Region.Filmstrip => Palette.Panel,
                _ => Palette.Surround,
            },
        };
        // The frame, not the window, is the screenshot: macOS sizes windows
        // as it likes, and Width is ignored once SizeToContent is set.
        var frame = new Border {
            Background = window.Background,
            Child = body,
            VerticalAlignment = Avalonia.Layout.VerticalAlignment.Top,
            HorizontalAlignment = Avalonia.Layout.HorizontalAlignment.Left,
        };
        (frame.Width, frame.Height) = entry.Region switch {
            Region.Tools or Region.Status => (Layout.ToolCol, double.NaN),   // as tall as the panel
            Region.Toolbar => (1440, Layout.Toolbar),
            Region.Filmstrip => (1440, Layout.Filmstrip),
            _ => (1100, 800),
        };
        if (entry.Region == Region.Tools) frame.Padding = new Thickness(Space.Pad, 0, Space.Pad, Space.Pad);
        window.Content = new ScrollViewer { Content = frame };
        Target = frame;
        return window;
    }

    /// What --screenshot captures: the panel's frame, or the whole window.
    static Visual? Target;

    static void Shoot(Window window, string path) {
        var target = Target ?? window;
        double scale = 2;   // Retina, like the Swift references; headless reports 1
        var size = new PixelSize((int)(target.Bounds.Width * scale), (int)(target.Bounds.Height * scale));
        using var rtb = new RenderTargetBitmap(size, new Vector(96 * scale, 96 * scale));
        rtb.Render(target);
        Directory.CreateDirectory(Path.GetDirectoryName(path)!);
        rtb.Save(path, new PngBitmapEncoderOptions());
        Console.WriteLine($"screenshot {path}");
    }

    /// Photo swaps, a held-arrow burst and memory over the photos given (pass
    /// the same RAWs several times for a list long enough to cache), then a
    /// 60-tick exposure drag through the same path a hand takes.
    static void Bench(Window window) {
        CacheReplay.Print();
        var engine = EngineModel.Shared;
        if (engine.Handle == 0) return;
        int n = engine.Photos.Count;
        void Settle() => Pump(() => !Opening && !Busy);
        // To laid out: the session active, its picture rendered, the window laid out.
        // `returned` collects how long the step itself took: the engine and the panels' Opened, no layout.
        var returned = new List<double>();
        double Swap(Func<bool> step) {
            var sw = Stopwatch.StartNew();
            step();
            returned.Add(sw.Elapsed.TotalMilliseconds);
            Pump(() => !Opening);
            window.UpdateLayout();
            Dispatcher.UIThread.RunJobs();
            return sw.Elapsed.TotalMilliseconds;
        }
        static string Ms(List<double> t) => $"{string.Join(", ", t.Select(x => $"{x:0}"))} ms (median {t.Order().ElementAt(t.Count / 2):0})";

        Settle();
        Console.WriteLine($"bench list: {n} photos · settled at photo {engine.Current} · {Kept} · {Memory()}");
        // A neighbour after a pause long enough for the cache to fill: the arrow key at reading speed.
        var neighbour = new List<double>();
        for (int i = 0; i < Math.Min(5, n - 1); i++) { neighbour.Add(Swap(engine.Next)); Settle(); }
        Console.WriteLine($"bench neighbour swap (cached): {Ms(neighbour)} to laid out; Next returned in {Ms(returned)}; " +
                          $"last engine {engine.Last.Render:0.0} ms + copy {engine.Last.Copy:0.0} ms");

        // A jump past both tiers: nothing kept there.
        var cold = new List<double>();
        foreach (int target in new[] { engine.Current + n / 2, engine.Current }) {
            if (target >= n) continue;
            cold.Add(Swap(() => engine.Open(target)));
            Settle();
        }
        Console.WriteLine($"bench cold swap: {Ms(cold)} to laid out");

        // Held arrow: ten presses 30 ms apart, each on time or as soon as the last one returns.
        int pictured = 0;
        var clock = Stopwatch.StartNew();
        for (int press = 0; press < 10; press++) {
            while (clock.Elapsed.TotalMilliseconds < press * 30) { Dispatcher.UIThread.RunJobs(); Thread.Sleep(1); }
            engine.Next();
            if (engine.Shown.Bitmap != null) pictured++;
        }
        double pressed = clock.Elapsed.TotalMilliseconds;
        Pump(() => !Opening);
        window.UpdateLayout();
        Dispatcher.UIThread.RunJobs();
        Console.WriteLine($"bench burst 10 × 30 ms to photo {engine.Current}: shown {clock.Elapsed.TotalMilliseconds - 270:0} ms after the last press was due " +
                          $"(the presses took {pressed:0} ms; {pictured}/10 put a picture up at once)");

        // Back and forth across the list at 60 ms a press, watching memory.
        Settle();
        double peak = 0;
        foreach (int dir in new[] { 1, -1, 1, -1 })
            for (int i = 0; i < Math.Min(15, n - 1); i++) {
                if (dir > 0) engine.Next(); else engine.Previous();
                var until = Stopwatch.StartNew();
                while (until.ElapsedMilliseconds < 60) { Dispatcher.UIThread.RunJobs(); Thread.Sleep(1); }
                peak = Math.Max(peak, Footprint().Footprint);
            }
        Settle();
        Console.WriteLine($"bench memory after browsing back and forth: {Memory()}, peak footprint {peak:0} MB · {Kept}");

        float start = engine.Get("exposure_ev");
        foreach (bool preview in new[] { true, false }) Drag(engine, preview);
        engine.Set("exposure_ev", start);
    }

    /// 60 ticks of an exposure drag, each followed by the frame the canvas draws
    /// (a forced headless render tick): the engine's time, this thread's time
    /// copying the picture, process CPU (all threads) and how many renders the
    /// drawn picture trails the engine (#386).
    static void Drag(EngineModel engine, bool preview) {
        var renders = new List<double>();
        var copies = new List<double>();
        int behind = 0, behindMax = 0;
        TimeSpan cpu0 = default;
        if (preview) engine.BeginInteraction();
        for (int i = 0; i < 60; i++) {
            if (i == 5) cpu0 = Process.GetCurrentProcess().TotalProcessorTime;
            double copied = CanvasSlice.CopiedPicture.Milliseconds;
            engine.Set("exposure_ev", -1 + 2 * i / 59f);
            Dispatcher.UIThread.RunJobs();
            Avalonia.Headless.AvaloniaHeadlessPlatform.ForceRenderTimerTick();
            Dispatcher.UIThread.RunJobs();
            if (i < 5) continue;
            renders.Add(engine.Last.Render);
            copies.Add(engine.Last.Copy + CanvasSlice.CopiedPicture.Milliseconds - copied);
            behind += CanvasSlice.CopiedPicture.Behind;
            behindMax = Math.Max(behindMax, CanvasSlice.CopiedPicture.Behind);
        }
        double cpu = (Process.GetCurrentProcess().TotalProcessorTime - cpu0).TotalMilliseconds;
        var shown = engine.Session?.Size(engine.Shown.Graph);
        string Shown = $"{engine.Shown.Graph} {shown?.Width}×{shown?.Height}";
        engine.EndInteraction();
        renders.Sort(); copies.Sort();
        int n = renders.Count;
        Console.WriteLine($"bench {(preview ? "preview" : "full   ")}: {Shown} drawn at {CanvasSlice.CopiedPicture.Size} · engine median {renders[n / 2]:0.0} ms " +
                          $"(p90 {renders[n * 9 / 10]:0.0}), copy median {copies[n / 2]:0.00} ms (p90 {copies[n * 9 / 10]:0.00}) · " +
                          $"process CPU {cpu / n:0.0} ms a frame · canvas behind the engine: mean {(double)behind / n:0.0}, " +
                          $"max {behindMax} renders · copies begun {CanvasSlice.CopiedPicture.Begun}, taken {CanvasSlice.CopiedPicture.Taken}");
    }

    [DllImport("libc")]
    static extern int proc_pid_rusage(int pid, int flavor, byte[] buffer);

    /// rusage_info_v0: resident_size at byte 64, phys_footprint (what Activity Monitor calls memory, GPU buffers included) at 72.
    static (double Rss, double Footprint) Footprint() {
        var info = new byte[256];
        proc_pid_rusage(Environment.ProcessId, 0, info);
        return (BitConverter.ToUInt64(info, 64) / 1048576.0, BitConverter.ToUInt64(info, 72) / 1048576.0);
    }

    static string Memory() => Footprint() is var (rss, footprint) ? $"RSS {rss:0} MB, footprint {footprint:0} MB" : "";
}
