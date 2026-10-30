using OrionShell.Develop;
using OrionShell.Library;
using OrionShell.Sheets;

namespace OrionShell.Chrome;

/// Every piece of state the toolbar and footer read that Swift keeps above the
/// facade, in one place so the integrator can see what is wired and what is
/// not. A stub answers what an ordinary session with one photo open would.
/// Compare is not here: the toolbar and the canvas both read CompareState, so
/// the button and the split cannot disagree.
static class ChromeStubs {
    public static bool Culling => GalleryMode.On;                // mode == .cull
    public static (int Done, int Total)? BatchProgress => ExportRun.Progress;
    /// exportSelected: the export targets of a real selection, 0 when Export writes the open photo.
    public static int ExportSelected => LibraryModel.Shared.HasExplicitSelection ? ExportRun.Targets().Count : 0;
    public static bool CanUndo => EditHistory.CanUndo;
    public static bool CanRedo => EditHistory.CanRedo;
    /// True while an agent's proposal is on the canvas (ProposalWatcher.isLive).
    public static bool DocumentEditsLocked => ProposalWatcher.Shared.IsLive;
    public static string? FlatFrame => FlatFrameCheck.Why;
    public static string? Complaint => Autosave.Shared.LastFailure;   // a mark that did not save says so on the status line (LibraryModel.Persist)
    public static bool IsFit => CanvasSlice.CanvasState.Viewport.IsFit;
    /// fitScale × zoom, the canvas's numbers: shown while a photo is in develop.
    public static int? Percent => EngineModel.Shared.Handle != 0 && !Culling ? CanvasSlice.CanvasState.Viewport.Percent : null;
    public static bool CropTab => (App.Window as MainWindow)?.Tab == "Crop";

    // What the toolbar's buttons do is a command each (Shared/CommandTable.cs).
}
