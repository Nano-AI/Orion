using Avalonia.Controls;
using Avalonia.Layout;
using Avalonia.Media;

namespace OrionShell;

/// The two faces of design/DESIGN.md. Words are the platform's own face;
/// every number is Barlow Semi Condensed 500 with tabular figures, so a
/// column of values reads like a dial and five-digit Kelvin fits a 54 px box.
/// (Not `Type`: that would hide System.Type across the namespace.)
public static class Faces {
    /// Words: whatever the platform gives (SF Pro, Segoe UI).
    public static readonly FontFamily Words = FontFamily.Default;
    /// Numbers: bundled in Assets/Fonts under SIL OFL 1.1 (NOTICE).
    public static readonly FontFamily Numeral = new("avares://OrionShell/Assets/Fonts#" + TypeSize.Numeral);
    public static readonly Typeface NumeralFace = new(Numeral, FontStyle.Normal, FontWeight.Medium);
    public static readonly FontFeatureCollection Tabular = FontFeatureCollection.Parse("+tnum");

    /// Roles over tokens.json's sizes. Nothing anywhere is under `Mark`.
    public const double Mark = TypeSize.Sm;      // 11: scale numerals, keycaps, the smallest word
    public const double Label = TypeSize.Base;   // 12: a control's name, tab names
    public const double Body = TypeSize.Md;      // 13: body text, section titles
    public const double Readout = TypeSize.Md;   // 13: a slider's value

    /// A number in the numeral face.
    public static TextBlock Figure(string text, double size = Readout, IBrush? color = null) => new() {
        Text = text, FontFamily = Numeral, FontWeight = FontWeight.Medium, FontFeatures = Tabular,
        FontSize = Math.Max(size, Mark), Foreground = color ?? Palette.Text, VerticalAlignment = VerticalAlignment.Center,
    };

    /// A signed figure with a true minus, as a meter prints it: −4, 0, +2.
    public static string Signed(float v, string format = "0.##") =>
        (v > 0 ? "+" : v < 0 ? "\u2212" : "") + MathF.Abs(v).ToString(format, System.Globalization.CultureInfo.InvariantCulture);
}
