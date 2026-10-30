using System.Diagnostics;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Headless;
using Avalonia.Input;
using Avalonia.Input.Raw;
using Avalonia.Media.Imaging;
using Avalonia.Threading;
using Avalonia.VisualTree;
using OrionShell.CanvasSlice;

namespace OrionShell;

/// Round 2, u2: the canvas the developer could not zoom ("I can't zoom in using
/// my trackpad") and the compare that was "very very slow". Every gesture goes
/// in as a real input event (headless MouseWheel/MouseDown/Move/Up, and the
/// trackpad's magnify as the raw event AppKit's backend sends), never a call
/// past the handler.
static partial class SelfTest {
    static partial void CanvasUxChecks() {
        SpringChecks();
        var engine = EngineModel.Shared;
        for (var wait = Stopwatch.StartNew(); engine.Opening && wait.Elapsed.TotalSeconds < 30;) { Dispatcher.UIThread.RunJobs(); Thread.Sleep(1); }
        if (engine.Handle == 0) return;
        var canvas = new CanvasPanel();
        var overlay = new CompareOverlay();
        var window = Show(new Grid { Children = { canvas, overlay } });
        try {
            ZoomChecks(window, canvas);
            PanChecks(window, canvas);
            CompareUxChecks(window, canvas, overlay);
        } finally {
            CompareState.Clear();
            CanvasState.Viewport.Reset();
            window.Close();
        }
    }

    static void SpringChecks() {
        var s = new CanvasSlice.Spring(0);
        double peak = 0, before = 0;
        bool monotone = true;
        int frames = 0;
        while (s.Step(1, 1 / 60.0, 0.35, 1e-4) && frames < 600) { frames++; monotone &= s.Value >= before; before = s.Value; peak = Math.Max(peak, s.Value); }
        Check(monotone && peak <= 1 && s.Value == 1 && frames is > 10 and < 90,
              $"the spring is critically damped: no overshoot, lands exactly, in {frames} frames at 60 Hz");
        var t = new CanvasSlice.Spring(0);
        for (int i = 0; i < 6; i++) t.Step(1, 1 / 60.0, 0.35, 1e-4);
        double at = t.Value, v = t.Velocity;
        t.Step(-1, 1e-6, 0.35, 1e-4);   // retargeted mid-flight: no jump, and it keeps its velocity
        Check(Math.Abs(t.Value - at) < 1e-3 && Math.Abs(t.Velocity - v) < 0.1 * Math.Abs(v),
              "a retargeted spring starts from the value on screen with the velocity it had");
        Check(Math.Abs(Viewport.Projected(1000) - 499) < 1e-9 && Math.Abs(Viewport.Band(1e9, 0.3) - 0.3) < 1e-6 && Viewport.Band(0.1, 1) < 0.1,
              "Apple's projection (0.998) and rubber band (0.55, bounded by its dimension)");
    }

    /// Frames until no spring runs: the headless render timer is the display link
    /// here. How many it took and how long, for a failing check to say. A slice of
    /// the real loop, not RunJobs: Avalonia paces animation frames that invalidate
    /// nothing with a DispatcherTimer, which only the loop fires (probe: a frame
    /// chain stopped after one under RunJobs, ran at 60 Hz in the loop).
    static string Frames(Func<bool>? until = null, double seconds = 5) {
        int n = 0;
        var sw = Stopwatch.StartNew();
        for (; sw.Elapsed.TotalSeconds < seconds; n++) {
            AvaloniaHeadlessPlatform.ForceRenderTimerTick();
            using (var slice = new CancellationTokenSource(TimeSpan.FromMilliseconds(4))) Dispatcher.UIThread.MainLoop(slice.Token);
            if (until?.Invoke() ?? !CanvasState.Viewport.Live) break;
        }
        return $"{n} frames, {sw.Elapsed.TotalSeconds:0.00} s";
    }

    static Point At(Window window, Control c, Point p) => c.TranslatePoint(p, window)!.Value;

    /// The trackpad's pinch as Avalonia's MouseDevice turns AppKit's magnify into
    /// it (the raw input route is internal): PointerTouchPadGestureMagnify, with
    /// the magnification as its delta, raised on whatever the window hit-tests at
    /// that point and bubbling from there.
    static void Pinch(Window window, Point at, double magnification) {
        AvaloniaHeadlessPlatform.ForceRenderTimerTick();   // hit-testing reads the last frame drawn
        var target = window.InputHitTest(at) as Avalonia.Interactivity.Interactive ?? window;
        target.RaiseEvent(new PointerDeltaEventArgs(InputElement.PointerTouchPadGestureMagnifyEvent, target,
            new Pointer(Pointer.GetNextFreeId(), PointerType.Mouse, true), window, at, (ulong)Environment.TickCount64,
            PointerPointProperties.None, KeyModifiers.None, new Vector(magnification, magnification)));
        Dispatcher.UIThread.RunJobs();
    }

