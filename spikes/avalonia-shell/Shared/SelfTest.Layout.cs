using System.Diagnostics;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Controls.Primitives;
using Avalonia.Headless;
using Avalonia.Input;
using Avalonia.Media;
using Avalonia.Threading;
using Avalonia.VisualTree;
using OrionShell.Chrome;
using OrionShell.Develop;
using OrionShell.Library;

namespace OrionShell;

/// Round 2, u1 (the developer's layout pass, 2026-10-28): a swap moves nothing
/// in the tool column, the histogram waits quietly, the splitter widens the
/// tools, scroll bars stay thin, the segmented control fills its well, one
/// library filter, the filmstrip's frames, the capture line.
static partial class SelfTest {
    static partial void LayoutChecks() {
        var engine = EngineModel.Shared;
        Rest(engine);   // an earlier check may have left a decode in flight
        SegmentedChecks();
        CaptureChecks();
        if (engine.Handle == 0 || engine.Photos.Count < 2) return;
        int start = engine.Current;
        var window = new MainWindow();
        window.Show();
        Commands.Run("fit");   // at fit a swap renders the preview graph first: the case that used to reflow
        engine.Open(start);    // a clean open: an earlier check's status message goes, as any open clears it
        Rest(engine);
        SwapChecks(window, engine);
        SplitterChecks(window);
        ScrollBarChecks(window);
        StripChecks(window, engine);
        ToolbarChecks(window);
        window.Close();
        StripWheelCheck();
        engine.Open(start);
        Rest(engine);
    }

    /// Runs the UI queue until the photo has opened and its full graph has rendered (or `ms` passed).
    static void Rest(EngineModel engine, int ms = 3000) {
        var sw = Stopwatch.StartNew();
        while ((engine.Opening || engine.Shown.Graph != Graph.Full) && sw.ElapsedMilliseconds < ms) Pump(5);
        Pump(150);   // and the 100 ms fade after it
    }

    static void Pump(int ms) {
        var sw = Stopwatch.StartNew();
        do { Dispatcher.UIThread.RunJobs(); Thread.Sleep(2); } while (sw.ElapsedMilliseconds < ms);
    }

    /// Every block in the tool column, by where it sits in the window.
    static List<Rect> ToolColumn(Window window) {
        window.UpdateLayout();
        var column = window.FindControl<DockPanel>("ToolColumn")!;
        var tools = window.FindControl<StackPanel>("ToolsHost")!;
        var histogram = window.GetVisualDescendants().OfType<HistogramPanel>().Single();
        IEnumerable<Visual> blocks = [.. column.Children, .. tools.Children, .. histogram.Children];
        return blocks.Select(b => new Rect(b.TranslatePoint(default, window) ?? default, b.Bounds.Size)).ToList();
    }

    static void SwapChecks(Window window, EngineModel engine) {
        var histogram = window.GetVisualDescendants().OfType<HistogramPanel>().Single();
        var plate = histogram.GetVisualDescendants().OfType<HistogramPlate>().Single();
        var before = ToolColumn(window);
        bool full = Levels.Bins.Length > 0;

        engine.Next();
        var swapping = ToolColumn(window);
        var sw = Stopwatch.StartNew();
        while (engine.Opening && sw.ElapsedMilliseconds < 5000) Pump(2);
        window.UpdateLayout();
        bool previewOnly = engine.Shown.Graph == Graph.Preview, empty = Levels.Bins.Length == 0;
        var waiting = ToolColumn(window);
        bool quiet = !histogram.Pulsing;   // inside 500 ms of the swap: no flash
        Rest(engine);
        sw.Restart();
        while (plate.Opacity < 1 && sw.ElapsedMilliseconds < 1000) Pump(10);   // the 100 ms fade, on whatever clock headless keeps
        var after = ToolColumn(window);
        string Moved(List<Rect> now) => string.Join("; ", before.Zip(now).Where(p => p.First != p.Second).Select(p => $"{p.First} → {p.Second}"));
        Check(full && previewOnly && empty && before.Count > 5
              && before.SequenceEqual(swapping) && before.SequenceEqual(waiting) && before.SequenceEqual(after),
              $"a swap moves nothing in the tool column, before, during and after the histogram ({before.Count} blocks; preview first {previewOnly}, " +
              $"empty {empty}){Moved(swapping)}{Moved(waiting)}{Moved(after)}");
        Check(quiet && !histogram.Pulsing && Levels.Bins.Length > 0 && plate.Opacity == 1,
              $"a swap that fills the histogram in time never pulses, and the curves end opaque (opacity {plate.Opacity:0.00})");

        // Held past 500 ms (a drag holds the full render back): the pulse runs, then stops when the bins land.
        engine.BeginInteraction();
        engine.Previous();
        sw.Restart();
        while (engine.Opening && sw.ElapsedMilliseconds < 5000) Pump(2);
        Pump(200);
        bool early = histogram.Pulsing;
        Pump(500);
        bool late = histogram.Pulsing;
        var pulsing = ToolColumn(window);
        engine.EndInteraction();
        Pump(150);
        Check(!early && late && !histogram.Pulsing && Levels.Bins.Length > 0 && before.SequenceEqual(pulsing),
              $"the histogram pulses only once it has waited 500 ms, in place, and stops when it fills (at 200 ms {early}, at 700 ms {late})");
        Check(histogram.GetVisualDescendants().OfType<Control>().Any(c => ToolTip.GetTip(c) is string t && t.StartsWith("Shadows clipped"))
              && histogram.GetVisualDescendants().OfType<Control>().Any(c => ToolTip.GetTip(c) is string t && t.StartsWith("Highlights clipped")),
              "each clipping flag says what it measures");
    }

