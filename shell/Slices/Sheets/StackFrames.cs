using System.Globalization;
using Avalonia;
using Avalonia.Animation;
using Avalonia.Controls;
using Avalonia.Controls.Templates;
using Avalonia.Input;
using Avalonia.Layout;
using Avalonia.Media;
using Avalonia.Media.Imaging;
using Rows = Avalonia.Controls.StackPanel;

namespace OrionShell.Sheets;

/// The Stack sheet's picture side (#411), laid out as Orion's window is: the reference frame large on the
/// canvas's surround, and under it a strip of every frame to choose it from. The sheet decides what the
/// large picture is (the preview, the sky painter, or why there is none); this draws it and the strip.
sealed class StackFrames : Grid {
    const double CellW = 76, CellH = 50;

    readonly List<HdrMergePanel.Candidate> _candidates;
    readonly Dictionary<string, Task<Bitmap?>> _pictures = [];
    readonly Decorator _back = new() { IsHitTestVisible = false }, _front = new();
    readonly Rows _cells = new() { Orientation = Orientation.Horizontal, Spacing = Space.Md, Margin = new Thickness(Space.Gap, Space.Gap, Space.Gap, Space.Pad) };   // the bottom clears the overlay scroll bar
    readonly ScrollViewer _scroller;
    readonly TextBlock _name = Faces.Figure("", Faces.Label, Palette.Text), _detail = Faces.Figure("", Faces.Label, Palette.Dim);
    int _reference;

    /// A frame was chosen in the strip: its index, and whether by key (shown at once, never animated).
    public event Action<int, bool>? Chose;

    /// The focusable strip: Tab reaches it, Left and Right (Home, End) choose the frame before or after.
    public Border Strip { get; }
    /// The pane the large picture is drawn in, and what it shows now.
    public Control Pane { get; }
    public Control? Shown => _front.Child;
    public string Caption => $"{_name.Text}  {_detail.Text}";
    /// Every frame's preview has been read (or found missing).
    public bool Read => _candidates.All(c => _pictures.TryGetValue(c.Path, out var t) && t.IsCompleted);

    public StackFrames(List<HdrMergePanel.Candidate> candidates, int reference) {
        (_candidates, _reference) = (candidates, reference);
        RowDefinitions = new RowDefinitions("*,Auto");
        Pane = new Panel { Background = Palette.Surround, Children = { _back, _front } };
        foreach (var layer in new[] { _back, _front }) layer.Margin = new Thickness(Space.Xl);
        for (int i = 0; i < candidates.Count; i++) _cells.Children.Add(Cell(i));
        _scroller = new ScrollViewer {
            Content = _cells, HorizontalScrollBarVisibility = Avalonia.Controls.Primitives.ScrollBarVisibility.Auto,
            VerticalScrollBarVisibility = Avalonia.Controls.Primitives.ScrollBarVisibility.Disabled,
        };
        Strip = new Border {
            Child = _scroller, Focusable = true, Background = Brushes.Transparent, CornerRadius = new CornerRadius(Radius.Md),
            FocusAdorner = new FuncTemplate<Control>(() => new Halo()),   // keyboard focus only
        };
        Avalonia.Automation.AutomationProperties.SetName(Strip, "Reference frame");
        Strip.KeyDown += (_, e) => {
            int to = e.Key switch {
                Key.Left => _reference - 1, Key.Right => _reference + 1, Key.Home => 0, Key.End => _candidates.Count - 1, _ => -1,
            };
            if (to < 0 || to >= _candidates.Count) return;
            e.Handled = true;
            Chose?.Invoke(to, true);
        };
        _name.FontSize = Faces.Body;
        var caption = new DockPanel { Margin = new Thickness(Space.Gap, 0, Space.Gap, Space.Sm) };
        var label = Engraved.Label("Reference frame");
        label.VerticalAlignment = VerticalAlignment.Center;
        DockPanel.SetDock(label, Dock.Left);
        var chosen = new Rows { Orientation = Orientation.Horizontal, Spacing = Space.Md, HorizontalAlignment = HorizontalAlignment.Right, Children = { _name, _detail } };
        caption.Children.AddRange([label, chosen]);
        var band = new Border {
            Background = Palette.Panel, BorderBrush = Palette.Line, BorderThickness = new Thickness(0, 1, 0, 0),
            Padding = new Thickness(Space.Pad, Space.Md, Space.Pad, Space.Sm), Child = new Rows { Children = { caption, Strip } },
        };
        SetRow(band, 1);
        Children.AddRange([Pane, band]);
        if (candidates.Count > 0) Mark(reference, reveal: false);
        // Centred on the middle frame once the strip has a width to centre in.
        _scroller.LayoutUpdated += Centre;
        Preload();
    }