    static void ZoomChecks(Window window, CanvasPanel canvas) {
        var vp = CanvasState.Viewport;
        var size = canvas.Bounds.Size;
        var p = new Point(size.Width * 0.3, size.Height * 0.35);
        vp.Reset();
        // Far enough that the picture fills the view both ways: on an axis it does
        // not fill it stays centred, as Swift's does, and no point holds still.
        double fill = 1.1 / Math.Min(vp.Coverage.Width, vp.Coverage.Height);
        Pinch(window, At(window, canvas, p), fill - 1);
        Check(Math.Abs(vp.Zoom - fill) < 1e-9, $"a trackpad pinch on the canvas zooms ({vp.Zoom:0.00}×)");
        var u = CanvasState.Map(size).ToUnit(p);
        Pinch(window, At(window, canvas, p), 0.5);
        var after = CanvasState.Map(size).ToUnit(p);
        Check(PictureMap.Dist(u, after) < 1e-9, $"a pinch keeps the image point under the pointer ({u.X:0.0000},{u.Y:0.0000} → {after.X:0.0000},{after.Y:0.0000})");
        var engine = EngineModel.Shared;
        Check(vp.Live && !engine.ZoomedPastFit, "mid-gesture the full render waits: no 24 MP render under the fingers");
        Frames();
        Check(!vp.Live && engine.ZoomedPastFit && engine.Shown.Graph is Graph.Full or Graph.Region,
              $"at rest past fit, the picture renders at full resolution (#318): only what is on screen when the edit allows it (#22), {engine.Shown.Graph}");
        // The full picture already rendered stays (it costs nothing more); the next edit
        // renders only what is in view, which the canvas reported as it drew.
        float ev = engine.Get("exposure_ev");
        engine.Set("exposure_ev", ev + 0.1f);
        var part = engine.Shown.Uv;
        Check(engine.Shown.Graph == Graph.Region && part.Width < 1 && part.Height <= 1 && engine.View is { } seen
              && seen.Uv == part && seen.Pixels.Width > 0,
              $"past fit with a plain edit, the engine renders only the part in view at the screen's pixels (#22): {engine.Shown.Graph} {part}");
        engine.Set("exposure_ev", ev);

        // Over the compare divider, which is the canvas's sibling, the pinch still zooms.
        u = CanvasState.Map(size).ToUnit(p);
        double z = vp.Zoom;
        window.MouseWheel(At(window, canvas, p), new Vector(0, 3), RawInputModifiers.Control);
        after = CanvasState.Map(size).ToUnit(p);
        Check(vp.Zoom > z && PictureMap.Dist(u, after) < 1e-9, "Ctrl+wheel zooms about the pointer, as ⌘+scroll does in Swift");
        z = vp.Zoom;
        window.MouseWheel(At(window, canvas, p), new Vector(0, 1), RawInputModifiers.None);
        Check(vp.Zoom > z, "a mouse wheel's whole notch zooms (Swift: a scroll without precise deltas)");
        z = vp.Zoom;
        var c = vp.Center;
        window.MouseWheel(At(window, canvas, p), new Vector(0.4, 0.3), RawInputModifiers.None);
        Check(vp.Zoom == z && vp.Center.X < c.X && vp.Center.Y < c.Y, "a trackpad's two-finger scroll pans and does not zoom");

        // Rubber band below fit, and back: the pinch out past fit shrinks the picture, then springs to fit.
        vp.Reset();
        Pinch(window, At(window, canvas, p), -0.5);
        double banded = vp.Zoom;
        string took = Frames();
        Check(banded is < 1 and > 0.5 && vp.IsFit, $"pinching out past fit rubber-bands ({banded:0.000}×, never below 0.5) and springs back to fit ({vp.Zoom:0.000}× after {took})");

        // A double-click springs to 1:1 about the point, a second back to fit; 9 jumps.
        vp.Reset();
        window.UpdateLayout();
        var q = At(window, canvas, p);
        window.MouseDown(q, MouseButton.Left); window.MouseUp(q, MouseButton.Left);
        window.MouseDown(q, MouseButton.Left); window.MouseUp(q, MouseButton.Left);
        bool moving = vp.Live;
        took = Frames();
        Check(moving && vp.Percent == 100, $"a double-click springs to 100% ({vp.Percent}%, {(moving ? "moving" : "still")} after the click, {took})");
        vp.Reset();
        Commands.Run("actual-size");
        Check(!vp.Live && vp.Percent == 100, "the 9 key jumps to 100%: a key never animates");
        vp.Reset();
    }

