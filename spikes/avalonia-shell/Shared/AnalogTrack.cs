using System.Globalization;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.Media;
using Avalonia.Rendering;

namespace OrionShell;

/// How a track is marked, like the scale in a light meter (DESIGN.md's
/// signature move): a tick every `Step` in the slider's own unit, and a numeral
/// on every `LabelEvery`-th tick counted from zero. Step 0 is five plain ticks
/// and no numerals, for unitless sliders.
public sealed record Scale(float Step = 0, int LabelEvery = 2, Func<float, string>? Text = null) {
    public static readonly Scale Plain = new();
    /// Exposure: a tick every stop, a numeral every other (−4 −2 0 +2 +4 on ±5).
    public static readonly Scale Stops = new(1, 2, v => Faces.Signed(v));
    /// White balance: a tick every 1000 K, a numeral every 2000 (4k 6k 8k).
    public static readonly Scale Kelvin = new(1000, 2, v => (v / 1000).ToString("0.#", CultureInfo.InvariantCulture) + "k");
    /// Angles: a tick every `step` degrees, a numeral every other; signed when the range crosses zero.
    public static Scale Degrees(float step, bool signed = true) =>
        new(step, 2, v => (signed ? Faces.Signed(v) : v.ToString("0.##", CultureInfo.InvariantCulture)) + "°");

    /// The scale a unit implies when a slider names none: EV in stops, K in
    /// Kelvin, ° in degrees (15° ticks over a quarter turn or more). Else plain.
    public static Scale For(string unit, float min, float max) => unit.Trim() switch {
        "EV" => Stops,
        "K" => Kelvin,
        "°" => Degrees(max - min >= 90 ? 15 : max - min > 30 ? 10 : 5, min < 0),
        _ => Plain,
    };

    public bool Numbered => Step > 0 && Text != null;

    /// Every tick from min to max, and whether it carries a numeral.
    public IEnumerable<(float At, bool Numbered)> Marks(float min, float max) {
        if (Step <= 0) {
            for (int i = 0; i <= 4; i++) yield return (min + i / 4f * (max - min), false);
            yield break;
        }
        for (long k = (long)MathF.Ceiling(min / Step - 1e-4f); k * Step <= max + Step * 1e-4f; k++)
            yield return (k * Step, Numbered && k % LabelEvery == 0);
    }
}

/// A port of app/AnalogTrack.swift, redrawn as a meter scale: ticks in the
/// slider's unit with numerals under them, the groove engraved in `ground`,
/// the edit filled in the accent from the default (the detent) to the value,
/// and a flat thumb. A tinted track shows its tint, and its edited span deeper.
///
/// Motion (design/DESIGN.md): a drag is 1:1 and never animates. A value set
/// from outside the drag (Glide) travels there on a Settle spring from wherever
/// the thumb is on screen. Hovering the row (the track's parent) lifts the thumb
/// and lights the marks near it; a grab presses it. Past an end the thumb
/// stretches against a rubber band and springs back at the hand's speed.
/// Double-click resets to the detent. The whole band takes a press, not only what it draws.
sealed class AnalogTrack : Control, ICustomHitTest {
    const double ThumbW = 12, ThumbH = 15, GrooveH = 4, GrooveY = 9.5, TickBottom = 6, NumeralY = 20;
    const double PlainHeight = 19, NumberedHeight = 34;
    /// Hover grows the thumb; a grab presses half of that back (the boards' pressed state).
    const double Grow = 0.12, PressBack = 0.06;
    /// How near the thumb a mark lights, and how far past an end the thumb can be pulled.
    const double Reach = 24, Stretch = ThumbW;

    public float Min, Max, Base, Value;
    public event Action<float>? Changed;
    public event Action<bool>? Interacting;   // arms the preview graph, as in Swift

    /// TrackTint: what each extreme does.
    public IBrush? Tint {
        get => _tint;
        set { _tint = value; _vivid = value == null ? null : TrackTint.Vivid(value); InvalidateVisual(); }
    }
    IBrush? _tint, _vivid;