    void Centre(object? sender, EventArgs e) {
        if (_scroller.Viewport.Width <= 0) return;
        _scroller.LayoutUpdated -= Centre;
        double x = _reference * (CellW + _cells.Spacing) + CellW / 2 - _scroller.Viewport.Width / 2;
        _scroller.Offset = new Vector(Math.Clamp(x, 0, Math.Max(0, _scroller.Extent.Width - _scroller.Viewport.Width)), 0);
    }

    /// `path`'s embedded preview, read once per sheet: off the UI thread, null when the raw has none.
    // ponytail: every frame's 800 px preview is held while the sheet is open (about 1.7 MB each, 100 MB at
    // 60 frames); keep only the strip's small copy past the reference if long stacks make that matter.
    public Task<Bitmap?> Picture(string path) {
        if (!_pictures.TryGetValue(path, out var task)) _pictures[path] = task = Library.Thumbnails.LoadAsync(path);
        return task;
    }

    /// The reference first, then outward from it, one at a time: the strip fills without a burst of reads.
    async void Preload() {
        try {
            foreach (int i in Enumerable.Range(0, _candidates.Count).OrderBy(i => Math.Abs(i - _reference)))
                await Picture(_candidates[i].Path);
        } catch (Exception e) {
            Console.Error.WriteLine($"orion stack: preview read failed: {e.Message}");
        }
    }

    /// The chosen frame ringed in the accent, named under the picture, and in view.
    public void Mark(int reference, bool reveal = true) {
        _reference = reference;
        for (int i = 0; i < _cells.Children.Count; i++) ((FrameCell)_cells.Children[i]).Chosen = i == reference;
        var c = _candidates[reference];
        _name.Text = System.IO.Path.GetFileName(c.Path);
        _detail.Text = string.Join("   ", new[] { Time(c), $"{reference + 1} of {_candidates.Count}" }.Where(s => s.Length > 0));
        if (reveal) _cells.Children[reference].BringIntoView();
    }

    /// A timecode, 24-hour to the second: the frames of a stack are seconds apart, and it fits under a cell.
    static string Time(HdrMergePanel.Candidate c) => c.Captured is { } d ? d.ToString("HH:mm:ss", CultureInfo.InvariantCulture) : "";

    /// The large picture, crossfaded in on Motion.Enter, ease-out, when it changes by the hand; at once by key.
    public void Show(Control view, bool animate) {
        if (ReferenceEquals(_front.Child, view)) return;
        var old = _front.Child;
        _back.Child = _front.Child = null;   // the painter may be coming back from the lower layer
        _back.Child = old;
        _front.Child = view;
        Fade(_back, 1, 0, animate);
        Fade(_front, 0, 1, animate);
    }

    static void Fade(Control layer, double from, double to, bool animate) {
        layer.Transitions = null;
        layer.Opacity = from;
        if (animate) layer.Transitions = [new DoubleTransition { Property = OpacityProperty, Duration = Motion.Enter, Easing = Motion.EaseOut }];
        layer.Opacity = to;
    }

    /// A preview, fitted and centred, with a photograph's nearly square corner (DESIGN.md: pictures).
    public static Control Framed(Bitmap picture) {
        var image = new Image { Source = picture, Stretch = Stretch.Uniform };
        RenderOptions.SetBitmapInterpolationMode(image, BitmapInterpolationMode.HighQuality);
        return new Border {
            Child = image, CornerRadius = new CornerRadius(2), ClipToBounds = true,
            HorizontalAlignment = HorizontalAlignment.Center, VerticalAlignment = VerticalAlignment.Center,
        };
    }

