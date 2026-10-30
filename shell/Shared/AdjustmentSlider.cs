using System.Globalization;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.Interactivity;
using Avalonia.Layout;
using Avalonia.Media;
using Avalonia.Threading;

namespace OrionShell;

/// A port of the Swift app's AdjustmentSlider.swift bound to one OrionAdjustments field:
/// a name, a boxed readout in the numeral face that turns accent when the value
/// is off its default, and an AnalogTrack marked in the slider's unit. Click the
/// readout, or Tab to it and press Return, to type (Return commits, Escape
/// cancels, Up/Down nudge, Shift for ten). Double-click the track to reset.
/// A value that moves from outside a drag (Auto, undo, a preset, a reset, a
/// typed value) glides there; a photo opening shows its values at once.
public sealed class AdjustmentSlider : StackPanel {
    /// Bubbles when Modified flips, so a SectionPlate can light its dot.
    public static readonly RoutedEvent<RoutedEventArgs> ModifiedChangedEvent =
        RoutedEvent.Register<AdjustmentSlider, RoutedEventArgs>("ModifiedChanged", RoutingStrategies.Bubble);

    /// The readout's box at its narrowest (Controls.dc.html).
    const double ReadoutWidth = 54, ReadoutPad = 6;

    readonly string _name, _field, _unit;
    readonly int _index, _decimals;
    readonly float? _fixedBase;
    readonly AnalogTrack _track;
    readonly Readout _window = new() { Height = 21, Padding = new Thickness(ReadoutPad, 0), Focusable = true };
    readonly TextBlock _text = Faces.Figure("");
    TextBox? _editor;
    bool _hovering, _modified, _keyboardFocus;

    public float Value { get; private set; }
    public float Base { get; private set; }
    public bool Modified => _modified;

    /// `baseValue` is the control's default; null means the value the photo opened with.
    /// `scale` marks the track; null means the one `unit` implies (Scale.For).
    public AdjustmentSlider(string name, string field, float min, float max, string unit = "", int decimals = 2,
                            int index = 0, float? baseValue = null, IBrush? tint = null, Scale? scale = null) {
        (_name, _field, _unit, _index, _decimals, _fixedBase) = (name, field, unit, index, decimals, baseValue);
        Spacing = 5;
        _track = new AnalogTrack { Min = min, Max = max, Tint = tint, Scale = scale ?? Scale.For(unit, min, max) };
        _track.Changed += v => Apply(v);
        _track.Interacting += on => { if (on) EngineModel.Shared.BeginInteraction(); else EngineModel.Shared.EndInteraction(); };

        // As wide as the widest value it can show, so the box never moves while the value is dragged.
        _window.MinWidth = Math.Max(ReadoutWidth, Math.Ceiling(Math.Max(FigureWidth(Text(min)), FigureWidth(Text(max)))) + 2 * ReadoutPad);
        _text.HorizontalAlignment = HorizontalAlignment.Right;
        _window.Child = _text;
        _window.PointerEntered += (_, _) => { _hovering = true; Paint(); };
        _window.PointerExited += (_, _) => { _hovering = false; Paint(); };
        // A click types at once: the reset is the track's double-click, so nothing waits out a double-click here.
        _window.Tapped += (_, _) => BeginEditing();
        _window.KeyDown += (_, e) => {
            if (e.Key is Key.Enter or Key.Space && _editor == null) { BeginEditing(); e.Handled = true; }
        };
        // The box's own focus only: the field inside it bubbles its focus here too, and
        // repainting on that, mid-swap, put one TextBlock in two parents (u3's crash report).
        _window.GotFocus += (_, e) => {
            if (e.Source != _window) return;
            _keyboardFocus = e.NavigationMethod is NavigationMethod.Tab or NavigationMethod.Directional;
            Paint();
        };
        _window.LostFocus += (_, e) => {
            if (e.Source != _window) return;
            _keyboardFocus = false;
            Paint();
        };

        var head = new DockPanel();
        DockPanel.SetDock(_window, Dock.Right);
        head.Children.Add(_window);
        head.Children.Add(Engraved.Label(name));
        Children.Add(head);
        Children.Add(_track);

        EngineModel.Shared.Opened += Reload;
        EngineModel.Shared.ValuesChanged += Reread;
        Reload();
    }

    static double FigureWidth(string figure) {
        var text = new FormattedText(figure, CultureInfo.InvariantCulture, FlowDirection.LeftToRight, Faces.NumeralFace, Faces.Readout, Brushes.White);
        text.SetFontFeatures(Faces.Tabular);
        return text.WidthIncludingTrailingWhitespace;
    }

    /// The value moved under the slider (Auto, undo, a preset): follow it, keep the detent.
    void Reread() {
        Value = EngineModel.Shared.Get(_field, _index);
        _track.Glide(Value);
        Paint();
    }

