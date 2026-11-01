using System.Diagnostics;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Headless;
using Avalonia.Input;
using Avalonia.Threading;
using Avalonia.VisualTree;
using OrionShell.CanvasSlice;

namespace OrionShell;

/// `--bench --pan`: a hand panning the photo at 100%, through the pointer path a
/// drag takes, at 60 Hz, in a 3440 × 1440 window. Each frame: this thread's time
/// in the canvas's pointer move (the viewport and every panel that follows it),
/// in its draw, and in engine renders and CPU copies, which is what a window
/// would wait on (copy_ms is the part of those spent on CPU copies); the
/// headless frame's own rasterising (CPU Skia, a new
/// framebuffer each frame, #393) is not a product number and is left out.
///
/// One line per scenario and phase, for tools/bench.py:
///   bench pan <scenario> <phase> frames=N ui_median_ms=.. ui_p95_ms=.. ui_max_ms=..
///     move_ms=.. draw_ms=.. renders=N engine_ms=.. copies=N copy_ms=.. stalls=N interval_median_ms=..
/// scenario: region (an edit the region graph renders) or whole (one it refuses);
/// phase: drag (the hand moving), hold (button down, still, 300 ms), rest (a
/// second after the release). A stall is a frame whose UI work passed 16.7 ms.
/// interval_median_ms is the real time between frames, the headless raster
/// included: past 150 ms the view's rest timer can fire between two moves.
/// ORION_PAN_WINDOW=<w>x<h> sizes the window (3440x1440 by default);
/// ORION_PAN_CSV=<path> appends every frame; ORION_PAN_SHOTS=<dir> saves what
/// each pan left on screen and the same view rendered afresh, to compare.
sealed partial class App {
    static bool PanOnly => Environment.GetCommandLineArgs().Contains("--pan");

    sealed record Frame(int I, string Phase, double Start, double Move, double Draw, int Renders, double EngineMs, int Copies, double CopyMs) {
        // A copy's start is inside the render's time and its take inside the draw's: counted once.
        public double Ui => Move + Draw + EngineMs;
    }

    static void Pan(Window window) {
        var engine = EngineModel.Shared;
        if (engine.Handle == 0 || window.GetVisualDescendants().OfType<CanvasPanel>().FirstOrDefault() is not { } canvas) return;
        var vp = CanvasState.Viewport;
        // The developer's display, the window filling it, unless ORION_PAN_WINDOW=<w>x<h> says otherwise.
        var asked = Environment.GetEnvironmentVariable("ORION_PAN_WINDOW")?.Split('x');
        (window.Width, window.Height) = asked is [var w, var h] && double.TryParse(w, out var ww) && double.TryParse(h, out var hh) ? (ww, hh) : (3440, 1440);
        window.UpdateLayout();
        foreach (var (label, field, value) in new[] { ("region", "", 0f), ("whole", "shadows", 0.3f) }) {
            float was = field == "" ? 0 : engine.Get(field);
            if (field != "") engine.Set(field, value);
            vp.Reset();
            Idle(0.5);
            Commands.Run("actual-size");
            // An edit past fit renders what the scenario names (the whole graph a card had room
            // for at fit would otherwise stay, and cover every view), then the view at rest.
            engine.Set("exposure_ev", engine.Get("exposure_ev") + 0.01f);
            Idle(1.5);
            string? shots = Environment.GetEnvironmentVariable("ORION_PAN_SHOTS");
            Report(label, Hand(window, canvas, shots == null ? null : Path.Combine(shots, $"{label}-dragging.png")));
            if (shots is { } dir) {
                // What the pan left on screen, then the same view rendered afresh: the two must match.
                PanShot(window, Path.Combine(dir, $"{label}-rested.png"));
                engine.Render(preview: false);
                Idle(0.5);
                PanShot(window, Path.Combine(dir, $"{label}-fresh.png"));
            }
            vp.Reset();
            engine.Set("exposure_ev", engine.Get("exposure_ev") - 0.01f);
            if (field != "") engine.Set(field, was);
            Idle(0.5);
        }
    }

    /// The window at its own pixels, for the before-and-after comparison ORION_PAN_SHOTS asks for.
    static void PanShot(Window window, string path) {
        var size = new PixelSize((int)window.Bounds.Width, (int)window.Bounds.Height);
        using var rtb = new Avalonia.Media.Imaging.RenderTargetBitmap(size);
        rtb.Render(window);
        Directory.CreateDirectory(Path.GetDirectoryName(path)!);
        rtb.Save(path);
    }