    Scale _scale = Scale.Plain;
    public Scale Scale {
        get => _scale;
        set { _scale = value; _numerals = null; InvalidateMeasure(); InvalidateVisual(); }
    }

    readonly Spring _shown, _lift, _press, _band;
    readonly EventHandler<PointerEventArgs> _enter, _exit;
    double _startX, _bandSpeed;
    float _startValue;
    ulong _lastMove;
    ulong? _clickedAt;   // when this track last let go of a press that never moved
    bool _dragging, _departing, _rowOver, _dragged;
    InputElement? _row;
    List<(float At, FormattedText Dim, FormattedText Lit)>? _numerals;
    (float, float) _numeralsFor;

    static readonly IPen Tick = new Pen(Palette.Faint, 1), TickLit = new Pen(Palette.Label, 1);
    static readonly IPen Detent = new Pen(Palette.Accent, 1);
    static readonly IPen Engraving = new Pen(Palette.Line, 0.5), EngravingLit = new Pen(Palette.Rail, 0.5);
    static readonly Color Shade = Color.FromArgb(179, 0, 0, 0);

    public AnalogTrack() {
        _shown = new Spring(this, Motion.Settle);
        _lift = new Spring(this, Motion.Touch);
        _press = new Spring(this, Motion.Touch);
        _band = new Spring(this, Motion.Settle) { Precision = 0.05 };
        foreach (var s in new[] { _shown, _lift, _press, _band }) s.Moved += InvalidateVisual;
        _enter = (_, _) => { _rowOver = true; Hot(); };
        _exit = (_, _) => { _rowOver = false; Hot(); };
    }

    double Travel => Math.Max(Bounds.Width - ThumbW, 1);
    double X(double v) => ThumbW / 2 + (v - Min) / (Max - Min) * Travel;
    static double Crisp(double x) => Math.Round(x) + 0.5;   // a 1 px line on a pixel, not across two

    /// The value, shown at once: a drag, or a photo opening (nothing animates on the arrow keys).
    public void Set(float v) {
        Value = v;
        _shown.Snap(v);
    }

    /// The value, set from outside a drag (a reset, Auto, undo, a preset, a typed
    /// value): the thumb and fill travel there from where they are on screen.
    public void Glide(float v) {
        if (_dragging) { Set(v); return; }   // the hand owns the thumb
        Value = v;
        _shown.Precision = (Max - Min) / Travel * 0.1;   // a tenth of a pixel
        _shown.To(v);
    }

    /// Where the thumb is drawn: the spring while it travels or a grab holds it, else the value.
    internal double Shown => _shown.Moving || _dragging ? _shown.Value : Value;
    internal bool Moving => _shown.Moving || _band.Moving;
    internal double ThumbScale => 1 + Grow * _lift.Value - PressBack * _press.Value;
    internal double ThumbX => X(Shown) + _band.Value;
    internal double ValueX => X(Value);
    internal double GlideSpeed => _shown.Velocity;
    /// Steps every spring as a frame would: for tests, which cannot wait on the clock.
    internal void Advance(double seconds) {
        foreach (var s in new[] { _shown, _lift, _press, _band }) s.Step(seconds);
        InvalidateVisual();
    }

    protected override Size MeasureOverride(Size available) => new(0, _scale.Numbered ? NumberedHeight : PlainHeight);

