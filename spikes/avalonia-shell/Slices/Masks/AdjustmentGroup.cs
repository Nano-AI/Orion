using Avalonia.Controls;
using Avalonia.Media;

namespace OrionShell;

/// A port of app/AdjustmentGroup.swift, local scope: the adjustments a mask
/// carries, one slider each, bound to the selected layer's slot of the
/// local_* arrays. These are real fields: they reach the engine.
///
/// Swift renders the global scope from the same catalogue; here the Develop
/// slice owns those sliders. LocalRefusals and PipelineOrder are not ported:
/// nothing in the Swift panel draws them (DevelopPanels+Mask.swift says why).
static class AdjustmentGroup {
    /// AdjustmentCatalogue.localSet, in its order: title, C field, range, unit, TrackTint.forAdjustment.
    static readonly (string Title, string Field, float Min, float Max, string Unit, IBrush? Tint)[] LocalSet = [
        ("Exposure", "local_exposure_ev", -3, 3, " EV", null),
        ("Contrast", "local_contrast", -1, 1, "", null),
        ("Highlights", "local_highlights", -1, 1, "", null),
        ("Shadows", "local_shadows", -1, 1, "", null),
        ("Whites", "local_whites", -1, 1, "", null),
        ("Blacks", "local_blacks", -1, 1, "", null),
        ("Saturation", "local_saturation", -1, 1, "", TrackTint.Presence),
        ("Warmth", "local_warmth", -1, 1, "", TrackTint.Warmth),
        ("Tint", "local_tint", -1, 1, "", TrackTint.LocalTint),
        ("Noise", "local_denoise_luma", 0, 4, "", null),
        ("Color noise", "local_denoise_color", 0, 4, "", null),
    ];

    /// A layer's defaults are zero, not the photograph's (AdjustmentGroup.reset).
    public static IEnumerable<Control> Local(int layer) =>
        LocalSet.Select(s => new AdjustmentSlider(s.Title, s.Field, s.Min, s.Max, s.Unit, 2, layer, baseValue: 0, tint: s.Tint));
}
