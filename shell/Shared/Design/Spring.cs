using Avalonia;
using Avalonia.Controls;

namespace OrionShell;

/// A critically damped spring (Apple's damping 1.0, from "Designing Fluid
/// Interfaces"), the one every moving thing in the shell shares. It has no
/// duration and never overshoots. A new target re-aims it from where it is on
/// screen at the speed it already has, so any frame can interrupt it, and a
/// gesture's release velocity hands straight over. It steps itself on the
/// owner's TopLevel.RequestAnimationFrame and raises Moved each frame; the owner
/// draws Value. Under reduced motion it lands at once and Fade runs from 1 to 0
/// over Motion.Fade, for an owner that cross-fades from From instead.
public sealed class Spring(Visual owner, double response, double at = 0) {
    readonly double _w = 2 * Math.PI / response;
    TimeSpan? _last;
    bool _running;

    public double Value { get; private set; } = at;
    public double Target { get; private set; } = at;
    /// Units a second.
    public double Velocity { get; private set; }
    /// Close enough to land, in Value's units.
    public double Precision { get; set; } = 1e-3;
    /// Reduced motion: where it was, and how much of that is still shown.
    public double From { get; private set; } = at;
    public double Fade { get; private set; }
    public bool Moving => Value != Target || Velocity != 0 || Fade > 0;
    public event Action? Moved;

    /// Heads for `target` from the on-screen value, at the speed it has or at
    /// `velocity` when a gesture hands one over.
    public void To(double target, double? velocity = null) {
        if (Motion.Reduced) {
            if (target != Value) (From, Fade) = (Value, 1);
            (Value, Velocity) = (target, 0);
        } else if (velocity is { } v) Velocity = v;
        Target = target;
        Run();
    }

    /// Jumps and stops: a drag, or a photo opening.
    public void Snap(double value) {
        Value = Target = From = value;
        Velocity = Fade = 0;
        Moved?.Invoke();
    }

    /// Advances `seconds` by the closed form, exact for any step. False once landed.
    public bool Step(double seconds) {
        Fade = Math.Max(0, Fade - seconds / Motion.Fade.TotalSeconds);
        double x = Value - Target, c = Velocity + _w * x, e = Math.Exp(-_w * seconds);
        x = (x + c * seconds) * e;
        Velocity = c * e - _w * x;
        Value = Target + x;
        if (Math.Abs(x) < Precision && Math.Abs(Velocity) < Precision * _w) (Value, Velocity) = (Target, 0);
        return Moving;
    }

    void Run() {
        if (_running || !Moving) return;
        if (TopLevel.GetTopLevel(owner) is not { } top) { Snap(Target); return; }   // not on screen: nothing to show
        _running = true;
        top.RequestAnimationFrame(Frame);
    }

    void Frame(TimeSpan now) {
        _running = false;
        Step(_last is { } last && now > last ? (now - last).TotalSeconds : 1 / 60.0);
        _last = Moving ? now : null;
        Moved?.Invoke();
        Run();
    }
}
