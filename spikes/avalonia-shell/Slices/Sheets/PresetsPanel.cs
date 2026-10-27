using Avalonia;
using Avalonia.Controls;
using Avalonia.Layout;
using Avalonia.Media;

namespace OrionShell;

/// The saved looks. Built-ins only: the photographer's own live in presets.json.
// stub(#315): Presets.swift owns this until the logic moves below the facade
static class PresetStub {
    public sealed record Preset(string Name, int Groups, bool BuiltIn);

    /// PresetGroup: title and what it covers, in its declared order.
    public static readonly (string Title, string Covers)[] Groups = [
        ("White Balance", "temperature, tint"),
        ("Light", "exposure, contrast, highlights, shadows, whites, blacks"),
        ("Color", "vibrance, saturation, the mixer, color grading"),
        ("Curve", "the tone curve"),
        ("Detail", "sharpening, noise reduction, lens corrections"),
        ("Effects", "clarity, dehaze, shadow lift, LUT strength"),
    ];

    /// PresetGroup.defaultSelection: a look, without white balance or detail.
    public static HashSet<int> DefaultSelection() => [1, 2, 3, 5];

    /// PresetStore.builtIns, each carrying light, color, curve and effects.
    public static readonly List<Preset> Presets =
        new[] { "Punch", "Soft", "Monochrome", "Lift the shadows" }.Select(n => new Preset(n, 4, true)).ToList();

    public static void Act(string what) => Console.Error.WriteLine($"presets: {what} is Presets.swift's");
}

/// A port of `presetsPanel` in app/DevelopPanels+Presets.swift: versions,
/// then presets, copy, paste, sync and batch export, on a tab of their own.
[Panel("presets", Region.Tools, 10, Tab = "Presets")]
public sealed class PresetsPanel : StackPanel {
    readonly HashSet<int> _groups = PresetStub.DefaultSelection();
    readonly SectionPlate _plate = new("Presets");
    readonly TextBox _name = SheetKit.Field("New preset");
    readonly Button _save = SheetKit.Button("Save", () => PresetStub.Act("saving a preset"));
    readonly Button _copy, _paste = SheetKit.Button("Paste", () => PresetStub.Act("pasting settings"));
    readonly Button _sync = SheetKit.Button("Sync all…", () => PresetStub.Act("syncing settings"));
    readonly Button _export = SheetKit.Button("Export all…", () => PresetStub.Act("batch export"));
    readonly TextBlock _copiedNote = SheetKit.Note("Settings copied. Paste puts them on this photo; Sync writes them to every photo in view.");
    bool _copied;

    // stub(#315): Library.swift owns this until the logic moves below the facade
    static int PhotosInView => EngineModel.Shared.Photos.Count;
    static int ExportTargets => PhotosInView;   // the photos in view less the rejects
    static int RejectedInView => 0;

    public PresetsPanel() {
        Spacing = Space.Xl;
        SheetKit.Restyle(this);
        // what Copy holds is engine.state:
        // stub(#315): Engine.swift owns this until the logic moves below the facade
        _copy = SheetKit.Button("Copy", () => { _copied = true; Enable(); });
        _name.TextChanged += (_, _) => Enable();
        Children.Add(new VersionsSection().Plate);
        Children.Add(_plate);
        Build();
        EngineModel.Shared.Opened += Enable;
    }

    /// Each control's guard from the Swift, re-read whenever its inputs change.
    void Enable() {
        _save.IsEnabled = (_name.Text ?? "").Trim().Length > 0 && _groups.Count > 0;
        _copy.IsEnabled = IsLoadedPhoto;
        _paste.IsEnabled = _copied && _groups.Count > 0;
        _sync.IsEnabled = _copied && _groups.Count > 0 && PhotosInView >= 2;
        _copiedNote.IsVisible = _copied;
        // Guarded on the export targets, not the photos: a folder of rejects writes nothing.
        _export.IsEnabled = IsLoadedPhoto && ExportTargets > 0;
    }

    static bool IsLoadedPhoto => EngineModel.Shared.Handle != 0;

