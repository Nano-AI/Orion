using Avalonia;
using Avalonia.Automation;
using Avalonia.Controls;
using Avalonia.Controls.Presenters;
using Avalonia.Controls.Templates;
using Avalonia.Layout;
using Avalonia.Media;
using Avalonia.Media.Immutable;

namespace OrionShell;

/// OrionApp+Chrome.swift's `chip` and `iconChip`: a plain button one height
/// for the whole toolbar, outlined in the hairline, dim ink or faint when off.
///
/// Its own template, not Fluent's: SwiftUI's `.plain` style has no hover fill
/// and no disabled fill, and Fluent's would grey the undo chips in every shot.
public sealed class Chip : Button {
    public const double ChipHeight = 23;

    static readonly FuncControlTemplate<Button> Plain = new((b, _) => new ContentPresenter {
        [!ContentPresenter.ContentProperty] = b[!ContentProperty],
        [!ContentPresenter.BackgroundProperty] = b[!BackgroundProperty],
        [!ContentPresenter.BorderBrushProperty] = b[!BorderBrushProperty],
        [!ContentPresenter.BorderThicknessProperty] = b[!BorderThicknessProperty],
        [!ContentPresenter.CornerRadiusProperty] = b[!CornerRadiusProperty],
        [!ContentPresenter.PaddingProperty] = b[!PaddingProperty],
        HorizontalContentAlignment = HorizontalAlignment.Center,
        VerticalContentAlignment = VerticalAlignment.Center,
    });

    public Chip(Control content, string help, Action action, bool enabled = true) {
        Template = Plain;
        Content = content;
        Height = ChipHeight;
        Padding = new Thickness(9, 0);
        CornerRadius = new CornerRadius(Radius.Md);
        BorderThickness = new Thickness(1);
        BorderBrush = Palette.Line;
        Background = Brushes.Transparent;
        VerticalAlignment = VerticalAlignment.Center;
        IsEnabled = enabled;
        if (help.Length > 0) { ToolTip.SetTip(this, help); AutomationProperties.SetName(this, help); }
        Click += (_, _) => action();
    }

    /// A word: `chip(title, enabled:)`.
    public static Chip Text(string title, bool enabled, Action action, string help = "") =>
        new(Ink(title, enabled ? Palette.Dim : Palette.Faint), help, action, enabled);

    /// A symbol, 26 wide: `iconChip(symbol, name:, enabled:)`. `name` is the
    /// tooltip and the spoken label both, as in Swift.
    public static Chip Icon(string symbol, string name, bool enabled, Action action) =>
        new(Icons.Create(symbol, 12, enabled ? Palette.Dim : Palette.Faint), name, action, enabled) {
            Width = 26, Padding = new Thickness(0),
        };

    public static TextBlock Ink(string text, IBrush ink, double size = TypeSize.Sm) =>
        new() { Text = text, FontSize = size, Foreground = ink, VerticalAlignment = VerticalAlignment.Center };

    /// `color` at `opacity`, for the Export chip's accent wash.
    public static IBrush Faded(IBrush color, double opacity) =>
        new ImmutableSolidColorBrush(((ISolidColorBrush)color).Color, opacity);

    /// The hairline between the toolbar's groups: a rule reads as a boundary.
    public static Control Divider() =>
        new Border { Width = 1, Height = 18, Background = Palette.Line, VerticalAlignment = VerticalAlignment.Center };
}
