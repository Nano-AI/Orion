using System.Diagnostics;
using System.Globalization;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Threading;
using OrionShell.CanvasSlice;

namespace OrionShell;

/// tools/bench.py's scenarios (decision #402). `Orion <photos> --bench` with
/// ORION_BENCH_SCENARIO set runs one scenario the way a person meets it and
/// prints one line, `bench-json {...}`, for the tool to read: a scenario is one
/// sample, and the tool repeats processes, not loops, so every sample starts as
/// cold as a launch.
///
/// Harness only (tools/check-wiring.py counts this file as harness): the
/// product carries no instrumentation for it. Everything is read from outside,
/// by polling the state the canvas draws from, on the UI thread, between passes
/// of its queue.
///
/// ⚠ Headless renders a frame on the UI thread (`ForceRenderTimerTick`), where
/// the desktop app renders on its own thread. So a stall here includes one
/// headless frame; `frame_idle_ms` is what that frame costs with nothing
/// happening, the floor every stall number sits on.
sealed partial class App {
    static string? BenchScenario => Environment.GetEnvironmentVariable("ORION_BENCH_SCENARIO");

    /// Milliseconds since tools/bench.py started this process (ORION_BENCH_T0_NS,
    /// Unix nanoseconds), so a cold open counts the runtime's own start; since
    /// this process began, without it.
    static double SinceLaunch() {
        long now = (DateTime.UtcNow - DateTime.UnixEpoch).Ticks * 100;
        if (long.TryParse(Environment.GetEnvironmentVariable("ORION_BENCH_T0_NS"), out long t0)) return (now - t0) / 1e6;
        return (DateTime.Now - Process.GetCurrentProcess().StartTime).TotalMilliseconds;
    }

