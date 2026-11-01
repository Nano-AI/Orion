using Avalonia.Input;

namespace OrionShell.CanvasSlice;

/// What a scroll over a picture is: a mouse wheel's notch, which zooms, or a
/// touchpad's two fingers, which pan (as Lightroom and Photos do). The canvas
/// and the side-by-side panes read it here, so the two never disagree.
static class Scroll {
    static ulong _touchpadUntil;   // a scroll that looked continuous keeps that reading this long

    /// Whether `e` is a touchpad's (precise) scroll, and how many points one
    /// unit of its delta stands for.
    public static (bool Precise, double Points) Read(PointerWheelEventArgs e) {
        bool precise = CanvasNative.PreciseScroll() ?? Touchpad(e.Delta, e.Timestamp);
        // Avalonia's AppKit view hands over NSEvent.scrollingDelta ÷ 50 from a trackpad
        // and ÷ 5 from a wheel (libAvaloniaNative, AvnView mouseEvent); undone, Swift's
        // 0.01 a point applies as it is. Elsewhere a notch is 1.0, read as 10 points.
        return (precise, precise ? 50 : OperatingSystem.IsMacOS() ? 5 : 10);
    }

    /// Off AppKit nothing says which device scrolled, so the deltas do. A
    /// wheel's notch is a whole step, or eighths of one from a high-resolution
    /// wheel (libinput's v120 steps of 15, as XWayland and X11 pass them on); a
    /// touchpad's is continuous and often sideways. Read once as continuous, the
    /// rest of that scroll is too: a touchpad's slow tail can land on an eighth.
    /// The whole-step test this replaces read a high-resolution wheel as a
    /// touchpad, so it panned instead of zooming.
    static bool Touchpad(Avalonia.Vector d, ulong timestamp) {
        bool continuous = d.X != 0 || Math.Abs(d.Y * 8 - Math.Round(d.Y * 8)) > 1e-6;
        if (continuous) _touchpadUntil = timestamp + 400;
        return timestamp < _touchpadUntil;
    }
}
