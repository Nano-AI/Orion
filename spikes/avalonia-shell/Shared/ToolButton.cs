using Avalonia;
using Avalonia.Controls;
using Avalonia.Layout;

namespace OrionShell;

/// app/ToolButton.swift's CanvasTool: what a click on the canvas does.
public enum CanvasTool { None, Targeted, MaskColor, Spot }

/// A port of app/ToolButton.swift: arms one canvas tool, or disarms it.
/// The armed tool is EngineModel.Tool, shared by every panel and the canvas.
public sealed class ToolButton : Button {
    readonly CanvasTool _tool;
    readonly string _icon, _armedIcon, _label, _armedLabel;
    readonly StackPanel _content = new() { Orientation = Orientation.Horizontal, Spacing = 5 };

    public ToolButton(CanvasTool tool, string icon, string label, string armedLabel, string? armedIcon = null, string help = "") {
        (_tool, _icon, _armedIcon, _label, _armedLabel) = (tool, icon, armedIcon ?? icon, label, armedLabel);
        Height = 22;
        Padding = new Thickness(8, 0);
        CornerRadius = new CornerRadius(5);
        BorderThickness = new Thickness(1);
        Background = Avalonia.Media.Brushes.Transparent;
        VerticalContentAlignment = VerticalAlignment.Center;
        Content = _content;
        if (help.Length > 0) ToolTip.SetTip(this, help);
        Click += (_, _) => EngineModel.Shared.Tool = Armed ? CanvasTool.None : _tool;
        EngineModel.Shared.PropertyChanged += (_, e) => { if (e.PropertyName == nameof(EngineModel.Tool)) Refresh(); };
        Refresh();
    }

    bool Armed => EngineModel.Shared.Tool == _tool;

    void Refresh() {
        var ink = Armed ? Palette.Accent : Palette.Text;
        _content.Children.Clear();
        _content.Children.Add(Icons.Create(Armed ? _armedIcon : _icon, 11, ink));
        _content.Children.Add(new TextBlock { Text = Armed ? _armedLabel : _label, FontSize = 11, Foreground = ink });
        BorderBrush = Armed ? Palette.Accent : Palette.Line;
    }
}
