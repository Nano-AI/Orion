using Avalonia;
using Avalonia.Controls;
using Avalonia.VisualTree;

namespace OrionShell;

/// The Color tab's black and white, by pointer: Treatment and its eight gray-mix sliders.
static partial class SelfTest {
    static partial void ColorChecks() {
        var engine = EngineModel.Shared;
        if (engine.Handle == 0) return;
        engine.Open(engine.Current);   // a clean history
        Arrived(engine);
        var window = Shell();
        Click(window, TabButton(window, "Color"));
        var host = window.FindControl<StackPanel>("ToolsHost")!;
        string[] bands = Enum.GetNames<Develop.HueBand>();
        List<AdjustmentSlider> Mix() => host.GetVisualDescendants().OfType<AdjustmentSlider>()
            .Where(s => s.IsEffectivelyVisible && bands.Any(b => Named(s, b))).ToList();
        var treatment = host.GetVisualDescendants().OfType<Segmented>().First(t => Named(t, "Black & white"));

        double colour = Chroma(engine.Picture);
        bool hidden = Mix().Count == 0;
        Click(window, Labelled(window, "Black & white"));
        double grey = Chroma(engine.Picture);
        Check(hidden && engine.Get("black_white") == 1 && grey < 2 && colour > grey + 5 && Mix().Count == 8,
              $"Treatment's Black & white renders gray and shows the eight-band mix (chroma {colour:0.0} → {grey:0.0})");
        Commands.Run("undo");
        bool undone = engine.Get("black_white") == 0 && treatment.Selected == 0 && Chroma(engine.Picture) > colour - 1 && Mix().Count == 0;
        Commands.Run("redo");
        Check(undone && engine.Get("black_white") == 1 && treatment.Selected == 1,
              "undo and redo take Black & white back and forth in one step, the Treatment control following");

        List<string> wrote = [], moved = [];
        foreach (var slider in Mix()) {
            var track = slider.GetVisualDescendants().OfType<AnalogTrack>().First();
            track.BringIntoView();
            Settle(window);
            string band = bands.First(b => Named(slider, b));
            long picture = Fingerprint(engine.Picture);
            var on = new Point(track.Bounds.Width / 2, 9.5);
            Drag(window, track, on, on + new Vector(track.Bounds.Width * 0.3, 0));
            if (slider.Value > 0 && engine.Get("bw_mix", Array.IndexOf(bands, band)) == slider.Value) wrote.Add(band);
            if (Fingerprint(engine.Picture) != picture) moved.Add(band);
        }
        grey = Chroma(engine.Picture);
        Check(wrote.Count == 8 && moved.Count >= 4 && grey < 2,
              $"each gray-mix slider writes its band and the render stays gray (wrote {string.Join(", ", wrote)}; " +
              $"moved {string.Join(", ", moved)}; chroma {grey:0.0})");

        Commands.Run("reset-adjustments");
        Check(engine.Get("black_white") == 0 && treatment.Selected == 0 && Mix().Count == 0, "Reset turns black & white off");
        window.Close();
    }
}
