using Avalonia.Platform.Storage;

namespace OrionShell.Library;

/// The file half of OrionApp+Files.swift: opening a photo or a folder, and the
/// confirmed move to the Trash. The confirmation is Trash.cs's.
static class LibraryFiles {
    /// Library.rawExtensions, Swift's list (Program's own is shorter).
    static readonly string[] Raw = ["arw", "srf", "sr2", "dng", "nef", "nrw", "cr2", "cr3", "crw", "raf", "orf", "rw2",
                                    "pef", "srw", "erf", "3fr", "fff", "iiq", "mrw", "raw", "rwl"];

    static EngineModel E => EngineModel.Shared;
    static LibraryModel L => LibraryModel.Shared;

    /// The raws in a folder, in Finder's order (localizedStandardCompare, near enough).
    public static List<string> RawsIn(string folder) =>
        Directory.EnumerateFiles(folder).Where(f => Raw.Contains(Path.GetExtension(f).TrimStart('.').ToLowerInvariant()))
                 .Order(StringComparer.OrdinalIgnoreCase).ToList();

    static IStorageProvider? Picker => App.Window?.StorageProvider is { CanOpen: true } p ? p : null;

    /// `openFile`: one raw, then its folder around it, the photo focused.
    public static async void OpenPhoto() {
        if (Picker is not { } picker) { E.Say("no file dialog in this window"); return; }
        var chosen = await picker.OpenFilePickerAsync(new FilePickerOpenOptions {
            Title = "Open Photo", AllowMultiple = false,
            FileTypeFilter = [new FilePickerFileType("Raw photos") { Patterns = Raw.Select(e => "*." + e).ToArray() }],
        });
        if (chosen.Count > 0 && chosen[0].TryGetLocalPath() is { } path) OpenPhoto(path);
    }

    public static void OpenPhoto(string path) {
        var list = RawsIn(Path.GetDirectoryName(path)!);
        int i = list.FindIndex(p => p == path);
        if (i < 0) { list = [path]; i = 0; }
        E.Load(list);
        E.Open(i);
        if (GalleryMode.On) GalleryMode.SetFocus(L.Open);
    }

    /// `openFolder`: the folder's raws, the first one in view opened.
    public static async void OpenFolder() {
        if (Picker is not { } picker) { E.Say("no file dialog in this window"); return; }
        var chosen = await picker.OpenFolderPickerAsync(new FolderPickerOpenOptions { Title = "Open Folder", AllowMultiple = false });
        if (chosen.Count > 0 && chosen[0].TryGetLocalPath() is { } path) OpenFolder(path);
    }

    public static void OpenFolder(string folder) {
        var list = RawsIn(folder);
        if (list.Count == 0) { E.Say($"no raw photos in {Path.GetFileName(folder)}"); return; }
        E.Load(list);
        if (L.Visible.FirstOrDefault() is { } first) E.Open(first.Index);
        if (GalleryMode.On) GalleryMode.SetFocus(L.Open);
    }

    /// TrashPlan.plan: the raw and what Orion wrote beside it — the sidecar,
    /// the saved versions, the mattes — so nothing is left orphaned.
    public static IEnumerable<string> Siblings(string photo) {
        string folder = Path.GetDirectoryName(photo)!, stem = Path.GetFileNameWithoutExtension(photo);
        var listed = Directory.EnumerateFiles(folder).Select(Path.GetFileName).ToHashSet();
        foreach (var owned in new[] { stem + ".xmp", stem + ".orion-snapshots.json" })
            if (listed.Contains(owned)) yield return Path.Combine(folder, owned);
        foreach (var name in listed.Where(n => n!.StartsWith(stem + ".orion-matte-") && n.EndsWith(".png")).Order())
            yield return Path.Combine(folder, name!);
    }

    /// `runTrash`, after the confirmation: moves each raw and its siblings with
    /// `move` (a reason on failure, null when it went), takes them off the list,
    /// and puts the nearest survivor on the canvas. Returns the complaint, if any.
    public static string? Trash(IReadOnlyList<string> paths, Func<string, string?> move) {
        if (paths.Count == 0) return null;
        // Survivors first, from the order the photographer was looking at:
        // afterwards the trashed photos are not in it to measure from.
        var goes = paths.ToHashSet();
        var visible = L.Visible.Select(p => p.Path).ToList();
        string? Survivor(string? path) {
            if (path == null || !goes.Contains(path)) return path;
            int i = visible.IndexOf(path);
            if (i < 0) return null;
            return visible.Skip(i + 1).FirstOrDefault(p => !goes.Contains(p))
                ?? visible.Take(i).Reverse().FirstOrDefault(p => !goes.Contains(p));
        }
        string? current = E.Current >= 0 ? E.Photos[E.Current] : null;
        string? canvasSurvivor = Survivor(current), focusSurvivor = Survivor(GalleryMode.Focus?.Path);

        var trashed = new List<string>();
        var failures = new List<string>();
        foreach (var path in paths) {
            if (move(path) is { } why) { failures.Add($"{Path.GetFileName(path)} could not be moved - {why}."); continue; }
            trashed.Add(path);
            foreach (var sibling in Siblings(path))
                if (move(sibling) is { } w) failures.Add($"{Path.GetFileName(sibling)} could not be moved - {w}.");
        }

        if (trashed.Count > 0) {
            var gone = trashed.ToHashSet();
            bool currentGone = current != null && gone.Contains(current);
            // ponytail: EngineModel cannot drop a photo from its list without reopening the
            // one on screen, so its edits are carried across by value (undo history is not);
            // a list edit that keeps the session removes this.
            float[]? kept = current != null && !currentGone ? Develop.EditHistory.Snapshot() : null;
            if (currentGone) E.Close();
            E.Load(E.Photos.Where(p => !gone.Contains(p)).ToList());
            string? show = currentGone ? (GalleryMode.On ? null : canvasSurvivor) : current;
            if (show != null) {
                E.Open(E.Photos.ToList().IndexOf(show));
                if (kept != null) Develop.EditHistory.Restore(kept);
            } else if (!GalleryMode.On) {
                GalleryMode.Enter();   // nothing left for the canvas: the empty gallery is the honest face
            }
            if (GalleryMode.On) GalleryMode.SetFocus(L.Photos.FirstOrDefault(p => p.Path == focusSurvivor) ?? L.Visible.FirstOrDefault());
            CanvasSlice.CanvasState.Touch();   // a canvas left with nothing open says so
        }

        if (failures.Count == 0) return null;
        return $"Moved {trashed.Count} of {paths.Count} photo{(paths.Count == 1 ? "" : "s")} to the Trash. " + string.Join(" ", failures);
    }
}
