using Avalonia;
using Avalonia.Controls;
using Avalonia.Controls.Primitives;
using Avalonia.Layout;
using Avalonia.Media;
using Avalonia.Threading;
using OrionShell.Chrome;

namespace OrionShell.Library;

/// A port of app/GalleryView.swift, laid out as Library.dc.html: a filter bar,
/// the contact sheet, and an info column for the photo in focus. A click moves
/// focus without decoding, and only a double-click opens a raw. Cells are 3:2
/// and the picture is letterboxed inside, never cropped, because the question
/// this view answers is framing.
[Panel("gallery", Region.Sheet)]
public sealed class GalleryPanel : DockPanel {
    readonly GalleryGrid _grid = new();
    readonly Segmented _filter = LibraryUi.Filter();
    readonly Dictionary<LibraryModel.Photo, (Control Cell, Border Ring, TextBlock Name)> _cells = new();
    readonly List<Border> _pictures = new();
    readonly InfoColumn _info = new();
    /// The ring last drawn: GalleryMode.Focus, the photo the keys act on (not
    /// necessarily the one decoded on the canvas).
    LibraryModel.Photo? _ringed;

    public GalleryPanel() {
        Background = Palette.Ground;
        _filter.HorizontalAlignment = HorizontalAlignment.Left;

        SetDock(_info, Dock.Right);
        Children.Add(_info);
        var bar = new DockPanel { Margin = new Thickness(18, 12) };
        var size = SizeSlider();
        SetDock(size, Dock.Right);
        var order = LibraryUi.SortMenu();
        order.Margin = new Thickness(10, 0, 0, 0);
        bar.Children.AddRange([size, new StackPanel { Orientation = Orientation.Horizontal, Children = { _filter, order } }]);
        // On the panel tone, as the toolbar above it, so the filter's ground well reads as a well.
        var header = new Border { Background = Palette.Panel, BorderBrush = Palette.Line, BorderThickness = new Thickness(0, 0, 0, 1), Child = bar };
        SetDock(header, Dock.Top);
        Children.Add(header);
        Children.Add(new ScrollViewer { Content = _grid, Padding = new Thickness(8), HorizontalScrollBarVisibility = ScrollBarVisibility.Disabled });

        LibraryModel.Shared.Changed += Rebuild;
        EngineModel.Shared.Opened += Rebuild;
        GalleryMode.Changed += Ring;
        Rebuild();
        // Opening the gallery lands on the photograph being edited, once the
        // grid has laid out enough to know where its cell is.
        AttachedToVisualTree += (_, _) => Dispatcher.UIThread.Post(Reveal);
    }

    void Reveal() {
        if (GalleryMode.Focus is { } f && _cells.TryGetValue(f, out var cell)) cell.Cell.BringIntoView();
    }

    /// The focus moved: two rings and the info column change, not the grid.
    void Ring() {
        if (_ringed != GalleryMode.Focus) {
            Paint(_ringed, false);
            Paint(_ringed = GalleryMode.Focus, true);
            _info.Show(GalleryMode.Focus);
        }
        if (GalleryMode.On) Dispatcher.UIThread.Post(Reveal);
    }

    void Paint(LibraryModel.Photo? photo, bool focused) {
        if (photo == null || !_cells.TryGetValue(photo, out var c)) return;
        c.Ring.BorderBrush = focused ? Palette.Accent : Brushes.Transparent;
        c.Name.Foreground = focused ? Palette.Text : Palette.Dim;
    }

    /// Trades photos per screen against the detail a framing judgement needs.
    /// The size is not remembered: Swift keeps it in @AppStorage("galleryCellSize").
    // stub(#315): GalleryView.swift owns this until the logic moves below the facade
    Control SizeSlider() {
        var slider = new Slider {
            Minimum = GalleryGrid.MinCell, Maximum = GalleryGrid.MaxCell, Value = GalleryGrid.DefaultCell,
            Width = 120, VerticalAlignment = VerticalAlignment.Center,
        };
        var r = slider.Resources;
        r["SliderHorizontalHeight"] = 16.0;
        r["SliderTrackThemeHeight"] = 3.0;
        r["SliderHorizontalThumbWidth"] = 12.0;
        r["SliderHorizontalThumbHeight"] = 15.0;
        r["SliderThumbCornerRadius"] = new CornerRadius(Radius.Sm);
        foreach (var state in new[] { "", "PointerOver", "Pressed" }) {
            r["SliderTrackFill" + state] = Palette.Ground;
            r["SliderTrackValueFill" + state] = Palette.Accent;
            r["SliderThumbBackground" + state] = Palette.Text;
        }
        slider.PropertyChanged += (_, e) => { if (e.Property != RangeBase.ValueProperty) return;
            _grid.Cell = slider.Value;
            foreach (var picture in _pictures) picture.Height = _grid.Cell / GalleryGrid.CellAspect;
        };
        return new StackPanel { Orientation = Orientation.Horizontal, Spacing = 8, VerticalAlignment = VerticalAlignment.Center,
            Children = { new TextBlock { Text = "Size", Foreground = Palette.Dim, VerticalAlignment = VerticalAlignment.Center }, slider } };
    }

