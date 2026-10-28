using Avalonia;
using Avalonia.Controls;
using Avalonia.Controls.Primitives;
using Avalonia.Controls.Templates;
using Avalonia.Input;
using Avalonia.Interactivity;
using Avalonia.Layout;
using Avalonia.Media;

namespace OrionShell.Library;

/// A port of app/Filmstrip.swift: the library filter on the left, a hairline,
/// then the frames. On the panel tone, not film base: the board's near-black
/// base and perforations swallowed dark frames and the gaps between them.
///
/// Each frame is a raised mat with the picture letterboxed in it (never
/// cropped, as in the gallery) and a row under it for its marks, so a night
/// shot still has an edge and a rating never sits on the picture. The accent
/// ring is the photograph on the canvas.
[Panel("filmstrip", Region.Filmstrip)]
public sealed class FilmstripPanel : Grid {
    /// Layout.ThumbW wide; the row of marks under the picture takes the height a 3:2 picture leaves.
    const double CellWidth = Layout.ThumbW, MarksHeight = 12;
    /// A mouse wheel's notch, in pixels along the strip.
    const double WheelStep = 48;

    readonly StackPanel _frames = new() { Orientation = Orientation.Horizontal, Spacing = 8, Margin = new Thickness(12, 0),
                                          HorizontalAlignment = HorizontalAlignment.Center, VerticalAlignment = VerticalAlignment.Center };
    readonly ScrollViewer _film;
    readonly Dictionary<LibraryModel.Photo, Control> _cells = new();

    public FilmstripPanel() {
        ColumnDefinitions = new ColumnDefinitions("Auto,1,*");
        Background = Palette.Panel;
        var filter = LibraryUi.Filter();
        filter.Margin = new Thickness(Space.Pad, 0);
        filter.CellPadding = new Thickness(8, 0);   // the strip is short of width, not of words
        Children.Add(filter);
        var line = new Border { Background = Palette.Line };
        SetColumn(line, 1);
        Children.Add(line);
        _film = new ScrollViewer {
            Content = _frames,
            HorizontalScrollBarVisibility = ScrollBarVisibility.Hidden, VerticalScrollBarVisibility = ScrollBarVisibility.Disabled,
        };
        // A mouse has only a vertical wheel: here it scrolls along the strip. A trackpad's sideways swipe passes through.
        _film.AddHandler(PointerWheelChangedEvent, (_, e) => {
            if (e.Delta.X != 0 || e.Delta.Y == 0) return;
            _film.Offset = _film.Offset.WithX(Math.Max(0, _film.Offset.X - e.Delta.Y * WheelStep));
            e.Handled = true;
        }, RoutingStrategies.Tunnel);
        SetColumn(_film, 2);
        Children.Add(_film);

        var library = LibraryModel.Shared;
        library.Changed += Rebuild;
        EngineModel.Shared.Opened += () => {
            Rebuild();
            if (library.Open is { } open && _cells.TryGetValue(open, out var cell)) cell.BringIntoView();
        };
        Rebuild();
    }

    void Rebuild() {
        var library = LibraryModel.Shared;
        _frames.Children.Clear();
        _cells.Clear();
        foreach (var photo in library.Visible) {
            var cell = Cell(photo, photo == library.Open);
            _cells[photo] = cell;
            _frames.Children.Add(cell);
        }
    }

    /// One frame: the picture letterboxed on its mat, the marks under it.
    static Control Cell(LibraryModel.Photo photo, bool isCurrent) {
        var picture = new Image { Source = photo.Thumbnail, Stretch = Stretch.Uniform, Opacity = photo.Rejected ? LibraryUi.RejectedOpacity : 1 };
        var marks = LibraryUi.Marks(photo, Faces.Mark);
        marks.HorizontalAlignment = HorizontalAlignment.Left;
        Grid.SetRow(marks, 1);
        var cell = new Frame(isCurrent) {
            Width = CellWidth,
            Child = new Grid { RowDefinitions = new RowDefinitions($"*,{MarksHeight}"), RowSpacing = 3, Children = { picture, marks } },
        };
        ToolTip.SetTip(cell, photo.Tooltip);
        Avalonia.Automation.AutomationProperties.SetName(cell, LibraryUi.Spoken(photo) + (isCurrent ? ", open" : ""));
        // Command/shift-click selection, PhotoSelection.
        // stub(#315): Library.swift owns this until the logic moves below the facade
        cell.Tapped += (_, _) => EngineModel.Shared.Open(photo.Index);
        cell.KeyDown += (_, e) => {
            if (e.Key is Key.Enter or Key.Space) { EngineModel.Shared.Open(photo.Index); e.Handled = true; }
        };
        // Merge to HDR… and Export… need a group selection.
        // stub(#315): OrionApp.swift owns this until the logic moves below the facade
        cell.ContextMenu = new ContextMenu { ItemsSource = LibraryUi.CullItems(photo) };
        return cell;
    }

    /// A frame's mat: raised at rest, the hover fill under the pointer, the
    /// accent ring when it is the open photo, and the buttons' two-ring halo
    /// when the keyboard brings focus to it. The ring is always 2 px, clear
    /// when off, so nothing moves when it lights.
    sealed class Frame : Border {
        readonly bool _current;

        public Frame(bool current) {
            _current = current;
            Focusable = true;
            FocusAdorner = new FuncTemplate<Control>(() => new FocusRing());   // shown for keyboard focus only
            Cursor = new Cursor(StandardCursorType.Hand);
            CornerRadius = new CornerRadius(Radius.Md);
            BorderThickness = new Thickness(2);
            Padding = new Thickness(3);
            VerticalAlignment = VerticalAlignment.Center;
            Height = Layout.Filmstrip - 18;
            Paint();
        }

        void Paint() {
            BorderBrush = _current ? Palette.Accent : Brushes.Transparent;
            Background = IsPointerOver ? Palette.Hover : Palette.Raised;
        }

        protected override void OnPropertyChanged(AvaloniaPropertyChangedEventArgs change) {
            base.OnPropertyChanged(change);
            if (change.Property == IsPointerOverProperty) Paint();
        }
    }

    sealed class FocusRing : Control {
        public override void Render(DrawingContext context) => StateButton.DrawFocusRing(context, new Rect(Bounds.Size), Radius.Md);
    }
}