    /// A photo opened: its values, shown at once (the arrow-key path).
    void Reload() {
        Value = EngineModel.Shared.Get(_field, _index);
        Base = _fixedBase ?? Value;
        _track.Base = Base;
        _track.Set(Value);
        Paint(opened: true);
    }

    /// A drag's value lands at once; one set from outside the drag glides.
    void Apply(float v, bool glide = false) {
        Value = v;
        if (glide) _track.Glide(v);
        else _track.Set(v);
        EngineModel.Shared.Set(_field, v, _index);
        Paint();
    }

    /// As the meter prints it: a true minus, and none on a value that shows as zero.
    string Text(float v) {
        string digits = MathF.Abs(v).ToString("F" + _decimals, CultureInfo.InvariantCulture);
        return (v < 0 && digits.Any(d => d is >= '1' and <= '9') ? "−" : "") + digits + _unit;
    }

    /// The readout's states (Controls.dc.html): at rest the box is engraved in
    /// ground; hover lifts its edge; typing rings it in the accent; keyboard
    /// focus gets the two-ring halo. Runs on every drag tick, so it touches only
    /// what changed.
    void Paint(bool opened = false) {
        bool modified = AdjustmentMath.IsModified(Value, Base, _decimals), editing = _editor != null;
        Control content = (Control?)_editor ?? _text;
        if (_window.Child != content) _window.Child = content;
        _text.Text = Text(Value);
        _text.Foreground = modified && !editing ? Palette.Accent : Palette.Text;
        var edge = editing ? Palette.Accent : _hovering ? Palette.Rail : Palette.Line;
        bool ring = _keyboardFocus && !editing;
        if (_window.Edge != edge || _window.Ring != ring) {
            (_window.Edge, _window.Ring) = (edge, ring);
            _window.InvalidateVisual();
        }
        if (modified == _modified && !opened) return;
        ToolTip.SetTip(_window, modified ? $"Click to type a value. Double-click the track to reset {_name} to {Text(Base)}"
                                         : "Click to type a value");
        if (modified == _modified) return;
        _modified = modified;
        RaiseEvent(new ModifiedChangedEventArgs(opened));
    }

    void BeginEditing() {
        if (_editor != null) return;
        _editor = new TextBox {
            Text = Value.ToString("F" + _decimals, CultureInfo.InvariantCulture),
            FontFamily = Faces.Numeral, FontWeight = FontWeight.Medium, FontFeatures = Faces.Tabular, FontSize = Faces.Readout,
            MinWidth = 40, Padding = new Thickness(0), BorderThickness = new Thickness(0), Background = Brushes.Transparent,
            TextAlignment = TextAlignment.Right, MinHeight = 0, VerticalAlignment = VerticalAlignment.Center,
            CaretBrush = Palette.Accent, SelectionBrush = new SolidColorBrush(((ISolidColorBrush)Palette.Accent).Color, 0.35),
        };
        _editor.KeyDown += (_, e) => {
            switch (e.Key) {
                case Key.Enter: Commit(); e.Handled = true; break;
                case Key.Escape: Close(); e.Handled = true; break;
                case Key.Up or Key.Down:
                    int steps = (e.Key == Key.Up ? 1 : -1) * (e.KeyModifiers.HasFlag(KeyModifiers.Shift) ? 10 : 1);
                    if (AdjustmentMath.Nudged(_editor!.Text ?? "", steps, _unit, _decimals, _track.Min, _track.Max) is { } n)
                        _editor.Text = n.ToString("F" + _decimals, CultureInfo.InvariantCulture);
                    e.Handled = true;
                    break;
            }
        };
        _editor.LostFocus += (_, _) => Commit();
        Paint();
        Dispatcher.UIThread.Post(() => { _editor?.Focus(); _editor?.SelectAll(); });
    }

    void Commit() {
        if (_editor == null) return;
        var parsed = AdjustmentMath.Parsed(_editor.Text ?? "", _unit, _track.Min, _track.Max);
        Close();
        if (parsed is { } v && v != Value) Apply(v, glide: true);
    }

    void Close() {
        _editor = null;
        Paint();
    }

    /// The readout's box: a 1 px edge round a fill, and StateButton's focus halo.
    /// Drawn rather than a Border, whose Render is sealed and whose BoxShadow
    /// clips to it in Avalonia 12.
    sealed class Readout : Decorator {
        public IBrush Edge = Palette.Line;
        public bool Ring;
        public override void Render(DrawingContext c) {
            var r = new Rect(Bounds.Size);
            c.DrawRectangle(Palette.Ground, new Pen(Edge, 1), r.Deflate(0.5), Radius.Sm, Radius.Sm);
            if (Ring) StateButton.DrawFocusRing(c, r, Radius.Sm);
        }
    }
}

/// ModifiedChanged's arguments. Opened: the change came with a photo opening,
/// the arrow-key path, where nothing animates.
public sealed class ModifiedChangedEventArgs(bool opened) : RoutedEventArgs(AdjustmentSlider.ModifiedChangedEvent) {
    public bool Opened { get; } = opened;
}
