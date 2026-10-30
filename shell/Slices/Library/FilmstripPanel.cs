using Avalonia;
using Avalonia.Controls;
using Avalonia.Controls.Primitives;
using Avalonia.Controls.Templates;
using Avalonia.Input;
using Avalonia.Interactivity;
using Avalonia.Layout;
using Avalonia.Media;

namespace OrionShell.Library;

/// A port of the Swift app's Filmstrip.swift: the filter and the order on the left, a
/// hairline, then the film: 135 film to scale (#347). The picture is the 36 mm
/// gate, so a millimetre is ThumbW / 36 points; frames sit 38 mm apart (ISO 1007),
/// and eight KS-1870 perforations run along each edge of every frame, on past the
/// last one as on a roll's tail. The film is 35 mm wide to scale, so its edges
/// show the panel above and below it, and the holes show the same panel through
/// them: a cut-out, not a dashed border (#350). A hairline gate edge keeps a
/// night frame from ending nowhere.
/// The edge print, under the lower holes where 135 film carries it, is the frame
/// number and, only when there are any, its marks. The one liberty is the print's
/// height: true to scale it would be 3 pt type.
///
/// The roll starts at the strip's left. The open photo is ringed in the accent,
/// and opening one out of view glides the roll on the shared spring just far
/// enough to show it with a frame of room (#350; centring it, #349, left a
/// short roll stranded mid-strip). A rejected frame is crossed out in red, as a
/// grease pencil marks a contact sheet.
[Panel("filmstrip", Region.Filmstrip)]
public sealed class FilmstripPanel : Grid {
    const double Mm = Layout.ThumbW / 36;
    /// One frame and the 2 mm of base before the next: eight perforations exactly.
    const double Pitch = 38 * Mm, Gate = 24 * Mm;
    /// KS-1870: 4.75 mm pitch, a 2.794 x 1.981 mm hole with 0.5 mm corners.
    const double PerfPitch = 4.75 * Mm, PerfW = 2.794 * Mm, PerfH = 1.981 * Mm, PerfR = 0.5 * Mm;
    /// The panel above and below the film, the film's edge to the holes, and the holes to the
    /// picture. The film is the strip's height less `Inset` twice: 94 pt, 35 mm. The edge print takes what is left.
    const double Inset = 2, Edge = 1.5, Clear = 2.5;
    /// A mouse wheel's notch, in pixels along the strip.
    const double WheelStep = 48;

    readonly StackPanel _frames = new() { Orientation = Orientation.Horizontal, HorizontalAlignment = HorizontalAlignment.Left };
    readonly ScrollViewer _film;
    readonly Spring _glide;
    readonly Dictionary<LibraryModel.Photo, Border> _cells = new();
    readonly Dictionary<LibraryModel.Photo, Panel> _gates = new();
    /// The one accent ring, moved to the open frame: an outline just outside the gate, by a negative
    /// margin so lighting it moves nothing. Not a BoxShadow on the frame: with one, its picture did not draw.
    readonly Border _ring = new() { Margin = new Thickness(-2.5), BorderThickness = new Thickness(2), CornerRadius = new CornerRadius(3.5),
                                    BorderBrush = Palette.Accent, IsHitTestVisible = false };
    LibraryModel.Photo? _ringed;

    public FilmstripPanel() {
        ColumnDefinitions = new ColumnDefinitions("Auto,1,*");
        Background = Palette.Panel;
        var filter = LibraryUi.Filter();
        filter.CellPadding = new Thickness(8, 0);
        var order = LibraryUi.SortMenu();
        Children.Add(new StackPanel { Spacing = 6, Margin = new Thickness(Space.Pad, 0), VerticalAlignment = VerticalAlignment.Center,
                                      Children = { filter, order, LibraryUi.SelectionCount() } });
        var line = new Border { Background = Palette.Line };
        SetColumn(line, 1);
        Children.Add(line);
        _film = new ScrollViewer {
            // The film: its base and holes under the frames, stretched to the viewport by the scroller.
            Content = new Panel { Background = Palette.FilmBase, Margin = new Thickness(0, Inset), Children = { new Holes(), _frames } },
            HorizontalScrollBarVisibility = ScrollBarVisibility.Hidden, VerticalScrollBarVisibility = ScrollBarVisibility.Disabled,
        };
        // Apple's reposition spring: damping 1, response 0.4 (Shared/Design/Spring.cs).
        _glide = new Spring(_film, 0.4) { Precision = 0.5 };
        _glide.Moved += () => _film.Offset = _film.Offset.WithX(_glide.Value);
        // A mouse has only a vertical wheel: here it scrolls along the strip. A trackpad's sideways swipe passes through.
        _film.AddHandler(PointerWheelChangedEvent, (_, e) => {
            if (e.Delta.X != 0 || e.Delta.Y == 0) return;
            _glide.Snap(Math.Max(0, _film.Offset.X - e.Delta.Y * WheelStep));   // the hand takes over from a glide
            e.Handled = true;
        }, RoutingStrategies.Tunnel);
        SetColumn(_film, 2);
        Children.Add(_film);
        // A narrower strip can hide the open frame: show it again, at once.
        _film.PropertyChanged += (_, e) => {
            if (e.Property == ScrollViewer.ViewportProperty) Reveal(glide: false);
        };

        var library = LibraryModel.Shared;
        library.Changed += () => { Rebuild(); Reveal(glide: false); };
        library.SelectionChanged += Rebuild;
        EngineModel.Shared.Opened += () => { Ring(); Reveal(glide: true); };
        Rebuild();
    }

