using System.Text.Json;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Layout;
using Avalonia.Media;
using Avalonia.VisualTree;

namespace OrionShell.Sheets;

/// A saved look (Presets.swift). A patch, not a state: it carries a whole look
/// and the groups it may touch, and applying copies only those. The crop, the
/// spots and the masks are in no group, so no preset can ever carry them.
sealed record Preset(string Name, HashSet<int> Groups, Dictionary<string, float[]> State, List<(float X, float Y)>[] Curves,
                     bool BuiltIn = false);

/// A port of PresetGroup, Preset.applied and PresetStore. The user's presets are
/// kept in presets-shell.json in Orion's Application Support folder (AppFolder);
/// SettingsImport carries the Swift app's presets.json into it once.
static class PresetStore {
    /// PresetGroup in its declared order: title, what it covers, the C fields Preset.applied copies.
    public static readonly (string Title, string Covers, string[] Fields)[] Groups = [
        ("White balance", "temperature, tint", ["temperature_k", "tint"]),
        ("Light", "exposure, contrast, highlights, shadows, whites, blacks",
         ["exposure_ev", "contrast", "highlights", "shadows", "whites", "blacks", "highlight_recovery"]),
        ("Color", "vibrance, saturation, the mixer, color grading",
         ["vibrance", "saturation", "hue_shift", "sat_shift", "lum_shift", "grade_shadow", "grade_midtone", "grade_highlight", "grade_balance"]),
        ("Curve", "the tone curve", []),
        ("Detail", "sharpening, noise reduction, lens corrections",
         ["sharpen_amount", "sharpen_radius", "sharpen_masking", "denoise_luma", "denoise_color", "denoise_detail", "denoise_nlm",
          "denoise_smoothness", "lens_distortion", "lens_vignette", "lens_ca_red", "lens_ca_blue"]),
        ("Effects", "clarity, dehaze, shadow lift, LUT strength",
         ["clarity", "texture", "dehaze", "fusion", "lut_strength", "grain_amount", "grain_size", "vignette_amount", "vignette_field_angle"]),
    ];
    const int CurveGroup = 3, Channels = 4;

    /// PresetGroup.defaultSelection: a look, without white balance or detail.
    public static HashSet<int> DefaultSelection() => [1, 2, 3, 5];

