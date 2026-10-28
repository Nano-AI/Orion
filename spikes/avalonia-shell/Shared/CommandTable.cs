using Avalonia.Controls;
using OrionShell.CanvasSlice;
using OrionShell.Chrome;
using OrionShell.Library;

namespace OrionShell;

/// Every toolbar button, menu command and key of the Swift app: PhotoCommands
/// and handleKey (OrionApp+Commands.swift) and the toolbar (OrionApp+Chrome.swift).
/// A built row runs the shell's own port; a stub row names the Swift file that
/// still owns the behaviour. CanRun is Swift's `.disabled(…)`, inverted, read
/// from the same stand-ins the toolbar reads (ChromeStubs).
///
/// Not here: the gallery's own key map (arrows move the focus ring, Return
/// opens) arrives with the gallery mode; the assistant terminal's ⌘= ⌘- ⌘0
/// belong to a terminal that is not ported.
static class CommandTable {
    static EngineModel E => EngineModel.Shared;
    static bool Loaded => E.Handle != 0;
    static bool Develop => !ChromeStubs.Culling;
    static bool Locked => ChromeStubs.DocumentEditsLocked;
    static bool Editable => Loaded && Develop && !Locked;
    static MainWindow? Shell => App.Window as MainWindow;
    static bool Cropping => Shell?.Tab == "Crop" && !Locked;
    /// What the culling keys act on: the photo on the canvas (no gallery focus yet).
    static LibraryModel.Photo? Focused => LibraryModel.Shared.Open;

    public static IEnumerable<Command> All => [
        // View
        .. Panels.Tabs.Select((t, i) => Built($"tab-{t.ToLowerInvariant()}", t, $"Cmd+D{i + 1}", "OrionApp+Commands.swift",
                                              () => Editable, () => Shell?.Select(t))),
        Stub("gallery", "Gallery", "G", "OrionApp+Commands.swift", () => E.Photos.Count > 0),
        Stub("compare", "Compare Original", "OemPipe", "Engine+Compare.swift", () => Loaded && Develop),
        Stub("swap-split", "Swap Split", null, "Engine+Compare.swift", () => ChromeStubs.Comparing),
        Built("fit", "Fit in Window", "Cmd+D0, D0", "Viewport.swift", () => Loaded && Develop, () => CanvasState.Viewport.Reset()),
        Built("actual-size", "Actual Size", "Cmd+D9, D9", "Viewport.swift", () => Loaded && Develop,
              () => CanvasState.Viewport.ToggleFitAndActual()),

        // File
        Stub("open-photo", "Open Photo…", null, "OrionApp+Files.swift", () => true),
        Stub("open-folder", "Open Folder…", null, "OrionApp+Files.swift", () => true),
        Built("export", "Export…", "Cmd+E", "OrionApp+Chrome.swift", () => Loaded && !Locked, () => Sheet("sheet-export", "Export")),
        Stub("export-all", "Export All…", "Cmd+Shift+E", "OrionApp+Files.swift", () => Loaded && !Locked && E.Photos.Count > 0),
        Stub("stop-batch", "Stop", null, "OrionApp+Files.swift", () => ChromeStubs.BatchProgress != null),
        Stub("reveal-session-log", "Reveal Session Log in Finder", null, "InteractionLog.swift", () => true),

        // Edit
        Stub("undo", "Undo", "Cmd+Z", "EditHistory.swift", () => ChromeStubs.CanUndo && Develop && !Locked),
        Stub("redo", "Redo", "Cmd+Shift+Z", "EditHistory.swift", () => ChromeStubs.CanRedo && Develop && !Locked),
        Stub("reset-adjustments", "Reset Adjustments", "Cmd+R", "Engine+Document.swift", () => Editable),
        Built("rotate-left", "Rotate Left", "OemOpenBrackets", "Engine+Geometry.swift", () => Editable, () => CropEdit.Rotate(-1)),
        Built("rotate-right", "Rotate Right", "OemCloseBrackets", "Engine+Geometry.swift", () => Editable, () => CropEdit.Rotate(1)),

        // Photo
        Built("reject", "Reject", "R", "Library.swift", () => Focused != null, () => LibraryModel.Shared.ToggleRejected(Focused!)),
        .. Enumerable.Range(0, 6).Select(n => Built($"rate-{n}", n == 0 ? "No Rating" : $"{n} Star{(n == 1 ? "" : "s")}",
                                                    n == 0 ? "OemTilde" : $"D{n}", "Library.swift",
                                                    () => Focused != null, () => LibraryModel.Shared.SetRating(Focused!, n))),
        Built("next-photo", "Next Photo", "Cmd+Right, Right", "Library.swift", () => E.Photos.Count > 0, () => E.Next()),
        Built("previous-photo", "Previous Photo", "Cmd+Left, Left", "Library.swift", () => E.Photos.Count > 0, () => E.Previous()),
        Stub("select-all", "Select All Photos", "Cmd+A", "PhotoSelection.swift", () => Focused != null),
        Stub("deselect", "Deselect", "Cmd+Shift+A", "PhotoSelection.swift", () => false),   // needs a selection of two
        Stub("trash", "Move to Trash…", "Cmd+Back", "OrionApp+Files.swift", () => Focused != null),
        Stub("delete-rejected", "Delete Rejected Photos…", null, "OrionApp+Files.swift", () => LibraryModel.Shared.RejectedCount > 0),
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
        new Window { Content = Panels.Create(sheet.Type), Background = Palette.Panel, SizeToContent = SizeToContent.WidthAndHeight,
                     Title = title }.ShowDialog(owner);
    }
}
