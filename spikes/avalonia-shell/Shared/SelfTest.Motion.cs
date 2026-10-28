using System.Diagnostics;
using Avalonia;
using Avalonia.Animation;
using Avalonia.Controls;
using Avalonia.Controls.Shapes;
using Avalonia.Headless;
using Avalonia.Input;
using Avalonia.Media.Imaging;
using Avalonia.Threading;
using Avalonia.VisualTree;

namespace OrionShell;

/// Round 2, stream u5: the sliders' motion (design/DESIGN.md, Motion). Springs
/// are stepped by hand (AnalogTrack.Advance, Spring.Step) at 60 frames a second,
/// so each check sees exact frames rather than whatever the clock gave it.
static partial class SelfTest {
    const double Frame = 1 / 60.0;

    static partial void MotionChecks() {
        var engine = EngineModel.Shared;
        // An earlier stream's checks may leave the photo reopening in the background.
        if (engine.Handle == 0 && !engine.Opening && engine.Photos.Count > 0) engine.Open(0);
        Settle(() => !engine.Opening);
        if (engine.Handle == 0) { Check(false, "motion checks need an open photo"); return; }
        Console.WriteLine($"motion: reduce motion from the system reads {Motion.System()}");
        Motion.Forced = false;   // the springs themselves, whatever this machine is set to
        var window = new MainWindow("Light");
        window.Show();
        window.UpdateLayout();
        Dispatcher.UIThread.RunJobs();
        float start = engine.Get("exposure_ev");
        try {
            SpringChecks(window);
            var slider = Slider(window, "Exposure");
            var track = slider.GetVisualDescendants().OfType<AnalogTrack>().Single();
            Shoot(slider, "1-rest");
            GlideChecks(window, engine, slider, track);
            HandChecks(window, engine, slider, track);
            BrowseChecks(window, engine, track);
            ReducedChecks(window, engine, track);
            ReadoutChecks(window, engine, slider);
            ControlChecks(window, engine, slider, track);
            engine.Set("exposure_ev", start);
            DragCost("in the window", window, slider, track);
        } finally {
            Motion.Forced = null;
            engine.Set("exposure_ev", start);
            window.Close();
        }
        // The same slider alone: what the shared controls themselves cost a tick.
        var lone = new AdjustmentSlider("Exposure", "exposure_ev", -5, 5, " EV", 2, baseValue: 0) { Width = Layout.ToolCol - 2 * Space.Pad };
        var host = Show(new Border { Padding = new Thickness(Space.Pad), Child = lone });
        DragCost("alone", host, lone, lone.GetVisualDescendants().OfType<AnalogTrack>().Single());
        host.Close();
        engine.Set("exposure_ev", start);
    }

    static AdjustmentSlider Slider(Visual root, string name) => root.GetVisualDescendants().OfType<AdjustmentSlider>()
        .First(s => s.GetVisualDescendants().OfType<TextBlock>().Any(t => t.Text == name));

    static void Settle(Func<bool> done) {
        var waited = Stopwatch.StartNew();
        while (!done() && waited.Elapsed.TotalSeconds < 30) { Dispatcher.UIThread.RunJobs(); Thread.Sleep(1); }
    }

    static readonly Pointer Hand = new(Pointer.GetNextFreeId(), PointerType.Mouse, true);

    /// A press on the thumb raised on the track itself. Through the window, input pumps the
    /// dispatcher, whose real frames and double-click clock would make the check as fast as the machine.
    static void Press(AnalogTrack t, int clicks = 1) => t.RaiseEvent(new PointerPressedEventArgs(t, Hand, t, new Point(t.ThumbX, 9.5), 0,
        new PointerPointProperties(RawInputModifiers.LeftMouseButton, PointerUpdateKind.LeftButtonPressed), KeyModifiers.None, clicks));