    /// Where the user's presets live. The selftest points it at a scratch file.
    public static string File = AppFolder.File("presets-shell.json")
                                ?? Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData), "Orion", "presets-shell.json");

    /// User presets first, then the built-ins: built at load, never written, so improving one reaches everybody.
    public static List<Preset> All { get; } = [.. Load(), .. BuiltIns()];

    /// Why the last save or delete did not reach the file, or null.
    public static string? LastFailure { get; private set; }

    static int Length(string field) => EngineModel.Fields.First(f => f.Name == field).Length;
    static List<(float X, float Y)>[] Identity() => Enumerable.Range(0, Channels).Select(_ => Develop.CurveMath.Identity.ToList()).ToArray();

    /// PresetStore.builtIns: four looks over DevelopState(), Orion's base rendering
    /// (contrast 1.45, LUT strength 1, grain size 1.5, a 45° field angle).
    static IEnumerable<Preset> BuiltIns() {
        Preset Look(string name, params (string Field, float Value)[] look) {
            var state = Groups.SelectMany(g => g.Fields).ToDictionary(f => f, f => new float[Length(f)]);
            (string, float)[] orion = [("temperature_k", 5500), ("contrast", 1.45f), ("lut_strength", 1), ("grain_size", 1.5f),
                                       ("vignette_field_angle", 45), ("sharpen_radius", 1)];
            foreach (var (f, v) in orion.Concat(look)) state[f][0] = v;
            return new(name, DefaultSelection(), state, Identity(), BuiltIn: true);
        }
        return [
            Look("Punch", ("contrast", 1.62f), ("blacks", -0.18f), ("whites", 0.12f), ("vibrance", 0.30f), ("clarity", 0.25f)),
            Look("Soft", ("contrast", 1.24f), ("blacks", 0.14f), ("highlights", -0.20f), ("vibrance", 0.10f), ("clarity", -0.15f)),
            Look("Monochrome", ("saturation", -1.0f), ("contrast", 1.55f), ("clarity", 0.20f)),
            Look("Lift the shadows", ("fusion", 0.55f), ("shadows", 0.25f), ("blacks", 0.08f)),
        ];
    }

    /// `engine.state`, as much of it as a preset can carry.
    public static Preset? Capture(string name, HashSet<int> groups) {
        if (EngineModel.Shared.Session is not { } s) return null;
        var state = Groups.SelectMany(g => g.Fields).ToDictionary(f => f, f => Enumerable.Range(0, Length(f)).Select(i => s.Get(f, i)).ToArray());
        try {
            return new(name, [.. groups], state, Enumerable.Range(0, Channels).Select(c => s.Curve(c).ToList()).ToArray());
        } catch (NotBuiltException) {
            return new(name, [.. groups], state, Identity());
        }
    }

    /// `engine.apply(preset:)`: the preset's groups over the photo, one write, one render, one undo step.
    public static void Apply(Preset p) {
        var engine = EngineModel.Shared;
        if (engine.Handle == 0) return;
        Develop.EditHistory.Label(p.Name);
        engine.Apply(s => {
            foreach (int g in p.Groups)
                if (g == CurveGroup) for (int c = 0; c < Channels; c++) s.SetCurve(c, p.Curves[c]);
                else foreach (var f in Groups[g].Fields) for (int i = 0; i < p.State[f].Length; i++) s.Set(f, p.State[f][i], i);
        }, reread: true);
    }

    /// The photo already looks like this preset in every group it carries: the row shows as chosen.
    public static bool InEffect(Preset p) {
        if (EngineModel.Shared.Session is not { } s) return false;
        try {
            return p.Groups.All(g => g == CurveGroup
                ? Enumerable.Range(0, Channels).All(c => s.Curve(c).SequenceEqual(p.Curves[c]))
                : Groups[g].Fields.All(f => Enumerable.Range(0, p.State[f].Length).All(i => s.Get(f, i) == p.State[f][i])));
        } catch (NotBuiltException) {
            return false;
        }
    }

    /// PresetStore.add: replaces a user preset of that name; a built-in of that name is shadowed, never touched.
    /// True only when it reached the file: a save that fails would otherwise vanish at the next launch.
    public static bool Save(Preset p) {
        All.RemoveAll(q => !q.BuiltIn && q.Name == p.Name);
        All.Insert(All.FindIndex(q => q.BuiltIn) is var i and >= 0 ? i : All.Count, p);
        return Write();
    }

    public static bool Remove(Preset p) => !p.BuiltIn && All.Remove(p) && Write();

    sealed record Saved(string Name, int[] Groups, Dictionary<string, float[]> State, float[][] Curves);

    static bool Write() {
        try {
            Directory.CreateDirectory(Path.GetDirectoryName(File)!);
            var user = All.Where(p => !p.BuiltIn).Select(p => new Saved(p.Name, [.. p.Groups.Order()], p.State,
                p.Curves.Select(c => c.SelectMany(q => new[] { q.X, q.Y }).ToArray()).ToArray()));
            System.IO.File.WriteAllText(File, JsonSerializer.Serialize(user));
            LastFailure = null;
            return true;
        } catch (Exception e) when (e is IOException or UnauthorizedAccessException) {
            LastFailure = $"That preset could not be saved. {e.Message}";
            return false;
        }
    }

    /// PresetStore.decode: an element that cannot be read costs that preset, and no other.
    static IEnumerable<Preset> Load() {
        JsonElement rows;
        try { rows = JsonDocument.Parse(System.IO.File.ReadAllText(File)).RootElement; }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException or JsonException) { yield break; }
        if (rows.ValueKind != JsonValueKind.Array) yield break;
        foreach (var row in rows.EnumerateArray()) {
            Preset? p = null;
            try {
                var s = row.Deserialize<Saved>()!;
                bool whole = s.Groups.All(g => g >= 0 && g < Groups.Length) && s.Curves.Length == Channels
                             && Groups.SelectMany(g => g.Fields).All(f => s.State.TryGetValue(f, out var v) && v.Length == Length(f));
                if (whole) p = new(s.Name, [.. s.Groups], s.State,
                                   s.Curves.Select(c => Enumerable.Range(0, c.Length / 2).Select(i => (c[2 * i], c[2 * i + 1])).ToList()).ToArray());
            } catch (JsonException) { }
            if (p != null) yield return p;
        }
    }
}

