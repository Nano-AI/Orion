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
    // stub(#315): OrionApp+Files.swift owns this until the logic moves below the facade
    public static int ExportSelected => 0;
    public static bool CanUndo => EditHistory.CanUndo;
    public static bool CanRedo => EditHistory.CanRedo;
    // stub(#315): Engine.swift owns this until the logic moves below the facade
    public static bool DocumentEditsLocked => false;
    // stub(#315): Engine.swift owns this until the logic moves below the facade
    public static string? FlatFrame => null;
    // stub(#315): Autosave.swift owns this until the logic moves below the facade
    public static string? Complaint => null;                     // autosave ?? library lastFailure
    // stub(#315): Viewport.swift owns this until the logic moves below the facade
    public static bool IsFit => true;
    // stub(#315): Viewport.swift owns this until the logic moves below the facade
    public static int? Percent => null;                          // fitScale × zoom: the canvas's numbers
    // stub(#315): OrionApp.swift owns this until the logic moves below the facade
    public static bool CropTab => false;                         // the shell keeps the selected tab private

    // What the toolbar's buttons do is a command each (Shared/CommandTable.cs).
}
