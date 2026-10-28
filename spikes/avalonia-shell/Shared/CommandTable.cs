using Avalonia.Controls;
using OrionShell.CanvasSlice;
using OrionShell.Chrome;
using OrionShell.Develop;
using OrionShell.Library;
using OrionShell.Sheets;

namespace OrionShell;

/// Every toolbar button, menu command and key of the Swift app: PhotoCommands
/// and handleKey (OrionApp+Commands.swift) and the toolbar (OrionApp+Chrome.swift).
/// A built row runs the shell's own port; a stub row names the Swift file that
/// still owns the behaviour. CanRun is Swift's `.disabled(…)`, inverted, read
/// from the same stand-ins the toolbar reads (ChromeStubs).
///
/// Not here: the gallery's own key map (arrows in two dimensions, Return opens,
/// Escape and G leave) is GalleryMode.Handle, which the window asks first while the
/// gallery is up, as handleKey hands the gallery to handleGalleryKey; the
/// assistant terminal's ⌘= ⌘- ⌘0 belong to a terminal that is not ported.
static class CommandTable {
    static EngineModel E => EngineModel.Shared;
    static bool Loaded => E.Handle != 0;
    static bool Develop => !ChromeStubs.Culling;
    static bool Locked => ChromeStubs.DocumentEditsLocked;
    static bool Editable => Loaded && Develop && !Locked;
    static MainWindow? Shell => App.Window as MainWindow;
    static bool Cropping => Develop && Shell?.Tab == "Crop" && !Locked;
    /// What the culling keys act on: the gallery's focus while it is up, else the
    /// photo on the canvas (cullActions), so the grid and the keys never disagree.
    static LibraryModel.Photo? Focused => GalleryMode.On ? GalleryMode.Focus : LibraryModel.Shared.Open;

