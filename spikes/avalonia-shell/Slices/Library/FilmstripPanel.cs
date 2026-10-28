using Avalonia;
using Avalonia.Controls;
using Avalonia.Controls.Primitives;
using Avalonia.Controls.Templates;
using Avalonia.Input;
using Avalonia.Interactivity;
using Avalonia.Layout;
using Avalonia.Media;

namespace OrionShell.Library;

/// A port of app/Filmstrip.swift: the filter and the order on the left, a
/// hairline, then the film. Film, as the developer asked to keep it, minus what
/// made it hard to read: the base is `ground`, not the near-black film base that
/// swallowed night frames; every picture has a rebate edge, so a dark frame
/// still ends somewhere; and the gap between frames is the base itself.
///
/// Each frame carries its own perforations, four per frame as on 35 mm film, so
/// they travel with the pictures (app/Filmstrip.swift `sprockets`), and its
/// edge print: the frame number and its marks. The accent ring is the photo on
/// the canvas, drawn just outside the rebate so lighting it moves nothing.
[Panel("filmstrip", Region.Filmstrip)]
public sealed class FilmstripPanel : Grid {
    /// A frame and the base after it, which is the gap to the next one.
    const double CellWidth = Layout.ThumbW + Gap, Gap = 10, Perfs = 7, PrintHeight = 12;
    /// A mouse wheel's notch, in pixels along the strip.
    const double WheelStep = 48;

    readonly StackPanel _frames = new() { Orientation = Orientation.Horizontal, HorizontalAlignment = HorizontalAlignment.Center };
    readonly ScrollViewer _film;
    readonly Dictionary<LibraryModel.Photo, Control> _cells = new();

    public FilmstripPanel() {
        ColumnDefinitions = new ColumnDefinitions("Auto,1,*");
        Background = Palette.Panel;
        var filter = LibraryUi.Filter();
        filter.CellPadding = new Thickness(8, 0);
        var order = LibraryUi.SortMenu();
        Children.Add(new StackPanel { Spacing = 6, Margin = new Thickness(Space.Pad, 0), VerticalAlignment = VerticalAlignment.Center,
                                      Children = { filter, order } });
        var line = new Border { Background = Palette.Line };
        SetColumn(line, 1);
        Children.Add(line);
        _film = new ScrollViewer {
            Content = _frames, Background = Palette.Ground,
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

    /// One frame of film: perforations, the picture in its rebate, the edge print, perforations.
    static Control Cell(LibraryModel.Photo photo, bool isCurrent) {
        var picture = new Image { Source = photo.Thumbnail, Stretch = Stretch.Uniform, Opacity = photo.Rejected ? LibraryUi.RejectedOpacity : 1 };
        var number = Faces.Figure($"{photo.Index + 1}", Faces.Mark, Palette.Faint);
        var marks = LibraryUi.Marks(photo, Faces.Mark);
        marks.HorizontalAlignment = HorizontalAlignment.Right;
        var print = new Panel { Height = PrintHeight, Children = { number, marks } };
        var frame = new Frame { Child = picture };
        // The open photo's ring: an outline just outside the rebate, by a negative margin so lighting it
        // moves nothing. Not a BoxShadow on the frame: with one, the frame's picture did not draw.
        var held = new Panel { Children = { frame } };
        if (isCurrent) held.Children.Add(new Border { Margin = new Thickness(-3), BorderThickness = new Thickness(2), CornerRadius = new CornerRadius(4),
                                                       BorderBrush = Palette.Accent, IsHitTestVisible = false });
        var column = new Grid {
            RowDefinitions = new RowDefinitions($"{Perfs},*,{PrintHeight},{Perfs}"), RowSpacing = 2,
            Children = { new Perforations(), held, print, new Perforations() },
        };
        for (int row = 0; row < 4; row++) SetRow(column.Children[row], row);
        var cell = new Border {
            Width = CellWidth, Padding = new Thickness(Gap / 2, 3), Background = Brushes.Transparent, Child = column,
            Focusable = true, Cursor = new Cursor(StandardCursorType.Hand),
            FocusAdorner = new FuncTemplate<Control>(() => new FocusRing()),   // shown for keyboard focus only
        };
        cell.PointerEntered += (_, _) => frame.Hot = true;
        cell.PointerExited += (_, _) => frame.Hot = false;
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

    /// The picture's rebate: a hairline edge that brightens under the pointer.
    sealed class Frame : Border {
        bool _hot;

        public Frame() {
            BorderThickness = new Thickness(1);
            Background = Palette.FilmBase;   // what shows around a letterboxed picture: the frame's own dark
            CornerRadius = new CornerRadius(2);
            Paint();
        }

        public bool Hot { set { _hot = value; Paint(); } }

        void Paint() => BorderBrush = _hot ? Palette.Rail : Palette.Line;
    }

    /// One frame's perforations: four holes, the KS-1870 proportions, centred in
    /// each quarter so they line up with the frame line (app/Filmstrip.swift).
    sealed class Perforations : Control {
        public Perforations() => IsHitTestVisible = false;

        public override void Render(DrawingContext c) {
            double pitch = (Bounds.Width + Gap) / 4, w = Math.Min(pitch * 0.42, 8), h = w * 0.78;
            for (int i = 0; i < 4; i++)
                c.DrawRectangle(Palette.Surround, null,
                                new Rect((i + 0.5) * pitch - Gap / 2 - w / 2, (Bounds.Height - h) / 2, w, h), 1.5, 1.5);
        }
    }

    sealed class FocusRing : Control {
        public override void Render(DrawingContext context) => StateButton.DrawFocusRing(context, new Rect(Bounds.Size), Radius.Md);
    }
}
