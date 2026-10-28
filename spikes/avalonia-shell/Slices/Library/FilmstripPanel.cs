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
/// hairline, then the film: 135 film to scale (#347). The picture is the 36 mm
/// gate, so a millimetre is ThumbW / 36 points; frames sit 38 mm apart (ISO 1007),
/// and eight KS-1870 perforations run along each edge of every frame, on past the
/// last one as on a roll's tail. The base is film black and the holes show the
/// light through; a hairline gate edge keeps a night frame from ending nowhere.
/// The edge print, under the lower holes where 135 film carries it, is the frame
/// number and, only when there are any, its marks. The one liberty is the print's
/// height: true to scale it would be 3 pt type.
///
/// The accent ring is the photo on the canvas, drawn just outside the gate so
/// lighting it moves nothing.
[Panel("filmstrip", Region.Filmstrip)]
public sealed class FilmstripPanel : Grid {
    const double Mm = Layout.ThumbW / 36;
    /// One frame and the 2 mm of base before the next: eight perforations exactly.
    const double Pitch = 38 * Mm, Gate = 24 * Mm;
    /// KS-1870: 4.75 mm pitch, a 2.794 x 1.981 mm hole with 0.5 mm corners.
    const double PerfPitch = 4.75 * Mm, PerfW = 2.794 * Mm, PerfH = 1.981 * Mm, PerfR = 0.5 * Mm;
    /// The film's edge to the holes, and the holes to the picture. The edge print takes what is left.
    const double Edge = 2.5, Clear = 3;
    /// A mouse wheel's notch, in pixels along the strip.
    const double WheelStep = 48;

    readonly StackPanel _frames = new() { Orientation = Orientation.Horizontal, HorizontalAlignment = HorizontalAlignment.Left };
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
            // The film: its base and holes under the frames, stretched to the viewport by the scroller.
            Content = new Panel { Background = Palette.FilmBase, Children = { new Holes(), _frames } }, Background = Palette.FilmBase,
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
        var number = Faces.Figure($"{photo.Index + 1}", Faces.Mark, Palette.Dim);
        var marks = LibraryUi.Marks(photo, Faces.Mark, unearned: false);
        // The marks follow their own number: at the frame's right they read as the next frame's.
        var print = new StackPanel { Orientation = Orientation.Horizontal, Spacing = 6, VerticalAlignment = VerticalAlignment.Center, Children = { number, marks } };
        var frame = new Frame { Child = picture };
        // The open photo's ring: an outline just outside the rebate, by a negative margin so lighting it
        // moves nothing. Not a BoxShadow on the frame: with one, the frame's picture did not draw.
        var held = new Panel { Children = { frame } };
        if (isCurrent) held.Children.Add(new Border { Margin = new Thickness(-2.5), BorderThickness = new Thickness(2), CornerRadius = new CornerRadius(3.5),
                                                       BorderBrush = Palette.Accent, IsHitTestVisible = false });
        // Rows: over the upper holes, the picture, over the lower holes, the edge print.
        var column = new Grid { Children = { held, print } };
        foreach (double h in (double[])[Edge + PerfH + Clear, Gate, Clear + PerfH]) column.RowDefinitions.Add(new RowDefinition(h, GridUnitType.Pixel));
        column.RowDefinitions.Add(new RowDefinition(GridLength.Star));
        SetRow(held, 1);
        SetRow(print, 3);
        var cell = new Border {
            Width = Pitch, Padding = new Thickness((Pitch - Layout.ThumbW) / 2, 0), Background = Brushes.Transparent, Child = column,
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

    /// Both rows of holes along the film's whole length, so they travel with the
    /// frames when it scrolls and a short roll runs on to the strip's end.
    sealed class Holes : Control {
        public Holes() => IsHitTestVisible = false;

        public override void Render(DrawingContext c) {
            double lower = Edge + PerfH + Clear + Gate + Clear;
            for (double x = (PerfPitch - PerfW) / 2; x < Bounds.Width; x += PerfPitch) {
                c.DrawRectangle(Palette.FilmHole, null, new Rect(x, Edge, PerfW, PerfH), PerfR, PerfR);
                c.DrawRectangle(Palette.FilmHole, null, new Rect(x, lower, PerfW, PerfH), PerfR, PerfR);
            }
        }
    }

    sealed class FocusRing : Control {
        public override void Render(DrawingContext context) => StateButton.DrawFocusRing(context, new Rect(Bounds.Size), Radius.Md);
    }
}
