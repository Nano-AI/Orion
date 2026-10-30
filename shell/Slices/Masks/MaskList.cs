using Avalonia;
using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.Layout;
using Avalonia.Media;
using Avalonia.Threading;
using static OrionShell.Masks.MaskControls;

namespace OrionShell.Masks;

/// A port of the Swift app's DevelopPanels+MaskList.swift: one card per mask, its shapes
/// as rows beneath, and the count with the cap said out loud. The selected
/// card carries Lightroom's Add and Subtract, each a kind menu, and its menu
/// button; pointing at a card paints that mask's coverage on the photo. Its own
/// control because renaming carries draft state the panel's rebuilds must not lose.
sealed class MaskList : StackPanel {
    readonly MaskGroup _group;
    int? _renaming;   // which layer's name is being typed; lands only on Return

    static readonly IBrush SelectedFill = Tinted(Palette.Faint, 0.18), HoldingFill = Tinted(Palette.Faint, 0.10);
    static readonly IBrush HoldingStroke = Tinted(Palette.Accent, 0.6);

    public MaskList(MaskGroup group) {
        _group = group;
        Spacing = 4;
    }

    static IBrush Tinted(IBrush b, double opacity) => new SolidColorBrush(((ISolidColorBrush)b).Color, opacity);

    public void Refresh() {
        Children.Clear();
        var runs = _group.Runs();
        for (int layer = 0; layer < runs.Count; layer++) Children.Add(Card(layer, runs[layer]));
        var footer = new DockPanel { Margin = new Thickness(Space.Xs, 0) };
        var cap = Caption($"{_group.Shapes.Count} of {MaskGroup.Max} shapes");
        DockPanel.SetDock(cap, Dock.Right);
        footer.Children.Add(cap);
        footer.Children.Add(Caption(runs.Count == 1 ? "1 mask" : $"{runs.Count} masks"));
        Children.Add(footer);
    }

    Control Card(int layer, List<int> run) {
        bool holds = run.Contains(_group.Selected);
        var body = new StackPanel { Children = { Header(layer, run, holds) } };
        for (int p = 0; p < run.Count; p++) body.Children.Add(ShapeRow(run[p], p));
        var card = new Border {
            Padding = new Thickness(3), CornerRadius = new CornerRadius(Radius.Md), BorderThickness = new Thickness(1),
            Background = holds ? HoldingFill : Brushes.Transparent, BorderBrush = holds ? HoldingStroke : Palette.Line,
            Opacity = _group.LayerHidden(layer) ? 0.45 : 1, Child = body,
        };
        // Pointing at a mask shows where it is (Lightroom's hover), switch or no switch.
        card.PointerEntered += (_, _) => _group.Hover = layer;
        card.PointerExited += (_, _) => { if (_group.Hover == layer) _group.Hover = null; };
        return card;
    }

    Control Header(int layer, List<int> run, bool holds) {
        int start = run[0];
        bool hidden = _group.LayerHidden(layer);
        var row = new DockPanel { Margin = new Thickness(4, 3) };
        var eye = Eye(hidden, 10, 16, hidden ? "Show this mask" : "Hide this mask", () => _group.ToggleLayerHidden(layer));
        DockPanel.SetDock(eye, Dock.Left);
        row.Children.Add(eye);
        var menu = CardMenu(layer, start, hidden);
        if (holds) {
            // The selected mask grows by a shape: Add or Subtract, then the kind. Visible, not in a context menu (N6).
            bool room = _group.Shapes.Count < MaskGroup.Max;
            var actions = new StackPanel {
                Orientation = Orientation.Horizontal, Spacing = 0,
                Children = {
                    Menu(Glyph(Plus, 9, Palette.Text), "Add", IntoItems(0), room, "Add a shape to this mask", chevron: false),
                    Menu(Glyph(Minus, 9, Palette.Text), "Subtract", IntoItems(1), room, "Subtract a shape from this mask", chevron: false),
                    Menu(Glyph(Ellipsis, 13, Palette.Text), null, menu, true, "Mask actions", chevron: false),
                },
            };
            DockPanel.SetDock(actions, Dock.Right);
            row.Children.Add(actions);
        } else if (run.Count > 1) {
            var count = Caption($"{run.Count} shapes");
            DockPanel.SetDock(count, Dock.Right);
            row.Children.Add(count);
        }
        row.Children.Add(_renaming == layer ? RenameField(layer, start) : NameLabel(layer, start));
        row.ContextMenu = new ContextMenu { ItemsSource = CardMenu(layer, start, hidden) };
        row.Background = Brushes.Transparent;
        return row;
    }

