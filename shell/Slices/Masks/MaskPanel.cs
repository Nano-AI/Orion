using Avalonia;
using Avalonia.Controls;
using Avalonia.Layout;
using Avalonia.Media;
using static OrionShell.Masks.MaskControls;

namespace OrionShell.Masks;

/// A port of the Swift app's DevelopPanels+Mask.swift, the Mask tab: the draw tools, the
/// mask cards, the overlay switch, the adjustments the selected mask carries
/// and the controls its selected shape's kind needs. Rebuilt whenever the
/// group changes, because which controls exist follows the selected row's kind.
[Panel("masks", Region.Tools, 10, Tab = "Mask")]
public sealed class MaskPanel : Border {
    readonly MaskGroup _group = MaskGroup.Shared;
    readonly MaskList _list;
    string? _message;   // said once by the next rebuild, then gone

    public MaskPanel() {
        _list = new MaskList(_group);
        _group.Changed += Rebuild;
        MaskScenes.PoseIfAsked();
        Rebuild();
    }

    void Rebuild() {
        (_list.Parent as Panel)?.Children.Remove(_list);
        _list.Refresh();
        Child = new SectionPlate("Masks", null, Rows().ToArray());
        _message = null;
    }

    IEnumerable<Control> Rows() {
        yield return Tools();
        if (_group.Shapes.Count == 0) {
            // The empty state names what a mask is for and how one is made (N6, N10).
            yield return Caption("A mask limits an adjustment to part of the photo. Pick a kind, then drag on the photo to place it.");
            if (_message != null) yield return Caption(_message);
            yield break;
        }
        yield return _list;
        yield return Switch("Show overlay", _group.Overlay, on => _group.Overlay = on);
        if (_message != null) yield return Caption(_message);
        if (_group.Current is not { } m) yield break;
        if (m.Kind == 4) yield return Caption(MatteCaption(m));

        int layer = _group.SelectedLayer;
        yield return Engraved.Label(_group.LayerCount > 1 ? $"{_group.DisplayName(layer)}, mask {layer + 1} of {_group.LayerCount}"
                                                          : _group.DisplayName(layer), Palette.Accent);
        foreach (var slider in AdjustmentGroup.Local(layer)) yield return slider;
        yield return new AdjustmentSlider("Refine", "mask_refine", 0, 1, "", 2, baseValue: 0);

        yield return Engraved.Label($"{MaskGroup.KindName(m.Kind)} shape");
        foreach (var row in KindControls(m)) yield return row;
        yield return Check("Invert", m.Invert, on => { m.Invert = on; _group.Push("Invert shape"); });
    }

    /// The draw tools, always in view: a new mask is one press here and one drag on the photo.
    Control Tools() {
        bool can = _group.Shapes.Count < MaskGroup.Max && EngineModel.Shared.Handle != 0 && !SubjectMatte.Running;
        Control Tool(int kind, Control icon, string name, string help) {
            bool armed = _group.Drawing == kind && _group.DrawInto == null;
            var b = new StateButton {
                Kind = ButtonKind.Segment, On = armed, IsEnabled = can, Height = 26, Padding = new Thickness(8, 0),
                Content = new StackPanel { Orientation = Orientation.Horizontal, Spacing = 5, Children = { icon, new TextBlock { Text = name, VerticalAlignment = VerticalAlignment.Center } } },
            }.Help(help);
            b.Click += (_, _) => Avalonia.Threading.Dispatcher.UIThread.Post(() => _group.Arm(armed ? 0 : kind, null));
            return b;
        }
        var more = Menu(null, "More", [
            Item("Brightness range", () => _group.Add(5)), Item("Color range", () => _group.Add(6)),
            new Separator(), Heading("Detect"),
            Item("Subject", () => Detect(SubjectMatte.Kind.Subject)), Item("Person", () => Detect(SubjectMatte.Kind.Person)),
            // "estimated": not a model, the calm region joined to the top edge (research/sky-detection.md).
            Item("Sky (estimated)", () => Detect(SubjectMatte.Kind.Sky)),
        ], can);
        return new WrapPanel {
            Orientation = Orientation.Horizontal,
            Children = {
                Tool(2, KindGlyph(2), "Radial", "Drag on the photo to draw a radial mask"),
                Tool(1, KindGlyph(1), "Linear", "Drag on the photo from full effect to none"),
                Tool(3, KindGlyph(3), "Brush", "Paint a mask on the photo"),
                more,
            },
        };
    }

