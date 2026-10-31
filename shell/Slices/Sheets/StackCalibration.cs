using Avalonia.Controls;
using Avalonia.Layout;
using Avalonia.Platform.Storage;
using Rows = Avalonia.Controls.StackPanel;

namespace OrionShell.Sheets;

/// The Stack sheet's calibration row (#391): Darks… and Flats… through the platform's file picker, a dim
/// count, Clear. The lists go to Stack.Run as they stand; the engine refuses frames of another size, camera
/// or kind with a sentence, so nothing here checks them.
sealed class StackCalibration : Rows {
    readonly List<string> _darks = [], _flats = [];
    readonly TextBlock _count = new() { FontSize = Faces.Mark, Foreground = Palette.Dim, VerticalAlignment = VerticalAlignment.Center };
    readonly Button _clear;

    public IReadOnlyList<string> Darks => _darks;
    public IReadOnlyList<string> Flats => _flats;

    /// A list was picked or cleared.
    public event Action? Changed;

    public StackCalibration() {
        Orientation = Orientation.Horizontal;
        Spacing = Space.Md;
        _clear = SheetKit.Button("Clear", () => { _darks.Clear(); _flats.Clear(); Show(); });
        Children.AddRange([SheetKit.Button("Darks…", () => Pick("Dark frames", _darks)),
                           SheetKit.Button("Flats…", () => Pick("Flat frames", _flats)), _count, _clear]);
        Show();
    }

    /// A pick replaces that list; cancelling the picker keeps it. async void, so nothing may escape it.
    async void Pick(string title, List<string> into) {
        try {
            if (TopLevel.GetTopLevel(this)?.StorageProvider is not { CanOpen: true } storage) {
                EngineModel.Shared.Say("no file dialog in this window");
                return;
            }
            var chosen = await storage.OpenFilePickerAsync(new FilePickerOpenOptions {
                Title = title, AllowMultiple = true,
                FileTypeFilter = [new FilePickerFileType("Raw photos") { Patterns = Program.RawExtensions.Select(e => "*." + e).ToArray() }],
            });
            var paths = chosen.Select(f => f.TryGetLocalPath()).OfType<string>().ToList();
            if (paths.Count == 0) return;
            into.Clear();
            into.AddRange(paths);
            Show();
        } catch (Exception e) {
            Console.Error.WriteLine($"orion stack: {title} picker failed: {e}");
            EngineModel.Shared.Say($"Could not choose {title.ToLowerInvariant()}: {e.Message}");
        }
    }

    void Show() {
        _count.Text = Count(_darks.Count, _flats.Count);
        _clear.IsVisible = _darks.Count + _flats.Count > 0;
        Changed?.Invoke();
    }

    /// "3 darks · 5 flats", "1 dark", or "none" when there is nothing to calibrate with.
    internal static string Count(int darks, int flats) {
        static string N(int n, string what) => n == 1 ? $"1 {what}" : $"{n} {what}s";
        var parts = new List<string>();
        if (darks > 0) parts.Add(N(darks, "dark"));
        if (flats > 0) parts.Add(N(flats, "flat"));
        return parts.Count > 0 ? string.Join(" · ", parts) : "none";
    }
}
