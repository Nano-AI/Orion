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

/// the Swift app's ToolButton.swift's CanvasTool: what a click on the canvas does.
/// MaskDraw: a mask shape being drawn on the photo, one armed tool among the rest (#211).
public enum CanvasTool { None, Targeted, MaskColor, Spot, MaskDraw }

/// The buttons of Controls.dc.html, ranked by importance rather than coloured by meaning.
public enum ButtonKind {
    /// Raised fill in a hairline: Open, Auto, Rotate left.
    Standard,
    /// No fill, a hairline, dim ink: Reset, Reset crop, Delete rejected….
    Quiet,
    /// The accent, filled, in the regular weight as macOS draws a default button: Export. One per screen.
    Primary,
    /// A cell of a segmented control: no edge, raised while `On`.
    Segment,
    /// No fill and no edge until hovered: icon groups, stars, text links.
    Bare,
}

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
    /// The primary button under the pointer: the accent a quarter of the way to text (Main.dc.html's a:hover).
    static readonly IBrush AccentLift = new ImmutableSolidColorBrush(Color.FromRgb(
        (byte)((Of(Palette.Accent).R * 3 + Of(Palette.Text).R) / 4), (byte)((Of(Palette.Accent).G * 3 + Of(Palette.Text).G) / 4),
        (byte)((Of(Palette.Accent).B * 3 + Of(Palette.Text).B) / 4)));
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
        [!ContentPresenter.HorizontalContentAlignmentProperty] = b[!HorizontalContentAlignmentProperty],
        [!ContentPresenter.VerticalContentAlignmentProperty] = b[!VerticalContentAlignmentProperty],
    });

    bool _on, _keyboardFocus;
    ButtonKind _kind;
    IBrush? _ink;

    /// A word, optionally after an icon, in the label size; `help` is the tooltip and the spoken name.
    public StateButton(string? icon, string title, Action click, ButtonKind kind = ButtonKind.Standard, string help = "") : this() {
        Kind = kind;
        var text = new TextBlock { Text = title, FontSize = Faces.Body, VerticalAlignment = VerticalAlignment.Center };
        Content = icon == null ? text : new StackPanel { Orientation = Orientation.Horizontal, Spacing = Space.Sm,
            Children = { Icons.Create(icon, IconSize.Md, Palette.Text), text } };
        Help(help);
        Click += (_, _) => click();
    }

    /// An icon alone: `help` names it, because nothing else does.
    public static StateButton Glyph(string icon, string help, Action click, ButtonKind kind = ButtonKind.Bare, double size = IconSize.Md) {
        var b = new StateButton { Kind = kind, Content = Icons.Create(icon, size, Palette.Text), Padding = new Thickness(0), Width = 30 };
        b.Help(help);
        b.Click += (_, _) => click();
        return b;
    }

    public StateButton Help(string help) {
        if (help.Length == 0) return this;
        ToolTip.SetTip(this, help);
        Avalonia.Automation.AutomationProperties.SetName(this, help);
        return this;
    }

    public ButtonKind Kind {
        get => _kind;
        set {
            _kind = value;
            BorderThickness = new Thickness(value is ButtonKind.Segment or ButtonKind.Bare ? 0 : 1);
            if (value == ButtonKind.Primary) Padding = new Thickness(Space.Pad, 0);
            Paint();
        }
    }

    /// The content's ink at rest, when the kind's own is wrong for it (a star, a reject mark).
    public IBrush? Ink { get => _ink; set { _ink = value; Paint(); } }

    /// The first word on the button, for finding one by its label.
    public string? Label => Words(Content).FirstOrDefault();
    static IEnumerable<string> Words(object? content) => content switch {
        string s => [s],
        TextBlock t => t.Text is { } s ? [s] : [],
        Panel p => p.Children.SelectMany(c => Words(c)),
        _ => [],
    };

    public StateButton() {
        Template = Plain;
        // Centred unless a caller says otherwise (LensPicker's left-aligned list button).
        HorizontalContentAlignment = HorizontalAlignment.Center;
        VerticalContentAlignment = VerticalAlignment.Center;
        Height = MinSize;
        MinWidth = MinSize;
        Padding = new Thickness(Space.Md, 0);
        CornerRadius = new CornerRadius(Radius.Md);
        BorderThickness = new Thickness(1);
        FocusAdorner = null;
        ClipToBounds = false;   // the halo is drawn outside the bounds
        // Kept under reduced motion (Motion.Reduced): a 3% press in place is feedback, not travel.
        Transitions = [new TransformOperationsTransition {
            Property = RenderTransformProperty, Duration = TimeSpan.FromMilliseconds(100), Easing = Motion.EaseOut,
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
        (Background, BorderBrush, var ink) = _kind switch {
            _ when !enabled && _kind == ButtonKind.Primary => (Palette.Raised, Palette.Line, Palette.Faint),
            ButtonKind.Primary => (lit ? AccentLift : Palette.Accent, lit ? AccentLift : Palette.Accent, Palette.Ground),
            ButtonKind.Segment => (_on ? Palette.Raised : lit ? Palette.Panel : Brushes.Transparent, Brushes.Transparent,
                                   _on ? _ink ?? Palette.Text : lit ? Palette.Text : _ink ?? Palette.Dim),
            _ when _on => (Wash, Palette.Accent, Palette.Accent),
            ButtonKind.Quiet => (lit ? Palette.Raised : Brushes.Transparent, lit ? Palette.Rail : Palette.Line,
                                 lit ? Palette.Text : _ink ?? Palette.Dim),
            ButtonKind.Bare => (lit ? Palette.Hover : Brushes.Transparent, Brushes.Transparent, _ink ?? (lit ? Palette.Text : Palette.Label)),
            _ => (lit ? Palette.Hover : Palette.Raised, lit ? Palette.Rail : Palette.Line, _ink ?? Palette.Text),
        };
        RenderTransform = enabled && IsPressed ? Down : Rest;
        InvalidateVisual();
        InkContent(Content, enabled ? ink : Palette.Faint);
    }

    public override void Render(DrawingContext c) {
        base.Render(c);
        if (_keyboardFocus) DrawFocusRing(c, new Rect(Bounds.Size), CornerRadius.TopLeft);   // concentric with this button, whatever its radius
    }

    /// The keyboard-focus halo around `r`: 2 px of ground, then 2 px of accent
    /// (CSS's `0 0 0 2px ground, 0 0 0 4px accent`). Drawn, not a BoxShadow: in
    /// Avalonia 12 a spread shadow on the template hid the button's label.
    /// Tag a piece of content with this and the button leaves its colour alone (a count beside a word).
    public const string Keep = "keep-ink";

    public static void DrawFocusRing(DrawingContext c, Rect r, double radius) {
        c.DrawRectangle(null, GroundRing, r.Inflate(1), radius + 1, radius + 1);
        c.DrawRectangle(null, AccentRing, r.Inflate(3), radius + 3, radius + 3);
    }

    // Theme.axaml styles every TextBlock's Foreground, which beats inheritance: ink them directly.
    static void InkContent(object? content, IBrush ink) {
        if (content is Control { Tag: Keep }) return;
        switch (content) {
            case TextBlock t: t.Foreground = ink; break;
            case Shape s when s.Fill != null: s.Fill = ink; break;
            case Shape s: s.Stroke = ink; break;
            case Panel p: foreach (var child in p.Children) InkContent(child, ink); break;
        }
    }
}

/// A port of the Swift app's ToolButton.swift: arms one canvas tool, or disarms it.
/// The armed tool is EngineModel.Tool, shared by every panel and the canvas.
public sealed class ToolButton : StateButton {
    readonly CanvasTool _tool;
    readonly string _icon, _armedIcon, _label, _armedLabel;
    readonly StackPanel _content = new() { Orientation = Orientation.Horizontal, Spacing = Space.Sm };

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
        _content.Children.Add(Icons.Create(Armed ? _armedIcon : _icon, IconSize.Md, Palette.Text));
        _content.Children.Add(new TextBlock { Text = Armed ? _armedLabel : _label, FontSize = Faces.Label, VerticalAlignment = VerticalAlignment.Center });
        On = Armed;   // and inks the new content
    }
}
