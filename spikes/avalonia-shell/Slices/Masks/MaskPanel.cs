using Avalonia;
using Avalonia.Controls;
using Avalonia.Layout;
using Avalonia.Media;
using static OrionShell.Masks.MaskControls;

namespace OrionShell.Masks;

/// A port of app/DevelopPanels+Mask.swift, the Mask tab: the mask cards, the
/// Add menu, the controls the selected shape's kind needs, and the local
/// adjustments its layer carries. Rebuilt whenever the row list changes,
/// because which controls exist follows the selected row's kind.
[Panel("masks", Region.Tools, 10, Tab = "Mask")]
public sealed class MaskPanel : Border {
    readonly MaskGroup _group = MaskGroup.Shared;
    readonly MaskList _list;
    string? _message;   // said once by the next rebuild, then gone

    public MaskPanel() {
        _list = new MaskList(_group);
        _group.Changed += Rebuild;
        Rebuild();
    }

    void Rebuild() {
        (_list.Parent as Panel)?.Children.Remove(_list);
        _list.Refresh();
        Child = new SectionPlate("Local", null, Rows().ToArray());
        _message = null;
        var engine = EngineModel.Shared;
        if (engine.Get("mask_overlay") != 0 && engine.Get("mask_overlay_layer") != _group.SelectedLayer)
            engine.Set("mask_overlay_layer", _group.SelectedLayer);   // the overlay paints the layer being edited
    }

    IEnumerable<Control> Rows() {
        var engine = EngineModel.Shared;
        if (_group.Shapes.Count > 0) {
            yield return _list;
            yield return ListButtons();
            yield return new StackPanel {
                Orientation = Orientation.Horizontal, Spacing = Space.Sm,
                Children = {
                    new TextBlock { Text = "Kind", FontSize = TypeSize.Sm, Foreground = Palette.Faint, VerticalAlignment = VerticalAlignment.Center },
                    Menu(null, MaskGroup.KindName(_group.Kind), [
                        Item("Linear", () => _group.SetKind(_group.Selected, 1)),
                        Item("Radial", () => _group.SetKind(_group.Selected, 2)),
                        Item("Brush", () => _group.SetKind(_group.Selected, 3)),
                        Item("Brightness range", () => _group.SetKind(_group.Selected, 5)),
                        Item("Color range", () => _group.SetKind(_group.Selected, 6))]),
                },
            };
        } else {
            yield return AddMenu();
        }

        yield return Switch("Show mask", engine.Get("mask_overlay") != 0, on => {
            engine.Set("mask_overlay_layer", _group.SelectedLayer);
            engine.Set("mask_overlay", on ? 1 : 0);
        });
        if (_message != null) yield return Caption(_message, TypeSize.Sm);
        if (_group.Current is not { } m) yield break;
        if (m.Kind == 4) yield return Caption(MatteCaption(m));

        // The op applies only to a row that continues a layer: one that begins
        // a layer folds from zero, where subtract and intersect empty it.
        if (_group.Selected > 0 && !m.StartsLayer)
            yield return Segmented(["Add", "Subtract", "Intersect"], m.Compose, _group.SetCompose);
        int layer = _group.SelectedLayer;
        if (_group.LayerCount > 1)
            yield return Engraved.Label($"{_group.DisplayName(layer)} — mask {layer + 1} of {_group.LayerCount}", Palette.Accent);
        foreach (var slider in AdjustmentGroup.Local(layer)) yield return slider;
        yield return new AdjustmentSlider("Refine", "mask_refine", 0, 1, "", 2, baseValue: 0);

        foreach (var row in KindControls(m)) yield return row;

        yield return Check("Invert", m.Invert, on => { m.Invert = on; _group.Push(); });
    }

