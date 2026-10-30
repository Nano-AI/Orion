// GENERATED from design/tokens.json by design/build-tokens.py.
// Do not edit by hand — your changes will be overwritten.

using Avalonia.Media;
using Avalonia.Media.Immutable;

namespace OrionShell;

public static class Palette {
    /// App background, outermost shell
    public static readonly IBrush Ground = new ImmutableSolidColorBrush(Color.FromRgb(20, 20, 22));
    /// Tool panels, toolbar, filmstrip
    public static readonly IBrush Panel = new ImmutableSolidColorBrush(Color.FromRgb(27, 27, 29));
    /// A slider's engraved channel. Darker than ground so a horizontal track stands off the panel it lies on; ground alone was seven levels from the panel and the track vanished (2026-10-28)
    public static readonly IBrush Groove = new ImmutableSolidColorBrush(Color.FromRgb(12, 12, 14));
    /// Controls, cells, inset wells
    public static readonly IBrush Raised = new ImmutableSolidColorBrush(Color.FromRgb(35, 35, 38));
    /// A hovered or pressed control's fill
    public static readonly IBrush Hover = new ImmutableSolidColorBrush(Color.FromRgb(46, 46, 50));
    /// Canvas surround — what the photo is judged against
    public static readonly IBrush Surround = new ImmutableSolidColorBrush(Color.FromRgb(42, 42, 44));
    /// Hairlines, borders, dividers
    public static readonly IBrush Line = new ImmutableSolidColorBrush(Color.FromRgb(49, 49, 53));
    /// Scrollbar thumbs, and the lit lower lip of a slider's groove (AnalogTrack); tinted tracks draw their own label gradient (TrackTint)
    public static readonly IBrush Rail = new ImmutableSolidColorBrush(Color.FromRgb(58, 58, 62));
    /// Primary text, slider thumbs
    public static readonly IBrush Text = new ImmutableSolidColorBrush(Color.FromRgb(232, 232, 234));
    /// Labels, secondary text
    public static readonly IBrush Dim = new ImmutableSolidColorBrush(Color.FromRgb(138, 138, 144));
    /// A control's name (slider and field labels): a step above dim notes
    public static readonly IBrush Label = new ImmutableSolidColorBrush(Color.FromRgb(160, 160, 166));
    /// Inactive icons, detents, separators. Not for words a person must read: 2.5:1 on panel, under AA (dim is 5.0:1)
    public static readonly IBrush Faint = new ImmutableSolidColorBrush(Color.FromRgb(90, 90, 96));
    /// Interactive and active states ONLY. Never decorative.
    public static readonly IBrush Accent = new ImmutableSolidColorBrush(Color.FromRgb(77, 182, 196));
    /// Reject flag
    public static readonly IBrush Reject = new ImmutableSolidColorBrush(Color.FromRgb(196, 87, 77));
    /// Star rating marks
    public static readonly IBrush Star = new ImmutableSolidColorBrush(Color.FromRgb(240, 198, 116));
    /// What a person must see: a failed render or write, clipped highlights, an unreadable file. Star's value today, its own role so either can move
    public static readonly IBrush Warn = new ImmutableSolidColorBrush(Color.FromRgb(240, 198, 116));
    /// Histogram red channel
    public static readonly IBrush ChanR = new ImmutableSolidColorBrush(Color.FromRgb(196, 67, 58));
    /// Histogram green channel
    public static readonly IBrush ChanG = new ImmutableSolidColorBrush(Color.FromRgb(58, 168, 79));
    /// Histogram blue channel
    public static readonly IBrush ChanB = new ImmutableSolidColorBrush(Color.FromRgb(58, 111, 196));
    /// Film base — the darkest value in the interface, under the strip
    public static readonly IBrush FilmBase = new ImmutableSolidColorBrush(Color.FromRgb(9, 8, 7));
    /// Sprocket perforations and the rebate edge — a hole shows the light through
    public static readonly IBrush FilmHole = new ImmutableSolidColorBrush(Color.FromRgb(76, 74, 71));
}

public static class Space {
    public const double Xs = 2;
    public const double Gap = 4;
    public const double Sm = 6;
    public const double Md = 10;
    public const double Pad = 14;
    public const double Lg = 18;
    public const double Xl = 26;
}

public static class Radius {
    public const double Sm = 3;
    public const double Md = 5;
    public const double Lg = 7;
}

public static class TypeSize {
    public const double Xs = 10;
    public const double Sm = 11;
    public const double Base = 12;
    public const double Md = 13;
    public const double Title = 17;
    public const double Display = 28;
    public const string Numeral = "Barlow Semi Condensed";
}

public static class Layout {
    public const double Toolbar = 44;
    public const double Filmstrip = 98;
    public const double ToolCol = 322;
    public const double ThumbW = 96;
    public const double ThumbH = 66;
}

public static class Shadow {
    public const double Alpha = 0.7;
    public const double Y = 1;
    public const double Blur = 3;
}

public static class IconSize {
    public const double Stroke = 1.6;
    public const double Sm = 12;
    public const double Md = 16;
}
