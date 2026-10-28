using System.Globalization;
using Avalonia.Media;

namespace OrionShell;

/// Stage 2a's checks: the numeral face is the bundled one with its tabular
/// figures on, and the meter scales mark what DESIGN.md says they mark.
/// SelfTest.Run calls it once the integrator adds `DesignChecks();` (SelfTest.cs is frozen).
static partial class SelfTest {
    static void DesignChecks() {
        // The file's legacy family name carries the weight ("… Medium"); a fallback face would not match at all.
        Check(FontManager.Current.TryGetGlyphTypeface(Faces.NumeralFace, out var face) && face.FamilyName.StartsWith(TypeSize.Numeral),
              $"the numeral face is the bundled {TypeSize.Numeral} (got {face?.FamilyName ?? "nothing"})");
        double Width(string s, bool tabular = true) {
            var text = new FormattedText(s, CultureInfo.InvariantCulture, FlowDirection.LeftToRight, Faces.NumeralFace, 13, Brushes.White);
            if (tabular) text.SetFontFeatures(Faces.Tabular);
            return text.WidthIncludingTrailingWhitespace;
        }
        // The face's default figures are proportional, so equal widths mean +tnum took.
        Check(Math.Abs(Width("1111", false) - Width("8888", false)) > 0.5 && Math.Abs(Width("1111") - Width("8888")) < 0.01,
              $"figures are tabular: 1111 and 8888 are one width ({Width("1111"):0.00}, {Width("8888"):0.00}; " +
              $"proportional {Width("1111", false):0.00}, {Width("8888", false):0.00})");

        static string Numerals(Scale s, float min, float max) =>
            string.Join(" ", s.Marks(min, max).Where(m => m.Numbered).Select(m => s.Text!(m.At)));
        Check(Scale.Stops.Marks(-5, 5).Count() == 11 && Numerals(Scale.Stops, -5, 5) == "−4 −2 0 +2 +4",
              $"exposure ticks every stop and numbers every other ({Numerals(Scale.Stops, -5, 5)})");
        Check(Numerals(Scale.Kelvin, 2000, 12000) == "2k 4k 6k 8k 10k 12k", $"Kelvin numbers every 2000 ({Numerals(Scale.Kelvin, 2000, 12000)})");
        Check(Numerals(Scale.For("°", -45, 45), -45, 45) == "−30° 0° +30°", "±45° ticks every 15 and numbers every 30");
        Check(Numerals(Scale.For("°", 10, 70), 10, 70) == "20° 40° 60°", "an angle that never goes negative carries no sign");
        Check(Scale.For(" px", 0, 1) == Scale.Plain && Scale.Plain.Marks(0, 1).Count() == 5 && !Scale.Plain.Numbered,
              "a unitless slider gets five plain ticks");
        Check(AdjustmentMath.Parsed("−0.5", "", -1, 1) == -0.5f, "a typed true minus parses");
    }
}