    void Rebuild() {
        var library = LibraryModel.Shared;
        _grid.Children.Clear();
        _cells.Clear();
        _pictures.Clear();
        foreach (var photo in library.Visible) {
            var cell = Cell(photo);
            _cells[photo] = cell;
            _grid.Children.Add(cell.Cell);
        }
        _ringed = GalleryMode.Focus;
        _info.Show(_ringed);
    }

    (Control Cell, Border Ring, TextBlock Name) Cell(LibraryModel.Photo photo) {
        bool isFocused = photo == GalleryMode.Focus;
        var picture = new Border {
            Height = _grid.Cell / GalleryGrid.CellAspect, Opacity = photo.Rejected ? LibraryUi.RejectedOpacity : 1,
            Child = photo.Thumbnail is { } image ? new Image { Source = image, Stretch = Stretch.Uniform } : null,
        };
        _pictures.Add(picture);
        var ring = new Border {
            CornerRadius = new CornerRadius(Radius.Md), Background = Palette.Raised, ClipToBounds = true, BorderThickness = new Thickness(2),
            BorderBrush = isFocused ? Palette.Accent : Brushes.Transparent, Child = picture,
        };
        var name = new TextBlock { Text = photo.Name, Foreground = isFocused ? Palette.Text : Palette.Dim,
                                   TextTrimming = TextTrimming.CharacterEllipsis, VerticalAlignment = VerticalAlignment.Center };
        var caption = new DockPanel();
        var marks = LibraryUi.Marks(photo, Faces.Body);
        SetDock(marks, Dock.Right);
        caption.Children.AddRange([marks, name]);
        var cell = new StackPanel { Spacing = 8, Background = Brushes.Transparent, Focusable = true, Children = { ring, caption } };
        ToolTip.SetTip(cell, photo.Tooltip);
        Avalonia.Automation.AutomationProperties.SetName(cell, LibraryUi.Spoken(photo));
        // A plain click focuses; it never decodes. Opening is the double-click's job.
        // Command/shift-click selection, PhotoSelection.
        // stub(#315): Library.swift owns this until the logic moves below the facade
        cell.Tapped += (_, _) => GalleryMode.SetFocus(photo);
        // Return is the gallery's key map (GalleryMode.Handle), wherever focus is.
        cell.DoubleTapped += (_, _) => GalleryMode.Open(photo);
        cell.ContextMenu = new ContextMenu { ItemsSource = Menu(photo) };
        return (cell, ring, name);
    }

    static List<Control> Menu(LibraryModel.Photo photo) {
        var items = new List<Control> { LibraryUi.Item("Open in Develop", () => GalleryMode.Open(photo)), new Separator() };
        items.AddRange(LibraryUi.CullItems(photo));
        items.Add(new Separator());
        // Editor.confirmTrash over the photo right-clicked, which takes the focus first.
        items.Add(LibraryUi.Item("Move to Trash…", () => { GalleryMode.SetFocus(photo); Commands.Run("trash"); }));
        return items;
    }

    /// Library.dc.html's aside: the focused photo larger, its name and size,
    /// what the camera recorded (read from the file, not decoded), and the way into Develop.
    sealed class InfoColumn : Border {
        readonly Image _preview = new() { Stretch = Stretch.Uniform, MaxHeight = 220 };
        readonly TextBlock _name = new() { FontWeight = FontWeight.Medium, TextTrimming = TextTrimming.CharacterEllipsis };
        readonly TextBlock _size = new() { Foreground = Palette.Dim };
        readonly ContentControl _capture = new();
        readonly Dictionary<string, CaptureInfo?> _read = new();
        LibraryModel.Photo? _shown;

        public InfoColumn() {
            Width = 280;
            Background = Palette.Panel;
            BorderBrush = Palette.Line;
            BorderThickness = new Thickness(1, 0, 0, 0);
            Padding = new Thickness(14, 18);
            var open = new StateButton(null, "Open in Develop", () => { if (_shown != null) GalleryMode.Open(_shown); }) {
                Height = 32, HorizontalAlignment = HorizontalAlignment.Stretch,
            };
            Child = new StackPanel { Spacing = 18, Children = {
                new Border { Background = Palette.Raised, CornerRadius = new CornerRadius(4), ClipToBounds = true, Child = _preview },
                new StackPanel { Spacing = 4, Children = { _name, _size } },
                _capture, open,
            } };
        }

        public void Show(LibraryModel.Photo? photo) {
            _shown = photo;
            IsVisible = photo != null;
            if (photo == null) return;
            _preview.Source = photo.Thumbnail;
            _name.Text = photo.Name;
            if (!_read.TryGetValue(photo.Path, out var info)) _read[photo.Path] = info = CaptureInfo.ReadFile(photo.Path);
            _size.Inlines = info is { Width: > 0, Height: > 0 }
                ? [Faces.Num($"{info.Width} × {info.Height}"), new Avalonia.Controls.Documents.Run(" pixels")] : [];
            var rows = info?.Rows.Where(r => r.Item1 != "Dimensions").ToList() ?? [];
            _capture.Content = rows.Count == 0 ? null : new SectionPlate("Capture", null, CapturePopover.Grid(rows));
        }
    }
}
