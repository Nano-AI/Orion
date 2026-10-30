using System.Diagnostics;
using System.Text.Json;
using System.Text.Json.Nodes;
using System.Xml.Linq;

namespace OrionShell;

/// What the Swift app kept, carried over once into the files this app keeps,
/// so a photographer who updates keeps their presets and their assistant
/// (decision #380). Both apps share `~/Library/Application Support/Orion`, so
/// `watermark.json` and `current.json` need nothing; what differs is:
///
///   presets.json            Swift's Preset array (DevelopState keys) → presets-shell.json (C field names)
///   UserDefaults com.nanoai.orion:
///     assistantPanelWidth, assistantCommand,
///     assistantFontSize, assistantWorkingDirectory → assistant-shell.json
///     galleryCellSize                              → shell-settings.json
///   ~/.config/Orion/*        where the "Orion Avalonia" preview kept presets-shell.json
///                            and shell-settings.json (.NET's ApplicationData on macOS)
///
/// ⚠ Each target is written only when it does not exist yet, and never
/// rewritten: an import that ran again would bring back a preset the
/// photographer deleted. Swift's files are read and never touched, so the
/// old app still works on the same Mac. Only the app at a window imports.
static class SettingsImport {
    public static void Run() {
        if (!AppFolder.Writable || AppFolder.Root is not { } root) return;
        string home = Environment.GetFolderPath(Environment.SpecialFolder.UserProfile);
        Into(root, Path.Combine(home, ".config", "Orion"), ReadDefaults("com.nanoai.orion"));
    }

    /// The import, given where everything is: the selftest points it at a scratch folder.
    public static void Into(string root, string preview, Dictionary<string, object> defaults) {
        Write(Path.Combine(root, "presets-shell.json"), () => Presets(Path.Combine(root, "presets.json"), Path.Combine(preview, "presets-shell.json")));
        Write(Path.Combine(root, "shell-settings.json"), () => {
            var o = Read(Path.Combine(preview, "shell-settings.json")) as JsonObject ?? new JsonObject();
            if (o["galleryCellSize"] is null && Number(defaults, "galleryCellSize") is { } cell) o["galleryCellSize"] = cell;
            return o.Count > 0 ? o : null;
        });
        Write(Path.Combine(root, "assistant-shell.json"), () => {
            var o = new JsonObject();
            if (Number(defaults, "assistantPanelWidth") is { } width) o["width"] = width;
            if (defaults.GetValueOrDefault("assistantCommand") is string command) o["command"] = command;
            if (Number(defaults, "assistantFontSize") is { } font) o["fontSize"] = font;
            if (defaults.GetValueOrDefault("assistantWorkingDirectory") is string folder) o["workingDirectory"] = folder;
            return o.Count > 0 ? o : null;
        });
    }

    static void Write(string path, Func<JsonNode?> build) {
        if (File.Exists(path)) return;
        try {
            if (build() is { } node) AppFolder.WriteAtomically(path, node.ToJsonString());
        } catch (Exception e) when (e is IOException or UnauthorizedAccessException or JsonException or InvalidOperationException) {
            Console.Error.WriteLine($"orion: settings not carried over to {Path.GetFileName(path)} ({e.Message})");
        }
    }

