using Avalonia.Controls;

namespace OrionShell.Develop;

/// A port of app/DevelopPanels+Light.swift: white balance, the tone sliders,
/// highlight recovery and the curve. Ranges and decimals from that file;
/// a null base is the value the photo opened with (engine.defaults).
[Panel("develop-light", Region.Tools, 10, Tab = "Light")]
public sealed class LightPanel : StackPanel {
    public LightPanel() {
        Spacing = Space.Xl;
        Children.Add(new SectionPlate("White Balance", null,
            new AdjustmentSlider("Temperature", "temperature_k", 2000, 12000, " K", 0, tint: TrackTint.Temperature),
            new AdjustmentSlider("Tint", "tint", -1, 1, tint: TrackTint.Tint)));

        // Auto writes Exposure, Whites and Blacks (three of the sliders below
        // it) plus Lift and Clarity, so it sits with the controls it moves.
        // stub(#315): Engine.swift owns this until the logic moves below the facade
        //   (orion_engine_auto_enhance writes a whole OrionAdjustments, which the shim keeps private)
        var auto = Develop.Row(8, [Develop.PanelButton("Auto", () => { }),
                                   Engraved.Label("Exp · Wht · Blk · Lift · Clarity", Palette.Faint)]);
        Children.Add(new SectionPlate("Light", null,
            auto,
            new AdjustmentSlider("Exposure", "exposure_ev", -5, 5, " EV"),
            new AdjustmentSlider("Contrast", "contrast", 0.5f, 2),
            new AdjustmentSlider("Highlights", "highlights", -1, 1),
            new AdjustmentSlider("Shadows", "shadows", -1, 1),
            new AdjustmentSlider("Whites", "whites", -1, 1),
            new AdjustmentSlider("Blacks", "blacks", -1, 1)));

        Children.Add(new SectionPlate("Highlight Recovery",
            "Where one channel clips before the others, it stops carrying detail while the rest still do — " +
            "a bright sky goes flat where blue ran out. This rebuilds that channel from the ones still reading. " +
            "A fully blown highlight is already rendered white, so this only changes the places where a single " +
            "channel ran out on its own.",
            new AdjustmentSlider("Amount", "highlight_recovery", 0, 1)));

        // Below the sliders: the control you reach for when a slider was not specific enough.
        Children.Add(new SectionPlate("Curve", null, new CurveEditor()));
    }
}