    /// The dispatcher's own loop, with frames, for `seconds`: timers fire only there.
    static void Idle(double seconds) {
        var sw = Stopwatch.StartNew();
        while (sw.Elapsed.TotalSeconds < seconds) {
            AvaloniaHeadlessPlatform.ForceRenderTimerTick();
            using var slice = new CancellationTokenSource(TimeSpan.FromMilliseconds(16));
            Dispatcher.UIThread.MainLoop(slice.Token);
        }
    }

    /// One drag: 60 frames left (1440 px), back 360 and up 300: past the rendered margin, 18 frames (300 ms) held still with the button
    /// down, 60 frames up and right, the release, then 60 frames of what follows.
    static List<Frame> Hand(Window window, CanvasPanel canvas, string? shot) {
        var engine = EngineModel.Shared;
        var frames = new List<Frame>();
        double engineMs = 0;
        void Rendered() => engineMs += engine.Last.Render + engine.Last.Copy;
        engine.Rendered += Rendered;
        var size = canvas.Bounds.Size;
        var at = canvas.TranslatePoint(new Point(size.Width / 2, size.Height / 2), window)!.Value;
        const double Hz = 1000 / 60.0;
        window.MouseDown(at, MouseButton.Left);
        var clock = Stopwatch.StartNew();
        double due = 0;
        for (int i = 0; i < 198; i++) {
            string phase = i < 60 ? "drag" : i < 78 ? "hold" : i < 138 ? "drag" : "rest";
            int r0 = Session.Renders, c0 = CopiedPicture.Begun;
            double start = clock.Elapsed.TotalMilliseconds;
            double m0 = CanvasPanel.MoveMilliseconds, d0 = CanvasPanel.DrawMilliseconds, e0 = engineMs, k0 = CopiedPicture.Milliseconds;
            if (phase == "drag") { at += i < 60 ? new Vector(-24, 0) : new Vector(6, -5); window.MouseMove(at); }
            if (i == 138) window.MouseUp(at, MouseButton.Left);
            Dispatcher.UIThread.RunJobs();
            AvaloniaHeadlessPlatform.ForceRenderTimerTick();
            // The rest of the frame in the dispatcher's loop, where timers and posted work run, as in a window.
            // A frame that overran starts the next at once: the hand's time is real time.
            due = Math.Max(due + Hz, clock.Elapsed.TotalMilliseconds);
            double left = due - clock.Elapsed.TotalMilliseconds;
            using (var slice = new CancellationTokenSource(TimeSpan.FromMilliseconds(Math.Max(left, 1))))
                Dispatcher.UIThread.MainLoop(slice.Token);
            if (i == 59 && shot != null) PanShot(window, shot);   // mid-drag: what the hand uncovered
            frames.Add(new Frame(i, phase, start, CanvasPanel.MoveMilliseconds - m0, CanvasPanel.DrawMilliseconds - d0, Session.Renders - r0,
                                 engineMs - e0, CopiedPicture.Begun - c0, CopiedPicture.Milliseconds - k0));
        }
        engine.Rendered -= Rendered;
        return frames;
    }

    static void Report(string label, List<Frame> frames) {
        if (Environment.GetEnvironmentVariable("ORION_PAN_CSV") is { } path)
            File.AppendAllLines(path, frames.Select(f => $"{label},{f.I},{f.Phase},{f.Start:0.0},{f.Move:0.000},{f.Draw:0.000},{f.Renders},{f.EngineMs:0.0},{f.Copies},{f.CopyMs:0.00}"));
        foreach (var phase in new[] { "drag", "hold", "rest" }) {
            var f = frames.Where(x => x.Phase == phase).ToList();
            var ui = f.Select(x => x.Ui).Order().ToList();
            double Q(double q) => ui[(int)Math.Min(ui.Count - 1, ui.Count * q)];
            var gaps = f.Zip(f.Skip(1), (a, b) => b.Start - a.Start).Order().ToList();
            double gap = gaps.Count == 0 ? 0 : gaps[gaps.Count / 2];
            Console.WriteLine($"bench pan {label} {phase} frames={f.Count} ui_median_ms={Q(0.5):0.00} ui_p95_ms={Q(0.95):0.00} ui_max_ms={ui[^1]:0.0} " +
                              $"move_ms={f.Sum(x => x.Move):0.0} draw_ms={f.Sum(x => x.Draw):0.0} renders={f.Sum(x => x.Renders)} " +
                              $"engine_ms={f.Sum(x => x.EngineMs):0.0} copies={f.Sum(x => x.Copies)} copy_ms={f.Sum(x => x.CopyMs):0.0} " +
                              $"stalls={f.Count(x => x.Ui > 1000 / 60.0)} interval_median_ms={gap:0.0}");
        }
    }
}
