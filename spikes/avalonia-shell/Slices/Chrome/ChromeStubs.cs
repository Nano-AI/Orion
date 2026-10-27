namespace OrionShell.Chrome;

/// Every piece of state the toolbar and footer read that Swift keeps above the
/// facade, in one place so the integrator can see what is not wired yet. Each
/// answers what an ordinary session with one photo open would.
static class ChromeStubs {
    // stub(#315): OrionApp.swift owns this until the logic moves below the facade
    public static bool Culling => false;                         // mode == .cull
    // stub(#315): OrionApp.swift owns this until the logic moves below the facade
    public static (int Done, int Total)? BatchProgress => null;
    // stub(#315): OrionApp+Files.swift owns this until the logic moves below the facade
    public static int ExportSelected => 0;
    // stub(#315): Engine+Compare.swift owns this until the logic moves below the facade
    public static bool Comparing => false;
    // stub(#315): EditHistory.swift owns this until the logic moves below the facade
    public static bool CanUndo => false;
    // stub(#315): EditHistory.swift owns this until the logic moves below the facade
    public static bool CanRedo => false;
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

    // What the toolbar's buttons do. Each is one call into a Swift object.
    // stub(#315): OrionApp.swift owns this until the logic moves below the facade
    public static void ToggleGallery() { }                       // enterGallery / leaveGallery
    // stub(#315): OrionApp.swift owns this until the logic moves below the facade
    public static void StopBatch() { }                           // batchCancelled = true
    // stub(#315): OrionApp+Files.swift owns this until the logic moves below the facade
    public static void OpenPhoto() { }                           // NSOpenPanel, then Library
    // stub(#315): OrionApp+Files.swift owns this until the logic moves below the facade
    public static void OpenFolder() { }
    // stub(#315): Engine+Compare.swift owns this until the logic moves below the facade
    public static void ToggleCompare() { }                       // setCompare(split: 0.5) / clearCompare
    // stub(#315): Engine+Compare.swift owns this until the logic moves below the facade
    public static void SwapSplit() { }                           // compareVertical.toggle()
    // stub(#315): EditHistory.swift owns this until the logic moves below the facade
    public static void Undo() { }
    // stub(#315): EditHistory.swift owns this until the logic moves below the facade
    public static void Redo() { }
    // stub(#315): Engine.swift owns this until the logic moves below the facade
    public static void Rotate(int quarters) { }                  // then viewport.reset()
    // stub(#315): Engine.swift owns this until the logic moves below the facade
    public static void ResetEdits() { }
}
