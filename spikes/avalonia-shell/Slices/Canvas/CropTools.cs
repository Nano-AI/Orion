using Avalonia;
using Avalonia.Controls;
using Avalonia.Layout;

namespace OrionShell.CanvasSlice;

/// A port of the `.crop` tab in app/OrionApp+Tools.swift: the one tab whose
/// controls are the geometry rather than the develop state.
[Panel("tools-crop", Region.Tools, 10, Tab = "Crop")]
public sealed class CropTools : StackPanel {
    /// Ratios photographers actually shoot and print to; null is the original frame.
    static readonly (string Name, double? Ratio)[] Ratios =
        [("Original", null), ("1:1", 1), ("4:5", 0.8), ("3:2", 1.5), ("16:9", 16.0 / 9.0)];

    readonly Segmented _aspect = new(Ratios.Select(r => r.Name).ToList(), 0) { CellPadding = new Thickness(2, 0) };

    public CropTools() {
        Spacing = SectionPlate.Gap;
        if (CanvasState.Alone("tools-crop")) CanvasScenes.CropAngle();

        _aspect.Changed += i => CropEdit.SetAspect(Ratios[i].Ratio);
        EngineModel.Shared.Rendered += ShowAspect;
        ShowAspect();
        Children.Add(new SectionPlate("Aspect", null, _aspect));
        Children.Add(Straighten());
        var rotate = new Grid { ColumnDefinitions = new("*,*"), ColumnSpacing = 6, Children = {
            new StateButton("rotate.left", "Rotate left", () => CropEdit.Rotate(-1)) { Height = 28, HorizontalAlignment = HorizontalAlignment.Stretch },
            new StateButton("rotate.right", "Rotate right", () => CropEdit.Rotate(1)) { Height = 28, HorizontalAlignment = HorizontalAlignment.Stretch },
        } };
        Grid.SetColumn(rotate.Children[1], 1);
        Children.Add(new SectionPlate("Rotate", "The camera's own orientation is applied automatically; this rotates on top of it.", rotate));
        Children.Add(new SectionPlate("Perspective",
            "Straightens converging lines — a building shot looking up, a wall shot from one side. The frame zooms to stay full, " +
            "as the lens corrections do. Aspect undoes the squeeze a strong correction leaves behind.",
            new AdjustmentSlider("Vertical", "perspective_vertical", -1, 1, baseValue: 0),
            new AdjustmentSlider("Horizontal", "perspective_horizontal", -1, 1, baseValue: 0),
            new AdjustmentSlider("Aspect", "perspective_aspect", -1, 1, baseValue: 0)));
        Children.Add(new StateButton(null, "Reset crop", () => {
            CropEdit.Reset();
            // AdjustmentSlider re-reads its field only when a photo opens: rebuild the one Reset moved.
            Children[1] = Straighten();
        }, ButtonKind.Quiet) { HorizontalAlignment = HorizontalAlignment.Left });
        // stub(#315): ProposalWatcher.swift owns this until the logic moves below the facade (documentEditsLocked disables the panel)
    }

    /// The ratio the crop has now: the whole frame is Original; a ratio within
    /// a percent of one in the list is that one; else nothing is chosen.
    void ShowAspect() {
        var c = CropEdit.Crop;
        double ratio = c.Height > 0 ? c.Width * CanvasNative.FrameAspect / c.Height : 0;
        _aspect.Selected = c == new Rect(0, 0, 1, 1) ? 0
            : Array.FindIndex(Ratios, r => r.Ratio is { } x && Math.Abs(ratio / x - 1) < 0.01);
    }

    /// `resetsTo: engine.defaults`: a fresh state's angle, which is upright.
    static SectionPlate Straighten() => new("Straighten", null,
        new AdjustmentSlider("Angle", "straighten_deg", -90, 90, "°", 1, baseValue: 0),
        Engraved.Note("Turns the picture about the frame's center. The rectangle shrinks to stay inside the turned frame, so a corner is never empty."));
}