    /// Kinds for Add or Subtract into the selected mask. A gradient or brush arms
    /// the canvas, as a new mask does; a range has no placing, so it lands at once.
    IEnumerable<Control> IntoItems(int compose) => [
        Item("Radial", () => _group.Arm(2, compose)), Item("Linear", () => _group.Arm(1, compose)),
        Item("Brush", () => _group.Arm(3, compose)),
        new Separator(),
        Item("Brightness range", () => _group.AddInto(5, compose)), Item("Color range", () => _group.AddInto(6, compose)),
    ];

    List<Control> CardMenu(int layer, int start, bool hidden) {
        var menu = new List<Control> { Item("Rename…", () => BeginRename(layer)) };
        menu.Add(Item("Move up", () => _group.MoveLayer(layer, -1)));
        menu.Add(Item("Move down", () => _group.MoveLayer(layer, 1)));
        if (layer > 0) {
            menu.Add(new Separator());
            menu.Add(Item("Subtract from mask above", () => _group.MergeIntoLayerAbove(start, 1)));
            menu.Add(Item("Intersect with mask above", () => _group.MergeIntoLayerAbove(start, 2)));
            menu.Add(Item("Add to mask above", () => _group.MergeIntoLayerAbove(start, 0)));
        }
        menu.Add(new Separator());
        menu.Add(Item(hidden ? "Show mask" : "Hide mask", () => _group.ToggleLayerHidden(layer)));
        menu.Add(Item("Delete mask", () => _group.RemoveLayer(layer)));
        return menu;
    }

    Control NameLabel(int layer, int start) {
        var name = new TextBlock {
            Text = _group.DisplayName(layer), FontSize = TypeSize.Sm, FontWeight = FontWeight.SemiBold,
            Foreground = Palette.Text, TextTrimming = TextTrimming.CharacterEllipsis, Margin = new Thickness(Space.Sm, 0, 0, 0),
            VerticalAlignment = VerticalAlignment.Center,
        };
        ToolTip.SetTip(name, "Double-click to rename");
        name.Tapped += (_, _) => Dispatcher.UIThread.Post(() => _group.Selected = start);
        name.DoubleTapped += (_, _) => BeginRename(layer);
        return name;
    }

    Control RenameField(int layer, int start) {
        var field = new TextBox {
            Text = _group.Shapes[start].Name ?? "", FontSize = TypeSize.Sm, FontWeight = FontWeight.SemiBold,
            Padding = new Thickness(0), MinHeight = 0, BorderThickness = new Thickness(0), Background = Brushes.Transparent,
            Margin = new Thickness(Space.Sm, 0, 0, 0),
        };
        field.KeyDown += (_, e) => {
            if (e.Key == Key.Enter) { _renaming = null; _group.Rename(start, field.Text ?? ""); Refresh(); e.Handled = true; }
            else if (e.Key == Key.Escape) { EndRename(); e.Handled = true; }
        };
        field.LostFocus += (_, _) => { if (_renaming == layer) EndRename(); };   // clicking away abandons
        Dispatcher.UIThread.Post(() => { field.Focus(); field.SelectAll(); });
        return field;
    }

    void BeginRename(int layer) { _renaming = layer; Refresh(); }
    void EndRename() { _renaming = null; Dispatcher.UIThread.Post(Refresh); }