    /// Where a picture would be: what is missing and what that means, quietly, in the middle of the pane.
    public static Control Empty(string headline, string detail) => new Rows {
        Spacing = Space.Sm, MaxWidth = 300, HorizontalAlignment = HorizontalAlignment.Center, VerticalAlignment = VerticalAlignment.Center,
        Children = {
            new Border { Child = Icons.Create("camera", IconSize.Md, Palette.Dim), HorizontalAlignment = HorizontalAlignment.Center, Margin = new Thickness(0, 0, 0, Space.Xs) },
            new TextBlock { Text = headline, FontSize = Faces.Body, FontWeight = FontWeight.SemiBold, Foreground = Palette.Text, TextAlignment = TextAlignment.Center, TextWrapping = TextWrapping.Wrap },
            new TextBlock { Text = detail, FontSize = Faces.Label, Foreground = Palette.Dim, TextAlignment = TextAlignment.Center, TextWrapping = TextWrapping.Wrap },
        },
    };

    FrameCell Cell(int index) {
        var c = _candidates[index];
        var cell = new FrameCell(Time(c));
        // The tooltip says when in full, in the locale's words.
        ToolTip.SetTip(cell, string.Join(" · ", new[] { System.IO.Path.GetFileName(c.Path), c.Captured?.ToString("G", CultureInfo.CurrentCulture) ?? "" }.Where(s => s.Length > 0)));
        Avalonia.Automation.AutomationProperties.SetName(cell, System.IO.Path.GetFileName(c.Path));
        cell.PointerPressed += (_, e) => {
            Strip.Focus(NavigationMethod.Pointer);   // so the arrows carry on from here
            Chose?.Invoke(index, false);
            e.Handled = true;
        };
        Fill(cell, c.Path);
        return cell;
    }

    async void Fill(FrameCell cell, string path) {
        try { cell.Picture(await Picture(path)); }
        catch (Exception e) { Console.Error.WriteLine($"orion stack: preview read failed: {e.Message}"); }
    }

    /// One frame in the strip: its picture cropped to the cell, the accent ring when chosen (the filmstrip's
    /// ring on the open photo, so "this one" reads one way across Orion), its capture time under it.
    sealed class FrameCell : Rows {
        readonly Border _frame = new() {
            Width = CellW, Height = CellH, CornerRadius = new CornerRadius(2), ClipToBounds = true,
            Background = Palette.FilmBase, BorderBrush = Palette.Line, BorderThickness = new Thickness(1),
        };
        readonly Border _ring = new() {
            Margin = new Thickness(-2.5), BorderThickness = new Thickness(2), CornerRadius = new CornerRadius(3.5),
            BorderBrush = Palette.Accent, IsHitTestVisible = false, IsVisible = false,
        };
        readonly TextBlock _time;

        public FrameCell(string time) {
            Spacing = Space.Gap;
            Width = CellW;
            Background = Brushes.Transparent;
            Cursor = new Cursor(StandardCursorType.Hand);
            _time = Faces.Figure(time.Length > 0 ? time : "–", Faces.Mark, Palette.Dim);
            _time.HorizontalAlignment = HorizontalAlignment.Center;
            Children.AddRange([new Panel { Children = { _frame, _ring } }, _time]);
            PointerEntered += (_, _) => _frame.BorderBrush = Palette.Rail;
            PointerExited += (_, _) => _frame.BorderBrush = Palette.Line;
        }

        public bool Chosen {
            set {
                _ring.IsVisible = value;
                _time.Foreground = value ? Palette.Text : Palette.Dim;
            }
        }

        /// The preview, or for a raw with none a dash on the film's dark.
        public void Picture(Bitmap? picture) => _frame.Child = picture != null
            ? new Image { Source = picture, Stretch = Stretch.UniformToFill }
            : new TextBlock { Text = "No preview", FontSize = Faces.Mark, Foreground = Palette.Dim, HorizontalAlignment = HorizontalAlignment.Center, VerticalAlignment = VerticalAlignment.Center };
    }

    /// The keyboard-focus halo around the strip.
    sealed class Halo : Control {
        public override void Render(DrawingContext context) => StateButton.DrawFocusRing(context, new Rect(Bounds.Size), Radius.Md);
    }
}