    static void Release(AnalogTrack t) => t.RaiseEvent(new PointerReleasedEventArgs(t, Hand, t, new Point(t.ThumbX, 9.5), 0,
        new PointerPointProperties(RawInputModifiers.None, PointerUpdateKind.LeftButtonReleased), KeyModifiers.None, MouseButton.Left));

    /// Steps until it lands (or two seconds of frames): the thumb positions it passed through.
    static List<double> Run(AnalogTrack track) {
        var path = new List<double>();
        for (int i = 0; i < 120 && track.Moving; i++) { track.Advance(Frame); path.Add(track.Shown); }
        return path;
    }

    /// The spring alone: critically damped, on time, a release velocity carried, off screen at once.
    static void SpringChecks(Window window) {
        var owner = new Border();
        ((Panel)window.FindControl<Grid>("ToolbarHost")!).Children.Add(owner);
        var s = new Spring(owner, Motion.Settle);
        s.To(1);
        var path = new List<double>();
        for (int i = 0; i < 120 && s.Moving; i++) { s.Step(Frame); path.Add(s.Value); }
        double At(double share) => (path.FindIndex(v => v >= share) + 1) * Frame * 1000;
        Check(path.Count > 1 && path.Zip(path.Skip(1)).All(p => p.Second >= p.First) && path.Max() <= 1 && s.Value == 1 && !s.Moving
              && At(0.9) is > 150 and < 220 && path.Count * Frame < 0.5,
              $"a Settle spring rises without overshoot and lands exactly: 90% at {At(0.9):0} ms, landed at {path.Count * Frame * 1000:0} ms");

        // A hand let go 10 px past an end moving outward at 400 px/s: it carries on out, then back, never past 0.
        s.Snap(10);
        s.To(0, velocity: 400);
        path.Clear();
        for (int i = 0; i < 120 && s.Moving; i++) { s.Step(Frame); path.Add(s.Value); }
        Check(path[0] > 10 && path.Min() >= 0 && s.Value == 0, $"a release velocity carries into the spring back (first frame {path[0]:0.0} px from 10, lowest {path.Min():0.00})");

        var away = new Spring(new Border(), Motion.Settle);
        away.To(5);
        Check(away.Value == 5 && !away.Moving, "a spring whose owner is not on screen lands at once");
        ((Panel)window.FindControl<Grid>("ToolbarHost")!).Children.Remove(owner);
    }

    /// A value set from outside a drag (Auto, undo, a preset) travels there from the thumb's place on screen.
    static void GlideChecks(Window window, EngineModel engine, AdjustmentSlider slider, AnalogTrack track) {
        float from = track.Value;
        engine.Apply(s => s.Set("exposure_ev", from + 2), reread: true);   // how Auto, undo and presets tell the panels
        Check(slider.Value == from + 2 && track.Value == from + 2 && track.Moving && Math.Abs(track.Shown - from) < 1e-6,
              "an outside change sets the value at once and starts the thumb from where it was on screen");
        track.Advance(5 * Frame);
        Shoot(slider, "3-mid-glide");
        var path = Run(track);
        Check(path.Count > 1 && path.Zip(path.Skip(1)).All(p => p.Second >= p.First) && path.Max() <= from + 2 && track.Shown == from + 2 && !track.Moving,
              $"the glide ends exactly on the target, no overshoot ({(path.Count + 5) * Frame * 1000:0} ms of frames)");

        // Interrupted: a second change mid-flight starts from the on-screen value at the speed it had.
        engine.Apply(s => s.Set("exposure_ev", from - 1), reread: true);
        track.Advance(6 * Frame);
        double there = track.Shown, speed = track.GlideSpeed;
        engine.Apply(s => s.Set("exposure_ev", from + 1), reread: true);
        Check(track.Shown == there && track.GlideSpeed == speed && speed < 0,
              $"a change mid-glide re-aims from the thumb's place ({there:0.000}) at its speed ({speed:0.00}/s), no jump");
        Run(track);
        Check(track.Shown == from + 1 && !track.Moving, "and lands on the new target");

        // Grabbed mid-flight: caught where it is, and let go unmoved it finishes the trip.
        engine.Apply(s => s.Set("exposure_ev", from - 2), reread: true);
        track.Advance(4 * Frame);
        double caught = track.Shown;
        Press(track);
        double grabbed = track.Shown;
        bool held = Math.Abs(grabbed - caught) < 1e-5 && !track.Moving;   // caught as a float, the value's own type
        Release(track);
        bool resumed = track.Moving;
        Run(track);
        Check(held && resumed && track.Shown == from - 2 && engine.Get("exposure_ev") == from - 2,
              $"a grab catches a gliding thumb where it is on screen ({caught:0.0000} → {grabbed:0.0000}); released unmoved, " +
              $"it finishes the glide ({track.Shown:0.0000}, engine {engine.Get("exposure_ev"):0.0000}, target {from - 2:0.0000})");
        engine.Apply(s => s.Set("exposure_ev", from), reread: true);
        Run(track);
        window.MouseMove(new Point(2, 2));
        Run(track);
    }

