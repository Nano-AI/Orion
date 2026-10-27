using Avalonia.Controls;
using Avalonia.Layout;

namespace OrionShell;

/// Seed from the #315 spike: the Chrome slice replaces it with a port of
/// OrionApp+Chrome.swift. Wordmark and file left, modes centre, timing right.
[Panel("toolbar", Region.Toolbar)]
public sealed class ToolbarPanel : Grid {
    public ToolbarPanel() {
        ColumnDefinitions = new("Auto,*,Auto");
        var engine = EngineModel.Shared;
        var file = new TextBlock { Classes = { "label" } };
        var timing = new TextBlock { Classes = { "label", "tnum" }, VerticalAlignment = VerticalAlignment.Center };

        var left = new StackPanel { Orientation = Orientation.Horizontal, Spacing = Space.Md, VerticalAlignment = VerticalAlignment.Center,
            Children = { new TextBlock { Text = "ORION", FontWeight = Avalonia.Media.FontWeight.SemiBold, LetterSpacing = 2 }, file } };
        var modes = new StackPanel { Orientation = Orientation.Horizontal, Spacing = 22,
            HorizontalAlignment = HorizontalAlignment.Center, VerticalAlignment = VerticalAlignment.Center,
            Children = {
                new TextBlock { Text = "Library", Classes = { "label" } },
                new TextBlock { Text = "Develop", Foreground = Palette.Accent, FontSize = TypeSize.Sm },  // stub(#315): mode switching lives in OrionApp.swift
            } };
        SetColumn(modes, 1);
        SetColumn(timing, 2);
        Children.Add(left);
        Children.Add(modes);
        Children.Add(timing);

        void Refresh() { file.Text = engine.CurrentName; timing.Text = engine.Timing; }
        engine.PropertyChanged += (_, _) => Refresh();
        Refresh();
    }
}