    static void PanChecks(Window window, CanvasPanel canvas) {
        var vp = CanvasState.Viewport;
        var size = canvas.Bounds.Size;
        vp.ZoomBy(3, new Point(0.5, 0.5));
        Frames();
        var v = vp.Visible;
        var start = At(window, canvas, new Point(size.Width / 2, size.Height / 2));
        window.MouseDown(start, MouseButton.Left);
        window.MouseMove(start + new Vector(size.Width * 3, 0));   // three views right: far past the left edge
        double raw = vp.Raw.X, shown = vp.Center.X, edge = v.Width / 2;
        Check(raw < edge - v.Width && shown < edge && shown > edge - v.Width,
              $"dragged past the edge the picture follows with resistance (raw {raw:0.000}, shown {shown:0.000}, edge {edge:0.000})");
        window.MouseUp(start + new Vector(size.Width * 3, 0), MouseButton.Left);
        Frames();
        Check(Math.Abs(vp.Center.X - edge) < 1e-9 && Math.Abs(vp.Center.Y - 0.5) < 1e-9, $"let go, it springs back to the edge exactly ({vp.Center.X:0.000000})");

        // A flick carries on to where it would rest (Apple's projection), clamped.
        vp.ZoomBy(1, new Point(0.5, 0.5));
        double from = vp.Center.X;
        vp.Begin();
        vp.Settle(new Vector(0.1, 0));
        Frames();
        Check(Math.Abs(vp.Center.X - Math.Min(from + Viewport.Projected(0.1), 1 - edge)) < 1e-6,
              $"a flick travels its projection, 0.1/s → {Viewport.Projected(0.1):0.000} ({from:0.000} → {vp.Center.X:0.000})");
        vp.Reset();
    }

    static void CompareUxChecks(Window window, CanvasPanel canvas, CompareOverlay overlay) {
        var engine = EngineModel.Shared;
        float ev = engine.Get("exposure_ev");
        engine.Set("exposure_ev", ev + 2);   // the edit far brighter than the original
        CompareState.Clear();
        var sw = Stopwatch.StartNew();
        Commands.Run("compare");
        double on = sw.Elapsed.TotalMilliseconds;
        Frames(() => CompareState.Original?.PixelSize.Width >= CanvasNative.ImageSize.Width, 60);   // the sharp one, from its worker
        Commands.Run("compare");
        sw.Restart();
        Commands.Run("compare");
        double again = sw.Elapsed.TotalMilliseconds;
        Check(on < 60 && again < 5 && CompareState.Showing && CompareState.Original!.PixelSize.Width == CanvasNative.ImageSize.Width,
              $"compare on takes {on:0.0} ms, {again:0.0} ms the second time: the original is made off the UI thread, once per photo, kept");

        // A divider drag: 1:1 from where it was taken, no engine render, no canvas-wide change.
        window.UpdateLayout();
        var frame = CanvasState.DrawnFrame(overlay.Bounds.Size);
        var (p, _, _) = CompareState.Line(frame);
        var grabbed = At(window, overlay, p + new Vector(5, -3));   // off the grip's centre: the offset is kept
        int renders = Session.Renders, changes = 0;
        void Count() => changes++;
        CanvasState.Changed += Count;
        var from = CompareState.Center;
        window.MouseDown(grabbed, MouseButton.Left);
        for (int i = 1; i <= 30; i++) window.MouseMove(grabbed + new Vector(4 * i, 2 * i));
        window.MouseUp(grabbed + new Vector(120, 60), MouseButton.Left);
        CanvasState.Changed -= Count;
        var expect = new Point(from.X + 120 / frame.Width, from.Y + 60 / frame.Height);
        Check(Session.Renders == renders && changes == 0,
              $"30 divider moves cost {Session.Renders - renders} engine renders and {changes} canvas-wide changes");
        Check(PictureMap.Dist(CompareState.Center, expect) < 1e-9, "the divider moves 1:1 with the hand, from where it was taken");

        // Turning: the handle sits on the line above the grip; take it round the grip.
        CompareState.MoveTo(new Point(0.5, 0.5));
        window.UpdateLayout();
        (p, _, _) = CompareState.Line(frame);
        void Turn(double degrees, RawInputModifiers keys) {
            var knob = p + new Vector(0, -46);   // straight up at 0°, the handle's place
            window.MouseDown(At(window, overlay, knob), MouseButton.Left, keys);
            for (int i = 1; i <= 10; i++) {
                double r = (-90 + degrees * i / 10) * Math.PI / 180;
                window.MouseMove(At(window, overlay, p + new Vector(46 * Math.Cos(r), 46 * Math.Sin(r))), keys);
            }
            double end = (-90 + degrees) * Math.PI / 180;
            Thread.Sleep(60);   // placed, not thrown: the hand rests before it lets go
            window.MouseUp(At(window, overlay, p + new Vector(46 * Math.Cos(end), 46 * Math.Sin(end))), MouseButton.Left, keys);
            Frames(() => !CompareState.Spinning);
        }
        Turn(42, RawInputModifiers.None);
        Frames(() => !CompareState.Spinning);
        double snapped = CompareState.Angle;
        Check(snapped == 45, $"turned to 42° the divider holds at the 45° detent ({snapped:0.00}°)");
        CompareState.TurnTo(0, false);
        Turn(42, RawInputModifiers.Meta);
        Frames(() => !CompareState.Spinning);
        Check(Math.Abs(CompareState.Angle - 42) < 0.5, $"with ⌘ held it stays where it was put ({CompareState.Angle:0.00}°)");
        CompareState.TurnTo(30, false);
        CompareState.Release(40, false);
        Frames(() => !CompareState.Spinning);
        Check(CompareState.Angle == 45, $"let go turning at 40°/s from 30°, it projects to 50° and settles on 45° ({CompareState.Angle:0.00}°)");

        // At 45° the original is on the upper-left side of the line, the edit on the lower right.
        Shot(window, "compare-45");
        var (px, stride, c) = Pixels(window, canvas);
        var (q, n, _) = CompareState.Line(CanvasState.DrawnFrame(canvas.Bounds.Size));
        double Luma(Point v) { int i = (int)(c.Y + v.Y * 2) * stride + (int)(c.X + v.X * 2) * 4; return (px[i] + px[i + 1] + px[i + 2]) / 3.0; }
        double before = Luma(q - n * 60), edited = Luma(q + n * 60);
        Check(edited > before + 20, $"at 45° the original shows on its side ({before:0}) and the edit on the other ({edited:0})");
        Check(GripCentred(window, canvas, q), "the grip's arrows are centred on the divider at 45°");

        // swap-split still works: to Swift's horizontal split and back, at once (it has a key).
        Commands.Run("swap-split");
        Check(!CompareState.Vertical && CompareState.Angle == 90 && !CompareState.Spinning, $"swap-split puts the divider at 90° without animating ({CompareState.Angle:0.00}°)");
        Check(GripCentred(window, canvas, CompareState.Line(CanvasState.DrawnFrame(canvas.Bounds.Size)).P), "the grip's arrows are centred at 90°");
        Commands.Run("swap-split");
        Check(CompareState.Vertical && CompareState.Angle == 0 && GripCentred(window, canvas, CompareState.Line(CanvasState.DrawnFrame(canvas.Bounds.Size)).P),
              "and back to 0°, centred there too");
        Shot(window, "compare-0");
        // Mid-zoom: the split holds still in the view while the picture is 2.5× under it.
        CanvasState.Viewport.ZoomBy(2.5, new Point(0.6, 0.4));
        Frames();
        Shot(window, "compare-zoomed");
        CanvasState.Viewport.Reset();
        engine.Set("exposure_ev", ev);
    }