    /// The hand: a drag is 1:1, hover lifts, a grab presses, past an end it stretches and springs back, double-click resets.
    static void HandChecks(Window window, EngineModel engine, AdjustmentSlider slider, AnalogTrack track) {
        Point Thumb() => track.TranslatePoint(new Point(track.ThumbX, 9.5), window)!.Value;
        window.MouseMove(new Point(2, 2));
        track.Advance(1);
        double rest = track.ThumbScale;
        var label = slider.GetVisualDescendants().OfType<TextBlock>().First(t => t.Text == "Exposure");
        window.MouseMove(label.TranslatePoint(new Point(4, 4), window)!.Value);   // the row, not the track
        track.Advance(1);
        double hover = track.ThumbScale;
        Shoot(slider, "2-hover");
        var at = Thumb();
        window.MouseMove(at);
        window.MouseDown(at, MouseButton.Left);
        track.Advance(1);
        double pressed = track.ThumbScale;
        Check(rest == 1 && hover > 1.1 && pressed > 1 && pressed < hover,
              $"hovering the row lifts the thumb ({rest:0.00} → {hover:0.00}); a grab presses it ({pressed:0.00})");

        float before = track.Value;
        window.MouseMove(at + new Point(30, 0));
        Check(track.Value > before && !track.Moving && track.Shown == track.Value && track.ThumbX == track.ValueX
              && engine.Get("exposure_ev") == track.Value,
              $"a drag puts the thumb on the value at once, no animation ({before:0.00} → {track.Value:0.00} EV)");

        window.MouseMove(at + new Point(400, 0));
        double past1 = track.ThumbX - track.ValueX;
        window.MouseMove(at + new Point(800, 0));
        double past2 = track.ThumbX - track.ValueX;
        Check(track.Value == track.Max && engine.Get("exposure_ev") == track.Max && past1 > 2 && past2 > past1 && past2 < 12 && past2 - past1 < past1,
              $"past the end the value holds at {track.Max} and the thumb stretches less and less ({past1:0.0}, then {past2:0.0} px, under 12)");
        window.MouseUp(at + new Point(800, 0), MouseButton.Left);
        bool springsBack = track.Moving;
        var path = Run(track);
        Check(springsBack && track.ThumbX == track.ValueX, $"let go, it springs back onto the end ({path.Count * Frame * 1000:0} ms)");

        // Double-click the track: back to the detent, gliding.
        Press(track);
        Release(track);
        Press(track, clicks: 2);
        bool gliding = track.Moving;
        Release(track);
        Run(track);
        Check(gliding && track.Value == track.Base && engine.Get("exposure_ev") == track.Base && track.Shown == track.Base,
              "a double-click on the track resets to the detent, and the thumb glides there");

        // Two quick nudges from one spot are not a double-click: Avalonia counts the second press as
        // ClickCount 2, which reset what the first drag had just set.
        var grip = Thumb();
        window.MouseMove(grip);
        window.MouseDown(grip, MouseButton.Left);
        window.MouseMove(grip + new Point(30, 0));
        window.MouseUp(grip + new Point(30, 0), MouseButton.Left);
        float nudged = track.Value;
        window.MouseDown(grip, MouseButton.Left);
        bool kept = track.Value == nudged && nudged != track.Base;
        window.MouseUp(grip, MouseButton.Left);
        Run(track);
        Check(kept, $"a press right after a drag, at the drag's start, does not reset it ({track.Base} → {nudged:0.00})");
        engine.Set("exposure_ev", track.Base);
        Dispatcher.UIThread.RunJobs();
        Run(track);

        // A click on the readout types at once: no wait for a double-click.
        var readout = slider.GetVisualDescendants().OfType<Decorator>().First(d => d.GetType().Name == "Readout");
        var box = readout.TranslatePoint(new Point(readout.Bounds.Width / 2, readout.Bounds.Height / 2), window)!.Value;
        window.MouseMove(box);
        window.MouseDown(box, MouseButton.Left);
        window.MouseUp(box, MouseButton.Left);
        bool typing = readout.Child is TextBox;
        Dispatcher.UIThread.RunJobs();
        window.KeyPress(Key.Escape, RawInputModifiers.None, PhysicalKey.Escape, null);
        Check(typing && readout.Child is TextBlock, "a click on the readout opens the field at once; Escape closes it");
        window.MouseMove(new Point(2, 2));
        track.Advance(1);
    }

