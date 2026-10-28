using Avalonia;
using Avalonia.Animation;
using Avalonia.Animation.Easings;
using Avalonia.Controls;
using Avalonia.Controls.Presenters;
using Avalonia.Controls.Shapes;
using Avalonia.Controls.Templates;
using Avalonia.Input;
using Avalonia.Layout;
using Avalonia.Media;
using Avalonia.Media.Immutable;
using Avalonia.Media.Transformation;

namespace OrionShell;

/// app/ToolButton.swift's CanvasTool: what a click on the canvas does.
public enum CanvasTool { None, Targeted, MaskColor, Spot }

/// A button in every state of Controls.dc.html: rest, hover, pressed (scale .97
/// over 100 ms, ease-out, from pointer-down), keyboard focus as a two-ring halo
/// (ground, then accent), disabled, and `On`, a toggle that is on: an accent
/// wash inside an accent border. Its own template, not Fluent's, so no theme
/// state paints over these. Content is a TextBlock, an Icons path, or a panel
/// of them; the button inks them for its state.
public class StateButton : Button {
    /// 24 is the floor (WCAG 2.2 target size); the boards' in-panel buttons are 26.
    public const double MinSize = 26;

    static Color Of(IBrush b) => ((ISolidColorBrush)b).Color;
    static readonly IBrush Wash = new ImmutableSolidColorBrush(Of(Palette.Accent), 0.12);
    static readonly IPen GroundRing = new Pen(Palette.Ground, 2), AccentRing = new Pen(Palette.Accent, 2);
    static readonly ITransform Rest = TransformOperations.Parse("scale(1)"), Down = TransformOperations.Parse("scale(0.97)");
    static readonly FuncControlTemplate<Button> Plain = new((b, _) => new ContentPresenter {
        Name = "PART_ContentPresenter",
        [!ContentPresenter.ContentProperty] = b[!ContentProperty],
        [!ContentPresenter.BackgroundProperty] = b[!BackgroundProperty],
        [!ContentPresenter.BorderBrushProperty] = b[!BorderBrushProperty],
        [!ContentPresenter.BorderThicknessProperty] = b[!BorderThicknessProperty],
        [!ContentPresenter.CornerRadiusProperty] = b[!CornerRadiusProperty],
        [!ContentPresenter.PaddingProperty] = b[!PaddingProperty],
        HorizontalContentAlignment = HorizontalAlignment.Center,
        VerticalContentAlignment = VerticalAlignment.Center,
    });

    bool _on, _keyboardFocus;

    public StateButton() {
        Template = Plain;
        Height = MinSize;
        MinWidth = MinSize;
        Padding = new Thickness(10, 0);
        CornerRadius = new CornerRadius(Radius.Md);
        BorderThickness = new Thickness(1);
        FocusAdorner = null;
        ClipToBounds = false;   // the halo is drawn outside the bounds
        // ponytail: no reduced-motion check; Avalonia 12 exposes none. A 3% scale is the only motion here.
        Transitions = [new TransformOperationsTransition {
            Property = RenderTransformProperty, Duration = TimeSpan.FromMilliseconds(100), Easing = new CubicEaseOut(),
        }];
        Paint();
    }

    /// A toggle that is on (Compare, an armed tool).
    public bool On { get => _on; set { _on = value; Paint(); } }

    protected override void OnPropertyChanged(AvaloniaPropertyChangedEventArgs change) {
        base.OnPropertyChanged(change);
        if (change.Property == IsPointerOverProperty || change.Property == IsPressedProperty ||
            change.Property == IsEffectivelyEnabledProperty || change.Property == ContentProperty)
            Paint();
    }

    protected override void OnGotFocus(FocusChangedEventArgs e) {
        base.OnGotFocus(e);
        _keyboardFocus = e.NavigationMethod is NavigationMethod.Tab or NavigationMethod.Directional;
        Paint();
    }

    protected override void OnLostFocus(FocusChangedEventArgs e) {
        base.OnLostFocus(e);
        _keyboardFocus = false;
        Paint();
    }

    void Paint() {
        bool enabled = IsEffectivelyEnabled, lit = enabled && (IsPointerOver || IsPressed);
        Background = _on ? Wash : lit ? Palette.Hover : Palette.Raised;
        BorderBrush = _on ? Palette.Accent : lit ? Palette.Rail : Palette.Line;
        RenderTransform = enabled && IsPressed ? Down : Rest;
        InvalidateVisual();
        Ink(Content, !enabled ? Palette.Faint : _on ? Palette.Accent : Palette.Text);
    }

    public override void Render(DrawingContext c) {
        base.Render(c);
        if (_keyboardFocus) DrawFocusRing(c, new Rect(Bounds.Size), Radius.Md);
    }

    /// The keyboard-focus halo around `r`: 2 px of ground, then 2 px of accent
    /// (CSS's `0 0 0 2px ground, 0 0 0 4px accent`). Drawn, not a BoxShadow: in
    /// Avalonia 12 a spread shadow on the template hid the button's label.
    public static void DrawFocusRing(DrawingContext c, Rect r, double radius) {
        c.DrawRectangle(null, GroundRing, r.Inflate(1), radius + 1, radius + 1);
        c.DrawRectangle(null, AccentRing, r.Inflate(3), radius + 3, radius + 3);
    }

    // Theme.axaml styles every TextBlock's Foreground, which beats inheritance: ink them directly.
    static void Ink(object? content, IBrush ink) {
        switch (content) {
            case TextBlock t: t.Foreground = ink; break;
            case Shape s when s.Fill != null: s.Fill = ink; break;
            case Shape s: s.Stroke = ink; break;
            case Panel p: foreach (var child in p.Children) Ink(child, ink); break;
        }
    }
}

/// A port of app/ToolButton.swift: arms one canvas tool, or disarms it.
/// The armed tool is EngineModel.Tool, shared by every panel and the canvas.
public sealed class ToolButton : StateButton {
    readonly CanvasTool _tool;
    readonly string _icon, _armedIcon, _label, _armedLabel;
    readonly StackPanel _content = new() { Orientation = Orientation.Horizontal, Spacing = 6 };

    public ToolButton(CanvasTool tool, string icon, string label, string armedLabel, string? armedIcon = null, string help = "") {
        (_tool, _icon, _armedIcon, _label, _armedLabel) = (tool, icon, armedIcon ?? icon, label, armedLabel);
        Content = _content;
        if (help.Length > 0) ToolTip.SetTip(this, help);
        Click += (_, _) => EngineModel.Shared.Tool = Armed ? CanvasTool.None : _tool;
        EngineModel.Shared.PropertyChanged += (_, e) => { if (e.PropertyName == nameof(EngineModel.Tool)) Refresh(); };
        Refresh();
    }

    bool Armed => EngineModel.Shared.Tool == _tool;

    void Refresh() {
        _content.Children.Clear();
        _content.Children.Add(Icons.Create(Armed ? _armedIcon : _icon, 13, Palette.Text));
        _content.Children.Add(new TextBlock { Text = Armed ? _armedLabel : _label, FontSize = Faces.Label, VerticalAlignment = VerticalAlignment.Center });
        On = Armed;   // and inks the new content
    }
}
