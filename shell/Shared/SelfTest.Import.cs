using System.Text.Json.Nodes;

namespace OrionShell;

/// SettingsImport: a Swift user's presets, assistant and gallery size reach
/// this app's files once, beside what the "Orion Avalonia" preview kept, and
/// an import never runs twice. On a scratch folder, never the photographer's.
static partial class SelfTest {
    static partial void ImportChecks() {
        string root = Directory.CreateTempSubdirectory("orion-import-").FullName, preview = Path.Combine(root, "preview");
        try {
            Directory.CreateDirectory(preview);
            File.WriteAllText(Path.Combine(root, "presets.json"), """
                [{"id":"A","name":"Warm","groups":["light","colour"],"builtIn":false,
                  "state":{"exposureEv":0.5,"temperatureK":6000,"hueShift":[0.1,0,0,0,0,0,0,0.2],
                           "curve":{"master":[{"x":0,"y":0},{"x":0.5,"y":0.6},{"x":1,"y":1}],"red":[{"x":0,"y":0},{"x":1,"y":1}],
                                    "green":[{"x":0,"y":0},{"x":1,"y":1}],"blue":[{"x":0,"y":0},{"x":1,"y":1}]}}},
                 {"id":"B","name":"Punch","groups":["light"],"builtIn":true,"state":{}},
                 {"id":"C","name":"Old","groups":["light"],"state":{}}]
                """);
            File.WriteAllText(Path.Combine(preview, "presets-shell.json"), """[{"Name":"Old","Groups":[1],"State":{},"Curves":[]}]""");
            var defaults = SettingsImport.ParsePlist("""
                <?xml version="1.0" encoding="UTF-8"?><plist version="1.0"><dict>
                <key>NSWindow Frame</key><string>0 0 10 10</string><key>nested</key><dict><key>x</key><integer>1</integer></dict>
                <key>assistantPanelWidth</key><real>420</real><key>assistantCommand</key><string>codex</string>
                <key>assistantFontSize</key><integer>15</integer><key>assistantWorkingDirectory</key><string>/tmp/tools</string>
                <key>galleryCellSize</key><real>180</real></dict></plist>
                """);
            SettingsImport.Into(root, preview, defaults);

            var presets = JsonNode.Parse(File.ReadAllText(Path.Combine(root, "presets-shell.json")))!.AsArray();
            var warm = presets.FirstOrDefault(p => p?["Name"]?.GetValue<string>() == "Warm");
            Check(presets.Select(p => p!["Name"]!.GetValue<string>()).SequenceEqual(["Old", "Warm"]),
                  "Swift's presets join the preview's, a built-in and a taken name skipped");
            var fields = Sheets.PresetStore.Groups.SelectMany(g => g.Fields).ToList();
            Check(warm != null && warm["Groups"]!.AsArray().Select(g => g!.GetValue<int>()).SequenceEqual([1, 2])
                  && warm["State"]!["exposure_ev"]![0]!.GetValue<float>() == 0.5f && warm["State"]!["temperature_k"]![0]!.GetValue<float>() == 6000
                  && warm["State"]!["hue_shift"]![7]!.GetValue<float>() == 0.2f
                  && fields.All(f => warm["State"]![f]?.AsArray().Count == EngineModel.Fields.First(x => x.Name == f).Length)
                  && warm["Curves"]!.AsArray().Count == 4 && warm["Curves"]![0]!.AsArray().Count == 6,
                  "a Swift preset converts to C fields: groups by name (colour too), every group field, the curves interleaved");
            var assistant = JsonNode.Parse(File.ReadAllText(Path.Combine(root, "assistant-shell.json")))!;
            Check(assistant["width"]!.GetValue<double>() == 420 && assistant["command"]!.GetValue<string>() == "codex"
                  && assistant["fontSize"]!.GetValue<double>() == 15 && assistant["workingDirectory"]!.GetValue<string>() == "/tmp/tools",
                  "the assistant's four UserDefaults reach assistant-shell.json");
            Check(JsonNode.Parse(File.ReadAllText(Path.Combine(root, "shell-settings.json")))!["galleryCellSize"]!.GetValue<double>() == 180,
                  "galleryCellSize reaches shell-settings.json");

            File.WriteAllText(Path.Combine(root, "presets-shell.json"), "[]");
            SettingsImport.Into(root, preview, defaults);
            Check(File.ReadAllText(Path.Combine(root, "presets-shell.json")) == "[]" && File.Exists(Path.Combine(root, "presets.json")),
                  "an import never runs twice, so a deleted preset stays deleted, and Swift's file is left as it was");
        } finally {
            Directory.Delete(root, recursive: true);
        }
    }
}
