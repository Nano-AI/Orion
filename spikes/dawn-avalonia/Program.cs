// Dawn -> Avalonia with no CPU copy (#317 spike).
//   DawnAvalonia [--gl] [--frames N] [--shots DIR]
// Opens one window and runs two phases in it: the shared texture at the view's
// size, then at 6000x4000 (Avalonia scales it to the view). Each phase prints
// its timings; a screenshot of the window is taken mid-phase when --shots is
// given. Exits by itself, and after 10 s whatever happens.
// --gl uses Avalonia's OpenGL renderer (the shell's default on macOS); the
// default here is Metal.
using System.Diagnostics;
using System.Runtime.InteropServices;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Controls.ApplicationLifetimes;
using Avalonia.Media;
using Avalonia.Platform;
using Avalonia.Rendering.Composition;
using Avalonia.Themes.Fluent;
using Avalonia.Threading;

static class Native {
    const string Lib = "dawn_surface";
    [DllImport(Lib)] public static extern int ds_create(int width, int height);
    [DllImport(Lib)] public static extern IntPtr ds_error();
    [DllImport(Lib)] public static extern IntPtr ds_iosurface();
    [DllImport(Lib)] public static extern IntPtr ds_consumer_event();
    [DllImport(Lib)] public static extern int ds_frame(float t, ulong consumerValue, out IntPtr producerEvent, out ulong producerValue);
    [DllImport(Lib)] public static extern long ds_window_number(IntPtr nsWindow);
    [DllImport(Lib)] public static extern void ds_destroy();
    public static string Error => Marshal.PtrToStringUTF8(ds_error()) ?? "";
}

static class Program {
    public static bool Gl;
    public static int Frames = 100;
    public static string? Shots;
    public static int Exit = 1;

    [STAThread]
    static int Main(string[] args) {
        string? Value(string flag) => Array.IndexOf(args, flag) is var i and >= 0 && i + 1 < args.Length ? args[i + 1] : null;
        Gl = args.Contains("--gl");
        Frames = int.Parse(Value("--frames") ?? "100");
        Shots = Value("--shots") is { } d ? Path.GetFullPath(d) : null;
        AppBuilder.Configure<App>().UsePlatformDetect()
            .With(new AvaloniaNativePlatformOptions {
                RenderingMode = [Gl ? AvaloniaNativeRenderingMode.OpenGl : AvaloniaNativeRenderingMode.Metal]
            })
            .StartWithClassicDesktopLifetime(args);
        return Exit;
    }

    public static void Quit(int code) {
        Exit = code;
        ((IClassicDesktopStyleApplicationLifetime)Application.Current!.ApplicationLifetime!).Shutdown(code);
    }
}

sealed class App : Application {
    public override void Initialize() => Styles.Add(new FluentTheme());

    public override void OnFrameworkInitializationCompleted() {
        var label = new TextBlock { Margin = new Thickness(12), Padding = new Thickness(8, 4), FontSize = 18,
                                    Foreground = Brushes.White, Background = new SolidColorBrush(Color.FromArgb(170, 0, 0, 0)),
                                    HorizontalAlignment = Avalonia.Layout.HorizontalAlignment.Left,
                                    VerticalAlignment = Avalonia.Layout.VerticalAlignment.Top };
        var window = new Window {
            Title = "Dawn in Avalonia", Width = 960, Height = 600, CanResize = false,
            WindowStartupLocation = WindowStartupLocation.CenterScreen,
            // The label is ordinary Avalonia drawing composited over the Dawn picture.
            Content = new Panel { Children = { new DawnView(label), label } }
        };
        ((IClassicDesktopStyleApplicationLifetime)ApplicationLifetime!).MainWindow = window;
        window.Show();
        // Never outstays its welcome, even if a GPU wait never returns.
        DispatcherTimer.RunOnce(() => { Console.Error.WriteLine("timed out after 10 s"); Program.Quit(2); }, TimeSpan.FromSeconds(10));
    }
}

sealed class DawnView(TextBlock label) : Control {
    const string IOSurface = KnownPlatformGraphicsExternalImageHandleTypes.IOSurfaceRef;
    const string SharedEvent = KnownPlatformGraphicsExternalSemaphoreHandleTypes.MetalSharedEvent;
    const int Warmup = 20;
    CompositionSurfaceVisual? _visual;

    protected override void OnAttachedToVisualTree(VisualTreeAttachmentEventArgs e) {
        base.OnAttachedToVisualTree(e);
        Dispatcher.UIThread.Post(Run, DispatcherPriority.Background);   // after first layout: Bounds are known
    }

    protected override void OnSizeChanged(SizeChangedEventArgs e) {
        base.OnSizeChanged(e);
        if (_visual != null) _visual.Size = new(e.NewSize.Width, e.NewSize.Height);   // a tiling WM may resize us
    }

    async void Run() {
        var code = 1;
        try { code = await RunPhases(); }
        catch (Exception ex) { Console.Error.WriteLine(ex); }
        Program.Quit(code);
    }