    public override void Render(DrawingContext c) {
        double w = Bounds.Width, tx = ThumbX, hot = _lift.Value;
        // The marks near the thumb light while the row is hot: the meter read where its needle is.
        double Lit(double x) {
            double t = Math.Clamp(1 - Math.Abs(x - tx) / Reach, 0, 1);
            return hot * t * t * (3 - 2 * t);
        }
        foreach (var (at, numbered) in _scale.Marks(Min, Max)) {
            double x = Crisp(X(at)), h = _scale.Step <= 0 ? 4 : numbered ? 6 : 3;
            Point top = new(x, TickBottom - h), bottom = new(x, TickBottom);
            c.DrawLine(Tick, top, bottom);
            if (Lit(x) is var lit and > 0.01) using (c.PushOpacity(lit)) c.DrawLine(TickLit, top, bottom);
        }
        foreach (var (at, dim, bright) in Numerals()) {
            var p = new Point(Math.Clamp(X(at) - dim.Width / 2, 0, w - dim.Width), NumeralY);
            c.DrawText(dim, p);
            if (Lit(X(at)) is var lit and > 0.01) using (c.PushOpacity(lit)) c.DrawText(bright, p);
        }

        double bx = X(Base);
        c.DrawLine(Detent, new Point(Crisp(bx), 0), new Point(Crisp(bx), TickBottom));

        var groove = new Rect(0, GrooveY - GrooveH / 2, w, GrooveH);
        c.DrawRectangle(Palette.Ground, Engraving, groove, GrooveH / 2, GrooveH / 2);
        if (hot > 0.01) using (c.PushOpacity(hot)) c.DrawRectangle(null, EngravingLit, groove, GrooveH / 2, GrooveH / 2);
        if (_tint != null) c.DrawRectangle(_tint, null, groove.Deflate(0.5), GrooveH / 2, GrooveH / 2);

        // Reduced motion: the old place fades out under the new one instead of travelling.
        double fade = _shown.Fade;
        if (fade <= 0) { Edit(c, groove, X(Shown), tx); return; }
        using (c.PushOpacity(fade)) Edit(c, groove, X(_shown.From), X(_shown.From));
        using (c.PushOpacity(1 - fade)) Edit(c, groove, X(Shown), tx);
    }

    /// The edit from the detent to `vx`, and the thumb at `tx`.
    void Edit(DrawingContext c, Rect groove, double vx, double tx) {
        double bx = X(Base), lo = Math.Min(bx, vx), hi = Math.Max(bx, vx);
        if (hi - lo > 0.5) {
            var span = new Rect(lo, groove.Y, hi - lo, GrooveH);
            if (_vivid != null) using (c.PushClip(span)) c.DrawRectangle(_vivid, null, groove.Deflate(0.5), GrooveH / 2, GrooveH / 2);
            else c.DrawRectangle(Palette.Accent, null, span, 1, 1);
        }
        // Lifted on hover (a longer, softer shadow), pressed into the groove by a grab (a tight one).
        double lift = _lift.Value, press = _press.Value, s = ThumbScale;
        var shadow = new BoxShadows(new BoxShadow { OffsetY = 1 + 0.5 * lift - 0.75 * press, Blur = 3 + 2 * lift - 2.5 * press, Color = Shade });
        var thumb = new Rect(tx - ThumbW / 2, GrooveY - ThumbH / 2, ThumbW, ThumbH);
        using (c.PushTransform(Matrix.CreateTranslation(-tx, -GrooveY) * Matrix.CreateScale(s, s) * Matrix.CreateTranslation(tx, GrooveY)))
            c.DrawRectangle(Palette.Text, null, new RoundedRect(thumb, Radius.Sm), shadow);
    }

    /// The numerals, shaped once per range rather than on every drag frame: dim, and lit.
    List<(float At, FormattedText Dim, FormattedText Lit)> Numerals() {
        if (_numerals != null && _numeralsFor == (Min, Max)) return _numerals;
        _numeralsFor = (Min, Max);
        static FormattedText Shaped(string s, IBrush ink) {
            var text = new FormattedText(s, CultureInfo.InvariantCulture, FlowDirection.LeftToRight, Faces.NumeralFace, Faces.Mark, ink);
            text.SetFontFeatures(Faces.Tabular);
            return text;
        }
        return _numerals = _scale.Marks(Min, Max).Where(m => m.Numbered)
            .Select(m => (m.At, Shaped(_scale.Text!(m.At), Palette.Dim), Shaped(_scale.Text!(m.At), Palette.Text))).ToList();
    }

    // The row is the track's parent (AdjustmentSlider, and the panels' own slider
    // rows): hovering its name or readout lights the track as well.
    protected override void OnAttachedToVisualTree(VisualTreeAttachmentEventArgs e) {
        base.OnAttachedToVisualTree(e);
        if (Parent is not InputElement row) return;
        (_row = row).PointerEntered += _enter;
        row.PointerExited += _exit;
    }

