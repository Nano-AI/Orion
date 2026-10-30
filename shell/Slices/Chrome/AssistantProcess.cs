using System.Runtime.InteropServices;
using System.Text.Json;
using System.Text.Json.Nodes;

namespace OrionShell.Chrome;

/// A port of app/AssistantProcess.swift: the assistant column's state (which
/// agent, where it runs, how wide, the font) and the command line that starts
/// it. No Avalonia types: AssistantPanel hosts it.
///
/// The Swift app kept these in UserDefaults; they live in `assistant-shell.json`
/// beside the other files Orion keeps (AppFolder), and SettingsImport carries
/// the old values over once.
sealed partial class AssistantModel {
    public static readonly string[] Commands = ["claude", "codex"];
    public const double MinFontSize = 8, MaxFontSize = 28, DefaultFontSize = 13;
    public const double Narrowest = 360, Widest = 800;

    public static AssistantModel Shared { get; } = new(AppFolder.File("assistant-shell.json"), AppFolder.Writable);

    readonly string? _file;
    readonly bool _writes;

    public bool IsOpen { get; private set; }
    public double Width { get; private set; } = Narrowest;
    public string Command { get; private set; } = "claude";
    public double FontSize { get; private set; } = DefaultFontSize;
    /// Resolved at launch or by the tools-folder picker, never while drawing.
    public string WorkingDirectory { get; private set; }
    /// Why the working directory looks wrong, for one line in the header; null when it is fine.
    public string? Notice { get; private set; }
    /// The bundled server still carries the download's quarantine flag (#310): the Allow button.
    public bool ToolsQuarantined { get; private set; }
    /// Bumped by Restart: the terminal relaunches whenever this changes, even with nothing else changed.
    public int RestartToken { get; private set; }

    /// Anything a view shows changed.
    public event Action? Changed;

    public AssistantModel(string? file, bool writes) {
        (_file, _writes) = (file, writes);
        string? saved = null;
        try {
            if (file != null && File.Exists(file) && JsonNode.Parse(File.ReadAllText(file)) is JsonObject o) {
                if (o["width"]?.GetValue<double>() is > 0 and var w) Width = Math.Clamp(w, Narrowest, Widest);
                if (o["command"]?.GetValue<string>() is { } c && Commands.Contains(c)) Command = c;
                if (o["fontSize"]?.GetValue<double>() is > 0 and var f) FontSize = Math.Clamp(f, MinFontSize, MaxFontSize);
                saved = o["workingDirectory"]?.GetValue<string>();
            }
        } catch (Exception e) when (e is IOException or UnauthorizedAccessException or JsonException or InvalidOperationException) {
            Console.Error.WriteLine($"orion: assistant settings not read ({e.Message})");
        }
        // The saved folder when it still exists; else the checkout this app was built in, found by
        // its `.mcp.json` (what tells the agent which MCP server to load); else home, with a notice.
        WorkingDirectory = saved != null && Directory.Exists(saved) ? saved : RepoWithConfig() ?? Home;
        RefreshQuarantine();
    }

    static string Home => Environment.GetFolderPath(Environment.SpecialFolder.UserProfile);

    static string? RepoWithConfig() {
        for (var dir = new DirectoryInfo(AppContext.BaseDirectory); dir != null; dir = dir.Parent)
            if (HasMcpConfig(dir.FullName)) return dir.FullName;
        return null;
    }

    static bool HasMcpConfig(string directory) => File.Exists(Path.Combine(directory, ".mcp.json"));

    /// The `orion-mcp` a packaged app carries beside its executable (#310); null in a development build.
    public static string? BundledServer {
        get {
            string server = Path.Combine(AppContext.BaseDirectory, "orion-mcp");
            return File.Exists(server) ? server : null;
        }
    }

    /// The server to hand the agent on its command line: the bundled one, unless
    /// the working directory registers its own, where both would load two servers named `orion`.
    public static string? LaunchServer(string directory, string? bundled) => HasMcpConfig(directory) ? null : bundled;
    public string? Server => LaunchServer(WorkingDirectory, BundledServer);

    public static string? ConfigurationNotice(string directory, string? bundled, bool quarantined) {
        if (HasMcpConfig(directory)) return null;
        if (bundled == null) return "Choose a tools folder containing .mcp.json";
        return quarantined ? "macOS is blocking Orion’s tools" : null;
    }

    void RefreshQuarantine() {
        ToolsQuarantined = BundledServer is { } s && Quarantine.IsSet(s);
        Notice = ConfigurationNotice(WorkingDirectory, BundledServer, ToolsQuarantined);
    }

    /// The Allow button: the quarantine off this app, as README-FIRST's `xattr -dr` line does.
    /// Fails, and says so, where the bundle is read-only (a disk image opened in place).
    public void AllowTools() {
        if (!Quarantine.Clear(AppContext.BaseDirectory)) {
            Notice = "Move Orion to Applications, then Allow";
            Changed?.Invoke();
            return;
        }
        RefreshQuarantine();
        Restart();
    }

    public void SetWorkingDirectory(string directory) {
        WorkingDirectory = directory;
        RefreshQuarantine();
        Save();
        Restart();
    }

    public void SetCommand(string command) {
        if (command == Command || !Commands.Contains(command)) return;
        Command = command;
        Save();
        Changed?.Invoke();
    }

    public void SetWidth(double width) {
        Width = Math.Clamp(width, Narrowest, Widest);
        Save();
    }

    public void Toggle() { IsOpen = !IsOpen; Changed?.Invoke(); }
    public void Close() { if (!IsOpen) return; IsOpen = false; Changed?.Invoke(); }
    public void Restart() { RestartToken++; Changed?.Invoke(); }

