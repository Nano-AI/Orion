using Avalonia;
using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.Layout;
using Avalonia.Media;
using Avalonia.Threading;
using static OrionShell.Masks.MaskControls;

namespace OrionShell.Masks;

/// A port of app/DevelopPanels+MaskList.swift: one card per mask, its shapes
/// as rows beneath, and the count with the cap said out loud. Its own control
/// because renaming carries draft state the panel's rebuilds must not lose.
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
        var body = new StackPanel { Children = { Header(layer, run) } };
        for (int p = 0; p < run.Count; p++) body.Children.Add(ShapeRow(run[p], p));
        return new Border {
            Padding = new Thickness(3), CornerRadius = new CornerRadius(Radius.Md), BorderThickness = new Thickness(1),
            Background = holds ? HoldingFill : Brushes.Transparent, BorderBrush = holds ? HoldingStroke : Palette.Line,
            Opacity = _group.LayerHidden(layer) ? 0.45 : 1, Child = body,
        };
    }

    Control Header(int layer, List<int> run) {
        int start = run[0];
        bool hidden = _group.LayerHidden(layer);
        var row = new DockPanel { Margin = new Thickness(4, 3) };
        var eye = Eye(hidden, 10, 16, hidden ? "Show this mask" : "Hide this mask", () => _group.ToggleLayerHidden(layer));
        DockPanel.SetDock(eye, Dock.Left);
        row.Children.Add(eye);
        if (run.Count > 1) {
            var count = Caption($"{run.Count} shapes");
            DockPanel.SetDock(count, Dock.Right);
            row.Children.Add(count);
        }
        row.Children.Add(_renaming == layer ? RenameField(layer, start) : NameLabel(layer, start));

        var menu = new List<Control> { Item("Rename…", () => BeginRename(layer)) };
        if (layer > 0) {
            menu.Add(new Separator());
            menu.Add(Item("Subtract from mask above", () => _group.MergeIntoLayerAbove(start, 1)));
            menu.Add(Item("Intersect with mask above", () => _group.MergeIntoLayerAbove(start, 2)));
            menu.Add(Item("Add to mask above", () => _group.MergeIntoLayerAbove(start, 0)));
        }
        menu.Add(new Separator());
        menu.Add(Item(hidden ? "Show mask" : "Hide mask", () => _group.ToggleLayerHidden(layer)));
        row.ContextMenu = new ContextMenu { ItemsSource = menu };
        row.Background = Brushes.Transparent;
        return row;
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
        // The op the row folds in with. The first shape has none: the fold
        // starts from zero, and a glyph the engine ignores would be a label that lies.
        string op = position == 0 ? "circle.dotted" : m.Compose switch { 1 => "minus.circle", 2 => "circle.lefthalf.filled", _ => "plus.circle" };
        var glyph = new Border { Width = 13, Child = Icons.Create(op, 9, position == 0 ? Palette.Faint : Palette.Dim) };
        ToolTip.SetTip(glyph, position == 0 ? "The mask's base shape" : m.Compose switch { 1 => "subtract", 2 => "intersect", _ => "add" });
        var label = new DockPanel { Children = { glyph } };
        DockPanel.SetDock(glyph, Dock.Left);
        if (m.Kind == 3 && m.BrushStroke.Count > 0) {
            var dabs = Caption($"{m.BrushStroke.Count / 2} dabs");
            DockPanel.SetDock(dabs, Dock.Right);
            label.Children.Add(dabs);
        }
        label.Children.Add(new TextBlock { Text = MaskGroup.KindName(m.Kind), FontSize = TypeSize.Sm, Margin = new Thickness(Space.Sm, 0, 0, 0) });

        var pick = new Border {
            Padding = new Thickness(5, 2.5), CornerRadius = new CornerRadius(Radius.Sm), Child = label,
            Background = i == _group.Selected ? SelectedFill : Brushes.Transparent, Opacity = m.Hidden ? 0.45 : 1,
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