    /// Runs one scenario; 0 when it ran, whatever it measured. The verdict is the tool's.
    static int RunScenario(string name) {
        var m = new Dictionary<string, double>();
        Window? window = null;
        int exit = 0;
        try {
            window = OpenFolder(m, settle: name is "open");
            if (window == null) throw new InvalidOperationException("no window");
            if (EngineModel.Shared.Session == null && name is not "open") throw new InvalidOperationException("the first photo did not open");
            switch (name) {
                case "open": break;
                case "swap": SwapScenario(window, m); break;
                case "drag": DragScenario(m, preview: true); DragScenario(m, preview: false); break;
                case "drag-heavy": HeavyDragScenario(m); break;
                case "render": RenderScenario(m); break;
                case "export": ExportScenario(m); break;
                case "side-by-side": SideBySideScenario(m); break;
                default: throw new ArgumentException($"no bench scenario '{name}' (open, swap, drag, drag-heavy, render, export, side-by-side; the pan is --bench --pan, Unattended.Pan.cs)");
            }
            Native.orion_engine_gpu_memory(EngineModel.Shared.Handle, out ulong gpu);
            m["gpu_engine_mib"] = gpu / 1048576.0;
            m["rss_mib"] = Environment.WorkingSet / 1048576.0;
        } catch (Exception e) {
            Console.Error.WriteLine($"bench {name}: {e.Message}");
            exit = 1;
        } finally {
            window?.Close();
            EngineModel.Shared.Close();
            Drain();
        }
        string body = string.Join(",", m.Select(p => $"\"{p.Key}\":{p.Value.ToString("0.###", CultureInfo.InvariantCulture)}"));
        Console.WriteLine($"bench-json {{\"scenario\":\"{name}\",\"ok\":{(exit == 0 ? "true" : "false")},\"metrics\":{{{body}}}}}");
        return exit;
    }

    /// The UI thread's loop, run as the desktop runs it: a frame every 16 ms
    /// and the dispatcher's own loop in 4 ms slices between them. The loop,
    /// not RunJobs: DispatcherTimers (the pan's rest render, animation pacing)
    /// fire only there (SelfTest.CanvasUx's Frames found it). A pass longer than
    /// its slice is a job that held the thread, and that excess is the stall:
    /// how long input would have waited.
    sealed class Heartbeat {
        const double Slice = 4;
        readonly Stopwatch _frame = Stopwatch.StartNew();
        public double Worst, Frozen;
        public int Long;

        /// One pass; the milliseconds it held the thread past its slice.
        public double Pass() {
            var sw = Stopwatch.StartNew();
            if (_frame.Elapsed.TotalMilliseconds >= 16) { _frame.Restart(); Avalonia.Headless.AvaloniaHeadlessPlatform.ForceRenderTimerTick(); }
            using (var slice = new CancellationTokenSource(TimeSpan.FromMilliseconds(Slice))) Dispatcher.UIThread.MainLoop(slice.Token);
            double ms = Math.Max(0, sw.Elapsed.TotalMilliseconds - Slice);
            Worst = Math.Max(Worst, ms);
            if (ms > 50) { Long++; Frozen += ms; }
            return ms;
        }

        /// A frame now: what the canvas draws next.
        public static void Frame() {
            Avalonia.Headless.AvaloniaHeadlessPlatform.ForceRenderTimerTick();
            Dispatcher.UIThread.RunJobs();
        }

        /// Passes until `done` or `seconds`; false on the timeout.
        public bool Until(Func<bool> done, double seconds) {
            var sw = Stopwatch.StartNew();
            while (!done()) {
                if (sw.Elapsed.TotalSeconds > seconds) return false;
                Pass();
            }
            return true;
        }
    }

    /// This photo's own picture is what the canvas shows: not a neighbour's, not a blank.
    static bool Pictured() {
        var e = EngineModel.Shared;
        return e.Current >= 0 && !e.Opening && e.Shown.Any && CanvasState.ShownFor == e.Current;
    }

    /// The folder opens as App.OnFrameworkInitializationCompleted opens it: the
    /// list, the first photo's open begun, the window shown before its decode
    /// lands. Then the queue runs until the picture is up and, with `settle`,
    /// until the background work is done and the thread has been quiet.
    static Window? OpenFolder(Dictionary<string, double> m, bool settle) {
        var sw = Stopwatch.StartNew();
        OpenPhotos();
        m["load_ms"] = sw.Elapsed.TotalMilliseconds;
        sw.Restart();
        var window = MakeWindow();
        if (window == null) return null;
        (window.Width, window.Height) = (1440, 900);   // one laptop-sized window, so every run lays out the same filmstrip
        window.Show();
        window.UpdateLayout();
        Dispatcher.UIThread.RunJobs();
        m["window_ms"] = sw.Elapsed.TotalMilliseconds;
        m["responsive_ms"] = SinceLaunch();
        m["photos"] = EngineModel.Shared.Photos.Count;

        var beat = new Heartbeat();
        if (beat.Until(Pictured, 120)) {
            Heartbeat.Frame();
            m["first_picture_ms"] = SinceLaunch();
        }
        if (settle) {
            // Settled: nothing decoding, the marks read, and no pass over 50 ms for 1.5 s.
            var quiet = Stopwatch.StartNew();
            var all = Stopwatch.StartNew();
            bool settled = false;
            while (!settled && all.Elapsed.TotalSeconds < 180) {
                double ms = beat.Pass();
                if (ms > 50 || EngineModel.Shared.Opening || Busy || !Library.LibraryModel.Shared.MarksRead.IsCompleted) quiet.Restart();
                else settled = quiet.ElapsedMilliseconds >= 1500;
            }
            if (!settled)
                Console.Error.WriteLine($"bench open: not settled in 180 s: opening {EngineModel.Shared.Opening}, cache busy {Busy}, " +
                                        $"marks read {Library.LibraryModel.Shared.MarksRead.IsCompleted}, {beat.Long} passes over 50 ms ({Kept})");
            m["settled_ms"] = SinceLaunch() - (settled ? quiet.Elapsed.TotalMilliseconds : 0);
            m["timed_out_count"] = settled ? 0 : 1;
            // The open itself is the first stall: the list and the window are built on this
            // thread with nothing else let in. On a 336-photo folder that is where the time goes.
            m["worst_stall_ms"] = Math.Max(beat.Worst, m["load_ms"] + m["window_ms"]);
            m["stalls_over_50ms_count"] = beat.Long;
            m["frozen_ms"] = beat.Frozen;
            m["frame_idle_ms"] = IdleFrame();
        }
        return window;
    }

    /// The cost of one headless frame with nothing to do: the floor under every pass.
    static double IdleFrame() {
        var t = new List<double>();
        for (int i = 0; i < 9; i++) {
            var sw = Stopwatch.StartNew();
            Heartbeat.Frame();
            t.Add(sw.Elapsed.TotalMilliseconds);
            Thread.Sleep(2);
        }
        return Median(t);
    }

    static double Median(List<double> v) {
        if (v.Count == 0) return double.NaN;
        var s = v.Order().ToList();
        return s.Count % 2 == 1 ? s[s.Count / 2] : (s[s.Count / 2 - 1] + s[s.Count / 2]) / 2;
    }

    static double Quantile(List<double> v, double q) {
        if (v.Count == 0) return double.NaN;
        var s = v.Order().ToList();
        return s[Math.Clamp((int)Math.Ceiling(q * s.Count) - 1, 0, s.Count - 1)];
    }

    /// Waits out the background (decodes, the rest render) so the next step starts from rest.
    static void Rest(Heartbeat beat) {
        var still = Stopwatch.StartNew();
        beat.Until(() => {
            if (EngineModel.Shared.Opening || Busy) still.Restart();
            return still.ElapsedMilliseconds > 400;
        }, 60);
    }

    /// A neighbour at reading speed (the cache has had time to fill) and two
    /// jumps of half the list, each until its own picture is drawn. tools/bench.py
    /// passes 24 or more photos, so half the list is past both tiers (#318: about
    /// ten rendered either side).
    static void SwapScenario(Window window, Dictionary<string, double> m) {
        var engine = EngineModel.Shared;
        var beat = new Heartbeat();
        int n = engine.Photos.Count;
        double Swap(Func<bool> step) {
            var sw = Stopwatch.StartNew();
            step();
            beat.Until(Pictured, 60);
            Heartbeat.Frame();
            return sw.Elapsed.TotalMilliseconds;
        }
        Rest(beat);
        var neighbour = new List<double>();
        for (int i = 0; i < Math.Min(5, n - 1); i++) { neighbour.Add(Swap(engine.Next)); Rest(beat); }
        var jump = new List<double>();
        for (int i = 0; i < 2 && n > 2; i++) {
            int target = (engine.Current + n / 2) % n;
            jump.Add(Swap(() => engine.Open(target)));
            Rest(beat);
        }
        if (neighbour.Count > 0) m["swap_neighbour_ms"] = Median(neighbour);
        if (jump.Count > 0) m["swap_jump_ms"] = Median(jump);
    }

    /// 60 ticks of a slider drag (exposure unless told), each the frame a hand would see: the
    /// write and its render, the queue, then the canvas's frame. The first five
    /// warm up. `ui` is the product's own work on the UI thread, as the pan
    /// counts it: the write with its render and every Rendered handler, and the
    /// canvas's draw. `frame` is the whole tick, the headless raster included
    /// (see the note at the top): that raster also runs inside RunJobs, from the
    /// headless render timer's own DispatcherTimer, once a tick passes 16 ms.
    /// `raster`, frame less ui, is a reading only: it is no window's cost.
    static void DragScenario(Dictionary<string, double> m, bool preview, string field = "exposure_ev",
                             float from = -1, float to = 1, string key = "drag") {
        var engine = EngineModel.Shared;
        float start = engine.Get(field);
        var ui = new List<double>();
        var frames = new List<double>();
        var renders = new List<double>();
        if (preview) engine.BeginInteraction();
        for (int i = 0; i < 60; i++) {
            double drawn = CanvasPanel.DrawMilliseconds;
            var sw = Stopwatch.StartNew();
            engine.Set(field, from + (to - from) * i / 59f);
            double set = sw.Elapsed.TotalMilliseconds;
            Dispatcher.UIThread.RunJobs();
            Heartbeat.Frame();
            if (i < 5) continue;
            ui.Add(set + CanvasPanel.DrawMilliseconds - drawn);
            frames.Add(sw.Elapsed.TotalMilliseconds);
            renders.Add(engine.Last.Render);
        }
        engine.EndInteraction();
        engine.Set(field, start);
        string k = preview ? $"{key}_preview" : $"{key}_full";
        m[$"{k}_ui_ms"] = Median(ui);
        m[$"{k}_ui_p95_ms"] = Quantile(ui, 0.95);
        m[$"{k}_frame_ms"] = Median(frames);
        m[$"{k}_raster"] = Median(frames.Zip(ui, (f, u) => f - u).ToList());   // no unit suffix: tools/bench_stats.py reads it, never judges it
        m[$"{k}_engine_ms"] = Median(renders);
    }

    /// The drag with clarity 1, denoise luminance 2 and texture 1 on, the
    /// Luminance slider moved about 2: the heaviest common edit, timed as `drag`.
    static void HeavyDragScenario(Dictionary<string, double> m) {
        var engine = EngineModel.Shared;
        engine.Set("clarity", 1);
        engine.Set("texture", 1);
        engine.Set("denoise_luma", 2);
        Rest(new Heartbeat());
        DragScenario(m, preview: true, "denoise_luma", 1.5f, 2.5f, "drag_heavy");
        DragScenario(m, preview: false, "denoise_luma", 1.5f, 2.5f, "drag_heavy");
    }

    /// The first full-resolution render of the photo, as zooming in asks for
    /// it, timed from the moment its preview is up. tools/bench.py samples the
    /// card's memory for this process meanwhile: the peak is the number that
    /// decides whether a 42 MP frame opens on a shared 8 GB card.
    static void RenderScenario(Dictionary<string, double> m) {
        var engine = EngineModel.Shared;
        var sw = Stopwatch.StartNew();
        engine.Render(preview: false);
        double ms = sw.Elapsed.TotalMilliseconds;
        bool failed = engine.Shown.Graph != Graph.Full;
        // A failure's time is not a render's: an out-of-memory retry would pull the median both ways.
        if (!failed) m["render_full_ms"] = ms;
        m["render_full_failed_count"] = failed ? 1 : 0;
        if (failed) Console.Error.WriteLine($"bench render: no full render ({Describe()})");
        Heartbeat.Frame();
    }

    /// Two photos side by side (#400), linked, at fit and then at 100%: two
    /// engines holding their pictures at once, the case a 42 MP pair ran out on
    /// with 3 GB free (#406). The card's peak is the tool's (nvidia-smi); this
    /// counts what failed and how long each settled.
    static void SideBySideScenario(Dictionary<string, double> m) {
        if (EngineModel.Shared.Photos.Count < 2) throw new InvalidOperationException("side by side needs two photos");
        var beat = new Heartbeat();
        var (left, right) = (SideBySide.Left, SideBySide.Right);
        bool Rested(SideBySide.Side s) => !s.View.Live && s.Session != null && !s.Stale && s.Preview != null &&
                                          (s.Problem != null || !s.View.PastFit || s.Rendered == s.Wanted);
        var sw = Stopwatch.StartNew();
        SideBySide.Enter(wait: true);
        bool fit = beat.Until(() => Rested(left) && Rested(right), 60);
        m["sbs_fit_ms"] = sw.Elapsed.TotalMilliseconds;
        SideBySide.Activate(right);
        SideBySide.ActualSize();
        sw.Restart();
        bool actual = beat.Until(() => left.View.PastFit && right.View.PastFit && Rested(left) && Rested(right), 60);
        m["sbs_actual_ms"] = sw.Elapsed.TotalMilliseconds;
        int failed = (fit ? 0 : 1) + (actual ? 0 : 1) + (left.Problem != null ? 1 : 0) + (right.Problem != null ? 1 : 0);
        m["sbs_failed_count"] = failed;
        if (failed > 0)
            Console.Error.WriteLine($"bench side-by-side: fit {fit}, 100% {actual}; left: {left.Problem ?? "fine"}; right: {right.Problem ?? "fine"}");
    }

    /// The photo on screen written as the export sheet writes it, at the
    /// sheet's defaults, into ORION_BENCH_OUT: ExportRun.Run, the product's own path.
    static void ExportScenario(Dictionary<string, double> m) {
        var engine = EngineModel.Shared;
        string folder = Environment.GetEnvironmentVariable("ORION_BENCH_OUT") ?? Path.Combine(Path.GetTempPath(), "orion-bench-export");
        Directory.CreateDirectory(folder);
        var jobs = Sheets.ExportRun.Plan([engine.Photos[engine.Current]], folder, Sheets.ExportSettings.Shared.Extension);
        var sw = Stopwatch.StartNew();
        var run = Sheets.ExportRun.Run(jobs);
        new Heartbeat().Until(() => run.IsCompleted, 300);
        double ms = sw.Elapsed.TotalMilliseconds;
        string written = jobs[0].Destination;
        bool failed = !File.Exists(written);
        m["export_failed_count"] = failed ? 1 : 0;
        if (failed) { Console.Error.WriteLine($"bench export: nothing written: {(run.IsCompleted ? run.Result : "timed out")} ({Describe()})"); return; }
        m["export_ms"] = ms;   // as the render: only a write that happened is timed
        m["export_kib"] = new FileInfo(written).Length / 1024.0;
        File.Delete(written);
    }

    /// What the engine says it shows, for a scenario that did not get what it waited for.
    static string Describe() {
        var e = EngineModel.Shared;
        return $"shown {e.Shown.Graph} uv {e.Shown.Uv}, view {e.View?.Uv}, past fit {e.ZoomedPastFit}, " +
               $"moving {CanvasState.Viewport.Live}, timing '{e.Timing}', error '{e.Session?.LastError}'";
    }
}
