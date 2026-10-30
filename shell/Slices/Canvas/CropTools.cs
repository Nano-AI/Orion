using Avalonia;
using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.Interactivity;
using Avalonia.Layout;

namespace OrionShell.CanvasSlice;

/// A port of the `.crop` tab in the Swift app's OrionApp+Tools.swift: the one tab whose
/// controls are the geometry rather than the develop state.
[Panel("tools-crop", Region.Tools, 10, Tab = "Crop")]
public sealed class CropTools : StackPanel {
    /// Ratios photographers actually shoot and print to; null is the original frame.
    static readonly (string Name, double? Ratio)[] Ratios =
        [("Original", null), ("1:1", 1), ("4:5", 0.8), ("3:2", 1.5), ("16:9", 16.0 / 9.0)];

    readonly Segmented _aspect = new(Ratios.Select(r => r.Name).ToList(), 0) { CellPadding = new Thickness(Space.Xs, 0) };

    public CropTools() {
        Spacing = SectionPlate.Gap;
        if (CanvasState.Alone("tools-crop")) CanvasScenes.CropAngle();

        _aspect.Changed += i => CropEdit.SetAspect(Ratios[i].Ratio);
        EngineModel.Shared.Rendered += ShowAspect;
        ShowAspect();
        Children.Add(new SectionPlate("Aspect", null, _aspect));
        Children.Add(Straighten());
        var rotate = new Grid { ColumnDefinitions = new("*,*"), ColumnSpacing = Space.Sm, Children = {
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
        // Reset tells the sliders (ValuesChanged), so the Angle slider follows it, from here or from Escape.
        Children.Add(new StateButton(null, "Reset crop", CropEdit.Reset, ButtonKind.Quiet) { HorizontalAlignment = HorizontalAlignment.Left });
        // documentEditsLocked disables the panel content: the window's tool column (ProposalLock).
    }

    /// handleKey's Return and Escape while the crop tab is up. Swift's key monitor
    /// sees a key before any control; the window's command keys arrive last, after
    /// the focused control. The Crop tab button the person just clicked keeps focus,
    /// and a Button takes Return as its own click, so Return re-opened the tab and
    /// never reached apply-crop. Tunnelling, the crop hears it first, from any
    /// control but a text box, which keeps its own Return and Escape (NSTextView).
    protected override void OnAttachedToVisualTree(VisualTreeAttachmentEventArgs e) {
        base.OnAttachedToVisualTree(e);
        TopLevel.GetTopLevel(this)?.AddHandler(KeyDownEvent, CropKey, RoutingStrategies.Tunnel);
    }

    protected override void OnDetachedFromVisualTree(VisualTreeAttachmentEventArgs e) {
        TopLevel.GetTopLevel(this)?.RemoveHandler(KeyDownEvent, CropKey);
        base.OnDetachedFromVisualTree(e);
    }

    static void CropKey(object? sender, KeyEventArgs e) {
        if (e.Handled || e.Source is TextBox || Chrome.AssistantPanel.Owns(e) || e.KeyModifiers != KeyModifiers.None) return;
        string? command = e.Key switch { Key.Enter => "apply-crop", Key.Escape => "cancel-crop", _ => null };
        if (command == null || !Commands.CanRun(command)) return;
        Commands.Run(command);
        e.Handled = true;
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
