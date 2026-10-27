using Avalonia.Controls;

namespace OrionShell;

/// Seed from the #315 spike, and the pattern for the Develop slice to follow:
/// SectionPlates of AdjustmentSliders bound to OrionAdjustments fields. The
/// slice replaces it with ports of DevelopPanels+Light/+Color/+Detail/+Optics.
/// Ranges from AdjustmentCatalogue.swift; tints from TrackTint.forAdjustment.
[Panel("develop-basic", Region.Tools, 10, Tab = "Light")]
public sealed class BasicPanel : StackPanel {
    public BasicPanel() {
        Spacing = Space.Xl;
        Children.Add(new SectionPlate("White balance", null,
            new AdjustmentSlider("Temperature", "temperature_k", 2000, 12000, " K", 0, tint: TrackTint.Temperature),
            new AdjustmentSlider("Tint", "tint", -1, 1, tint: TrackTint.Tint)));
        Children.Add(new SectionPlate("Light", null,
            new AdjustmentSlider("Exposure", "exposure_ev", -5, 5, " EV"),
            new AdjustmentSlider("Contrast", "contrast", 0.5f, 2),
            new AdjustmentSlider("Highlights", "highlights", -1, 1),
            new AdjustmentSlider("Shadows", "shadows", -1, 1),
            new AdjustmentSlider("Whites", "whites", -1, 1),
            new AdjustmentSlider("Blacks", "blacks", -1, 1)));
        Children.Add(new SectionPlate("Color", null,
            new AdjustmentSlider("Vibrance", "vibrance", -1, 1, tint: TrackTint.Presence),
            new AdjustmentSlider("Saturation", "saturation", -1, 1, tint: TrackTint.Presence)));
        Children.Add(new SectionPlate("Presence", "Clarity is local contrast; dehaze removes veiling glare.",
            new AdjustmentSlider("Clarity", "clarity", -1, 1),
            new AdjustmentSlider("Dehaze", "dehaze", 0, 1)));
    }
}
