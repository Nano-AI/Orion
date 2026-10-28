using System.Diagnostics;
using System.Runtime.InteropServices;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Controls.ApplicationLifetimes;
using Avalonia.Headless;
using Avalonia.Media.Imaging;
using Avalonia.Styling;
using Avalonia.Themes.Fluent;
using Avalonia.Threading;

namespace OrionShell;

/// OrionShell <photos or folders> [--panel NAME | --tab NAME] [--command NAME] [--screenshot OUT.png] [--bench] [--selftest]
///            [--list | --commands] [--proof DIR]
static class Program {
    /// Library.rawExtensions (Library.swift): what counts as a raw, without the dot.
    public static readonly string[] RawExtensions = ["arw", "srf", "sr2", "dng", "nef", "nrw", "cr2", "cr3", "crw", "raf", "orf", "rw2",
                                                     "pef", "srw", "erf", "3fr", "fff", "iiq", "mrw", "raw", "rwl"];

    [STAThread]
    static int Main(string[] args) {
        // Paths first: the working directory changes below.
        string? Value(string flag) => Array.IndexOf(args, flag) is var i and >= 0 && i + 1 < args.Length ? args[i + 1] : null;
        var panel = Value("--panel");
        var shot = Value("--screenshot") is { } s ? Path.GetFullPath(s) : null;
        var proof = Value("--proof") is { } d ? Path.GetFullPath(d) : null;
        var photos = args.Where((a, i) => !a.StartsWith("--") && (i == 0 || args[i - 1] is not ("--panel" or "--screenshot" or "--tab" or "--command" or "--proof")))
                         .Select(Path.GetFullPath).SelectMany(Expand).ToArray();

        // The engine finds its compiled shaders relative to the CMake build
        // directory (engine/src/ResourcePaths.cpp). A .NET app host lives
        // nowhere near it, so run from there. Worktrees under .claude/ find
        // the main checkout's build by walking up.
        var dir = new DirectoryInfo(AppContext.BaseDirectory);
        while (dir != null && !File.Exists(Path.Combine(dir.FullName, "build", "CMakeCache.txt"))) dir = dir.Parent;
        if (dir != null) Directory.SetCurrentDirectory(Path.Combine(dir.FullName, "build"));

        if (args.Contains("--list")) {
            foreach (var e in Panels.All) Console.WriteLine($"{e.Name}\t{e.Region}\t{e.Type.Name}");
            return 0;
        }
        if (args.Contains("--commands")) {
            foreach (var c in Commands.All)
                Console.WriteLine($"{c.Name}\t{(c.Built ? "built" : "stub")}\t{c.Owner}\t{c.Shortcut}");
            return 0;
        }
        App.Run = new App.Options(photos, panel, Value("--tab") ?? "Light", shot, args.Contains("--bench"), args.Contains("--selftest"),
                                  Value("--command"), proof);

        // Anything that is not a person at the window runs headless: the macOS
        // backend cannot start while the display sleeps (RenderTimer -6661),
        // and swarm agents run unattended. Same Skia renderer, no window server.
        if (shot != null || App.Run.Bench || App.Run.SelfTest || App.Run.Command != null) {
            AppBuilder.Configure<App>().UseSkia()
                .UseHeadless(new AvaloniaHeadlessPlatformOptions { UseHeadlessDrawing = false })
                .SetupWithoutStarting();
            return App.RunUnattended();
        }
        // Metal first: Skia on Metal waits for the shared picture on the GPU, where
        // OpenGL waits on the CPU and cannot import it at all (SharedPicture, #354).
        return AppBuilder.Configure<App>().UsePlatformDetect()
            .With(new AvaloniaNativePlatformOptions {
                RenderingMode = [AvaloniaNativeRenderingMode.Metal, AvaloniaNativeRenderingMode.OpenGl, AvaloniaNativeRenderingMode.Software],
            })
            .StartWithClassicDesktopLifetime(args);
    }

    static IEnumerable<string> Expand(string path) => Directory.Exists(path)
        ? Directory.EnumerateFiles(path).Where(f => RawExtensions.Contains(Path.GetExtension(f).TrimStart('.').ToLowerInvariant())).Order()
        : [path];
}

sealed class App : Application {
    public sealed record Options(string[] Photos, string? Panel, string Tab, string? Screenshot, bool Bench, bool SelfTest,
                                 string? Command, string? Proof = null);
    public static Options Run = new([], null, "Light", null, false, false, null);

    /// The window MakeWindow built: what a command's dialog sits over.
    public static Window? Window { get; private set; }

    public override void Initialize() {
        Styles.Add(new FluentTheme());
        Styles.Add(new Theme());
        RequestedThemeVariant = ThemeVariant.Dark;
    }

    /// Interactive: a person at the window.
    public override void OnFrameworkInitializationCompleted() {
        if (ApplicationLifetime is IClassicDesktopStyleApplicationLifetime desktop) {
            OpenPhotos();   // the first photo arrives from a background decode
            desktop.MainWindow = MakeWindow() ?? throw new ArgumentException($"no panel '{Run.Panel}'");
            if (Run.Proof is { } dir) CanvasSlice.SharedProof.Start(desktop.MainWindow, dir);
            desktop.Exit += (_, _) => Drain();
        }
        base.OnFrameworkInitializationCompleted();
    }

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
    static void Drain() => EngineModel.Shared.Cache.Drain();

    /// Runs the UI thread's queue until `done`: background decodes post their results there.
    static void Pump(Func<bool> done, double seconds = 60) {
        var sw = Stopwatch.StartNew();
        while (!done() && sw.Elapsed.TotalSeconds < seconds) {
            Dispatcher.UIThread.RunJobs();
            Thread.Sleep(1);
        }
    }

    static void OpenPhotos() {
        EngineModel.Shared.Load(Run.Photos);
        if (EngineModel.Shared.Photos.Count > 0) EngineModel.Shared.Open(0);
    }

    static Window? MakeWindow() {
        if (Run.Panel is not { } name) return Window = new MainWindow(Run.Tab);
        if (Panels.Find(name) is { } entry) return Window = PanelWindow(entry);
        Console.Error.WriteLine($"no panel '{name}'. Registered: {string.Join(", ", Panels.All.Select(e => e.Name))}");
        return null;
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
        foreach (bool preview in new[] { true, false }) {
            var renders = new List<double>();
            var copies = new List<double>();
            if (preview) engine.BeginInteraction();
            for (int i = 0; i < 60; i++) {
                engine.Set("exposure_ev", -1 + 2 * i / 59f);
                if (i >= 5) { renders.Add(engine.Last.Render); copies.Add(engine.Last.Copy); }
            }
            engine.EndInteraction();
            renders.Sort(); copies.Sort();
            Console.WriteLine($"bench {(preview ? "preview" : "full   ")}: engine median {renders[renders.Count / 2]:0.0} ms " +
                              $"(p90 {renders[renders.Count * 9 / 10]:0.0}), copy median {copies[copies.Count / 2]:0.0} ms");
        }
        engine.Set("exposure_ev", start);
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