    /// The placement controls each kind reads, and only those: a slider the
    /// shader ignores is worse than no slider (the Swift comments say which).
    /// First, what the row is (Engine+Mask.swift's setMaskKind): the Add menu makes rows, this changes one.
    IEnumerable<Control> KindControls(MaskShape m) {
        int row = _group.Selected;
        yield return new StackPanel {
            Orientation = Orientation.Horizontal, Spacing = Space.Sm,
            Children = {
                Caption("Kind"),
                Menu(null, MaskGroup.KindName(m.Kind), [
                    Item("Linear", () => _group.SetKind(row, 1)), Item("Radial", () => _group.SetKind(row, 2)),
                    Item("Brush", () => _group.SetKind(row, 3)), Item("Brightness range", () => _group.SetKind(row, 5)),
                    Item("Color range", () => _group.SetKind(row, 6)),
                ]),
            },
        };
        switch (m.Kind) {
            case 5:
                yield return Shape("From", -8, 8, " EV", 1, s => s.RangeLo, (s, v) => s.RangeLo = v);
                yield return Shape("To", -8, 8, " EV", 1, s => s.RangeHi, (s, v) => s.RangeHi = v);
                yield return Shape("Softness", 0.05f, 4, " EV", 2, s => s.RangeSoft, (s, v) => s.RangeSoft = v);
                break;
            case 6:
                // What was under the click (maskColorSwatch); before any pick, the stored
                // target raised to something a screen shows, a label for it, not a rendering.
                static byte B(double c) => (byte)Math.Round(255 * Math.Clamp(c, 0, 1));
                static byte G(float c) => B(Math.Pow(Math.Max(c, 0), 1 / 2.2));
                var shade = _group.ColorSwatch is { } d ? Color.FromRgb(B(d[0]), B(d[1]), B(d[2])) : Color.FromRgb(G(m.ColorR), G(m.ColorG), G(m.ColorB));
                var swatch = new Border {
                    Width = 26, Height = 18, CornerRadius = new CornerRadius(Radius.Sm), BorderBrush = Palette.Line,
                    BorderThickness = new Thickness(1), Background = new SolidColorBrush(shade), Tag = "mask-swatch",
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
                yield return Shape("Size", 0.01f, 0.4f, "", 3, s => s.BrushRadius, (s, v) => s.BrushRadius = v, brush: true);
                // Feather is the soft edge: the engine's hardness, the other way up (Lightroom's word).
                yield return Shape("Feather", 0, 1, "", 2, s => 1 - s.BrushHardness, (s, v) => s.BrushHardness = 1 - v, brush: true);
                yield return Shape("Flow", 0.01f, 1, "", 2, s => s.BrushFlow, (s, v) => s.BrushFlow = v);
                yield return new StackPanel {
                    Orientation = Orientation.Horizontal, Spacing = Space.Md,
                    Children = {
                        Push("Clear stroke", () => { m.BrushStroke.Clear(); m.BrushErase.Clear(); _group.Push("Clear brush"); Rebuild(); },
                             m.BrushStroke.Count > 0),
                        Caption(m.BrushStroke.Count == 0 ? $"Drag on the photo to paint. {Option} erases."
                                                         : $"{m.BrushStroke.Count / 2} dabs. {Option} erases."),
                    },
                };
                break;
        }
        if (m.Kind == 1) {
            // No Feather: a linear ramp's Length is its feather.
            yield return Shape("Length", 0.05f, 1.5f, "", 2, s => s.Length, (s, v) => s.Length = v);
        } else if (m.Kind == 2) {
            yield return Shape("Feather", 0, 1, "", 2, s => s.Feather, (s, v) => s.Feather = v);
            yield return Shape("Roundness", 2, 8, "", 1, s => s.Roundness, (s, v) => s.Roundness = v);
            yield return Shape("Width", 0.02f, 1, "", 2, s => s.RadiusX, (s, v) => s.RadiusX = v);
            yield return Shape("Height", 0.02f, 1, "", 2, s => s.RadiusY, (s, v) => s.RadiusY = v);
        }
        if (m.Kind is 1 or 2) {
            yield return Shape("Angle", -3.15f, 3.15f, " rad", 2, s => s.Angle, (s, v) => s.Angle = v);
            yield return Shape("Center X", 0, 1, "", 2, s => s.CenterX, (s, v) => s.CenterX = v);
            yield return Shape("Center Y", 0, 1, "", 2, s => s.CenterY, (s, v) => s.CenterY = v);
        }
    }

    static string Option => OperatingSystem.IsMacOS() ? "Option" : "Alt";

    /// One of the selected shape's numbers; it resets to MaskComponentState's default.
    /// A brush number also shows the nib on the photo while it moves (size feedback).
    Control Shape(string name, float min, float max, string unit, int decimals,
                  Func<MaskShape, float> get, Action<MaskShape, float> set, bool brush = false) {
        var m = _group.Current!;
        return new MaskSlider(name, () => get(m), v => {
            set(m, v);
            if (brush) _group.BrushPreview = true;
            _group.Push($"Mask {name.ToLowerInvariant()}");
        }, min, max, unit, decimals, get(MaskGroup.Defaults));
    }

    /// The row and the model in one act (SubjectMatte.Select). The row is made
    /// only once the model has answered, so a Subject row never exists unfilled;
    /// "nothing was found" is a real answer and is said, not left as an empty matte.
    async void Detect(SubjectMatte.Kind kind) {
        _message = "Selecting…";
        Rebuild();
        string? said;
        try { said = await SubjectMatte.Select(kind); }
        catch (Exception e) { said = $"{kind} selection failed: {e.Message}"; }   // async void: nothing may escape
        _message = said;
        Rebuild();
    }

    /// The missing file and the unsaved selection are news; a saved one names its producer and stops.
    static string MatteCaption(MaskShape m) => EngineModel.Shared.Session is { } s && MatteStore.IsMissing(s, m.MatteId)
        ? "Missing file — this row covers nothing. Run it again."
        : m.MatteSource is not { } source
            ? "Empty. Press Subject, Person or Sky."
            : m.MatteId == null
                ? $"{source} selection — not saved yet, so it will not survive reopening."
                : $"{source} selection, saved with the photo.";
}