    void Rebuild() {
        var library = LibraryModel.Shared;
        _frames.Children.Clear();
        _cells.Clear();
        _gates.Clear();
        _ringed = null;
        foreach (var photo in library.Visible) {
            var (cell, gate) = Cell(photo);
            _cells[photo] = cell;
            _gates[photo] = gate;
            _frames.Children.Add(cell);
        }
        Ring();
    }

    /// The ring onto the open frame, and its spoken name with it.
    void Ring() {
        var open = LibraryModel.Shared.Open;
        if (_ring.Parent is Panel had) had.Children.Remove(_ring);
        if (_ringed != null && _cells.TryGetValue(_ringed, out var was))
            Avalonia.Automation.AutomationProperties.SetName(was, LibraryUi.Spoken(_ringed));
        _ringed = open != null && _gates.TryGetValue(open, out var gate) ? open : null;
        if (_ringed == null) return;
        _gates[_ringed].Children.Add(_ring);
        Avalonia.Automation.AutomationProperties.SetName(_cells[_ringed], LibraryUi.Spoken(_ringed) + ", open");
    }

    /// The open frame into view with a frame of room beside it, moving the roll
    /// no further than that and not at all when it already shows: by the spring
    /// when a photo opens, at once when the strip changed under it.
    void Reveal(bool glide) {
        int at = _ringed == null ? -1 : _frames.Children.IndexOf(_cells[_ringed]);
        // From where the roll is headed, or where a trackpad left it.
        double view = _film.Viewport.Width, from = _glide.Moving ? _glide.Target : _film.Offset.X;
        if (at < 0 || view <= 0) return;
        double left = at * Pitch, room = Math.Min(Pitch, Math.Max(0, (view - Pitch) / 2));
        double x = left - room < from ? left - room : left + Pitch + room > from + view ? left + Pitch + room - view : from;
        x = Math.Clamp(x, 0, Math.Max(0, _frames.Children.Count * Pitch - view));
        if (x == from) return;
        if (glide) _glide.To(x); else _glide.Snap(x);
    }

    /// One frame of film: the picture in its gate over the holes, the edge print under them.
    static (Border Cell, Panel Gate) Cell(LibraryModel.Photo photo) {
        var picture = new Image { Source = photo.Thumbnail, Stretch = Stretch.Uniform, Opacity = photo.Rejected ? LibraryUi.RejectedOpacity : 1 };
        var number = Faces.Figure($"{photo.Index + 1}", Faces.Mark, Palette.Dim);
        // A rejected frame's mark is the cross over it, so the print carries stars only.
        var marks = photo.Rejected ? new Panel() : LibraryUi.Marks(photo, Faces.Mark, unearned: false);
        // The marks follow their own number: at the frame's right they read as the next frame's.
        var print = new StackPanel { Orientation = Orientation.Horizontal, Spacing = 6, VerticalAlignment = VerticalAlignment.Center, Children = { number, marks } };
        var frame = new Frame { Child = picture };
        var held = new Panel { Children = { frame } };
        if (photo.Rejected) held.Children.Add(new Crossed());
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
        LibraryUi.Tip(cell, photo);
        Avalonia.Automation.AutomationProperties.SetName(cell, LibraryUi.Spoken(photo));
        // In the set a batch acts on: a dimmer ring than the open frame's, which, drawn later, covers it.
        if (LibraryModel.Shared.HasExplicitSelection && LibraryModel.Shared.IsSelected(photo))
            held.Children.Add(new Border { Margin = new Thickness(-2.5), BorderThickness = new Thickness(1.5), CornerRadius = new CornerRadius(3.5),
                                           BorderBrush = LibraryUi.SelectedRing, IsHitTestVisible = false });
        // A plain click opens; command and shift build the selection (Filmstrip.activate).
        cell.Tapped += (_, e) => LibraryModel.Shared.ClickStrip(photo, LibraryModel.From(e.KeyModifiers));
        cell.KeyDown += (_, e) => {
            if (e.Key is Key.Enter or Key.Space) { EngineModel.Shared.Open(photo.Index); e.Handled = true; }
        };
        cell.ContextMenu = LibraryUi.Menu(() => Menu(photo));
        return (cell, held);
    }

    /// The cull entries, then Merge to HDR… and Export… only over a group,
    /// counted like the rest: a merge of one frame means nothing.
    internal static List<Control> Menu(LibraryModel.Photo photo) {
        var items = LibraryUi.CullItems(photo);
        var scope = LibraryModel.Shared.CullScope(photo);
        if (scope.Count > 1)
            items.AddRange([new Separator(), LibraryUi.Item("Merge to HDR…" + LibraryUi.Suffix(scope), () => Commands.Run("hdr-merge")),
                            new Separator(), LibraryUi.Item("Export…" + LibraryUi.Suffix(scope), () => Commands.Run("export"))]);
        return items;
    }

    /// A rejected frame, crossed out corner to corner in the reject red.
    sealed class Crossed : Control {
        static readonly IPen Pencil = new Pen(Palette.Reject, 2, lineCap: PenLineCap.Round);

        public Crossed() => IsHitTestVisible = false;

        public override void Render(DrawingContext c) {
            var r = new Rect(Bounds.Size).Deflate(6);
            c.DrawLine(Pencil, r.TopLeft, r.BottomRight);
            c.DrawLine(Pencil, r.TopRight, r.BottomLeft);
        }
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
                c.DrawRectangle(Palette.Panel, null, new Rect(x, Edge, PerfW, PerfH), PerfR, PerfR);
                c.DrawRectangle(Palette.Panel, null, new Rect(x, lower, PerfW, PerfH), PerfR, PerfR);
            }
        }
    }

    sealed class FocusRing : Control {
        public override void Render(DrawingContext context) => StateButton.DrawFocusRing(context, new Rect(Bounds.Size), Radius.Md);
    }
}