    protected override void OnDetachedFromVisualTree(VisualTreeAttachmentEventArgs e) {
        base.OnDetachedFromVisualTree(e);
        if (_row != null) { _row.PointerEntered -= _enter; _row.PointerExited -= _exit; }
        _row = null;
        _rowOver = false;
        _lift.Snap(0);
    }

    void Hot() => _lift.To(_rowOver || _dragging ? 1 : 0);

    protected override void OnPointerPressed(PointerPressedEventArgs e) {
        // A double-click is two clicks on this track. ClickCount counts any two quick presses at one spot, a
        // drag's included, or the next slider scrolled under the same spot: those were resetting what they nudged.
        bool second = e.ClickCount == 2 && e.Timestamp - _clickedAt < 600;
        _clickedAt = null;
        if (second) { Reset(); e.Handled = true; return; }
        _dragged = false;
        // A grab catches the thumb where it is on screen, mid-glide too, and drags from there.
        _startValue = (float)Shown;
        _shown.Snap(_startValue);
        _band.Snap(0);
        _startX = e.GetPosition(this).X;
        _dragging = true;
        // A drag that begins on the detent is a departure and must not snap
        // back to it (decision #294).
        _departing = _startValue == Base;
        e.Pointer.Capture(this);
        _press.To(1);
        Hot();
        Interacting?.Invoke(true);
    }

    protected override void OnPointerMoved(PointerEventArgs e) {
        if (!_dragging) return;
        bool fine = e.KeyModifiers.HasFlag(KeyModifiers.Alt);
        double moved = (e.GetPosition(this).X - _startX) / Travel * (fine ? 0.2 : 1);
        _dragged |= Math.Abs(e.GetPosition(this).X - _startX) > 2;
        float raw = _startValue + (float)moved * (Max - Min);
        float v = AdjustmentMath.Detented(raw, Base, Min, Max, _departing);
        if (v != Base) _departing = false;
        // Past an end the value stays at the end and the thumb follows the hand less and less.
        double past = (raw < Min ? raw - Min : raw > Max ? raw - Max : 0) / (Max - Min) * Travel;
        double band = Motion.Reduced ? 0 : Motion.Band(past, Stretch), was = _band.Value;
        double dt = (e.Timestamp - _lastMove) / 1000.0;
        if (dt > 0) _bandSpeed = (_bandSpeed + (band - was) / dt) / 2;
        _lastMove = e.Timestamp;
        if (band != was) _band.Snap(band);
        if (v == Value) return;
        Set(v);
        Changed?.Invoke(v);
    }

    public bool HitTest(Point p) => new Rect(Bounds.Size).Contains(p);

    protected override void OnPointerReleased(PointerReleasedEventArgs e) {
        if (!_dragging) return;
        _clickedAt = _dragged ? null : e.Timestamp;
        // Back to the end at the speed the hand let go with; a hand that had stopped hands over none.
        Drop(e.Timestamp - _lastMove > 50 ? 0 : _bandSpeed);
        e.Pointer.Capture(null);
    }

    /// A dialog or lost focus took the pointer mid-drag: the drag still ends, so the full render runs.
    protected override void OnPointerCaptureLost(PointerCaptureLostEventArgs e) {
        base.OnPointerCaptureLost(e);
        if (_dragging) Drop(0);
    }

    void Drop(double speed) {
        _dragging = false;
        _band.To(0, speed);
        if (_shown.Value != Value) _shown.To(Value);   // a grab that caught a glide and never moved lets it finish
        _press.To(0);
        Hot();
        Interacting?.Invoke(false);
    }

    /// Double-click: back to the detent, travelling.
    void Reset() {
        if (Value == Base) return;
        var (x, speed) = (Shown, _shown.Velocity);
        Changed?.Invoke(Base);   // the owner writes it, and may Set it, which would jump
        Value = Base;
        _shown.Snap(x);
        _shown.Precision = (Max - Min) / Travel * 0.1;
        _shown.To(Base, speed);
    }
}