    /// ORION_SHOTS=<dir>: the window at 2× as <dir>/<name>.png, for a person to look at.
    static void Shot(Window window, string name) {
        if (Environment.GetEnvironmentVariable("ORION_SHOTS") is not { Length: > 0 } dir) return;
        window.UpdateLayout();
        Dispatcher.UIThread.RunJobs();
        using var rtb = new RenderTargetBitmap(new PixelSize((int)window.Bounds.Width * 2, (int)window.Bounds.Height * 2), new Vector(192, 192));
        rtb.Render(window);
        Directory.CreateDirectory(dir);
        rtb.Save(Path.Combine(dir, name + ".png"), new PngBitmapEncoderOptions());
    }

    /// The dark ink inside the white grip, at 2×: its centroid is the divider's point.
    static bool GripCentred(Window window, Control canvas, Point p) {
        var (px, stride, c) = Pixels(window, canvas);
        double sx = 0, sy = 0, n = 0;
        for (int y = -20; y <= 20; y++)
            for (int x = -20; x <= 20; x++) {
                if (x * x + y * y > 18 * 18) continue;
                int i = (int)(c.Y + p.Y * 2 + y) * stride + (int)(c.X + p.X * 2 + x) * 4;
                if ((px[i] + px[i + 1] + px[i + 2]) / 3.0 > 160) continue;
                (sx, sy, n) = (sx + x, sy + y, n + 1);
            }
        bool ok = n > 20 && Math.Abs(sx / n) < 0.75 && Math.Abs(sy / n) < 0.75;
        if (!ok) Console.Error.WriteLine($"grip ink centroid off by ({sx / Math.Max(n, 1) / 2:0.00}, {sy / Math.Max(n, 1) / 2:0.00}) pt over {n} px");
        return ok;
    }
}