    static JsonNode? Read(string path) {
        try { return File.Exists(path) ? JsonNode.Parse(File.ReadAllText(path)) : null; }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException or JsonException) { return null; }
    }

    static double? Number(Dictionary<string, object> d, string key) => d.GetValueOrDefault(key) is double v && v > 0 ? v : null;

    /// The preview's presets as they are, then each of Swift's whose name is not
    /// taken, in PresetStore's saved shape. One that will not convert is skipped.
    static JsonArray? Presets(string swift, string preview) {
        var all = Read(preview) as JsonArray ?? [];
        var names = all.Select(p => p?["Name"]?.GetValue<string>()).ToHashSet();
        if (Read(swift) is JsonArray presets)
            foreach (var p in presets.OfType<JsonObject>()) {
                try {
                    if (p["builtIn"]?.GetValue<bool>() == true || p["name"]?.GetValue<string>() is not { } name || names.Contains(name)) continue;
                    if (Convert(name, p) is { } saved) { all.Add(saved); names.Add(name); }
                } catch (Exception e) when (e is InvalidOperationException or FormatException or JsonException) { }
            }
        return all.Count > 0 ? all : null;
    }

    static readonly string[] GroupNames = ["whiteBalance", "light", "color", "curve", "detail", "effects"];

    /// One Swift Preset: groups by name (`colour` too, the spelling before #112),
    /// the state through DevelopJson's decoder so legacy keys convert, every
    /// group's C field by its camelCase key, and the four curves interleaved.
    static JsonObject? Convert(string name, JsonObject p) {
        var groups = (p["groups"] as JsonArray ?? []).Select(g => g?.GetValue<string>() is "colour" ? "color" : g?.GetValue<string>())
                     .Select(g => Array.IndexOf(GroupNames, g)).Where(i => i >= 0).Distinct().Order().ToArray();
        if (p["state"] is not JsonObject raw || DevelopJson.Decode(JsonSerializer.SerializeToUtf8Bytes(raw)) is not { } state) return null;
        var fields = new JsonObject();
        foreach (var field in Sheets.PresetStore.Groups.SelectMany(g => g.Fields)) {
            string key = System.Text.RegularExpressions.Regex.Replace(field, "_([a-z])", m => m.Groups[1].Value.ToUpperInvariant());
            var values = state[key] is JsonArray a ? DevelopJson.Floats(a) : DevelopJson.Float(state[key]) is { } f ? [f] : null;
            if (values == null) return null;
            fields[field] = new JsonArray(values.Select(v => (JsonNode)v).ToArray());
        }
        var curves = new JsonArray();
        foreach (var channel in new[] { "master", "red", "green", "blue" })
            curves.Add(new JsonArray(((JsonArray)state["curve"]![channel]!)
                .SelectMany(q => new[] { (JsonNode)DevelopJson.Float(q!["x"])!.Value, DevelopJson.Float(q["y"])!.Value }).ToArray()));
        return new JsonObject { ["Name"] = name, ["Groups"] = new JsonArray(groups.Select(g => (JsonNode)g).ToArray()), ["State"] = fields, ["Curves"] = curves };
    }

    /// A defaults domain's top-level numbers and strings, through `defaults
    /// export` (XML plist on stdout). Empty when it does not exist or will not read.
    public static Dictionary<string, object> ReadDefaults(string domain) {
        var values = new Dictionary<string, object>();
        if (!OperatingSystem.IsMacOS()) return values;
        try {
            using var p = Process.Start(new ProcessStartInfo("/usr/bin/defaults", ["export", domain, "-"]) {
                RedirectStandardOutput = true, RedirectStandardError = true, UseShellExecute = false })!;
            string xml = p.StandardOutput.ReadToEnd();
            p.WaitForExit(5000);
            return p.ExitCode == 0 ? ParsePlist(xml) : values;
        } catch (Exception e) when (e is System.ComponentModel.Win32Exception or IOException or InvalidOperationException or System.Xml.XmlException) {
            return values;
        }
    }

    public static Dictionary<string, object> ParsePlist(string xml) {
        var values = new Dictionary<string, object>();
        var dict = XDocument.Parse(xml).Root?.Element("dict");
        if (dict == null) return values;
        var items = dict.Elements().ToList();
        for (int i = 0; i + 1 < items.Count; i += 2) {
            if (items[i].Name != "key") continue;
            var v = items[i + 1];
            if (v.Name == "string") values[items[i].Value] = v.Value;
            else if ((v.Name == "real" || v.Name == "integer")
                     && double.TryParse(v.Value, System.Globalization.NumberStyles.Float, System.Globalization.CultureInfo.InvariantCulture, out double d))
                values[items[i].Value] = d;
        }
        return values;
    }
}
