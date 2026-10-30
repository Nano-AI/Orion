using Avalonia.Controls;
using OrionShell.CanvasSlice;
using OrionShell.Chrome;
using OrionShell.Develop;
using OrionShell.Library;
using OrionShell.Masks;
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
    static bool Masking => Develop && CanvasState.Tab == "Mask" && !Locked;
    static MaskGroup Mask => MaskGroup.Shared;
    /// What the culling keys act on: the gallery's focus while it is up, else the
    /// photo on the canvas (cullActions), so the grid and the keys never disagree.
    static LibraryModel.Photo? Focused => GalleryMode.On ? GalleryMode.Focus : LibraryModel.Shared.Open;

    public static IEnumerable<Command> All => [
        // View
        .. Panels.Tabs.Select((t, i) => Built($"tab-{t.ToLowerInvariant()}", t, $"Cmd+D{i + 1}", "OrionApp+Commands.swift",
                                              () => Editable, () => Shell?.Select(t))),
        Built("gallery", "Gallery", "G", "OrionApp+Commands.swift", () => E.Photos.Count > 0, GalleryMode.Toggle),
        Built("compare", "Compare original", "OemPipe", "Engine+Compare.swift", () => Loaded && Develop, CompareState.Toggle),
        Built("swap-split", "Swap split", null, "OrionApp+Chrome.swift", () => CompareState.Comparing && Develop,
              () => { CompareState.Vertical = !CompareState.Vertical; CanvasState.Touch(); }),
        Built("fit", "Fit in window", "Cmd+D0, D0", "Viewport.swift", () => Loaded && Develop, () => CanvasState.Viewport.Reset()),
        Built("actual-size", "Actual size", "Cmd+D9, D9", "Viewport.swift", () => Loaded && Develop,
              () => CanvasState.Viewport.ToggleFitAndActual()),

        // File
        Built("open-photo", "Open photo…", null, "OrionApp+Files.swift", () => true, LibraryFiles.OpenPhoto),
        Built("open-folder", "Open folder…", null, "OrionApp+Files.swift", () => true, LibraryFiles.OpenFolder),
        Built("export", "Export…", "Cmd+E", "OrionApp+Chrome.swift", () => Loaded && !Locked, () => Sheet("sheet-export", "Export")),
        Built("export-all", "Export all…", "Cmd+Shift+E", "OrionApp+Files.swift",
              () => Loaded && !Locked && ChromeStubs.BatchProgress == null && ExportRun.Targets().Count > 0,
              () => { Autosave.Shared.Flush(); ExportRun.ExportAll(); }),   // the batch reads sidecars: what is owed is written first
        // askHdrMerge, from the strip's menu over a group: the sheet reads the selection.
        Built("hdr-merge", "Merge to HDR…", null, "HdrMergePanel.swift", () => Loaded && LibraryModel.Shared.HasExplicitSelection,
              () => Sheet("sheet-hdr-merge", "Merge to HDR")),
        Built("stop-batch", "Stop", null, "OrionApp+Files.swift", () => ChromeStubs.BatchProgress != null, ExportRun.Stop),
        // The shell writes no interaction log (InteractionLog.swift), so there is no file to reveal.
        Stub("reveal-session-log", "Reveal session log in Finder", null, "InteractionLog.swift", () => true),

        // Edit
        Built("undo", "Undo", "Cmd+Z", "EditHistory.swift", () => ChromeStubs.CanUndo && Develop && !Locked, EditHistory.Undo),
        Built("redo", "Redo", "Cmd+Shift+Z", "EditHistory.swift", () => ChromeStubs.CanRedo && Develop && !Locked, EditHistory.Redo),
        Built("reset-adjustments", "Reset adjustments", "Cmd+R", "Engine+Document.swift", () => Editable, EditHistory.ResetEdits),
        Built("rotate-left", "Rotate left", "OemOpenBrackets", "Engine+Geometry.swift", () => Editable, () => Bracket(-1)),
        Built("rotate-right", "Rotate right", "OemCloseBrackets", "Engine+Geometry.swift", () => Editable, () => Bracket(1)),

        // Photo
        // Over the selection when the focus is part of one (cullScope), so the keys and the strip's menu agree.
        Built("reject", "Reject", "R", "Library.swift", () => Focused != null,
              () => LibraryModel.Shared.SetRejected(LibraryModel.Shared.CullScope(Focused!), !Focused!.Rejected)),
        .. Enumerable.Range(0, 6).Select(n => Built($"rate-{n}", n == 0 ? "No rating" : $"{n} star{(n == 1 ? "" : "s")}",
                                                    n == 0 ? "OemTilde" : $"D{n}", "Library.swift",
                                                    () => Focused != null, () => LibraryModel.Shared.SetRating(LibraryModel.Shared.CullScope(Focused!), n))),
        // In the gallery a step moves the focus ring, never the canvas: two hundred
        // frames must not be two hundred decodes (cullActions.step).
        Built("next-photo", "Next photo", "Cmd+Right, Right", "Library.swift", () => E.Photos.Count > 0,
              () => { if (GalleryMode.On) GalleryMode.Move(GalleryMode.Direction.Right); else E.Next(); }),
        Built("previous-photo", "Previous photo", "Cmd+Left, Left", "Library.swift", () => E.Photos.Count > 0,
              () => { if (GalleryMode.On) GalleryMode.Move(GalleryMode.Direction.Left); else E.Previous(); }),
        Built("select-all", "Select all photos", "Cmd+A", "PhotoSelection.swift", () => Focused != null, LibraryModel.Shared.SelectAll),
        Built("deselect", "Deselect", "Cmd+Shift+A", "PhotoSelection.swift", () => LibraryModel.Shared.HasExplicitSelection,
              LibraryModel.Shared.CollapseSelection),   // needs a selection of two
        // Both confirm before touching a file, and the confirmation moves to the Trash, never deletes.
        Built("trash", "Move to Trash…", "Cmd+Back", "OrionApp+Files.swift", () => Focused != null,
              () => Trash.Confirm(LibraryModel.Shared.CullScope(Focused!))),
        Built("delete-rejected", "Delete rejected photos…", null, "OrionApp+Files.swift", () => LibraryModel.Shared.RejectedCount > 0,
              () => Trash.Confirm(LibraryModel.Shared.Photos.Where(p => p.Rejected).ToList())),
        Built("apply-crop", "Apply crop", "Enter", "OrionApp+Commands.swift", () => Cropping, () => Shell?.Select("Light")),
        Built("cancel-crop", "Cancel crop", "Escape", "OrionApp+Commands.swift", () => E.Tool != CanvasTool.None || Cropping, Escape),
        // The Mask tab's keys (u4); `[` `]` fold into the rotate rows, which own those keys, and Escape's armed-tool step disarms a draw.
        Built("mask-overlay", "Show mask overlay", "O", "DevelopPanels+Mask.swift", () => Masking, () => Mask.Overlay ^= true),
        Built("mask-feather-less", "Less mask feather", "Shift+OemOpenBrackets", "DevelopPanels+Mask.swift", () => Masking && Mask.Current != null, () => Feather(-0.1f)),
        Built("mask-feather-more", "More mask feather", "Shift+OemCloseBrackets", "DevelopPanels+Mask.swift", () => Masking && Mask.Current != null, () => Feather(0.1f)),
        Built("delete-mask-shape", "Delete mask shape", "Delete", "DevelopPanels+Mask.swift", () => Masking && Mask.Current != null, () => Mask.Remove(Mask.Selected)),
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

    /// `[` and `]` size the brush being painted on the Mask tab, as Lightroom's do; elsewhere they turn the photo.
    static void Bracket(int step) {
        if (Masking && Mask.Current is { Kind: 3 } brush) {
            brush.BrushRadius = Math.Clamp(brush.BrushRadius * (step > 0 ? 1.2f : 1 / 1.2f), 0.01f, 0.4f);
            Mask.BrushPreview = true;
            Mask.Push("Mask size");
        } else CropEdit.Rotate(step);
    }

    /// A brush softens by losing hardness; every other shape by its feather.
    static void Feather(float step) {
        if (Mask.Current is not { } m) return;
        if (m.Kind == 3) m.BrushHardness = Math.Clamp(m.BrushHardness - step, 0, 1);
        else m.Feather = Math.Clamp(m.Feather + step, 0, 1);
        Mask.Push("Mask feather");
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
