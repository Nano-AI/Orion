using System.Globalization;
using System.Text.RegularExpressions;
using Avalonia;
using Avalonia.Animation;
using Avalonia.Animation.Easings;
using Avalonia.Controls;
using Avalonia.Headless;
using Avalonia.LogicalTree;
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
        var illegible = new List<string>();
        int glyphs = 0;
        foreach (var tab in Panels.Tabs) {
            window.Select(tab);
            window.UpdateLayout();
            Dispatcher.UIThread.RunJobs();
            var visuals = window.GetVisualDescendants().Where(v => v.IsEffectivelyVisible).ToList();
            // The logical tree as well, hidden words included: a word shown only when it applies (Detail's LUT
            // Remove) is held while it is still hidden.
            foreach (var t in visuals.OfType<TextBlock>().Concat(window.GetLogicalDescendants().OfType<TextBlock>()).Distinct())
                illegible.AddRange(Illegible(t).Select(w => $"{tab}: {w}"));
            // A meter's numerals are drawn in Render, not by a TextBlock: record what it draws.
            foreach (var (size, ink, opacity) in visuals.OfType<AnalogTrack>().SelectMany(v => Glyphs(v))) {
                glyphs++;
                if (size < Faces.Mark) illegible.Add($"{tab}: a track numeral at {size} pt");
                if (Unreadable(ink, opacity) is { } why) illegible.Add($"{tab}: a track numeral {why}");
            }
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
        // Each sheet in a window of its own, as it opens: the theme gives its words their ink and Fluent its fields.
        foreach (var sheet in Panels.All.Where(e => e.Name.StartsWith("sheet-"))) {
            var host = new Window { Width = 900, Height = 900, Content = Panels.Create(sheet.Type) };
            host.Show();
            host.UpdateLayout();
            var visibleWords = host.GetVisualDescendants().Where(v => v.IsEffectivelyVisible).OfType<TextBlock>();
            illegible.AddRange(visibleWords.Concat(host.GetLogicalDescendants().OfType<TextBlock>()).Distinct().SelectMany(Illegible).Select(w => $"{sheet.Name}: {w}"));
            host.Close();
        }
        Check(glyphs > 0 && illegible.Count == 0, $"every tab and sheet: no word under {Faces.Mark} pt, none under 4.5:1 on panel while its control works " +
                                                  $"(exempt: a disabled control, words on the accent, the separators {string.Join(" ", Separators.Select(x => $"'{x}'"))}; " +
                                                  $"{glyphs} track numerals drawn)" + (illegible.Count > 0 ? ": " + string.Join("; ", illegible.Distinct().Take(8)) : ""));
        // A disabled field's hint is faint, as a disabled button's words are: Fluent draws it as an enabled one.
        var tray = (Panel)window.FindControl<Grid>("ToolbarHost")!;
        var field = new TextBox { PlaceholderText = "Find a lens…", IsEnabled = false };
        tray.Children.Add(field);
        window.UpdateLayout();
        var hint = field.GetVisualDescendants().OfType<TextBlock>().FirstOrDefault(x => x.Name == "PART_Placeholder");
        var drawn = Drawn(hint?.Foreground, hint?.Opacity ?? 0) as ISolidColorBrush;
        Check(drawn?.Color == ((ISolidColorBrush)Palette.Faint).Color, $"a disabled field's hint draws in faint (got {drawn?.Color.ToString() ?? "no hint"})");
        tray.Children.Remove(field);
        // WCAG 2.x contrast; each step 1.3:1 or more and lighter than the one below, so text, label and dim read as three levels.
        string Ratios(IBrush ink, params IBrush[] grounds) => string.Join(", ", grounds.Select(g => $"{Contrast(ink, g):0.00}"));
        Check(Contrast(Palette.Dim, Palette.Panel) >= 7 && new[] { Palette.Ground, Palette.Raised, Palette.Hover, Palette.Surround }.All(g => Contrast(Palette.Dim, g) >= 4.5)
              && Luminance(Palette.Label) > Luminance(Palette.Dim) && Luminance(Palette.Text) > Luminance(Palette.Label)
              && Contrast(Palette.Label, Palette.Dim) >= 1.3 && Contrast(Palette.Text, Palette.Label) >= 1.3,
              $"dim is 7:1 on panel and 4.5:1 on what words sit on (panel, ground, raised, hover, surround: " +
              $"{Ratios(Palette.Dim, Palette.Panel, Palette.Ground, Palette.Raised, Palette.Hover, Palette.Surround)}); " +
              $"label is lighter than dim by {Contrast(Palette.Label, Palette.Dim):0.00} ({Luminance(Palette.Label):0.000} over {Luminance(Palette.Dim):0.000}) " +
              $"and darker than text by {Contrast(Palette.Text, Palette.Label):0.00} ({Luminance(Palette.Text):0.000} over {Luminance(Palette.Label):0.000}), each 1.3 or more");

        // Tabs: one segmented well. Every tab shows its name, at least Faces.Mark, the same weight; an unchosen one
        // reads at 4.5:1 on the well, the chosen one is in text colour on the pill. Each is one target at least 24
        // high, named by a tooltip with the key and to a screen reader.
        window.Select("Detail");
        window.UpdateLayout();
        var tabs = window.FindControl<Grid>("TabBar")!.Children.OfType<StateButton>().ToList();
        var names = tabs.Select(b => b.GetVisualDescendants().OfType<TextBlock>().Single()).ToList();
        var pill = window.FindControl<Border>("TabPill")!;
        static string Spoken(Control c) => Avalonia.Automation.AutomationProperties.GetName(c) ?? "";
        static double Luminance(IBrush b) {
            var c = ((ISolidColorBrush)b).Color;
            static double Linear(byte v) => v / 255.0 <= 0.04045 ? v / 255.0 / 12.92 : Math.Pow((v / 255.0 + 0.055) / 1.055, 2.4);
            return 0.2126 * Linear(c.R) + 0.7152 * Linear(c.G) + 0.0722 * Linear(c.B);
        }
        double well = Luminance(window.FindControl<Border>("TabWell")!.Background!);
        double faintest = names.Where((_, i) => i != 2).Min(n => (Luminance(n.Foreground!) + 0.05) / (well + 0.05));
        Check(names.All(n => n.IsEffectivelyVisible && n.Opacity == 1 && n.FontSize >= Faces.Mark) && faintest >= 4.5
              && names.Select(n => n.FontWeight).Distinct().Count() == 1 && names[2].Foreground == Palette.Text && pill.Bounds == tabs[2].Bounds
              && tabs.All(b => b.Bounds.Height >= 24 && b.GetVisualDescendants().OfType<Avalonia.Controls.Shapes.Path>().Single().Width == IconSize.Md)
              && tabs.Select((b, i) => ToolTip.GetTip(b) as string == $"{Panels.Tabs[i]} ({(OperatingSystem.IsMacOS() ? "⌘" : "Ctrl+")}{i + 1})" && Spoken(b) == Panels.Tabs[i]).All(b => b),
              $"every tab shows its name at {names[0].FontSize} pt, unchosen at {faintest:0.0}:1 on the well, the chosen one on the pill " +
              $"({pill.Bounds} under {tabs[2].Bounds}); IconSize.Md icons, {tabs[0].Bounds.Height} high, tooltips with the key and spoken names");

        // At the column's narrowest and widest, whichever tab is chosen, every name is shown and lies inside its cell, whole, with
        // the room the names' face leaves at 322 (TabRoomNow each side), and no cell moves when the choice does; at
        // the widest the seven cells are equal, as a segmented control's are.
        var column = window.FindControl<Grid>("Body")!.ColumnDefinitions[2];
        var cramped = new List<string>();
        double tightest = double.MaxValue, bar = 2 * window.TabRoomNow;
        if (window.ToolColFit.Contains("not fit")) cramped.Add(window.ToolColFit);
        foreach (double width in new[] { Layout.ToolCol, window.ToolColMax }) {
            column.Width = new GridLength(width);
            Rect[]? cells = null;
            foreach (var tab in Panels.Tabs) {
                window.Select(tab);
                window.UpdateLayout();
                for (int i = 0; i < tabs.Count; i++) {
                    var at = names[i].TranslatePoint(default, tabs[i]) ?? new Point(-1, -1);
                    var typeface = new Typeface(names[i].FontFamily, names[i].FontStyle, names[i].FontWeight);
                    double whole = new Avalonia.Media.TextFormatting.TextLayout(names[i].Text, typeface, names[i].FontSize, null).WidthIncludingTrailingWhitespace;
                    double spare = tabs[i].Bounds.Width - whole;
                    if (width == Layout.ToolCol) tightest = Math.Min(tightest, spare / 2);
                    if (spare < bar || at.X < 0 || at.X + whole > tabs[i].Bounds.Width || at.Y < 0 || at.Y + names[i].Bounds.Height > tabs[i].Bounds.Height
                        || names[i].Bounds.Width < whole || !names[i].IsEffectivelyVisible || names[i].Opacity < 1)
                        cramped.Add($"{Panels.Tabs[i]} at {width} with {tab} chosen ({whole:0.#} pt of name at {at} in {tabs[i].Bounds.Size})");
                }
                cells ??= tabs.Select(b => b.Bounds).ToArray();
                if (!tabs.Select(b => b.Bounds).SequenceEqual(cells)) cramped.Add($"the cells move at {width} when {tab} is chosen");
                if (width == window.ToolColMax && tabs.Max(b => b.Bounds.Width) - tabs.Min(b => b.Bounds.Width) > 1)
                    cramped.Add($"unequal cells at {width} with {tab} chosen");
            }
        }
        column.Width = new GridLength(Layout.ToolCol);
        // The measurement itself, so a reviewer can see how close 322 is without a failure to read it from.
        Console.WriteLine($"selftest: tab fit at {Layout.ToolCol}: tightest name has {tightest:0.0} pt each side (bar {bar / 2})");
        Console.WriteLine($"selftest: tool column widest {window.ToolColMax} ({window.ToolColFit})");
        Check(cramped.Count == 0, $"every tab's name fits its cell at {Layout.ToolCol} and the widest with room, the cells stay put, and are equal at the widest ({window.ToolColFit})"
                                  + (cramped.Count > 0 ? ": " + string.Join("; ", cramped.Take(4)) : ""));

        // A face whose names cannot fit 322 even at TabRoomMin (a larger size stands in for one, through the face watch):
        // the column's floor rises to hold them and ToolColFit says so; back at Faces.Mark the floor is 322 again.
        names[0].FontSize = 16;
        window.UpdateLayout();
        double raised = column.MinWidth, shown = column.ActualWidth;
        string said = window.ToolColFit;
        names[0].FontSize = Faces.Mark;
        window.UpdateLayout();
        Check(raised > Layout.ToolCol && shown == raised && said.Contains($"floor widened to {raised}") && column.MinWidth == Layout.ToolCol,
              $"names too wide for {Layout.ToolCol} raise the column's floor to hold them ({raised}, the column {shown}: {said}), and it returns to {column.MinWidth}");

        // A click: the pill's first frame is exactly where the old tab's was, it lands under the new one, and it gets
        // there by transform, easing out under 200 ms, while every name stays shown. Under reduced motion it is there at once.
        // (The headless clock does not carry a transition on, so the first frame and the base value stand for the ends.)
        window.Select("Detail");
        window.UpdateLayout();
        double from = tabs[2].Bounds.X - tabs[0].Bounds.X;
        window.Select("Light", animate: true);
        AvaloniaHeadlessPlatform.ForceRenderTimerTick();
        double start = pill.RenderTransform?.Value.M31 ?? 0, dimmest = names.Min(n => n.Opacity);
        double lands = pill.GetBaseValue(Control.RenderTransformProperty).GetValueOrDefault()?.Value.M31 ?? double.NaN;
        var slide = pill.Transitions?.OfType<TransformOperationsTransition>().SingleOrDefault();
        Motion.Forced = true;
        window.Select("Detail", animate: true);
        AvaloniaHeadlessPlatform.ForceRenderTimerTick();
        bool jumped = !pill.IsAnimating(Control.RenderTransformProperty) && pill.RenderTransform?.Value.M31 == 0;
        Motion.Forced = false;
        Check(Math.Abs(start - from) < 0.5 && lands == 0 && dimmest == 1 && jumped
              && slide is { Duration.TotalMilliseconds: < 200, Easing: SplineEasing },
              $"a click eases the pill from the old tab ({start:0.#} pt over at the first frame, the tab {from:0.#} away; lands at {lands:0.#}) " +
              $"in {slide?.Duration.TotalMilliseconds} ms, every name shown throughout (dimmest {dimmest:0.##}); reduced motion jumps ({jumped})");
        Check(Commands.All.All(c => !Regex.IsMatch(c.Title, @" (?!Finder|Trash|HDR)\p{Lu}")), "command titles are sentence case (an acronym stays one)");
        window.Close();

        // The canvas: a picture made for another photo is never this photo's; no session is a failure only when opening said so.
        static CanvasState.Scene S(int current, nint handle, int shownFor, string timing) => CanvasState.SceneFor(3, current, handle, shownFor, timing);
        Check(CanvasState.SceneFor(0, -1, 0, -1, "") == CanvasState.Scene.Welcome && S(1, 7, 1, "full") == CanvasState.Scene.Picture
              && S(1, 7, 0, "full") == CanvasState.Scene.Loading && S(1, 0, 0, "") == CanvasState.Scene.Loading
              && S(1, 0, 1, "decoding…") == CanvasState.Scene.Picture && S(1, 0, 0, "could not open") == CanvasState.Scene.Failed && S(1, 7, 0, "render failed: x") == CanvasState.Scene.Failed,
              "the canvas shows this photo's picture, a calm blank while it decodes, or what failed");

        BrandChecks();
    }

    /// The only words that may be faint while their control works: the crumb's " / " between folder and photo.
    static readonly string[] Separators = ["/"];

    /// A word under the type floor, or one too pale to read while its control works: faint (tokens.json: not for
    /// words) or a theme's own grey. A disabled control's words may be faint, and so may the Separators. Enabled
    /// is read up the logical tree as well as the visual one. Its opacity and its visual ancestors' count (Fluent's
    /// hint is 0.5); at 0 the word is not drawn (a pill faded out), so its ink is not judged.
    static IEnumerable<string> Illegible(TextBlock t) {
        bool works = t.IsEffectivelyEnabled && t.GetSelfAndLogicalAncestors().OfType<Avalonia.Input.InputElement>().All(e => e.IsEnabled);
        double opacity = t.GetSelfAndVisualAncestors().Aggregate(1.0, (o, v) => o * v.Opacity);
        var pieces = t.Inlines is { Count: > 0 } runs
            ? runs.OfType<Avalonia.Controls.Documents.Run>().Select(r => (Text: r.Text ?? "", r.FontSize, Ink: r.Foreground ?? t.Foreground))
            : [(Text: t.Text ?? "", t.FontSize, Ink: t.Foreground)];
        foreach (var (text, size, ink) in pieces) {
            if (size < Faces.Mark) yield return $"'{text}' at {size} pt";
            if (works && opacity > 0 && !Separators.Contains(text.Trim()) && text.Trim().Length > 0 && Unreadable(ink, opacity) is { } why)
                yield return $"'{text}' {why}";
        }
    }

    /// The size, ink and opacity of each run of glyphs a control draws itself, recorded rather than rasterised.
    static IEnumerable<(double Size, IBrush? Ink, double Opacity)> Glyphs(Drawing? d, double opacity = 1) => d switch {
        GlyphRunDrawing { GlyphRun: { } run } g => [(run.FontRenderingEmSize, g.Foreground, opacity)],
        DrawingGroup g => g.Children.SelectMany(c => Glyphs(c, opacity * g.Opacity)),
        _ => [],
    };

    static IEnumerable<(double Size, IBrush? Ink, double Opacity)> Glyphs(Visual v) {
        var drawing = new DrawingGroup();
        using (var c = drawing.Open()) v.Render(c);
        return Glyphs(drawing);
    }

    /// Why an ink cannot be read on panel, or null: the colour it draws, its alpha and opacity flattened onto panel,
    /// is under 4.5:1, or it is no single colour (a gradient, or none) and so cannot be measured. Words on the
    /// accent's fill are exempt: build-tokens.py holds onAccent on the accent.
    static string? Unreadable(IBrush? ink, double opacity = 1) {
        if (ink is ISolidColorBrush { Color: var c } && c == ((ISolidColorBrush)Palette.OnAccent).Color) return null;
        if (Drawn(ink, opacity) is not { } drawn) return $"in {ink?.GetType().Name ?? "no ink"}, which the check cannot measure";
        double ratio = Contrast(drawn, Palette.Panel);
        return ratio < 4.5 ? $"at {ratio:0.00}:1" : null;
    }

    static IBrush? Drawn(IBrush? ink, double opacity) {
        if (ink is not ISolidColorBrush b) return null;
        double a = b.Color.A / 255.0 * b.Opacity * opacity;
        var under = ((ISolidColorBrush)Palette.Panel).Color;
        byte Mix(byte over, byte below) => (byte)Math.Round(over * a + below * (1 - a));
        return new Avalonia.Media.Immutable.ImmutableSolidColorBrush(Color.FromRgb(Mix(b.Color.R, under.R), Mix(b.Color.G, under.G), Mix(b.Color.B, under.B)));
    }

    /// WCAG 2.x relative luminance.
    static double Luminance(IBrush brush) {
        var c = ((ISolidColorBrush)brush).Color;
        static double Linear(byte v) => v / 255.0 <= 0.04045 ? v / 255.0 / 12.92 : Math.Pow((v / 255.0 + 0.055) / 1.055, 2.4);
        return 0.2126 * Linear(c.R) + 0.7152 * Linear(c.G) + 0.0722 * Linear(c.B);
    }

    static double Contrast(IBrush a, IBrush b) {
        double x = Luminance(a), y = Luminance(b);
        return (Math.Max(x, y) + 0.05) / (Math.Min(x, y) + 0.05);
    }

    /// The brand's files load as the brand drew them, and keep its rules (#407).
    static void BrandChecks() {
        var accent = ((ISolidColorBrush)Palette.Accent).Color;
        foreach (var file in BrandArt.Files) {
            var art = BrandArt.Load(file);   // throws on anything outside plain filled paths
            var bounds = art.Shapes.Aggregate(art.Shapes[0].Data.Bounds, (b, s) => b.Union(s.Data.Bounds));
            Check(art.Shapes.Count >= 4 && new Rect(art.ViewBox.Size).Contains(bounds) && bounds.Width > art.ViewBox.Width / 2,
                  $"brand/app/{file} draws {art.Shapes.Count} shapes inside its artboard ({bounds})");
            Check(art.Shapes.All(s => s.Fill != accent), $"brand/app/{file} has no accent in the mark");
        }
        var welcome = BrandArt.Load("welcome-mark.svg").Shapes;
        Check(BrandArt.Load("toolbar-mark.svg").Shapes.All(s => s.Fill == null) && welcome.All(s => s.Fill != null),
              "the toolbar mark takes its ink from the toolbar; the welcome mark keeps its colours");
        // Alnilam, the middle star, is always the brightest point (the brand's "never make all stars equally bright").
        static double Y(Color c) => 0.2126 * c.R + 0.7152 * c.G + 0.0722 * c.B;
        Check(welcome.Count == 4 && Y(welcome[2].Fill!.Value) > Math.Max(Y(welcome[1].Fill!.Value), Y(welcome[3].Fill!.Value)),
              "the welcome mark's middle star is its brightest");
        var stars = BrandArt.Load("symbol-animatable.svg").Shapes.Where(s => s.Id?.StartsWith("star-") == true).Select(s => s.Id).ToList();
        Check(stars.SequenceEqual(["star-left", "star-mid", "star-right"]), $"the loading symbol names its three stars ({string.Join(", ", stars)})");
        // The brand's keyframes: dim at rest, full from 30% to 75% of the 1.6 s cycle, each star 0.2 s behind.
        Check(BeltLoader.Star(-0.1) == 0.3 && BeltLoader.Star(0.1) == 0.3 && BeltLoader.Star(0.8) == 1 && BeltLoader.Star(0.8 + 1.6) == 1
              && Math.Abs(BeltLoader.Star(1.6) - 0.3) < 1e-9 && BeltLoader.Star(0.36) > 0.3 && BeltLoader.Star(0.36) < 1,
              "the loading stars follow the brand's keyframes");
    }
}