/// A port of `presetsPanel` in the Swift app's DevelopPanels+Presets.swift: versions,
/// then presets, copy, paste, sync and batch export, on a tab of their own.
[Panel("presets", Region.Tools, 10, Tab = "Presets")]
public sealed class PresetsPanel : StackPanel {
    readonly HashSet<int> _groups = PresetStore.DefaultSelection();
    readonly SectionPlate _plate = new("Presets");
    readonly StackPanel _list = new() { Spacing = Space.Xs };
    readonly TextBox _name = SheetKit.Field("New preset");
    readonly Button _save, _copy, _paste, _sync, _export;
    readonly TextBlock _copiedNote = SheetKit.Note("Settings copied. Paste puts them on this photo; Sync writes them to every photo in view.");
    readonly TextBlock _failure = SheetKit.Note("", Palette.Star);
    /// What Copy took: the open photo's look, every group, as Swift's `copiedState`.
    Preset? _copied;
    /// The same look as the sidecar holds it: what Sync patches into the other photos' sidecars.
    System.Text.Json.Nodes.JsonObject? _copiedState;

    static int PhotosInView => EngineModel.Shared.Photos.Count;

    public PresetsPanel() {
        Spacing = SectionPlate.Gap;
        SheetKit.Restyle(this);
        _save = SheetKit.Button("Save", Save);
        _copy = SheetKit.Button("Copy", () => {
            _copied = PresetStore.Capture("Copied", [.. Enumerable.Range(0, PresetStore.Groups.Length)]);
            _copiedState = EngineModel.Shared.Session?.Capture(Masks.MaskGroup.Shared.Shapes);
            Enable();
        });
        // A paste is a preset made of the copied look and the groups ticked below.
        _paste = SheetKit.Button("Paste", () => { if (_copied != null) PresetStore.Apply(_copied with { Name = "Paste", Groups = [.. _groups] }); });
        // Confirmed, counted, and it names the groups: it writes sidecars with no undo across photos.
        _sync = SheetKit.Button("Sync all…", () => { if (_copied != null && _copiedState != null) SyncSettings.Confirm(_copied, _copiedState, _groups); });
        _export = SheetKit.Button("Export all…", () => Commands.Run("export-all"));
        _name.TextChanged += (_, _) => Enable();
        Children.Add(new VersionsSection().Plate);
        Children.Add(_plate);
        Build();
        var engine = EngineModel.Shared;
        engine.Opened += Enable;
        engine.Rendered += ShowChosen;
        ExportRun.Changed += Enable;
    }

    protected override void OnAttachedToVisualTree(VisualTreeAttachmentEventArgs e) {
        base.OnAttachedToVisualTree(e);
        ShowChosen();
        Enable();
    }

    /// Each control's guard from the Swift, re-read whenever its inputs change.
    void Enable() {
        _save.IsEnabled = IsLoadedPhoto && (_name.Text ?? "").Trim().Length > 0 && _groups.Count > 0;
        _copy.IsEnabled = IsLoadedPhoto;
        _paste.IsEnabled = IsLoadedPhoto && _copied != null && _groups.Count > 0;
        _sync.IsEnabled = _copied != null && _groups.Count > 0 && PhotosInView >= 2;
        _copiedNote.IsVisible = _copied != null;
        // The command's own guard: export targets, no batch already running.
        _export.IsEnabled = Commands.CanRun("export-all");
        _failure.Text = PresetStore.LastFailure;
        _failure.IsVisible = _failure.Text != null;
    }

    static bool IsLoadedPhoto => EngineModel.Shared.Handle != 0;

