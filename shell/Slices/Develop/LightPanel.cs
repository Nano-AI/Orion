using Avalonia.Controls;

namespace OrionShell.Develop;

/// A port of the Swift app's DevelopPanels+Light.swift: white balance, the tone sliders,
/// highlight recovery and the curve. Ranges and decimals from that file;
/// a null base is the value the photo opened with (engine.defaults). The white
/// balance selector and menu are Lightroom's (WhiteBalanceTools).
[Panel("develop-light", Region.Tools, 10, Tab = "Light")]
public sealed class LightPanel : StackPanel {
    readonly ComboBox _whiteBalance;
    bool _showing;

    public LightPanel() {
        Spacing = SectionPlate.Gap;
        var selector = new ToolButton(CanvasTool.WhiteBalance, "eyedropper", "Selector", "Click a neutral", "scope",
                                      "White balance selector (W): click something that should be neutral");
        _whiteBalance = Sheets.SheetKit.Menu(WhiteBalanceTools.Choices, 0, i => { if (!_showing) WhiteBalanceTools.Choose(i); });
        Children.Add(new SectionPlate("White balance",
            "The selector sets temperature and tint so whatever you click comes out neutral: a gray card, a white wall, " +
            "a cloud. Auto makes the whole frame gray on average.",
            Develop.Row(6, [selector], _whiteBalance),
            new AdjustmentSlider("Temperature", "temperature_k", 2000, 12000, " K", 0, tint: TrackTint.Temperature),
            new AdjustmentSlider("Tint", "tint", -1, 1, tint: TrackTint.Tint)));
        EngineModel.Shared.Opened += ShowWhiteBalance;
        EngineModel.Shared.Rendered += ShowWhiteBalance;
        ShowWhiteBalance();

        // Auto heads the section whose sliders it moves (Exposure, Whites,
        // Blacks; Lift and Clarity live in Detail). Session.AutoEnhance is stage
        // 1b's; until then the status line says so. `reread` moves the sliders.
        var auto = new StateButton("wand.and.stars", "Auto", () => EngineModel.Shared.Apply(s => s.AutoEnhance(), reread: true),
                                   help: "Set exposure, whites, blacks, lift and clarity from this photo") {
            Padding = new Avalonia.Thickness(9, 0),
        };
        Children.Add(new SectionPlate("Light", null,
            Tone("exposure_ev", " EV"),
            new AdjustmentSlider("Contrast", "contrast", 0.5f, 2),
            Tone("highlights"), Tone("shadows"), Tone("whites"), Tone("blacks")).With(auto));

        Children.Add(new SectionPlate("Highlight recovery",
            "Where one channel clips before the others, it stops carrying detail while the rest still do — " +
            "a bright sky goes flat where blue ran out. This rebuilds that channel from the ones still reading. " +
            "A fully blown highlight is already rendered white, so this only changes the places where a single " +
            "channel ran out on its own.",
            new AdjustmentSlider("Amount", "highlight_recovery", 0, 1)));

        // Below the sliders: the control you reach for when a slider was not specific enough.
        Children.Add(new SectionPlate("Curve", null, new CurveEditor()));
    }

    /// The menu names the preset the photo is on, or Custom.
    void ShowWhiteBalance() {
        _showing = true;
        _whiteBalance.SelectedIndex = WhiteBalanceTools.Current();
        _showing = false;
    }

    static AdjustmentSlider Tone(string field, string unit = "") {
        var t = Tones.Of(field);
        return new AdjustmentSlider(t.Name, field, t.Min, t.Max, unit);
    }
}

/// The five tone sliders the histogram's regions drag, left to right (HistogramDrag),
/// with the ranges the Light panel gives them.
static class Tones {
    public sealed record Tone(string Field, string Name, float Min, float Max);

    public static readonly Tone[] All = [
        new("blacks", "Blacks", -1, 1), new("shadows", "Shadows", -1, 1), new("exposure_ev", "Exposure", -5, 5),
        new("highlights", "Highlights", -1, 1), new("whites", "Whites", -1, 1),
    ];

    public static Tone Of(string field) => All.Single(t => t.Field == field);
}
