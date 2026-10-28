using Avalonia;
using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.Layout;
using Avalonia.Media;

namespace OrionShell.Develop;

/// A port of app/ColorWheel.swift: one grading wheel bound to a float[3]
/// field (grade_shadow, grade_midtone, grade_highlight) — the puck's x and y
/// in the unit disc, then the zone's luminance on the track underneath. Hue
/// and strength are one gesture; brightness is deliberately not on the disc.
sealed class ColorWheel : StackPanel {
    const double Size = 92, Puck = 11;
    /// One arrow press: a twentieth of the radius.
    const float Step = 0.05f;

    readonly string _title, _field;
    readonly TextBlock _name;
    readonly Control _reset;
    readonly Disc _disc;
    readonly AnalogTrack _luma = new() { Min = -0.5f, Max = 0.5f, Base = 0, Tint = TrackTint.WheelLuma, Width = Size };

    public ColorWheel(string title, string field) {
        (_title, _field) = (title, field);
        Spacing = 6;
        _name = new TextBlock { Text = title, FontSize = Faces.Mark, VerticalAlignment = VerticalAlignment.Center };
        // stub(#315): EditHistory.swift owns this until the logic moves below the facade
        var reset = StateButton.Glyph("arrow.uturn.backward", $"Reset {title}",
                                      () => { for (int i = 0; i < 3; i++) Engine.Set(_field, 0, i); _luma.Glide(0); Paint(); }, size: 9);
        (reset.Ink, reset.Width, reset.Height, reset.MinWidth, reset.MinHeight) = (Palette.Accent, 18, 18, 0, 0);
        _reset = reset;
        Children.Add(new StackPanel { Orientation = Orientation.Horizontal, Spacing = 2, HorizontalAlignment = HorizontalAlignment.Center,
                                      Height = 18, Children = { _name, _reset } });
        _disc = new Disc(this);
        Children.Add(_disc);
        // The track arms the preview graph like every AdjustmentSlider does.
        _luma.Changed += v => { Engine.Set(_field, v, 2); Paint(); };
        _luma.Interacting += on => { if (on) Engine.BeginInteraction(); else Engine.EndInteraction(); };
        Children.Add(_luma);
        Engine.Opened += Reload;
        Engine.ValuesChanged += () => { _luma.Glide(Engine.Get(_field, 2)); Paint(); };   // undo, a preset
        Reload();
    }

    static EngineModel Engine => EngineModel.Shared;

    internal (float X, float Y) Point => (Engine.Get(_field, 0), Engine.Get(_field, 1));

    void Reload() {
        _luma.Set(Engine.Get(_field, 2));
        Paint();
    }

    void Paint() {
        bool modified = Enumerable.Range(0, 3).Any(i => Math.Abs(Engine.Get(_field, i)) > 0.001f);
        _name.Foreground = modified ? Palette.Accent : Palette.Dim;
        _reset.IsVisible = modified;
        _disc.InvalidateVisual();
    }

    /// Clamped to the disc, not its bounding square: past the rim the angle
    /// still means something and the radius does not.
    internal void SetPuck(double x, double y) {
        double d = Math.Sqrt(x * x + y * y);
        if (d > 1) (x, y) = (x / d, y / d);
        Engine.Set(_field, (float)x, 0);
        Engine.Set(_field, (float)y, 1);
        Paint();
    }

    sealed class Disc : Control {
        readonly ColorWheel _wheel;
        bool _dragging;

        /// The ring at the mixer swatches' moderated chroma (decision #63), wound
        /// R, M, B, C, G, Y clockwise from three o'clock so it agrees with the
        /// engine's counter-clockwise, y-up gradeOffsets. Do not reorder it.
        static readonly IBrush Ring = RingBrush();
        static IBrush RingBrush() {
            var ring = new ConicGradientBrush { Angle = 90 };
            double[] hues = [0, 300, 240, 180, 120, 60, 0];
            for (int i = 0; i < hues.Length; i++) ring.GradientStops.Add(new GradientStop(HsvColor.ToRgb(hues[i], 0.60, 0.74), i / 6.0));
            return ring;
        }
        /// Saturation falls to nothing at the middle: the centre reads as no correction.
        static readonly IBrush Fade = FadeTo(((ISolidColorBrush)Palette.Panel).Color);
        static IBrush FadeTo(Color panel) => new RadialGradientBrush {
            GradientStops = { new(panel, 0), new(Color.FromArgb(0, panel.R, panel.G, panel.B), 1) },
        };
        static readonly IPen Cross = new Pen(new SolidColorBrush(Colors.White, 0.35), 1);

        public Disc(ColorWheel wheel) {
            _wheel = wheel;
            Width = Height = Size;
            Focusable = true;
            ToolTip.SetTip(this, wheel._title);
        }

        public override void Render(DrawingContext c) {
            var centre = new Point(Size / 2, Size / 2);
            c.DrawEllipse(Ring, null, centre, Size / 2, Size / 2);
            c.DrawEllipse(Fade, new Pen(Palette.Line, 1), centre, Size / 2 - 0.5, Size / 2 - 0.5);
            c.DrawLine(Cross, centre - new Vector(4, 0), centre + new Vector(4, 0));
            c.DrawLine(Cross, centre - new Vector(0, 4), centre + new Vector(0, 4));
            var (x, y) = _wheel.Point;
            double r = Size / 2 - Puck / 2;
            // Screen y grows downward and the wheel's does not.
            var puck = new Point(Size / 2 + x * r, Size / 2 - y * r);
            c.DrawRectangle(Brushes.White, new Pen(new SolidColorBrush(Colors.Black, 0.5), 1),
                            new RoundedRect(new Rect(puck.X - Puck / 2, puck.Y - Puck / 2, Puck, Puck), Puck / 2),
                            new BoxShadows(new BoxShadow { Blur = _dragging ? 6 : 3, Color = Color.FromArgb(128, 0, 0, 0) }));
        }

        protected override void OnPointerPressed(PointerPressedEventArgs e) {
            Focus();
            _dragging = true;
            e.Pointer.Capture(this);
            EngineModel.Shared.BeginInteraction();
            Drag(e.GetPosition(this));
        }

        protected override void OnPointerMoved(PointerEventArgs e) {
            if (_dragging) Drag(e.GetPosition(this));
        }

        void Drag(Point at) {
            double r = Size / 2 - Puck / 2;
            _wheel.SetPuck((at.X - Size / 2) / r, (Size / 2 - at.Y) / r);
        }

        protected override void OnPointerReleased(PointerReleasedEventArgs e) {
            if (_dragging) e.Pointer.Capture(null);   // ends the drag, below
        }

        /// Every drag ends here, a release or capture taken mid-drag (focus lost, a dialog):
        /// otherwise the engine stayed on the preview graph until the next drag anywhere.
        protected override void OnPointerCaptureLost(PointerCaptureLostEventArgs e) {
            base.OnPointerCaptureLost(e);
            if (!_dragging) return;
            _dragging = false;
            EngineModel.Shared.EndInteraction();
            InvalidateVisual();
        }

        /// The arrow keys reach the same two numbers the drag does, and are the
        /// only way to place a puck exactly.
        protected override void OnKeyDown(KeyEventArgs e) {
            var (x, y) = _wheel.Point;
            (float dx, float dy) = e.Key switch {
                Key.Left => (-Step, 0f), Key.Right => (Step, 0f), Key.Up => (0f, Step), Key.Down => (0f, -Step), _ => (0f, 0f),
            };
            if (dx == 0 && dy == 0) return;
            _wheel.SetPuck(x + dx, y + dy);
            e.Handled = true;
        }
    }
}