    void Build() {
        var rows = _plate.Rows.Children;
        rows.Clear();
        rows.Add(_list);
        ShowList();

        // A preset that did not reach the file is gone at the next launch; the
        // name stays in the field on a failure so the save can be retried.
        rows.Add(SheetKit.FieldWithButton(_name, _save));
        rows.Add(_failure);

        // Which groups a saved preset, a paste and a sync will carry: one list, one answer.
        var groups = new StackPanel { Spacing = Space.Xs };
        for (int i = 0; i < PresetStore.Groups.Length; i++) groups.Children.Add(GroupToggle(i));
        rows.Add(groups);

        rows.Add(new StackPanel { Orientation = Orientation.Horizontal, Spacing = Space.Sm, Children = { _copy, _paste, _sync } });
        rows.Add(_copiedNote);

        // Beside sync: the same gesture — do this to all of them — writing files, not sidecars.
        _export.HorizontalAlignment = HorizontalAlignment.Left;
        rows.Add(_export);

        rows.Add(SheetKit.Note("A preset changes only the groups it carries and leaves everything else alone. The crop, " +
                               "the dust spots and the masks are never included — those belong to one photograph."));
        Enable();
    }

    void ShowList() {
        _list.Children.Clear();
        if (PresetStore.All.Count == 0) _list.Children.Add(SheetKit.Note("No presets yet."));
        foreach (var p in PresetStore.All) _list.Children.Add(Row(p));
        ShowChosen();
    }

    /// The preset the photo already looks like, in every group it carries, is drawn on.
    /// Derived rather than remembered, so undo, another edit or a photo swap all answer it.
    void ShowChosen() {
        if (TopLevel.GetTopLevel(this) == null) return;   // off screen: no reads per render
        foreach (var b in _list.GetVisualDescendants().OfType<StateButton>())
            if (b.Tag is Preset p) b.On = PresetStore.InEffect(p);
    }

    void Save() {
        string name = (_name.Text ?? "").Trim();
        if (PresetStore.Capture(name, _groups) is not { } p) return;
        if (PresetStore.Save(p)) _name.Text = "";
        ShowList();
        Enable();
    }

    /// One saved look: a button that applies it, and delete when it is the photographer's own.
    Control Row(Preset p) {
        // What it will disturb, before it is pressed.
        var groups = new TextBlock {
            Text = p.Groups.Count == PresetStore.Groups.Length ? "all" : $"{p.Groups.Count} groups", FontSize = TypeSize.Sm, Foreground = Palette.Faint,
            Tag = StateButton.Keep,
        };
        var head = new DockPanel();
        DockPanel.SetDock(groups, Dock.Right);
        head.Children.AddRange([groups, new TextBlock { Text = p.Name, FontSize = TypeSize.Sm, TextTrimming = TextTrimming.CharacterEllipsis }]);
        // A button, so Tab reaches it and Space applies it; a tapped Border did neither.
        var apply = new StateButton {
            Content = head, Tag = p, HorizontalAlignment = HorizontalAlignment.Stretch, Padding = new Thickness(Space.Sm, 0), Height = 26,
        };
        apply.Help($"Apply {p.Name}: {string.Join(", ", p.Groups.Order().Select(g => PresetStore.Groups[g].Title.ToLowerInvariant()))}");
        // StateButton centres its content: the row's width follows the button's, so the count sits at the far end.
        apply.SizeChanged += (_, e) => head.Width = Math.Max(0, e.NewSize.Width - apply.Padding.Left - apply.Padding.Right - 2);
        apply.Click += (_, _) => { PresetStore.Apply(p); ShowChosen(); };
        if (p.BuiltIn) return apply;
        var row = new DockPanel();
        var delete = SheetKit.Glyph("minus.circle", $"Delete {p.Name}", () => { PresetStore.Remove(p); ShowList(); Enable(); });
        delete.Margin = new Thickness(Space.Sm, 0, 0, 0);
        DockPanel.SetDock(delete, Dock.Right);
        row.Children.AddRange([delete, apply]);
        return row;
    }

    /// FlowGroups: a small switch per group, its title and what it covers.
    Control GroupToggle(int i) {
        var (title, covers, _) = PresetStore.Groups[i];
        var box = new CheckBox {
            IsChecked = _groups.Contains(i), VerticalContentAlignment = VerticalAlignment.Center,
            Content = new StackPanel {
                Orientation = Orientation.Horizontal, Spacing = 4,
                Children = {
                    new TextBlock { Text = title, FontSize = Faces.Label },
                    new TextBlock { Text = covers, FontSize = Faces.Mark, Foreground = Palette.Dim, TextTrimming = TextTrimming.CharacterEllipsis },
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
