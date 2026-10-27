using Avalonia;
using Avalonia.Controls;
using Avalonia.Layout;
using Avalonia.Media;

namespace OrionShell.CanvasSlice;

/// A port of the `.crop` tab in app/OrionApp+Tools.swift: the one tab whose
/// controls are the geometry rather than the develop state.
[Panel("tools-crop", Region.Tools, 10, Tab = "Crop")]
public sealed class CropTools : StackPanel {
    /// Ratios photographers actually shoot and print to; null is the original frame.
    static readonly (string Name, double? Ratio)[] Ratios =
        [("Original", null), ("1:1", 1), ("4:5", 0.8), ("3:2", 1.5), ("16:9", 16.0 / 9.0)];

    public CropTools() {
        Spacing = Space.Xl;
        if (CanvasState.Alone("tools-crop")) CanvasScenes.CropAngle();

        Children.Add(new SectionPlate("Aspect", null, AspectGrid()));
        Children.Add(Straighten());
        Children.Add(new SectionPlate("Rotate", "The camera's own orientation is applied automatically; this rotates on top of it.",
            new StackPanel {
                Orientation = Orientation.Horizontal, Spacing = 8,
                Children = { Bordered("rotate.left", "Left", () => CropEdit.Rotate(-1)), Bordered("rotate.right", "Right", () => CropEdit.Rotate(1)) },
            }));
        Children.Add(new SectionPlate("Perspective",
            "Straightens converging lines — a building shot looking up, a wall shot from one side. The frame zooms to stay full, " +
            "as the lens corrections do. Aspect undoes the squeeze a strong correction leaves behind.",
            new AdjustmentSlider("Vertical", "perspective_vertical", -1, 1, baseValue: 0),
            new AdjustmentSlider("Horizontal", "perspective_horizontal", -1, 1, baseValue: 0),
            new AdjustmentSlider("Aspect", "perspective_aspect", -1, 1, baseValue: 0)));
        Children.Add(Bordered(null, "Reset crop", () => {
            CropEdit.Reset();
            // AdjustmentSlider re-reads its field only when a photo opens: rebuild the one Reset moved.
            Children[1] = Straighten();
        }));
        // stub(#315): ProposalWatcher.swift owns this until the logic moves below the facade (documentEditsLocked disables the panel)
    }

    /// `resetsTo: engine.defaults`: a fresh state's angle, which is upright.
    static SectionPlate Straighten() => new("Straighten", null,
        new AdjustmentSlider("Angle", "straighten_deg", -90, 90, "°", 1, baseValue: 0),
        new TextBlock {
            Text = "Turns the picture about the frame's center. The rectangle shrinks to stay inside the turned frame, so a corner is never empty.",
            FontSize = TypeSize.Xs, Foreground = Palette.Faint, TextWrapping = TextWrapping.Wrap,
        });

    /// Three columns, 4 apart; every modifier inside the label, so the whole cell takes the click.
    static Grid AspectGrid() {
        var grid = new Grid { ColumnDefinitions = new("*,*,*"), RowDefinitions = new("Auto,Auto"), ColumnSpacing = 4, RowSpacing = 4 };
        for (int i = 0; i < Ratios.Length; i++) {
            var (name, ratio) = Ratios[i];
            var cell = new Button {
                HorizontalAlignment = HorizontalAlignment.Stretch, HorizontalContentAlignment = HorizontalAlignment.Center,
                Padding = new Thickness(0, 7), Background = Palette.Raised, CornerRadius = new CornerRadius(4),
                BorderThickness = new Thickness(0),
                Content = new TextBlock { Text = name, FontSize = TypeSize.Xs, Foreground = Palette.Dim },
            };
            cell.Click += (_, _) => CropEdit.SetAspect(ratio);
            Grid.SetColumn(cell, i % 3);
            Grid.SetRow(cell, i / 3);
            grid.Children.Add(cell);
        }
        return grid;
    }

    /// `.buttonStyle(.bordered).controlSize(.small)`: Fluent's own button, small.
    static Button Bordered(string? icon, string label, Action act) {
        var content = new StackPanel { Orientation = Orientation.Horizontal, Spacing = 5 };
        if (icon != null) content.Children.Add(Icons.Create(icon, 11, Palette.Text));
        content.Children.Add(new TextBlock { Text = label, FontSize = TypeSize.Sm, VerticalAlignment = VerticalAlignment.Center });
        var button = new Button {
            Content = content, Padding = new Thickness(8, 3), MinHeight = 0,
            CornerRadius = new CornerRadius(Radius.Md), HorizontalAlignment = HorizontalAlignment.Left,
        };
        button.Click += (_, _) => act();
        return button;
    }
}