    Control ShapeRow(int i, int position) {
        var m = _group.Shapes[i];
        bool selected = i == _group.Selected;
        // The op the row folds in with, as a button that changes it. The first
        // shape has none: the fold starts from zero, and a glyph the engine
        // ignores would be a label that lies.
        string op = position == 0 ? "circle.dotted" : m.Compose switch { 1 => "minus.circle", 2 => "circle.lefthalf.filled", _ => "plus.circle" };
        Control glyph = position == 0
            ? new Border { Width = 18, Child = Icons.Create(op, 9, Palette.Faint) }
            : Menu(Icons.Create(op, 9, Palette.Dim), null, [
                  Item("Add", () => _group.SetCompose(i, 0)), Item("Subtract", () => _group.SetCompose(i, 1)),
                  Item("Intersect", () => _group.SetCompose(i, 2))],
                  true, m.Compose switch { 1 => "Subtracts: click to change", 2 => "Intersects: click to change", _ => "Adds: click to change" }, chevron: false);
        if (position == 0) ToolTip.SetTip(glyph, "The mask's base shape");
        var label = new DockPanel { Children = { glyph } };
        DockPanel.SetDock(glyph, Dock.Left);
        if (selected) {
            var remove = new StateButton { Kind = ButtonKind.Bare, Content = Glyph(Cross, 8, Palette.Dim), Padding = new Thickness(4, 0),
                                           MinWidth = 0, Height = 18, VerticalAlignment = VerticalAlignment.Center }.Help("Remove this shape");
            remove.Click += (_, _) => Dispatcher.UIThread.Post(() => _group.Remove(i));
            DockPanel.SetDock(remove, Dock.Right);
            label.Children.Add(remove);
        }
        if (m.Kind == 3 && m.BrushStroke.Count > 0) {
            var dabs = Caption($"{m.BrushStroke.Count / 2} dabs");
            DockPanel.SetDock(dabs, Dock.Right);
            label.Children.Add(dabs);
        }
        label.Children.Add(new StackPanel {
            Orientation = Orientation.Horizontal, Spacing = 5, Margin = new Thickness(Space.Xs, 0, 0, 0), VerticalAlignment = VerticalAlignment.Center,
            Children = { KindGlyph(m.Kind, 10), new TextBlock { Text = MaskGroup.KindName(m.Kind), FontSize = TypeSize.Sm, VerticalAlignment = VerticalAlignment.Center } },
        });

        var pick = new Border {
            Padding = new Thickness(3, 2.5), CornerRadius = new CornerRadius(Radius.Sm), Child = label,
            Background = selected ? SelectedFill : Brushes.Transparent, Opacity = m.Hidden ? 0.45 : 1,
        };
        pick.Tapped += (_, _) => Dispatcher.UIThread.Post(() => _group.Selected = i);
        var menu = new List<Control>();
        if (position > 0) menu.Add(Item("Make its own mask", () => _group.SetLayerBreak(true, i)));
        menu.Add(Item("Remove shape", () => _group.Remove(i)));
        pick.ContextMenu = new ContextMenu { ItemsSource = menu };

        var eye = Eye(m.Hidden, 9, 14, m.Hidden ? "Show this shape" : "Hide this shape", () => _group.ToggleHidden(i));
        DockPanel.SetDock(eye, Dock.Left);
        return new DockPanel { Margin = new Thickness(Space.Pad, 0, 0, 0), Children = { eye, pick } };
    }

    /// The eye: outside the select gesture, so hiding a mask does not select it.
    static Control Eye(bool hidden, double size, double width, string help, Action toggle) {
        var eye = new Border {
            Width = width, Background = Brushes.Transparent, Cursor = new Cursor(StandardCursorType.Hand),
            Child = Icons.Create(hidden ? "eye.slash" : "eye", size, hidden ? Palette.Faint : Palette.Dim),
        };
        ToolTip.SetTip(eye, help);
        eye.Tapped += (_, e) => { e.Handled = true; Dispatcher.UIThread.Post(toggle); };
        return eye;
    }
}
