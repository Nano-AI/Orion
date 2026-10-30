using System.Text.Json;

namespace OrionShell;

/// The shell's @AppStorage: a few numbers that outlive a launch, in one small
/// JSON file beside the presets. A file that is missing or unreadable reads as
/// every default, and a write that fails is dropped: a preference is never
/// worth an error.
static class ShellSettings {
    public static readonly string UserFile =
        Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData), "Orion", "shell-settings.json");

    /// Where the settings live. Null until the app is at a window (Program), so
    /// the headless modes draw every default and never write the user's file.
    public static string? File { get; set; }

    public static double Get(string key, double fallback) {
        if (File == null) return fallback;
        try {
            using var doc = JsonDocument.Parse(System.IO.File.ReadAllText(File));
            return doc.RootElement.TryGetProperty(key, out var v) && v.TryGetDouble(out double d) && double.IsFinite(d) ? d : fallback;
        } catch (Exception e) when (e is IOException or UnauthorizedAccessException or JsonException) {
            return fallback;
        }
    }

    public static void Set(string key, double value) {
        if (File == null) return;
        try {
            Dictionary<string, double> all;
            try { all = JsonSerializer.Deserialize<Dictionary<string, double>>(System.IO.File.ReadAllText(File)) ?? []; }
            catch (Exception e) when (e is IOException or JsonException) { all = []; }
            all[key] = value;
            Directory.CreateDirectory(Path.GetDirectoryName(File)!);
            System.IO.File.WriteAllText(File, JsonSerializer.Serialize(all));
        } catch (Exception e) when (e is IOException or UnauthorizedAccessException) {
            Console.Error.WriteLine($"orion: could not save the setting {key} — {e.Message}");
        }
    }
}
