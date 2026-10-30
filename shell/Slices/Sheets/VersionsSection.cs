using System.Globalization;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Layout;
using Avalonia.Media;
using OrionShell.Masks;

namespace OrionShell.Sheets;

/// A port of `versionsPanel` in the Swift app's DevelopPanels+Presets.swift. First in the
/// tab: the one section about the photograph in front of you. The versions are
/// SnapshotStore.Shared's; a refusal (no name, a hundred already, a file that
/// will not write) goes to the status line.
sealed class VersionsSection {
    public SectionPlate Plate { get; } = new("Versions");
    SnapshotStore.Snapshot? _renaming;
    string _renameText = "";

    static SnapshotStore Store => SnapshotStore.Shared;

    public VersionsSection() {
        Build();
        Store.Changed += Build;
        EngineModel.Shared.Opened += Build;
    }

    static bool Loaded => EngineModel.Shared.Handle != 0;

    /// A store refusal said where the user looks, not thrown up a click handler.
    static void Try(Action act) {
        try { act(); } catch (SnapshotStore.Refusal e) { EngineModel.Shared.Say(e.Message); }
    }

    void Build() {
        var rows = Plate.Rows.Children;
        rows.Clear();
        if (Store.UnreadableText is { } why) rows.Add(SheetKit.Note(why, Palette.Warn));
        else if (Store.Snapshots.Count == 0)
            rows.Add(SheetKit.Note(Loaded ? "No versions of this photo yet." : "Open a photo to save a version of it."));

        if (Store.Snapshots.Count > 0) {
            var list = new StackPanel { Spacing = Space.Xs };
            foreach (var v in Store.Snapshots) list.Children.Add(Row(v));
            rows.Add(list);
        }

        var name = SheetKit.Field("New version");
        var save = SheetKit.Button("Save", () => Try(() => {
            if (EngineModel.Shared.Session is not { } s) return;
            Store.Save(name.Text ?? "", s.Capture(MaskGroup.Shared.Shapes));
        }));
        void Enable() => save.IsEnabled = Loaded && !Store.Unreadable && (name.Text ?? "").Trim().Length > 0;
        name.TextChanged += (_, _) => Enable();
        Enable();
        rows.Add(SheetKit.FieldWithButton(name, save));

        rows.Add(SheetKit.Note("A version is this photo's whole edit under a name — the crop, the dust and the masks " +
                               "included. Restoring one keeps what you had first, as “Before restoring…”, and ⌘Z takes it back in one step."));
    }

    /// One saved version: restore, rename in place, delete.
    Control Row(SnapshotStore.Snapshot v) {
        var row = new DockPanel();
        if (_renaming?.Id == v.Id) {
            var field = SheetKit.Field("Name");
            field.Text = _renameText;
            field.TextChanged += (_, _) => _renameText = field.Text ?? "";
            var done = SheetKit.Button("Done", () => Commit(v));
            field.KeyDown += (_, e) => { if (e.Key == Avalonia.Input.Key.Enter) Commit(v); };
            return SheetKit.FieldWithButton(field, done);
        }
        var delete = SheetKit.Glyph("minus.circle", "Delete this version", () => Try(() => Store.Remove(v.Id)));
        var rename = SheetKit.Glyph("pencil", "Rename", () => { _renaming = v; _renameText = v.Name; Build(); });
        DockPanel.SetDock(delete, Dock.Right);
        DockPanel.SetDock(rename, Dock.Right);
        rename.Margin = new Thickness(Space.Gap, 0, 0, 0);

        var head = new DockPanel();
        var stamp = new TextBlock {
            // Short and unambiguous: a version list is read by date — "the one from before lunch".
            Text = v.Created.ToLocalTime().ToString("M/d/yy, h:mm tt", CultureInfo.InvariantCulture),
            FontSize = Faces.Mark, Foreground = Palette.Dim, Margin = new Thickness(Space.Sm, 0, 0, 0),
        };
        DockPanel.SetDock(stamp, Dock.Right);
        head.Children.AddRange([stamp, new TextBlock {
            Text = v.Name, FontSize = Faces.Label, TextTrimming = TextTrimming.CharacterEllipsis,
        }]);
        var body = new StackPanel { Spacing = Space.Xs, Children = { head } };
        var missing = SnapshotStore.MissingMattes(v, Store.Photo);
        if (missing.Count > 0)
            // Named, in the colour that means "look at this": a version that
            // quietly restores an empty mask is the defect this line refuses.
            body.Children.Add(new TextBlock {
                Text = $"{missing.Count} selection{(missing.Count == 1 ? "" : "s")} missing — {string.Join(", ", missing)}",
                FontSize = Faces.Mark, Foreground = Palette.Warn,
            });
        else if (v.Automatic)
            body.Children.Add(new TextBlock { Text = "Kept automatically. Rename it to keep it.", FontSize = Faces.Mark, Foreground = Palette.Dim });

        var restore = new Border {
            Background = Palette.Raised, CornerRadius = new CornerRadius(Radius.Sm), Padding = new Thickness(Space.Sm, Space.Gap),
            Child = body, IsEnabled = Loaded, Cursor = new Avalonia.Input.Cursor(Avalonia.Input.StandardCursorType.Hand),
        };
        restore.Tapped += (_, _) => Restore(v);
        row.Children.AddRange([delete, rename, restore]);
        return row;
    }

    /// Through SnapshotStore.Restore, which keeps the working edit before it is overwritten.
    public static void Restore(SnapshotStore.Snapshot v) {
        if (EngineModel.Shared.Session is not { } s) return;
        Store.Restore(v, s.Capture(MaskGroup.Shared.Shapes), SnapshotStore.Assign);
    }

    void Commit(SnapshotStore.Snapshot v) {
        _renaming = null;
        Try(() => Store.Rename(v.Id, _renameText));
        Build();
    }
}