    /// The placement controls each kind reads, and only those: a slider the
    /// shader ignores is worse than no slider (the Swift comments say which).
    /// stub(#315): Engine+Mask.swift owns this until the logic moves below the facade
    IEnumerable<Control> KindControls(MaskShape m) {
        switch (m.Kind) {
            case 5:
                yield return Shape("From", -8, 8, " EV", 1, s => s.RangeLo, (s, v) => s.RangeLo = v);
                yield return Shape("To", -8, 8, " EV", 1, s => s.RangeHi, (s, v) => s.RangeHi = v);
                yield return Shape("Softness", 0.05f, 4, " EV", 2, s => s.RangeSoft, (s, v) => s.RangeSoft = v);
                break;
            case 6:
                // The picked shade as a label, raised to something a screen shows.
                // stub(#315): Engine+Mask.swift owns this until the logic moves below the facade (maskColorSwatch)
                byte G(float c) => (byte)Math.Round(255 * Math.Clamp(Math.Pow(Math.Max(c, 0), 1 / 2.2), 0, 1));
                var swatch = new Border {
                    Width = 26, Height = 18, CornerRadius = new CornerRadius(Radius.Sm), BorderBrush = Palette.Line,
                    BorderThickness = new Thickness(1), Background = new SolidColorBrush(Color.FromRgb(G(m.ColorR), G(m.ColorG), G(m.ColorB))),
                };
                yield return new StackPanel {
                    Orientation = Orientation.Horizontal, Spacing = Space.Md,
                    Children = { swatch, new ToolButton(CanvasTool.MaskColor, "eyedropper", "Pick color", "Click the photo…",
                                                        help: "The next click on the photo sets the mask's target color") },
                };
                yield return Shape("Tolerance", 0.01f, 0.8f, "", 3, s => s.ColorTol, (s, v) => s.ColorTol = v);
                yield return Shape("Softness", 0.002f, 0.4f, "", 3, s => s.ColorSoft, (s, v) => s.ColorSoft = v);
                break;
            case 3:
                // Paint and erase are equal acts, so a pair rather than an "Erase" toggle.
                yield return Segmented(["Paint", "Erase"], _group.BrushErasing ? 1 : 0, i => { _group.BrushErasing = i == 1; Rebuild(); });
                yield return Shape("Size", 0.01f, 0.4f, "", 3, s => s.BrushRadius, (s, v) => s.BrushRadius = v);
                yield return Shape("Flow", 0.01f, 1, "", 2, s => s.BrushFlow, (s, v) => s.BrushFlow = v);
                yield return Shape("Hardness", 0, 1, "", 2, s => s.BrushHardness, (s, v) => s.BrushHardness = v);
                // stub(#315): Engine+Brush.swift owns this until the logic moves below the facade (strokes come from the canvas)
                yield return new StackPanel {
                    Orientation = Orientation.Horizontal, Spacing = Space.Md,
                    Children = {
                        Push("Clear stroke", () => { m.BrushStroke.Clear(); _group.Push(); Rebuild(); }, m.BrushStroke.Count > 0),
                        Caption(m.BrushStroke.Count == 0 ? "drag on the photo to paint" : $"{m.BrushStroke.Count} dabs"),
                    },
                };
                break;
        }
        if (m.Kind is 1 or 2) {
            yield return Shape("Center X", 0, 1, "", 2, s => s.CenterX, (s, v) => s.CenterX = v);
            yield return Shape("Center Y", 0, 1, "", 2, s => s.CenterY, (s, v) => s.CenterY = v);
            yield return Shape("Angle", -3.15f, 3.15f, " rad", 2, s => s.Angle, (s, v) => s.Angle = v);
        }
        if (m.Kind == 1) {
            // No Feather: a linear ramp's Length is its feather.
            yield return Shape("Length", 0.05f, 1.5f, "", 2, s => s.Length, (s, v) => s.Length = v);
        } else if (m.Kind == 2) {
            yield return Shape("Feather", 0, 1, "", 2, s => s.Feather, (s, v) => s.Feather = v);
            yield return Shape("Width", 0.02f, 1, "", 2, s => s.RadiusX, (s, v) => s.RadiusX = v);
            yield return Shape("Height", 0.02f, 1, "", 2, s => s.RadiusY, (s, v) => s.RadiusY = v);
            yield return Shape("Roundness", 2, 8, "", 1, s => s.Roundness, (s, v) => s.Roundness = v);
        }
    }

    /// One of the selected shape's numbers; it resets to MaskComponentState's default.
    Control Shape(string name, float min, float max, string unit, int decimals,
                  Func<MaskShape, float> get, Action<MaskShape, float> set) {
        var m = _group.Current!;
        return new MaskSlider(name, () => get(m), v => { set(m, v); _group.Push(); }, min, max, unit, decimals, get(MaskGroup.Defaults));
    }

    Control ListButtons() {
        int sel = _group.Selected, count = _group.Shapes.Count;
        Control Chevron(string icon, int by, bool enabled) =>
            Push(Icons.Create(icon, 10, enabled ? Palette.Text : Palette.Faint), () => _group.Move(_group.Selected, by), enabled, Space.Pad);
        var reorder = new StackPanel {
            Orientation = Orientation.Horizontal, Spacing = Space.Md,
            Children = { Chevron("chevron.up", -1, sel > 0), Chevron("chevron.down", 1, sel < count - 1) },
        };
        DockPanel.SetDock(reorder, Dock.Right);
        return new DockPanel {
            Children = {
                reorder,
                new StackPanel {
                    Orientation = Orientation.Horizontal, Spacing = Space.Md,
                    Children = { AddMenu(), Push("Remove", () => _group.Remove(_group.Selected), count > 0) },
                },
            },
        };
    }

    /// One way in, grouped by how the mask decides what it covers: Draw is
    /// placed by hand, Detect runs a model, Match measures the pixels.
    Control AddMenu() {
        var items = Families(into: false).ToList();
        if (_group.Shapes.Count > 0) {
            items.Add(new Separator());
            items.Add(Submenu("Into the selected mask", Families(into: true)));
        }
        return Menu("plus", "Add", items, _group.Shapes.Count < MaskGroup.Max && EngineModel.Shared.Handle != 0);
    }

    IEnumerable<Control> Families(bool into) {
        Func<int, bool> add = into ? _group.AddInto : _group.Add;
        return [
            Heading("Draw"),
            Item("Linear gradient", () => add(1)), Item("Radial gradient", () => add(2)), Item("Brush", () => add(3)),
            new Separator(), Heading("Detect"),
            Item("Subject", () => Detect("Subject", add)), Item("Person", () => Detect("Person", add)),
            Item("Sky (estimated)", () => Detect("Sky", add)),
            new Separator(), Heading("Match"),
            Item("Brightness range", () => add(5)), Item("Color range", () => add(6)),
        ];
    }

    /// The row and the model in one act, so a Subject row never exists unfilled.
    /// The row is added; the model is not run, and the panel says so rather
    /// than leave an empty matte that looks like a broken feature.
    // stub(#315): SubjectMatte.swift owns this until the logic moves below the facade
    void Detect(string what, Func<int, bool> add) {
        _message = $"{what} detection is not ported: SubjectMatte.swift runs it.";
        if (!add(4)) Rebuild();
    }

    // stub(#315): MatteStore.swift owns this until the logic moves below the facade (the missing-file case)
    static string MatteCaption(MaskShape m) => m.MatteSource is not { } source
        ? "Empty. Press Subject, Person or Sky."
        : m.MatteId == null
            ? $"{source} selection — not saved yet, so it will not survive reopening."
            : $"{source} selection, saved with the photo.";
}
