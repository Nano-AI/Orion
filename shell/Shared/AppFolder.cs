namespace OrionShell;

/// Orion's folder in Application Support, the one the Swift app and the MCP
/// server share: `~/Library/Application Support/Orion` on macOS (where .NET's
/// ApplicationData is ~/.config), the platform's own elsewhere: %APPDATA% on
/// Windows, $XDG_CONFIG_HOME or ~/.config on Linux.
static class AppFolder {
    /// Null when there is no home to put it in.
    public static string? Root {
        get {
            // DoNotVerify: otherwise .NET answers "" for a folder not made yet - a new
            // account's ~/.config, an XDG_CONFIG_HOME still to be created - and no
            // setting could be saved. Each writer makes the folder (#394).
            const Environment.SpecialFolderOption may = Environment.SpecialFolderOption.DoNotVerify;
            string home = Environment.GetFolderPath(Environment.SpecialFolder.UserProfile, may);
            if (OperatingSystem.IsMacOS()) return home.Length == 0 ? null : Path.Combine(home, "Library", "Application Support", "Orion");
            string data = Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData, may);
            return data.Length == 0 ? null : Path.Combine(data, "Orion");
        }
    }

    public static string? File(string name) => Root is { } root ? Path.Combine(root, name) : null;

    /// Only the app at a window writes the photographer's files, as only it arms
    /// Autosave: the headless flags and --proof read them and write none.
    public static bool Writable => App.Run is { SelfTest: false, Command: null, Screenshot: null, Bench: false, Proof: null, Tool: false };

    /// A temporary file beside it, then a rename over it: a reader sees the old file or the new one, never half.
    public static void WriteAtomically(string path, string text) {
        Directory.CreateDirectory(Path.GetDirectoryName(path)!);
        string temp = path + ".orion-tmp";
        System.IO.File.WriteAllText(temp, text);
        System.IO.File.Move(temp, path, overwrite: true);
    }
}