    void Build() {
        var rows = _plate.Rows.Children;
        rows.Clear();
        if (PresetStub.Presets.Count == 0) rows.Add(SheetKit.Note("No presets yet."));
        var list = new StackPanel { Spacing = Space.Xs };
        foreach (var p in PresetStub.Presets) list.Children.Add(Row(p));
        rows.Add(list);

        // A preset that did not reach the file is gone at the next launch; the
        // name stays in the field on a failure so the save can be retried.
        rows.Add(SheetKit.FieldWithButton(_name, _save));

        // Which groups a saved preset, a paste and a sync will carry: one list, one answer.
        var groups = new StackPanel { Spacing = Space.Xs };
        for (int i = 0; i < PresetStub.Groups.Length; i++) groups.Children.Add(GroupToggle(i));
        rows.Add(groups);

        // engine.apply(preset:), SyncSettings.sync:
        // stub(#315): Engine.swift owns this until the logic moves below the facade
        rows.Add(new StackPanel { Orientation = Orientation.Horizontal, Spacing = Space.Sm, Children = { _copy, _paste, _sync } });
        rows.Add(_copiedNote);

        // Beside sync: the same gesture — do this to all of them — writing files, not sidecars.
        // runBatchExport, its progress and Stop:
        // stub(#315): OrionApp+Files.swift owns this until the logic moves below the facade
        _export.HorizontalAlignment = HorizontalAlignment.Left;
        rows.Add(_export);
        if (RejectedInView > 0)
            rows.Add(SheetKit.Note($"Exports {ExportTargets} photos. " +
                                   (RejectedInView == 1 ? "1 rejected photo is left out." : $"{RejectedInView} rejected photos are left out.")));

        rows.Add(SheetKit.Note("A preset changes only the groups it carries and leaves everything else alone. The crop, " +
                               "the dust spots and the masks are never included — those belong to one photograph."));
        Enable();
    }

    /// One saved look: apply, and delete when it is the photographer's own.
    static Control Row(PresetStub.Preset p) {
        var head = new DockPanel();
        // What it will disturb, before it is pressed.
        var groups = new TextBlock {
            Text = p.Groups == PresetStub.Groups.Length ? "all" : $"{p.Groups} groups", FontSize = TypeSize.Sm, Foreground = Palette.Faint,
        };
        DockPanel.SetDock(groups, Dock.Right);
        head.Children.AddRange([groups, new TextBlock { Text = p.Name, FontSize = TypeSize.Sm }]);
        var apply = new Border {
            Background = Palette.Raised, CornerRadius = new CornerRadius(Radius.Sm), Padding = new Thickness(Space.Sm, 3),
            Child = head, Cursor = new Avalonia.Input.Cursor(Avalonia.Input.StandardCursorType.Hand),
        };
        // engine.apply(preset:):
        // stub(#315): Engine.swift owns this until the logic moves below the facade
        apply.Tapped += (_, _) => PresetStub.Act($"applying '{p.Name}'");
        if (p.BuiltIn) return apply;
        var row = new DockPanel();
        var delete = SheetKit.Glyph("minus.circle", "Delete this preset", () => PresetStub.Act("deleting a preset"));
        delete.Margin = new Thickness(Space.Sm, 0, 0, 0);
        DockPanel.SetDock(delete, Dock.Right);
        row.Children.AddRange([delete, apply]);
        return row;
    }

    /// FlowGroups: a small switch per group, its title and what it covers.
    Control GroupToggle(int i) {
        var (title, covers) = PresetStub.Groups[i];
        var box = new CheckBox {
            IsChecked = _groups.Contains(i), VerticalContentAlignment = VerticalAlignment.Center,
            Content = new StackPanel {
                Orientation = Orientation.Horizontal, Spacing = 4,
                Children = {
                    new TextBlock { Text = title, FontSize = TypeSize.Xs },
                    new TextBlock { Text = covers, FontSize = 9, Foreground = Palette.Faint, TextTrimming = TextTrimming.CharacterEllipsis },
                },
            },
        };
        box.IsCheckedChanged += (_, _) => {
            if (box.IsChecked == true) _groups.Add(i); else _groups.Remove(i);
            Enable();
        };
        return box;
    }
}