    /// The arrow keys: a photo opening shows its values at once, and the section dot does not animate.
    static void BrowseChecks(Window window, EngineModel engine, AnalogTrack track) {
        engine.Apply(s => s.Set("exposure_ev", track.Base + 1.5f), reread: true);
        var plate = track.FindAncestorOfType<SectionPlate>()!;
        var dot = plate.GetVisualDescendants().OfType<Ellipse>().First();
        double Lit() => dot.GetBaseValue(Visual.OpacityProperty).GetValueOrDefault();   // where it is going: Opacity reads the fade under way
        double lit = Lit();
        Check(lit == 1 && dot.Transitions?.Count == 2 && dot.Transitions.OfType<TransitionBase>().All(t => t.Duration == Motion.Enter),
              "an edit brings the section's dot in on the Enter transition (fade and grow)");
        bool moving = track.Moving;
        engine.Next();
        Settle(() => !engine.Opening);
        var tracks = window.GetVisualDescendants().OfType<AnalogTrack>().ToList();
        Check(moving && engine.Session != null && tracks.Count > 3 && tracks.All(t => !t.Moving && t.Shown == t.Value),
              $"after an arrow key every one of {tracks.Count} tracks shows its photo's value at once, a glide in flight included");
        Check(Lit() == lit || dot.Transitions == null, "a section dot a photo opening turns off or on does not animate");
        engine.Previous();
        Settle(() => !engine.Opening);
    }

    /// Reduce motion: no travel and no stretch; a short cross-fade instead.
    static void ReducedChecks(Window window, EngineModel engine, AnalogTrack track) {
        Motion.Forced = true;
        try {
            engine.Apply(s => s.Set("exposure_ev", track.Base - 2), reread: true);
            bool landed = track.Shown == track.Base - 2 && track.Moving;   // moving: the old place still fading out
            track.Advance(Motion.Fade.TotalSeconds);
            Check(landed && !track.Moving, "under reduced motion an outside change lands at once and cross-fades for 120 ms");
            var at = track.TranslatePoint(new Point(track.ThumbX, 9.5), window)!.Value;
            window.MouseMove(at);
            window.MouseDown(at, MouseButton.Left);
            window.MouseMove(at - new Point(600, 0));
            bool flush = track.ThumbX == track.ValueX && track.Value == track.Min;
            window.MouseUp(at - new Point(600, 0), MouseButton.Left);
            Check(flush, "under reduced motion the thumb stops at the end, no rubber band");
        } finally {
            Motion.Forced = false;
        }
        engine.Apply(s => s.Set("exposure_ev", track.Base), reread: true);
        Run(track);
        window.MouseMove(new Point(2, 2));
        track.Advance(1);
    }