    public void IncreaseFontSize() => SetFontSize(FontSize + 1);
    public void DecreaseFontSize() => SetFontSize(FontSize - 1);
    public void ResetFontSize() => SetFontSize(DefaultFontSize);
    void SetFontSize(double size) { FontSize = Math.Clamp(size, MinFontSize, MaxFontSize); Save(); Changed?.Invoke(); }

    void Save() {
        if (!_writes || _file == null) return;
        try {
            AppFolder.WriteAtomically(_file, new JsonObject {
                ["width"] = Width, ["command"] = Command, ["fontSize"] = FontSize, ["workingDirectory"] = WorkingDirectory,
            }.ToJsonString());
        } catch (Exception e) when (e is IOException or UnauthorizedAccessException) {
            Console.Error.WriteLine($"orion: assistant settings not saved ({e.Message})");
        }
    }

    // ── The command line (AssistantProcess) ─────────────────────────────────

    public const string OrionContext = "Orion is a RAW photo editor and this server is its agent surface. The photographer is looking at one photo in Orion; when they say 'this image', 'this photo', 'the current one' or give no path, that photo is the one `current_photo` returns, and every tool's `path` defaults to it, so never ask which file. Start any edit with `describe_edits` and `get_stats`; `get_edits` reads the current or proposed settings without writing anything. All values are absolute, never deltas. Local adjustments run three to four times a global's at the top of the scale: a blown sky near luma 0.98 barely moves under a small masked exposure pull, so probe strong and ease off. Numbers before pixels: `get_stats` with `region` reads a patch (luma, clippedHigh, shading, noise) for a fraction of a proxy's cost, and `detect_faces` places a radial mask on a portrait. Judge sharpening, skin and mask edges with `get_proxy`'s `region`, never a whole 2048 px frame. `detect_subject` lifts the subject, a person or the sky into a matte for a kind-4 mask row, and `sync_edits` carries one approved photo's look onto the rest of a shoot as proposals. Read `capture.iso` from `get_stats` or `current_photo` before deciding how hard to denoise. Proposals appear live in Orion's compare view; the photographer approves or rejects there, so after `propose_edit` say what you changed and stop; do not call `approve_edit` unless asked.";

    /// The arguments to `/bin/zsh`. Interactive and login, as Terminal.app: CLI installs commonly
    /// add PATH in .zshrc. Every command and argument is data, never shell source: zsh runs
    /// `exec "$0" "$@"` and the agent is $0. A missing agent is "command not found" in the terminal.
    /// `server`, when given, registers Orion's MCP server for this session only.
    public static string[] BuildArgv(string command, string? server = null) {
        string[] shell = ["-l", "-i", "-c", "exec \"$0\" \"$@\"", command];
        return command switch {
            "claude" => [.. shell, .. server is null ? [] : new[] { "--mcp-config", ClaudeConfig(server) },
                         "--append-system-prompt", OrionContext],
            "codex" => [.. shell, .. server is null ? [] : new[] { "-c", "mcp_servers.orion.command=" + TomlString(server) }],
            _ => shell,
        };
    }

    /// `{"mcpServers":{"orion":{"command":"…"}}}`, sorted keys, slashes unescaped, as Swift writes it.
    public static string ClaudeConfig(string server) =>
        "{\"mcpServers\":{\"orion\":{\"command\":" + JsonSerializer.Serialize(server, Relaxed) + "}}}";

    static readonly JsonSerializerOptions Relaxed = new() { Encoder = System.Text.Encodings.Web.JavaScriptEncoder.UnsafeRelaxedJsonEscaping };

    static string TomlString(string text) => "\"" + text.Replace("\\", "\\\\").Replace("\"", "\\\"") + "\"";

    /// `claude mcp add`, single-quoted for a shell: the Assistant menu's Copy MCP Setup Command.
    public static string ClaudeAddCommand(string server, string app) =>
        "xattr -dr com.apple.quarantine " + ShellQuoted(app) + " && claude mcp add orion -- " + ShellQuoted(server);

    static string ShellQuoted(string text) => "'" + text.Replace("'", "'\\''") + "'";

    /// The download's quarantine flag. Gatekeeper refuses a quarantined helper an agent spawns.
    static partial class Quarantine {
        const string Attribute = "com.apple.quarantine";
        const int NoFollow = 1, NoAttribute = 93;   // XATTR_NOFOLLOW, ENOATTR

        [LibraryImport("libc", StringMarshalling = StringMarshalling.Utf8, SetLastError = true)]
        private static partial nint getxattr(string path, string name, nint value, nint size, uint position, int options);
        [LibraryImport("libc", StringMarshalling = StringMarshalling.Utf8, SetLastError = true)]
        private static partial int removexattr(string path, string name, int options);

        public static bool IsSet(string path) => OperatingSystem.IsMacOS() && getxattr(path, Attribute, 0, 0, 0, NoFollow) >= 0;

        /// From `root` and everything under it, as `xattr -dr`. False if a file that carries it would not clear.
        public static bool Clear(string root) {
            if (!OperatingSystem.IsMacOS()) return true;
            bool cleared = true;
            try {
                foreach (var path in Directory.EnumerateFileSystemEntries(root, "*", SearchOption.AllDirectories).Prepend(root))
                    if (removexattr(path, Attribute, NoFollow) != 0 && Marshal.GetLastPInvokeError() != NoAttribute) cleared = false;
            } catch (Exception e) when (e is UnauthorizedAccessException or IOException) {
                return false;   // a folder it may not list: not cleared, said as such, not thrown at the button
            }
            return cleared;
        }
    }
}
