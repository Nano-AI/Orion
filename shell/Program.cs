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

/// Orion <photos or folders> [--panel NAME | --tab NAME] [--command NAME] [--screenshot OUT.png] [--bench] [--selftest]
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

        // The command-line tools: the MCP server's verbs (`--agent`, #310) and the
        // two modes tools/check-modes.py runs. Their file arguments are made
        // absolute first, since the working directory moves below.
        bool tool = args.Contains("--agent") || args.Contains("--batch-export") || args.Contains("--hdr-merge");
        if (tool) {
            string[] values = ["--agent", "--max", "--region", "--rating", "--reject", "--kind", "--groups", "--mode"];
            args = args.Select((a, i) => a.StartsWith("--") || (i > 0 && values.Contains(args[i - 1])) ? a : Path.GetFullPath(a)).ToArray();
        }

        // The engine finds its compiled shaders relative to the CMake build
        // directory (engine/src/ResourcePaths.cpp). A .NET app host lives
        // nowhere near it, so run from there. Worktrees under .claude/ find
        // the main checkout's build by walking up.
        var dir = new DirectoryInfo(AppContext.BaseDirectory);
        while (dir != null && !File.Exists(Path.Combine(dir.FullName, "build", "CMakeCache.txt"))) dir = dir.Parent;
        if (dir != null) Directory.SetCurrentDirectory(Path.Combine(dir.FullName, "build"));

        if (tool) {
            // Headless and writing none of the photographer's settings (AppFolder.Writable).
            App.Run = App.Run with { Tool = true };
            AppBuilder.Configure<App>().UseSkia().UseHeadless(new AvaloniaHeadlessPlatformOptions { UseHeadlessDrawing = false })
                .SetupWithoutStarting();
            return args.Contains("--agent") ? Agent.AgentCli.Execute(args)
                 : args.Contains("--batch-export") ? CommandLineModes.BatchExport(args) : CommandLineModes.HdrMergeMode(args);
        }
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

sealed partial class App : Application {
    public sealed record Options(string[] Photos, string? Panel, string Tab, string? Screenshot, bool Bench, bool SelfTest,
                                 string? Command, string? Proof = null, bool Tool = false);
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
            // Before the first open, so the photo it arrives with is armed too. Interactive only:
            // the headless modes read sidecars and never write the photographer's.
            if (Run.Proof == null) {
                SettingsImport.Run();   // first: the presets and the assistant read their files once
                Autosave.Install();
                Library.LibraryModel.Shared.Writes = true;   // ratings and labels, as Library.persist
                ShellSettings.File = ShellSettings.UserFile;  // and the remembered sizes
            }
            OpenPhotos();   // the first photo arrives from a background decode
            desktop.MainWindow = MakeWindow() ?? throw new ArgumentException($"no panel '{Run.Panel}'");
            if (Run.Proof is { } dir) CanvasSlice.SharedProof.Start(desktop.MainWindow, dir);
            // Autosave.swift's other triggers: willResignActive, and willTerminate inside the 900 ms.
            desktop.MainWindow.Deactivated += (_, _) => Autosave.Shared.Flush();
            desktop.Exit += (_, _) => { Autosave.Shared.Flush(); Drain(); };
        }
        base.OnFrameworkInitializationCompleted();
    }

    // The headless modes (--selftest, --bench, --command, --screenshot) are
    // in Shared/Unattended.cs: the harness, which tools/check-wiring.py
    // does not count as a caller.

    static void Drain() => EngineModel.Shared.Cache.Drain();

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
}