    static void SplitterChecks(Window window) {
        var splitter = window.FindControl<GridSplitter>("Splitter")!;
        var body = window.FindControl<Grid>("Body")!;
        var canvas = window.FindControl<Grid>("CanvasHost")!;
        var slider = window.GetVisualDescendants().OfType<AnalogTrack>().FirstOrDefault();
        window.UpdateLayout();
        double tools = body.ColumnDefinitions[2].ActualWidth, canvasWidth = canvas.Bounds.Width, sliderWidth = slider?.Bounds.Width ?? 0;

        // The mouse, through the real input path: hovering lights the grip, a drag moves the edge, leaving dims it.
        var grip = splitter.GetVisualDescendants().OfType<Border>().Single();
        var at = splitter.TranslatePoint(new Point(splitter.Bounds.Width / 2, splitter.Bounds.Height / 2), window)!.Value;
        window.MouseMove(at);
        Pump(200);
        // The grip's target opacity: the 120 ms fade runs on headless's own clock.
        double Target() => grip.GetBaseValue(Visual.OpacityProperty).GetValueOrDefault(double.NaN);
        bool hovered = splitter.IsPointerOver, lit = Target() == 1;
        window.MouseDown(at, MouseButton.Left);
        window.MouseMove(new Point(at.X - 60, at.Y));
        window.MouseMove(new Point(at.X - 120, at.Y));
        window.MouseUp(new Point(at.X - 120, at.Y), MouseButton.Left);
        window.MouseMove(new Point(20, 200));
        Pump(200);
        window.UpdateLayout();
        double dragged = body.ColumnDefinitions[2].ActualWidth;
        Check(hovered && lit && Math.Abs(dragged - (tools + 120)) < 2 && Target() == 0,
              $"hovering the splitter lights its grip and a 120 px drag widens the tools by 120 ({tools} → {dragged}, hovered {hovered}, lit {lit}, after {Target()})");
        body.ColumnDefinitions[2].Width = new GridLength(Layout.ToolCol);
        window.UpdateLayout();

        splitter.Focus(NavigationMethod.Tab);
        for (int i = 0; i < 5; i++) Press(window, splitter, Key.Left);
        window.UpdateLayout();
        double wider = body.ColumnDefinitions[2].ActualWidth;
        Check(tools == Layout.ToolCol && wider > tools + 40 && Math.Abs(canvas.Bounds.Width - (canvasWidth - (wider - tools))) < 1
              && (slider == null || slider.Bounds.Width > sliderWidth + 40),
              $"the splitter widens the tools from the keyboard, the canvas gives the room and the sliders take it ({tools} → {wider}, canvas {canvasWidth:0} → {canvas.Bounds.Width:0}, slider {sliderWidth:0} → {slider?.Bounds.Width:0})");
        for (int i = 0; i < 40; i++) Press(window, splitter, Key.Left);
        window.UpdateLayout();
        double widest = body.ColumnDefinitions[2].ActualWidth;
        for (int i = 0; i < 60; i++) Press(window, splitter, Key.Right);
        window.UpdateLayout();
        double narrowest = body.ColumnDefinitions[2].ActualWidth;
        Check(widest == MainWindow.ToolColMax && narrowest == Layout.ToolCol,
              $"the tool column stays between {Layout.ToolCol} and {MainWindow.ToolColMax} ({narrowest}–{widest})");
        Check(splitter.Bounds.Width >= 8 && splitter.Cursor != null && !string.IsNullOrEmpty(Avalonia.Automation.AutomationProperties.GetName(splitter)),
              $"the splitter is a {splitter.Bounds.Width} px target with a resize cursor and a spoken name");
    }

