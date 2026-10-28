using System.Globalization;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Layout;
using Avalonia.Media;

namespace OrionShell.Sheets;

/// The saved versions of the open photo, as the Presets tab shows them.
// stub(#315): Snapshots.swift owns this until the logic moves below the facade
static class SnapshotStub {
    public sealed record Version(string Name, DateTimeOffset Created, bool Automatic = false, string[]? Missing = null);

    /// SnapshotStore.Refusal.fileUnreadable's text when the versions file is unreadable.
    public static string? Unreadable => null;

    public static List<Version> Versions { get; } = Posed();

    /// `ORION_SCENE=versions` poses the three rows Screenshot+Scenes.swift's
    /// `snapshots(for:)` hands the Swift scene: one of each kind the panel draws.
    static List<Version> Posed() {
        if (Environment.GetEnvironmentVariable("ORION_SCENE") != "versions") return [];
        var epoch = DateTimeOffset.FromUnixTimeSeconds(1_700_000_000);
        return [
            new("Before restoring warmer", epoch, Automatic: true),
            new("warmer", epoch.AddHours(-1)),
            new("with the sky darkened", epoch.AddDays(-1), Missing: ["Subject"]),
        ];
    }

    /// A click on a control that is not built says so on the status line, not on stderr nobody reads.
    public static void Act(string what) => EngineModel.Shared.Say($"not built yet: {what} (Snapshots.swift)");
}

/// A port of `versionsPanel` in app/DevelopPanels+Presets.swift. First in the
/// tab: the one section about the photograph in front of you.
sealed class VersionsSection {
    public SectionPlate Plate { get; } = new("Versions");
    SnapshotStub.Version? _renaming;
    string _renameText = "";

    public VersionsSection() {
        Build();
        EngineModel.Shared.Opened += Build;
    }

    static bool Loaded => EngineModel.Shared.Handle != 0;

    void Build() {
        var rows = Plate.Rows.Children;
        rows.Clear();
        if (SnapshotStub.Unreadable is { } why) rows.Add(SheetKit.Note(why, Palette.Star));
        else if (SnapshotStub.Versions.Count == 0)
            rows.Add(SheetKit.Note(Loaded ? "No versions of this photo yet." : "Open a photo to save a version of it."));

        if (SnapshotStub.Versions.Count > 0) {
            var list = new StackPanel { Spacing = Space.Xs };
            foreach (var v in SnapshotStub.Versions) list.Children.Add(Row(v));
            rows.Add(list);
        }

        var name = SheetKit.Field("New version");
        var save = SheetKit.Button("Save", () => SnapshotStub.Act("saving a version"));
        void Enable() => save.IsEnabled = Loaded && SnapshotStub.Unreadable == null && (name.Text ?? "").Trim().Length > 0;
        name.TextChanged += (_, _) => Enable();
        Enable();
        rows.Add(SheetKit.FieldWithButton(name, save));

        rows.Add(SheetKit.Note("A version is this photo's whole edit under a name — the crop, the dust and the masks " +
                               "included. Restoring one keeps what you had first, as “Before restoring…”, and ⌘Z takes it back in one step."));
    }

    /// One saved version: restore, rename in place, delete.
    Control Row(SnapshotStub.Version v) {
        var row = new DockPanel();
        if (_renaming == v) {
            var field = SheetKit.Field("Name");
            field.Text = _renameText;
            field.TextChanged += (_, _) => _renameText = field.Text ?? "";
            var done = SheetKit.Button("Done", () => Commit(v));
            field.KeyDown += (_, e) => { if (e.Key == Avalonia.Input.Key.Enter) Commit(v); };
            return SheetKit.FieldWithButton(field, done);
        }
        var delete = SheetKit.Glyph("minus.circle", "Delete this version", () => SnapshotStub.Act("deleting a version"));
        var rename = SheetKit.Glyph("pencil", "Rename", () => { _renaming = v; _renameText = v.Name; Build(); });
        DockPanel.SetDock(delete, Dock.Right);
        DockPanel.SetDock(rename, Dock.Right);
        rename.Margin = new Thickness(Space.Sm - 2, 0, 0, 0);

        var head = new DockPanel();
        var stamp = new TextBlock {
            // Short and unambiguous: a version list is read by date — "the one from before lunch".
            Text = v.Created.ToLocalTime().ToString("M/d/yy, h:mm tt", CultureInfo.InvariantCulture),
            FontSize = TypeSize.Sm, Foreground = Palette.Faint, Margin = new Thickness(Space.Sm, 0, 0, 0),
        };
        DockPanel.SetDock(stamp, Dock.Right);
        head.Children.AddRange([stamp, new TextBlock {
            Text = v.Name, FontSize = Faces.Label, TextTrimming = TextTrimming.CharacterEllipsis,
        }]);
        var body = new StackPanel { Spacing = 1, Children = { head } };
        if (v.Missing is { Length: > 0 } missing)
            // Named, in the colour that means "look at this": a version that
            // quietly restores an empty mask is the defect this line refuses.
            body.Children.Add(new TextBlock {
                Text = $"{missing.Length} selection{(missing.Length == 1 ? "" : "s")} missing — {string.Join(", ", missing)}",
                FontSize = Faces.Mark, Foreground = Palette.Star,
            });
        else if (v.Automatic)
            body.Children.Add(new TextBlock { Text = "Kept automatically. Rename it to keep it.", FontSize = Faces.Mark, Foreground = Palette.Dim });

        var restore = new Border {
            Background = Palette.Raised, CornerRadius = new CornerRadius(Radius.Sm), Padding = new Thickness(Space.Sm, 3),
            Child = body, IsEnabled = Loaded, Cursor = new Avalonia.Input.Cursor(Avalonia.Input.StandardCursorType.Hand),
        };
        // SnapshotStore.restore, Engine.restore:
        // stub(#315): Snapshots.swift owns this until the logic moves below the facade
        restore.Tapped += (_, _) => SnapshotStub.Act("restoring a version");
        row.Children.AddRange([delete, rename, restore]);
        return row;
    }

    void Commit(SnapshotStub.Version v) {
        SnapshotStub.Act($"renaming '{v.Name}'");
        _renaming = null;
        Build();
    }
}