    public static IEnumerable<Command> All => [
        // View
        .. Panels.Tabs.Select((t, i) => Built($"tab-{t.ToLowerInvariant()}", t, $"Cmd+D{i + 1}", "OrionApp+Commands.swift",
                                              () => Editable, () => Shell?.Select(t))),
        Built("gallery", "Gallery", "G", "OrionApp+Commands.swift", () => E.Photos.Count > 0, GalleryMode.Toggle),
        Built("compare", "Compare Original", "OemPipe", "Engine+Compare.swift", () => Loaded && Develop, CompareState.Toggle),
        Built("swap-split", "Swap Split", null, "OrionApp+Chrome.swift", () => CompareState.Comparing && Develop,
              () => { CompareState.Vertical = !CompareState.Vertical; CanvasState.Touch(); }),
        Built("fit", "Fit in Window", "Cmd+D0, D0", "Viewport.swift", () => Loaded && Develop, () => CanvasState.Viewport.Reset()),
        Built("actual-size", "Actual Size", "Cmd+D9, D9", "Viewport.swift", () => Loaded && Develop,
              () => CanvasState.Viewport.ToggleFitAndActual()),

        // File
        Built("open-photo", "Open Photo…", null, "OrionApp+Files.swift", () => true, LibraryFiles.OpenPhoto),
        Built("open-folder", "Open Folder…", null, "OrionApp+Files.swift", () => true, LibraryFiles.OpenFolder),
        Built("export", "Export…", "Cmd+E", "OrionApp+Chrome.swift", () => Loaded && !Locked, () => Sheet("sheet-export", "Export")),
        Built("export-all", "Export All…", "Cmd+Shift+E", "OrionApp+Files.swift",
              () => Loaded && !Locked && ChromeStubs.BatchProgress == null && ExportRun.Targets().Count > 0, ExportRun.ExportAll),
        Built("stop-batch", "Stop", null, "OrionApp+Files.swift", () => ChromeStubs.BatchProgress != null, ExportRun.Stop),
        // The shell writes no interaction log (InteractionLog.swift), so there is no file to reveal.
        Stub("reveal-session-log", "Reveal Session Log in Finder", null, "InteractionLog.swift", () => true),

        // Edit
        Built("undo", "Undo", "Cmd+Z", "EditHistory.swift", () => ChromeStubs.CanUndo && Develop && !Locked, EditHistory.Undo),
        Built("redo", "Redo", "Cmd+Shift+Z", "EditHistory.swift", () => ChromeStubs.CanRedo && Develop && !Locked, EditHistory.Redo),
        Built("reset-adjustments", "Reset Adjustments", "Cmd+R", "Engine+Document.swift", () => Editable, EditHistory.ResetEdits),
        Built("rotate-left", "Rotate Left", "OemOpenBrackets", "Engine+Geometry.swift", () => Editable, () => CropEdit.Rotate(-1)),
        Built("rotate-right", "Rotate Right", "OemCloseBrackets", "Engine+Geometry.swift", () => Editable, () => CropEdit.Rotate(1)),

        // Photo
        Built("reject", "Reject", "R", "Library.swift", () => Focused != null, () => LibraryModel.Shared.ToggleRejected(Focused!)),
        .. Enumerable.Range(0, 6).Select(n => Built($"rate-{n}", n == 0 ? "No Rating" : $"{n} Star{(n == 1 ? "" : "s")}",
                                                    n == 0 ? "OemTilde" : $"D{n}", "Library.swift",
                                                    () => Focused != null, () => LibraryModel.Shared.SetRating(Focused!, n))),
        // In the gallery a step moves the focus ring, never the canvas: two hundred
        // frames must not be two hundred decodes (cullActions.step).
        Built("next-photo", "Next Photo", "Cmd+Right, Right", "Library.swift", () => E.Photos.Count > 0,
              () => { if (GalleryMode.On) GalleryMode.Move(GalleryMode.Direction.Right); else E.Next(); }),
        Built("previous-photo", "Previous Photo", "Cmd+Left, Left", "Library.swift", () => E.Photos.Count > 0,
              () => { if (GalleryMode.On) GalleryMode.Move(GalleryMode.Direction.Left); else E.Previous(); }),
        // The multi-photo selection (command/shift-click groups) is not ported; every cull acts on one photo.
        Stub("select-all", "Select All Photos", "Cmd+A", "PhotoSelection.swift", () => Focused != null),
        Stub("deselect", "Deselect", "Cmd+Shift+A", "PhotoSelection.swift", () => false),   // needs a selection of two
        // Both confirm before touching a file, and the confirmation moves to the Trash, never deletes.
        Built("trash", "Move to Trash…", "Cmd+Back", "OrionApp+Files.swift", () => Focused != null, () => Trash.Confirm([Focused!])),
        Built("delete-rejected", "Delete Rejected Photos…", null, "OrionApp+Files.swift", () => LibraryModel.Shared.RejectedCount > 0,
              () => Trash.Confirm(LibraryModel.Shared.Photos.Where(p => p.Rejected).ToList())),
        Built("apply-crop", "Apply Crop", "Enter", "OrionApp+Commands.swift", () => Cropping, () => Shell?.Select("Light")),
        Built("cancel-crop", "Cancel Crop", "Escape", "OrionApp+Commands.swift", () => E.Tool != CanvasTool.None || Cropping, Escape),
    ];

    static Command Built(string name, string title, string? keys, string owner, Func<bool> canRun, Action run) =>
        new(name, title, keys, owner, canRun, run);

    // stub(#315): <the row's Owner> owns this until the logic moves below the facade
    static Command Stub(string name, string title, string? keys, string owner, Func<bool> canRun) =>
        new(name, title, keys, owner, canRun, null);

    /// handleKey's Escape: an armed tool first, from any tab; then the crop,
    /// reset to the whole frame, and back to Light.
    static void Escape() {
        if (E.Tool != CanvasTool.None) { E.Tool = CanvasTool.None; return; }
        CropEdit.Reset();
        Shell?.Select("Light");
    }

    /// `showingExport = true`: a registered sheet panel as a dialog over the window.
    static void Sheet(string panel, string title) {
        if (Panels.Find(panel) is not { } sheet || App.Window is not { } owner) return;
        var content = Panels.Create(sheet.Type);
        var window = new Window { Content = content, Background = Palette.Panel, SizeToContent = SizeToContent.WidthAndHeight, Title = title };
        // A sheet's Cancel and Done call its Dismiss, which only the opener can fill.
        content.GetType().GetProperty("Dismiss")?.SetValue(content, (Action)window.Close);
        window.ShowDialog(owner);
    }
}
