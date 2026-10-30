using System.Globalization;
using System.Text.RegularExpressions;
using Avalonia.Animation;
using Avalonia.Animation.Easings;
using Avalonia.Controls;
using Avalonia.Headless;
using Avalonia.Media;
using Avalonia.Threading;
using Avalonia.VisualTree;
using OrionShell.CanvasSlice;

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
        RestyleChecks();
    }

    /// Stage 2b: the boards adopted across the shell, checked on the real window per tab.
    static void RestyleChecks() {
        var window = new MainWindow();
        window.Show();
        Dispatcher.UIThread.RunJobs();
        var words = new TextBlock();
        ((Panel)window.FindControl<Grid>("ToolbarHost")!).Children.Add(words);
        window.UpdateLayout();
        Check(FontManager.Current.TryGetGlyphTypeface(new Typeface(words.FontFamily), out var face) && !face.FamilyName.Contains("Inter")
              && (!OperatingSystem.IsMacOS() || face.FamilyName == "System Font"),
              $"words are the platform's face, not Inter (got {face?.FamilyName ?? "nothing"})");

        var found = new List<string>();
        foreach (var tab in Panels.Tabs) {
            window.Select(tab);
            window.UpdateLayout();
            Dispatcher.UIThread.RunJobs();
            var visuals = window.GetVisualDescendants().Where(v => v.IsEffectivelyVisible).ToList();
            // A raw Button is Fluent's look; RepeatButton and ToggleButton belong to sliders, scrollbars and switches.
            found.AddRange(visuals.Where(v => v.GetType() == typeof(Button)).Select(v => $"{tab}: Fluent button"));
            foreach (var t in visuals.OfType<TextBlock>()) {
                string text = t.Text ?? "";
                if (Regex.IsMatch(text, @"^[−+\-]?[\d.,:%×/ ]*\d[\d.,:%×/ ]*(K|EV|px|ms|°|%|k)?$") && t.FontFamily != Faces.Numeral)
                    found.Add($"{tab}: number '{text}' in the words face");
                // A camera or lens model on the capture line is the file's own spelling ("ILCE-7M3"), not a label.
                if (Regex.Matches(text, @"\p{L}{4,}").Any(m => m.Value == m.Value.ToUpperInvariant())
                    && t.FindAncestorOfType<OrionShell.Chrome.CaptureLine>() == null)
                    found.Add($"{tab}: capitals '{text}'");
                if (text.Contains(" · ")) found.Add($"{tab}: middle-dot string '{text}'");
            }
        }
        Check(found.Count == 0, "every tab: no Fluent button, numbers in the numeral face, no caps, no middle dots"
                                + (found.Count > 0 ? ": " + string.Join("; ", found.Distinct().Take(8)) : ""));

        // Tabs: one segmented well. The chosen tab alone shows its name, in text colour and never heavier, on the
        // pill; the rest are icons of one size, named by a tooltip with the key and to a screen reader.
        window.Select("Detail");
        window.UpdateLayout();
        var tabs = window.FindControl<Grid>("TabBar")!.Children.OfType<StateButton>().ToList();
        var names = tabs.Select(b => b.GetVisualDescendants().OfType<TextBlock>().Single()).ToList();
        var pill = window.FindControl<Border>("TabPill")!;
        static string Spoken(Control c) => Avalonia.Automation.AutomationProperties.GetName(c) ?? "";
        Check(names.Select(n => n.FontWeight).Distinct().Count() == 1 && names[2].Foreground == Palette.Text
              && names.Select((n, i) => n.IsVisible == (i == 2)).All(b => b) && pill.Bounds == tabs[2].Bounds
              && tabs.All(b => b.GetVisualDescendants().OfType<Avalonia.Controls.Shapes.Path>().Single().Width == IconSize.Md)
              && tabs.Select((b, i) => ToolTip.GetTip(b) as string == $"{Panels.Tabs[i]} (⌘{i + 1})" && Spoken(b) == Panels.Tabs[i]).All(b => b),
              $"the chosen tab alone shows its name, on the pill ({pill.Bounds} under {tabs[2].Bounds}); the rest are IconSize.Md icons named by tooltip and screen reader");

        // At the column's narrowest and widest, whichever tab is chosen, every cell holds its content with 8 to spare
        // each side; at the widest the seven cells are equal, as a segmented control's are.
        var column = window.FindControl<Grid>("Body")!.ColumnDefinitions[2];
        var cramped = new List<string>();
        foreach (double width in new[] { Layout.ToolCol, MainWindow.ToolColMax }) {
            column.Width = new GridLength(width);
            foreach (var tab in Panels.Tabs) {
                window.Select(tab);
                window.UpdateLayout();
                cramped.AddRange(tabs.Where(b => b.Bounds.Width - ((Control)b.Content!).DesiredSize.Width < 15.5)
                                     .Select(b => $"{Spoken(b)} at {width} with {tab} chosen ({b.Bounds.Width:0.#} pt)"));
                if (width == MainWindow.ToolColMax && tabs.Max(b => b.Bounds.Width) - tabs.Min(b => b.Bounds.Width) > 1)
                    cramped.Add($"unequal cells at {width} with {tab} chosen");
            }
        }
        column.Width = new GridLength(Layout.ToolCol);
        Check(cramped.Count == 0, "the tabs fit 322 and 560 with room in every cell, and are equal at 560" + (cramped.Count > 0 ? ": " + string.Join("; ", cramped.Take(4)) : ""));

        // A click: the pill's first frame is exactly where the old tab's was, it lands under the new one, and it gets
        // there by transform, easing out under 200 ms, as the name fades in. Under reduced motion it is there at once.
        // (The headless clock does not carry a transition on, so the first frame and the base value stand for the ends.)
        window.Select("Detail");
        window.UpdateLayout();
        double from = tabs[2].Bounds.X - tabs[0].Bounds.X;
        window.Select("Light", animate: true);
        AvaloniaHeadlessPlatform.ForceRenderTimerTick();
        double start = pill.RenderTransform?.Value.M31 ?? 0, fade = names[0].Opacity;
        double lands = pill.GetBaseValue(Control.RenderTransformProperty).GetValueOrDefault()?.Value.M31 ?? double.NaN;
        var slide = pill.Transitions?.OfType<TransformOperationsTransition>().SingleOrDefault();
        Motion.Forced = true;
        window.Select("Detail", animate: true);
        AvaloniaHeadlessPlatform.ForceRenderTimerTick();
        bool jumped = !pill.IsAnimating(Control.RenderTransformProperty) && pill.RenderTransform?.Value.M31 == 0 && names[2].Opacity == 1;
        Motion.Forced = null;
        Check(Math.Abs(start - from) < 0.5 && lands == 0 && fade < 0.1 && jumped
              && slide is { Duration.TotalMilliseconds: < 200, Easing: SplineEasing },
              $"a click eases the pill from the old tab ({start:0.#} pt over at the first frame, the tab {from:0.#} away; lands at {lands:0.#}) " +
              $"in {slide?.Duration.TotalMilliseconds} ms as the name fades in (from {fade:0.##}); reduced motion jumps ({jumped})");
        Check(Commands.All.All(c => !Regex.IsMatch(c.Title, @" (?!Finder|Trash|HDR)\p{Lu}")), "command titles are sentence case (an acronym stays one)");
        window.Close();

        // The canvas: a picture made for another photo is never this photo's; no session is a failure only when opening said so.
        static CanvasState.Scene S(int current, nint handle, int shownFor, string timing) => CanvasState.SceneFor(3, current, handle, shownFor, timing);
        Check(CanvasState.SceneFor(0, -1, 0, -1, "") == CanvasState.Scene.Welcome && S(1, 7, 1, "full") == CanvasState.Scene.Picture
              && S(1, 7, 0, "full") == CanvasState.Scene.Loading && S(1, 0, 0, "") == CanvasState.Scene.Loading
              && S(1, 0, 1, "decoding…") == CanvasState.Scene.Picture && S(1, 0, 0, "could not open") == CanvasState.Scene.Failed && S(1, 7, 0, "render failed: x") == CanvasState.Scene.Failed,
              "the canvas shows this photo's picture, a calm blank while it decodes, or what failed");
    }
}