    static bool Press(Window window, Control target, Key key) {
        var e = new KeyEventArgs { RoutedEvent = InputElement.KeyDownEvent, Key = key, Source = target };
        target.RaiseEvent(e);
        Dispatcher.UIThread.RunJobs();
        return e.Handled;
    }

    static void ScrollBarChecks(Window window) {
        window.UpdateLayout();
        var bar = window.GetVisualDescendants().OfType<ScrollBar>().FirstOrDefault(b => b.Orientation == Avalonia.Layout.Orientation.Vertical && b.IsVisible);
        var pill = bar?.GetVisualDescendants().OfType<Thumb>().SingleOrDefault()?.GetVisualDescendants().OfType<Border>().FirstOrDefault();
        Check(bar != null && bar.Bounds.Width == 10 && !bar.GetVisualDescendants().OfType<RepeatButton>().Any() && pill?.Bounds.Width == 4,
              $"the tool column's scroll bar is a 4 px pill in a 10 px lane with no arrow buttons (lane {bar?.Bounds.Width}, pill {pill?.Bounds.Width})");
        if (bar == null || pill == null) return;

        // Under the mouse it brightens and stays the same size; dragging it scrolls.
        var thumb = bar.GetVisualDescendants().OfType<Thumb>().Single();
        var viewer = bar.FindAncestorOfType<ScrollViewer>()!;
        var at = thumb.TranslatePoint(new Point(thumb.Bounds.Width / 2, thumb.Bounds.Height / 2), window)!.Value;
        var rest = pill.Background;
        window.MouseMove(at);
        Pump(100);
        window.UpdateLayout();
        bool steady = bar.Bounds.Width == 10 && pill.Bounds.Width == 4 && pill.Background != rest;
        double top = viewer.Offset.Y;
        window.MouseDown(at, MouseButton.Left);
        window.MouseMove(new Point(at.X, at.Y + 40));
        window.MouseUp(new Point(at.X, at.Y + 40), MouseButton.Left);
        window.MouseMove(new Point(20, 200));
        double scrolled = viewer.Offset.Y;
        viewer.Offset = viewer.Offset.WithY(top);
        Check(steady && scrolled > top + 20,
              $"hovering the scroll bar brightens it without widening it, and dragging it scrolls ({top:0} → {scrolled:0})");
    }

    /// A mouse's vertical wheel scrolls the strip sideways.
    static void StripWheelCheck() {
        var strip = new FilmstripPanel();
        var window = Show(new Border { Width = 520, Height = Layout.Filmstrip, Child = strip });
        var film = strip.GetVisualDescendants().OfType<ScrollViewer>().Single();
        var at = film.TranslatePoint(new Point(film.Bounds.Width / 2, film.Bounds.Height / 2), window)!.Value;
        window.MouseWheel(at, new Vector(0, -1));
        Dispatcher.UIThread.RunJobs();
        Check(film.Extent.Width > film.Viewport.Width && film.Offset.X > 0,
              $"a mouse wheel scrolls the filmstrip sideways (offset {film.Offset.X:0} of {film.Extent.Width - film.Viewport.Width:0})");
        window.Close();
    }

    static void SegmentedChecks() {
        // In a host taller than itself (the gallery's bar was): the well keeps its height and the pill fills it.
        var segmented = new Segmented(["All", "Rated", "Unrated", "Rejected"], 1);
        var window = Show(new Grid { Height = 60, Width = 400, Children = { segmented } });
        var cells = segmented.GetVisualDescendants().OfType<StateButton>().ToList();
        double inner = segmented.Bounds.Height - segmented.Padding.Top - segmented.Padding.Bottom;
        Check(segmented.Bounds.Height == 28 && segmented.BorderThickness == default && cells.All(c => c.Bounds.Height == inner)
              && cells.Select(c => c.Bounds.Width).Distinct().Count() == 1
              && Math.Abs(cells.Sum(c => c.Bounds.Width) - (segmented.Bounds.Width - segmented.Padding.Left - segmented.Padding.Right)) < 1,
              $"a segmented control has no edge, keeps a 28 px well in a 60 px host, and its cells share it evenly and fill its height ({segmented.Bounds.Height}, cell {cells[0].Bounds})");
        Check(segmented.CornerRadius.TopLeft == cells[1].CornerRadius.TopLeft + segmented.Padding.Left,
              "the pill's corners are concentric with the well's");
        window.Close();
    }