    /// The readout's box keeps one width for every value, so nothing beside it moves while a value is dragged.
    static void ReadoutChecks(Window window, EngineModel engine, AdjustmentSlider slider) {
        var readout = slider.GetVisualDescendants().OfType<Decorator>().First(d => d.GetType().Name == "Readout");
        var widths = new List<double>();
        foreach (float v in new[] { -5f, -0.5f, 0, 0.35f, 5 }) {
            engine.Apply(s => s.Set("exposure_ev", v), reread: true);
            window.UpdateLayout();
            widths.Add(readout.Bounds.Width);
        }
        Check(widths.Distinct().Count() == 1, $"the readout keeps one width from −5 to +5 EV ({string.Join(", ", widths.Select(w => w.ToString("0.#")))})");
        var track = slider.GetVisualDescendants().OfType<AnalogTrack>().Single();
        engine.Apply(s => s.Set("exposure_ev", track.Base + 0.7f), reread: true);
        Run(track);
        window.UpdateLayout();
        Shoot(slider, "4-edited");
        var temperature = Slider(window, "Temperature");
        Shoot(temperature, "5-tint-rest");
        var tint = temperature.GetVisualDescendants().OfType<AnalogTrack>().Single();
        float k = engine.Get("temperature_k");
        engine.Apply(s => s.Set("temperature_k", k + 1500), reread: true);
        Run(tint);
        Shoot(temperature, "6-tint-edited");
        engine.Apply(s => s.Set("temperature_k", k), reread: true);
        Run(tint);
    }

    /// u3's reports against these controls: typing into a readout from the keyboard, a press
    /// between the groove and the numerals, a drag that loses the pointer, a left-aligned button.
    static void ControlChecks(Window window, EngineModel engine, AdjustmentSlider slider, AnalogTrack track) {
        var readout = slider.GetVisualDescendants().OfType<Decorator>().First(d => d.GetType().Name == "Readout");
        string? thrown = null;
        int opened = 0;
        try {
            foreach (var (key, physical) in new[] { (Key.Enter, PhysicalKey.Enter), (Key.Space, PhysicalKey.Space) }) {
                readout.Focus(NavigationMethod.Tab);
                window.KeyPress(key, RawInputModifiers.None, physical, null);
                Dispatcher.UIThread.RunJobs();   // the field takes the focus
                if (readout.Child is TextBox { IsFocused: true }) opened++;
                window.KeyPress(Key.Enter, RawInputModifiers.None, PhysicalKey.Enter, null);
                Dispatcher.UIThread.RunJobs();
            }
        } catch (Exception e) {
            thrown = $"{e.GetType().Name}: {e.Message}";
        }
        Check(thrown == null && opened == 2 && readout.Child is TextBlock,
              $"Tab to a readout: Return and Space each open the focused field ({opened} of 2) and Return commits, nothing thrown"
              + (thrown == null ? "" : $" ({thrown})"));

        var gap = track.TranslatePoint(new Point(track.Bounds.Width * 0.15, 16.5), window)!.Value;   // under the groove, above the numerals
        Check(window.InputHitTest(gap) == track, $"the track takes a press anywhere in its band (hit {window.InputHitTest(gap)?.GetType().Name ?? "nothing"})");

        Press(track);
        bool armed = engine.Interacting;
        Hand.Capture(null);   // a dialog or lost focus takes the pointer mid-drag
        Check(armed && !engine.Interacting, "a drag that loses the pointer still ends: the preview graph disarms and the full render runs");
        track.Advance(1);

        var left = new StateButton(null, "24mm F1.4 DG DN", () => { }) { HorizontalContentAlignment = Avalonia.Layout.HorizontalAlignment.Left, Width = 240 };
        var centred = new StateButton(null, "Open", () => { }) { Width = 240 };
        var host = Show(new StackPanel { Children = { left, centred } });
        double X(StateButton b) => ((Control)b.Content!).TranslatePoint(default, b)!.Value.X;
        Check(X(left) < 16 && X(centred) > 80, $"a button's content alignment is its own: left {X(left):0} px in, centred {X(centred):0} px in");
        host.Close();
    }

    /// ORION_MOTION_SHOTS=dir saves the states these checks pass through, at 2×, to look at.
    static void Shoot(Control control, string name) {
        if (Environment.GetEnvironmentVariable("ORION_MOTION_SHOTS") is not { Length: > 0 } dir) return;
        var window = TopLevel.GetTopLevel(control)!;
        window.UpdateLayout();
        using var full = new RenderTargetBitmap(new PixelSize((int)window.Bounds.Width * 2, (int)window.Bounds.Height * 2), new Vector(192, 192));
        full.Render(window);
        // The window cropped round the control, with room for a stretched thumb and its shadow.
        var size = new Size(control.Bounds.Width + 28, control.Bounds.Height + 16);
        var at = control.TranslatePoint(new Point(-14, -8), window)!.Value;
        using var crop = new RenderTargetBitmap(new PixelSize((int)size.Width * 2, (int)size.Height * 2), new Vector(192, 192));
        using (var c = crop.CreateDrawingContext()) c.DrawImage(full, new Rect(at.X * 2, at.Y * 2, size.Width * 2, size.Height * 2), new Rect(size));   // the source in its pixels
        Directory.CreateDirectory(dir);
        crop.Save(System.IO.Path.Combine(dir, $"slider-{name}.png"), new PngBitmapEncoderOptions());
    }

    /// A 60-tick drag through the pointer path a hand takes, each tick timed with
    /// the slider's own redraw: what a change here must not make dearer.
    static void DragCost(string where, Window window, AdjustmentSlider slider, AnalogTrack track) {
        var engine = EngineModel.Shared;
        float start = engine.Get("exposure_ev");
        var at = track.TranslatePoint(new Point(track.Bounds.Width / 2, 9.5), window)!.Value;
        var size = new PixelSize((int)slider.Bounds.Width * 2, (int)slider.Bounds.Height * 2);
        using var shot = new RenderTargetBitmap(size, new Vector(192, 192));
        Settle(() => !engine.Cache.Busy);   // background decodes would share the cores
        var (input, engineMs, redraw) = (new List<double>(), new List<double>(), new List<double>());
        window.MouseMove(at);
        window.MouseDown(at, MouseButton.Left);
        for (int i = 0; i < 60; i++) {
            var sw = Stopwatch.StartNew();
            window.MouseMove(at + new Point((i % 30 - 15) * 3, 0));
            double moved = sw.Elapsed.TotalMilliseconds;
            shot.Render(slider);
            if (i < 5) continue;
            engineMs.Add(engine.Last.Render + engine.Last.Copy);
            input.Add(moved - engineMs[^1]);
            redraw.Add(sw.Elapsed.TotalMilliseconds - moved);
        }
        window.MouseUp(at, MouseButton.Left);
        Dispatcher.UIThread.RunJobs();
        engine.Set("exposure_ev", start);
        static string Median(List<double> t) => $"{t.Order().ElementAt(t.Count / 2):0.000}";
        Console.WriteLine($"motion: 60-tick slider drag {where}, median per tick: engine {Median(engineMs)} ms, " +
                          $"input and panels {Median(input)} ms, slider redraw {Median(redraw)} ms");
    }
}
