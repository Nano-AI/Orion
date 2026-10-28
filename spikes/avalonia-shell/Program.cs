using System.Diagnostics;
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
///            [--list | --commands]
static class Program {
    static readonly string[] RawExtensions = [".arw", ".nef", ".cr2", ".cr3", ".dng", ".raf", ".orf", ".rw2", ".pef"];

    [STAThread]
    static int Main(string[] args) {
        // Paths first: the working directory changes below.
        string? Value(string flag) => Array.IndexOf(args, flag) is var i and >= 0 && i + 1 < args.Length ? args[i + 1] : null;
        var panel = Value("--panel");
        var shot = Value("--screenshot") is { } s ? Path.GetFullPath(s) : null;
        var photos = args.Where((a, i) => !a.StartsWith("--") && (i == 0 || args[i - 1] is not ("--panel" or "--screenshot" or "--tab" or "--command")))
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
                                  Value("--command"));

        // Anything that is not a person at the window runs headless: the macOS
        // backend cannot start while the display sleeps (RenderTimer -6661),
        // and swarm agents run unattended. Same Skia renderer, no window server.
        if (shot != null || App.Run.Bench || App.Run.SelfTest || App.Run.Command != null) {
            AppBuilder.Configure<App>().UseSkia().WithInterFont()
                .UseHeadless(new AvaloniaHeadlessPlatformOptions { UseHeadlessDrawing = false })
                .SetupWithoutStarting();
            return App.RunUnattended();
        }
        return AppBuilder.Configure<App>().UsePlatformDetect().WithInterFont().StartWithClassicDesktopLifetime(args);
    }

    static IEnumerable<string> Expand(string path) => Directory.Exists(path)
        ? Directory.EnumerateFiles(path).Where(f => RawExtensions.Contains(Path.GetExtension(f).ToLowerInvariant())).Order()
        : [path];
}

sealed class App : Application {
    public sealed record Options(string[] Photos, string? Panel, string Tab, string? Screenshot, bool Bench, bool SelfTest,
                                 string? Command);
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
            OpenPhotos();
            desktop.MainWindow = MakeWindow() ?? throw new ArgumentException($"no panel '{Run.Panel}'");
        }
        base.OnFrameworkInitializationCompleted();
    }

    /// Headless: --selftest, --bench, --command, --screenshot. Returns the exit code.
    public static int RunUnattended() {
        OpenPhotos();
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
                window.UpdateLayout();
                Dispatcher.UIThread.RunJobs();
                Shoot(window, Run.Screenshot);
            }
            window.Close();
            return exit;
        } finally {
            EngineModel.Shared.Close();
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
                Region.Filmstrip => Palette.FilmBase,
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

    /// A 60-tick exposure drag through the same path a hand takes.
    static void Bench(Window window) {
        var engine = EngineModel.Shared;
        if (engine.Handle == 0) return;
        // Swapping photos, laid out: what a click on the filmstrip costs.
        for (int i = 1; i <= Math.Min(engine.Photos.Count, 4); i++) {
            var sw = Stopwatch.StartNew();
            engine.Open(i % engine.Photos.Count);
            window.UpdateLayout();
            Dispatcher.UIThread.RunJobs();
            Console.WriteLine($"bench open {engine.CurrentName}: {sw.Elapsed.TotalMilliseconds:0} ms to laid out, footer engine {engine.Last.Render:0.0} ms");
        }
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
}