    static void CaptureChecks() {
        Check(CaptureInfo.ShutterText(1 / 640f) == "1/640 s" && CaptureInfo.ShutterText(0.5f) == "0.5 s"
              && CaptureInfo.ApertureText(4) == "f/4" && CaptureInfo.ApertureText(5.6f) == "f/5.6" && CaptureInfo.ApertureText(11) == "f/11",
              "capture spellings: 1/640 s, 0.5 s, f/4, f/5.6, f/11");
        var engine = EngineModel.Shared;
        if (engine.Handle == 0) return;
        var info = CaptureInfo.Read(engine.Handle);
        var line = new CaptureLine();
        line.Show(info, engine.Photos[engine.Current]);
        var window = Show(new Border { Width = 294, Child = line });
        var words = line.GetVisualDescendants().OfType<TextBlock>().Select(t => t.Text ?? "").Where(t => t.Length > 0).ToList();
        string expected = string.Join("|", new[] { info?.Camera, info?.Lens }.Where(s => !string.IsNullOrEmpty(s)).Concat(info?.Viewfinder ?? []));
        Check(info != null && info.Lens.Length > 0 && string.Join("|", words) == expected && words.All(w => !w.Contains('·'))
              && line.GetVisualDescendants().OfType<TextBlock>().Where(t => info.Viewfinder.Contains(t.Text)).All(t => t.FontFamily == Faces.Numeral),
              $"the capture line reads camera, lens, then the exposure in numerals, no middle dots ({string.Join("  ", words)})");
        double height = line.Bounds.Height;
        line.Show(null, null);
        window.UpdateLayout();
        Check(height == CaptureLine.LineHeight && line.Bounds.Height == height, "the capture line keeps its height with nothing to show");
        window.Close();
    }

    static void StripChecks(Window window, EngineModel engine) {
        var strip = window.GetVisualDescendants().OfType<FilmstripPanel>().Single();
        window.UpdateLayout();
        var frames = strip.GetVisualDescendants().OfType<Border>().Where(b => b.Focusable && b.Cursor != null).ToList();
        bool rings = frames.All(f => f.BorderThickness == new Thickness(2))
                     && frames.Count(f => f.BorderBrush == Palette.Accent) == 1
                     && frames.All(f => f.Background != Palette.FilmBase && f.Background != strip.Background);
        var gaps = frames.Zip(frames.Skip(1), (a, b) => b.Bounds.X - a.Bounds.Right).Distinct().ToList();
        Check(frames.Count == LibraryModel.Shared.Visible.Count() && rings && gaps.Count == 1 && gaps[0] >= 8 && strip.Background == Palette.Panel,
              $"the strip's frames sit on mats of their own with one even gap ({string.Join(",", gaps)} px), one accent ring, all rings the same width");

        // One filter, one look: the strip and the gallery show the same control, and either drives both.
        Commands.Run("gallery");
        Dispatcher.UIThread.RunJobs();
        var filters = window.GetVisualDescendants().OfType<Segmented>()
            .Where(s => s.GetVisualDescendants().OfType<StateButton>().Select(b => b.Label).SequenceEqual(LibraryModel.FilterTitles)).ToList();
        var stripFilter = strip.GetVisualDescendants().OfType<Segmented>().SingleOrDefault();
        var galleryFilter = filters.FirstOrDefault(f => f != stripFilter);
        var was = LibraryModel.Shared.Current;
        galleryFilter?.GetVisualDescendants().OfType<StateButton>().First(b => b.Label == "Unrated")
            .RaiseEvent(new Avalonia.Interactivity.RoutedEventArgs(Button.ClickEvent));
        Check(stripFilter != null && galleryFilter != null && LibraryModel.Shared.Current == LibraryModel.Filter.Unrated && stripFilter.Selected == 2,
              "the strip and the gallery filter with one control, and picking in one moves the other");
        LibraryModel.Shared.Current = was;
        Commands.Run("gallery");
        Dispatcher.UIThread.RunJobs();
    }

    static void ToolbarChecks(Window window) {
        var tips = window.GetVisualDescendants().OfType<ToolbarPanel>().Single().GetVisualDescendants().OfType<StateButton>()
            .Select(b => ToolTip.GetTip(b) as string ?? "").ToList();
        Check(tips.Any(t => t.StartsWith("Undo") && t.EndsWith("Z)")) && tips.Any(t => t.StartsWith("Rotate left") && t.EndsWith("[)")),
              "the toolbar's undo and rotate buttons name their keys");
    }
}