    async Task<int> RunPhases() {
        var compositor = ElementComposition.GetElementVisual(this)!.Compositor;
        var surface = compositor.CreateDrawingSurface();
        _visual = compositor.CreateSurfaceVisual();
        _visual.Size = new(Bounds.Width, Bounds.Height);
        _visual.Surface = surface;
        ElementComposition.SetElementChildVisual(this, _visual);

        var interop = await compositor.TryGetCompositionGpuInterop();
        if (interop == null) { Console.Error.WriteLine("no GPU interop for this renderer"); return 1; }
        Console.WriteLine($"renderer   {(Program.Gl ? "OpenGL" : "Metal")}");
        Console.WriteLine($"images     {string.Join(", ", interop.SupportedImageHandleTypes)}");
        Console.WriteLine($"semaphores {string.Join(", ", interop.SupportedSemaphoreTypes)}");
        Console.WriteLine($"sync       {interop.GetSynchronizationCapabilities(IOSurface)}");

        var top = TopLevel.GetTopLevel(this)!;
        var window = top.TryGetPlatformHandle() is IMacOSTopLevelPlatformHandle mac ? Native.ds_window_number(mac.NSWindow) : 0;
        int w = (int)(Bounds.Width * top.RenderScaling), h = (int)(Bounds.Height * top.RenderScaling);
        foreach (var (pw, ph) in new[] { (w, h), (6000, 4000) })
            if (await Phase(interop, surface, pw, ph, window) != 0) return 1;
        return 0;
    }

    async Task<int> Phase(ICompositionGpuInterop interop, CompositionDrawingSurface surface, int w, int h, long window) {
        if (Native.ds_create(w, h) != 1) { Console.Error.WriteLine(Native.Error); return 1; }
        Console.WriteLine($"-- surface {w}x{h} BGRA8 IOSurface ({w * h * 4 / 1e6:F1} MB)");
        var image = interop.ImportImage(new PlatformHandle(Native.ds_iosurface(), IOSurface),
            new PlatformGraphicsExternalImageProperties {
                Width = w, Height = h, Format = PlatformGraphicsExternalImageFormat.B8G8R8A8UNorm, TopLeftOrigin = true
            });
        var consumer = interop.ImportSemaphore(new PlatformHandle(Native.ds_consumer_event(), SharedEvent));
        await image.ImportCompleted;
        await consumer.ImportCompleted;

        ICompositionImportedGpuSemaphore? producer = null;
        IntPtr producerHandle = 0;
        int imports = 0;
        var clock = Stopwatch.StartNew();
        var native = new List<double>(); var update = new List<double>(); var interval = new List<double>();
        TimeSpan cpu0 = default; long last = 0;

        for (ulong n = 0; n < (ulong)(Warmup + Program.Frames); n++) {
            if (n == Warmup) { cpu0 = Process.GetCurrentProcess().TotalProcessorTime; clock.Restart(); }
            long t0 = Stopwatch.GetTimestamp();
            // Dawn waits (GPU-side) for Avalonia to have read frame n-1, renders frame n.
            if (Native.ds_frame((float)clock.Elapsed.TotalSeconds, n, out var ev, out var value) != 1) {
                Console.Error.WriteLine(Native.Error); return 1;
            }
            long t1 = Stopwatch.GetTimestamp();
            if (ev != producerHandle) {   // Dawn's queue event; imported once unless Dawn changes it
                producer = interop.ImportSemaphore(new PlatformHandle(ev, SharedEvent));
                producerHandle = ev; imports++;
            }
            // Avalonia waits for Dawn's value, snapshots the image on the GPU, signals n+1.
            var shown = surface.UpdateWithTimelineSemaphoresAsync(image, producer!, value, consumer, n + 1);
            long t2 = Stopwatch.GetTimestamp();
            await shown;
            if (n >= Warmup) {
                native.Add(Stopwatch.GetElapsedTime(t0, t1).TotalMilliseconds);
                update.Add(Stopwatch.GetElapsedTime(t1, t2).TotalMilliseconds);
                if (n > Warmup) interval.Add(Stopwatch.GetElapsedTime(last, t0).TotalMilliseconds);
            }
            last = t0;
            if (n % 10 == 0) label.Text = $"{w}x{h}  frame {n}  t={clock.Elapsed.TotalSeconds:F2}s  {(Program.Gl ? "OpenGL" : "Metal")}";
            if (Program.Shots != null && window != 0 && n == (ulong)(Warmup + Program.Frames / 2)) {
                var cpuShot = Process.GetCurrentProcess().TotalProcessorTime;
                Shot(window, $"{w}x{h}");
                cpu0 += Process.GetCurrentProcess().TotalProcessorTime - cpuShot;   // our share of the shot is not a frame's
                last = Stopwatch.GetTimestamp();
            }
        }

        var frames = native.Count;
        var cpu = (Process.GetCurrentProcess().TotalProcessorTime - cpu0).TotalMilliseconds;
        string Stat(List<double> v) { v.Sort(); return $"mean {v.Average():F3} ms  p50 {v[v.Count / 2]:F3}  p95 {v[(int)(v.Count * 0.95)]:F3}"; }
        Console.WriteLine($"frames     {frames} in {clock.Elapsed.TotalSeconds:F2} s = {frames / clock.Elapsed.TotalSeconds:F1} fps");
        Console.WriteLine($"ds_frame   {Stat(native)}   (Dawn encode + submit + fence export, UI thread)");
        Console.WriteLine($"update     {Stat(update)}   (UpdateWithTimelineSemaphoresAsync call, UI thread)");
        Console.WriteLine($"interval   {Stat(interval)}");
        Console.WriteLine($"process    {cpu / frames:F3} ms CPU per frame, all threads");
        Console.WriteLine($"producer   imported {imports} time(s)");

        await image.DisposeAsync();
        await consumer.DisposeAsync();
        if (producer != null) await producer.DisposeAsync();
        Native.ds_destroy();
        return 0;
    }

    static void Shot(long window, string name) {
        Directory.CreateDirectory(Program.Shots!);
        var file = Path.Combine(Program.Shots!, $"{(Program.Gl ? "gl" : "metal")}-{name}.png");
        Process.Start("/usr/sbin/screencapture", ["-x", "-o", $"-l{window}", file])!.WaitForExit();
        Console.WriteLine($"shot       {file}");
    }
}
