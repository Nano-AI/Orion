using System.Text;
using System.Text.Json.Nodes;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.LogicalTree;
using Avalonia.Threading;
using Iciclecreek.Terminal;
using OrionShell.CanvasSlice;
using OrionShell.Chrome;
using OrionShell.Develop;
using OrionShell.Library;
using OrionShell.Masks;
using OrionShell.Sheets;

namespace OrionShell;

/// Issue 23, items 6-8: the stubs that went with AssistantProcess, Proposal,
/// ProposalWatcher, Watermark, Engine+Spots, TargetedAdjust, Viewport and the
/// app glue, one check each. Everything that writes does it in a temporary
/// folder, beside a link to the photo, never beside the photo.
static partial class SelfTest {
    static void ParityChecks() {
        AssistantChecks();
        WatermarkFileChecks();
        var engine = EngineModel.Shared;
        if (engine.Handle == 0) return;
        string original = engine.Photos[engine.Current];
        var list = engine.Photos.ToList();
        int at = engine.Current;
        string dir = Directory.CreateTempSubdirectory("orion-parity-").FullName;
        try {
            WatermarkRasterChecks(engine);
            ChromeChecks(engine);
            HdrChecks(dir, original);
            // Two links to the photo, so a switch away and back is possible with one RAW.
            string photo = Path.Combine(dir, "photo" + Path.GetExtension(original)), other = Path.Combine(dir, "other" + Path.GetExtension(original));
            File.CreateSymbolicLink(photo, original);
            File.CreateSymbolicLink(other, original);
            engine.Load([photo, other]);
            engine.Open(0);
            Arrived(engine);
            SpotChecks(engine, photo);
            ProposalChecks(engine, photo);
        } finally {
            Autosave.Shared.Stop();
            engine.Load(list);
            engine.Open(at);
            Arrived(engine);
            Directory.Delete(dir, recursive: true);
        }
    }

    [System.Runtime.InteropServices.DllImport("libc")] static extern int kill(int pid, int signal);

    static void Pump(Func<bool> done, double seconds = 10) {
        for (var sw = System.Diagnostics.Stopwatch.StartNew(); !done() && sw.Elapsed.TotalSeconds < seconds; Thread.Sleep(2))
            Dispatcher.UIThread.RunJobs();
    }

    static void AssistantChecks() {
        // The command line: zsh runs the agent as data, Orion's context and server on claude's.
        const string server = "/Apps/Orion Avalonia.app/Contents/MacOS/orion-mcp";
        string[] shell = ["-l", "-i", "-c", "exec \"$0\" \"$@\""];
        var claude = AssistantModel.BuildArgv("claude", server);
        Check(claude.Take(4).SequenceEqual(shell) && claude[4] == "claude" && claude[5] == "--mcp-config"
              && claude[6] == "{\"mcpServers\":{\"orion\":{\"command\":\"" + server + "\"}}}"
              && claude[7] == "--append-system-prompt" && claude[8] == AssistantModel.OrionContext,
              "claude starts under zsh with the server and Orion's context as arguments");
        Check(AssistantModel.BuildArgv("codex", server).SequenceEqual([.. shell, "codex", "-c", $"mcp_servers.orion.command=\"{server}\""])
              && AssistantModel.BuildArgv("codex").SequenceEqual([.. shell, "codex"]) && AssistantModel.BuildArgv("claude").Length == 7,
              "codex takes the server as a TOML override, and neither takes one when .mcp.json registers it");
        Check(AssistantModel.ClaudeAddCommand("/a/it's", "/b") == "xattr -dr com.apple.quarantine '/b' && claude mcp add orion -- '/a/it'\\''s'",
              "the setup command quotes its paths for a shell");

        string tools = Directory.CreateTempSubdirectory("orion-tools-").FullName;
        try {
            Check(AssistantModel.ConfigurationNotice(tools, null, false) == "Choose a tools folder containing .mcp.json"
                  && AssistantModel.ConfigurationNotice(tools, "/x", true) == "macOS is blocking Orion’s tools"
                  && AssistantModel.ConfigurationNotice(tools, "/x", false) == null && AssistantModel.LaunchServer(tools, "/x") == "/x",
                  "with no .mcp.json the notice asks for one, or names the quarantine; the bundled server is passed");
            File.WriteAllText(Path.Combine(tools, ".mcp.json"), "{}");
            Check(AssistantModel.ConfigurationNotice(tools, null, false) == null && AssistantModel.LaunchServer(tools, "/x") == null,
                  "a folder's .mcp.json registers the server itself: no notice, and no second server on the command line");

            string file = Path.Combine(tools, "assistant.json");
            var model = new AssistantModel(file, writes: true);
            model.SetWidth(2000);
            model.SetCommand("codex");
            model.SetWorkingDirectory(tools);
            for (int i = 0; i < 40; i++) model.IncreaseFontSize();
            var again = new AssistantModel(file, writes: true);
            Check(again is { Width: AssistantModel.Widest, Command: "codex", FontSize: AssistantModel.MaxFontSize, Notice: null }
                  && again.WorkingDirectory == tools, "the column's width, agent, folder and font survive a relaunch, clamped");
        } finally {
            Directory.Delete(tools, recursive: true);
        }

        // A key typed into the terminal is the terminal's; a ⌘ key is still Orion's.
        var terminal = new TerminalControl();
        Check(AssistantPanel.Owns(new KeyEventArgs { Source = terminal, Key = Key.R })
              && !AssistantPanel.Owns(new KeyEventArgs { Source = terminal, Key = Key.Z, KeyModifiers = KeyModifiers.Meta })
              && !AssistantPanel.Owns(new KeyEventArgs { Source = new Border(), Key = Key.R }),
              "bare keys from the terminal go to the terminal, not to rate or reject");
    }

    /// watermark.json as Swift's synthesized Codable writes it, both ways.
    static void WatermarkFileChecks() {
        string dir = Directory.CreateTempSubdirectory("orion-watermark-").FullName;
        try {
            string file = Path.Combine(dir, "watermark.json");
            string[] swift = [
                """{"enabled":true,"content":{"text":{"_0":"Ada L.","fontFamily":"Georgia","bold":true}},"placement":{"anchor":{"_0":"topLeft"}},"size":0.05,"span":0.6,"margin":0.02,"opacity":0.5}""",
                """{"enabled":false,"content":{"svg":{"_0":"PHN2Zy8+","name":"mark.svg"}},"placement":{"diagonal":{}},"size":0.04,"span":0.8,"margin":0.03,"opacity":0.35}""",
            ];
            File.WriteAllText(file, swift[0]);
            var text = new Watermark(file);
            Check(text is { Enabled: true, IsText: true, Words: "Ada L.", Family: "Georgia", Bold: true, Anchor: 0, Size: 0.05, Opacity: 0.5 }
                  && JsonNode.DeepEquals(text.Encode(), JsonNode.Parse(swift[0])), "a Swift text watermark reads, and encodes back the same");
            File.WriteAllText(file, swift[1]);
            var svg = new Watermark(file);
            Check(svg is { IsText: false, SvgName: "mark.svg", Diagonal: true, Span: 0.8 } && Encoding.UTF8.GetString(svg.Svg) == "<svg/>"
                  && JsonNode.DeepEquals(svg.Encode(), JsonNode.Parse(swift[1])) && svg.Summary == "mark.svg, diagonal",
                  "a Swift SVG watermark reads its bytes, and encodes back the same");
            svg.Opacity = 0.9;
            Check(svg.Save() && svg.LastFailure == null && new Watermark(file).Opacity == 0.9, "a saved watermark reopens as saved");
            File.WriteAllText(file, """{"enabled":true}""");
            Check(new Watermark(file) is { Enabled: false, Words: "" }, "a partial watermark.json changes nothing, as Swift's decoder refuses it");
            var nowhere = new Watermark(Path.Combine(dir, "gone", "deeper", "watermark.json"));
            File.WriteAllText(Path.Combine(dir, "gone"), "a file where the folder should be");
            Check(!nowhere.Save() && nowhere.LastFailure?.StartsWith("The watermark could not be saved.") == true, "a save that cannot land says why");
            var headless = new Watermark(Path.Combine(dir, "never.json"), writes: false);
            Check(headless.Save() && !File.Exists(Path.Combine(dir, "never.json")), "headless, the mark is never written to disk");
        } finally {
            Directory.Delete(dir, recursive: true);
        }

        // WatermarkLayout.place: inside the frame, in its corner, the diagonal along the frame's own.
        var mark = new Watermark(null) { Words = "x", Anchor = 8, Size = 0.04, Margin = 0.03 };
        var p = WatermarkRaster.Place(4, 1000, 500, mark);
        var (bx, by, bw, bh) = p.Bounds;
        Check(Math.Abs(p.H - 20) < 1e-9 && Math.Abs(p.W - 80) < 1e-9 && Math.Abs(bx + bw - 985) < 1e-9 && Math.Abs(by + bh - 485) < 1e-9,
              "an anchored mark is size × the short edge tall, a margin in from its corner");
        mark.Anchor = -1;
        var d = WatermarkRaster.Place(10, 1000, 500, mark);
        var db = d.Bounds;
        Check(Math.Abs(d.Angle - Math.Atan2(500, 1000)) < 1e-12 && db.W <= 1000 + 1e-9 && db.H <= 500 + 1e-9,
              "a diagonal mark runs along the frame's diagonal and its turned box fits");
    }

    static unsafe void WatermarkRasterChecks(EngineModel engine) {
        var mark = new Watermark(null) { Enabled = true, Words = "ORION", Anchor = 8, Size = 0.1, Opacity = 0.5 };
        var mask = WatermarkRaster.Mask(mark, 600, 400);
        int Sum(byte[] m, int x0, int y0, int x1, int y1) {
            int s = 0;
            for (int y = y0; y < y1; y++) for (int x = x0; x < x1; x++) s += m[y * 600 + x];
            return s;
        }
        Check(mask != null && Sum(mask, 300, 200, 600, 400) > 0 && Sum(mask, 0, 0, 300, 200) == 0 && mask.Max() <= 128,
              "a text mark covers its corner and nothing else, at its opacity");
        mark.IsText = false;
        mark.Svg = Encoding.UTF8.GetBytes("""<svg xmlns="http://www.w3.org/2000/svg" width="20" height="10"><rect width="20" height="10" fill="red"/></svg>""");
        mark.Anchor = 0;
        mark.Opacity = 1;
        var svg = WatermarkRaster.Mask(mark, 600, 400);
        Check(svg != null && svg[20 * 600 + 30] == 255 && Sum(svg, 300, 200, 600, 400) == 0,
              "an SVG mark keeps only its shape: its red is full coverage, in its own corner");
        mark.Svg = "not an svg"u8.ToArray();
        Check(WatermarkRaster.Mask(mark, 600, 400) == null && !new Watermark(null).IsDrawable && WatermarkRaster.Mask(new Watermark(null), 600, 400) == null,
              "a mark with nothing to draw writes nothing");

        // Through the encoder: the mark changes what is written, and the preview is a real export.
        var settings = new ExportSettings { Size = Array.FindIndex(ExportSettings.Sizes, c => c.Code == 1024) };
        var (sw, sh) = SheetsNative.ImageSize();
        var size = settings.Dimensions(sw, sh);
        var text = new Watermark(null) { Enabled = true, Words = "ORION", Size = 0.12, Opacity = 1 };
        ulong plain = 0, marked = 0;
        var options = settings.Options(sw, sh);
        WatermarkRaster.With(null, options, size, o => SheetsNative.orion_engine_export_size(engine.Handle, in o, out plain));
        WatermarkRaster.With(WatermarkRaster.Mask(text, (int)size.W, (int)size.H), options, size,
                             o => SheetsNative.orion_engine_export_size(engine.Handle, in o, out marked));
        Check(plain > 0 && marked > 0 && plain != marked, $"the watermark reaches the encoder ({plain} → {marked} bytes)");
        Check(WatermarkPreview.Render(text) is { PixelSize: var px } && Math.Max(px.Width, px.Height) == 1024, "the watermark preview is the open photo exported at 1024 px");
    }

    static void ChromeChecks(EngineModel engine) {
        var window = Shell();
        var vp = CanvasState.Viewport;
        vp.Reset();
        bool fit = ChromeStubs.IsFit && ChromeStubs.Percent == vp.Percent;
        vp.ToggleFitAndActual();
        Settle(() => !vp.Live);
        Check(fit && !ChromeStubs.IsFit && ChromeStubs.Percent == vp.Percent && vp.Percent == 100, $"the toolbar's zoom is the canvas's ({vp.Percent}%)");
        vp.Reset();
        window.Select("Crop");
        bool crop = ChromeStubs.CropTab;
        window.Select("Light");
        Check(crop && !ChromeStubs.CropTab, "the footer knows the crop tab is up");

        // The flat frame: five agreeing samples name it; a real photo never does.
        Check(FlatFrameCheck.Judge([.. Enumerable.Repeat(new float[] { 0.5f, 0.5f, 0.5f }, 5)]) == "the render is one flat colour, rgb(0.500, 0.500, 0.500)"
              && FlatFrameCheck.Judge([[0.5f, 0.5f, 0.5f], [0.5f, 0.5f, 0.501f]]) == null && FlatFrameCheck.Judge([null, [0f, 0f, 0f]]) == null,
              "a frame of one colour is named, a sample apart or missing is not");
        engine.Render(preview: false);
        Check(FlatFrameCheck.Why == null && ChromeStubs.FlatFrame == null, "the photo is not a flat frame");

        Check(TargetedAdjust.Field(TargetedAdjust.Mode.Hue) == "hue_shift" && TargetedAdjust.Field(TargetedAdjust.Mode.Saturation) == "sat_shift"
              && TargetedAdjust.Field(TargetedAdjust.Mode.Luminance) == "lum_shift", "the targeted tool moves the array its mode names");

        // The assistant column: the command puts it at the window's leading edge, and its terminal
        // runs what it is given (a harmless echo here, never the agent).
        var launcher = AssistantPanel.Launcher;
        // The process has a child of its own, as the agent has its MCP server: closing takes both.
        AssistantPanel.Launcher = (t, command, d, _) => t.LaunchProcess(d, "/bin/sh", "-c", $"echo orion-assistant-{command}; sleep 30 & sleep 30");
        try {
            var host = window.FindControl<Panel>("AssistantHost")!;
            AssistantModel.Shared.Close();   // the command sweep may have left it open
            string said = Commands.Run("toggle-assistant");
            Settle(window);
            var panel = host.Children.OfType<AssistantPanel>().FirstOrDefault();
            var term = panel?.GetLogicalDescendants().OfType<TerminalControl>().FirstOrDefault();
            string Screen() => term == null ? "" : string.Join("\n", Enumerable.Range(0, 5).Select(i => term.Terminal?.GetLine(i) ?? ""));
            Pump(() => Screen().Contains("orion-assistant-"));
            string text = Screen();
            int pid = term?.Pid ?? 0;
            bool group = pid > 0 && kill(-pid, 0) == 0;
            Commands.Run("toggle-assistant");
            Settle(window);
            Check(said == "ran toggle-assistant" && panel != null && text.Contains($"orion-assistant-{AssistantModel.Shared.Command}")
                  && host.Children.Count == 0, $"Show assistant opens the column and its terminal runs a process ('{text.Trim()}'); again closes it");
            Pump(() => kill(-pid, 0) != 0, 3);
            Check(group && kill(-pid, 0) != 0, $"closing the column ends the agent's whole process group, its children too (pid {pid})");
        } finally {
            AssistantPanel.Launcher = launcher;
        }
        window.Close();
    }

    static void HdrChecks(string dir, string original) {
        var taken = new HashSet<string> { Path.Combine(dir, "a-HDR.dng"), Path.Combine(dir, "a-HDR-2.dng") };
        Check(HdrMerge.OutputPath(Path.Combine(dir, "a.ARW"), taken.Contains) == Path.Combine(dir, "a-HDR-3.dng")
              && HdrMerge.OutputPath(Path.Combine(dir, "b.ARW"), _ => false) == Path.Combine(dir, "b-HDR.dng"), "a merge never writes over a file");
        // The real thing, on two links to the photo: a DNG that opens and renders.
        string one = Path.Combine(dir, "bracket-1" + Path.GetExtension(original)), two = Path.Combine(dir, "bracket-2" + Path.GetExtension(original));
        File.CreateSymbolicLink(one, original);
        File.CreateSymbolicLink(two, original);
        string output = HdrMerge.OutputPath(one);
        var run = HdrMerge.Run([one, two], 0, output);
        Pump(() => run.IsCompleted, 300);
        string? failed = run.IsCompleted ? run.Result : "still running";
        using var merged = failed == null ? Session.Open(output) : null;
        Check(failed == null && merged != null && merged.Size(merged.Render(preview: false)) != default,
              $"Merge writes a DNG beside the frames that opens and renders ({failed ?? Path.GetFileName(output)})");
    }

    static void SpotChecks(EngineModel engine, string photo) {
        var fd = CanvasNative.DisplayMap();
        _ = EditHistory.CanUndo;   // history listening before the first spot
        SpotModel.Clear();
        engine.Render(preview: false);
        long before = Fingerprint(engine.Picture);
        double radius = SpotModel.Radius;
        SpotModel.Radius = 0.1;
        SpotModel.Heal = false;
        SpotModel.Add(new Point(0.3, 0.3), fd);
        SpotModel.Move(0, null, new Point(0.75, 0.75), fd);
        engine.Render(preview: false);
        var held = engine.Session!.Spots();
        var saved = engine.Session.Capture()["spots"] as JsonArray;
        Check(held.Count == 1 && held[0].Heal == 0 && Math.Abs(held[0].Radius - 0.1f) < 1e-6 && saved?.Count == 1
              && saved[0]!["heal"]!.GetValue<bool>() == false && Fingerprint(engine.Picture) != before,
              "a spot placed on the canvas reaches the engine, changes the render, and is in the edit the sidecar takes");
        // Saved, and reopened from the sidecar alone.
        Check(Autosave.ToSidecar(photo, engine.Session.Capture()), "the spot is written to the sidecar");
        using (var reopened = Session.OpenEdited(photo))
            Check(reopened?.Spots() is [var s] && Math.Abs(s.SrcX - held[0].SrcX) < 1e-6, "the spot comes back from the sidecar into the engine");
        // Undo takes the placed spot out of the render and of what the sidecar takes; redo puts it back.
        string? label = EditHistory.UndoLabel;
        long spotted = Fingerprint(engine.Picture);
        EditHistory.Undo();
        bool undone = engine.Session.Spots().Count == 0 && SpotModel.Spots.Count == 0 && Fingerprint(engine.Picture) == before
                      && engine.Session.Capture()["spots"] is JsonArray { Count: 0 };
        Autosave.ToSidecar(photo, engine.Session.Capture());
        using (var reopened = Session.OpenEdited(photo)) undone &= reopened?.Spots().Count == 0;
        EditHistory.Redo();
        Check(undone && engine.Session.Spots() is [var back] && Math.Abs(back.SrcX - held[0].SrcX) < 1e-6 && back.Heal == 0
              && SpotModel.Spots.Count == 1 && Fingerprint(engine.Picture) == spotted,
              $"placing a spot is one history step ({label}): undo restores the render and the sidecar, redo the spot");
        SpotModel.RemoveLast();
        Check(engine.Session.Spots().Count == 0, "Undo spot takes it out of the engine");
        (SpotModel.Radius, SpotModel.Heal) = (radius, true);
        File.Delete(Sidecar.PathFor(photo));
        engine.Render(preview: false);
    }

    /// The MCP server's half is a file: propose_edit writes `<stem>.proposed.json`,
    /// a whole DevelopState. Written here as it would, then the watcher's half end to end.
    static void ProposalChecks(EngineModel engine, string photo) {
        var w = ProposalWatcher.Shared;
        ProposalWatcher.Install();
        var window = Shell();
        string proposed = Proposal.ProposedPath(photo);
        float committed = engine.Get("exposure_ev");
        void Propose(float ev) {
            var state = engine.Session!.Capture();
            state["exposureEv"] = ev;
            File.WriteAllBytes(proposed, DevelopJson.Encode(state));
        }

        Check(Proposal.ProposedPath("/a/b.c.ARW") == "/a/b.c.proposed.json" && Proposal.ProductLabel("hueShift") == "Color mixer hue"
              && Proposal.ProductLabel("lensChoice") == "Lens choice", "a proposal lives where the server writes it and is named as the panels name it");
        Check(Proposal.Transition(Proposal.Phase.Idle, new(Proposal.EventKind.Approved)) is ({ Previewing: false }, { Count: 0 })
              && Proposal.Transition(Proposal.Phase.Idle, new(Proposal.EventKind.Appeared, ["x"])).Item2[0].Kind == Proposal.Act.StopAutosave,
              "an approve with nothing up does nothing, and autosave stops before anything renders");

        // Appears: previewed live in compare, the edit locked.
        Propose(committed + 1);
        Pump(() => w.IsLive);
        Check(w.IsLive && w.Keys.SequenceEqual(["exposureEv"]) && engine.Get("exposure_ev") == committed + 1 && ChromeStubs.DocumentEditsLocked
              && CompareState.Comparing && CompareState.Reference != null && !Commands.CanRun("undo") && !Commands.CanRun("tab-color")
              && Commands.CanRun("approve-proposal") && window.FindControl<Control>("ToolsHost")!.IsEnabled == false,
              $"a proposal written beside the photo appears live in compare, naming what it changes, the edit locked " +
              $"(live {w.IsLive}, keys {string.Join(",", w.Keys)}, ev {engine.Get("exposure_ev")} vs {committed + 1}, locked {ChromeStubs.DocumentEditsLocked}, " +
              $"compare {CompareState.Comparing}/{CompareState.Reference != null}, undo {Commands.CanRun("undo")}, color {Commands.CanRun("tab-color")}, " +
              $"approve {Commands.CanRun("approve-proposal")}, tools {window.FindControl<Control>("ToolsHost")?.IsEnabled})");
        // Rejected: put back, the file gone, unlocked.
        Commands.Run("reject-proposal");
        Check(!w.IsLive && engine.Get("exposure_ev") == committed && !File.Exists(proposed) && !CompareState.Comparing
              && CompareState.Reference == null && window.FindControl<Control>("ToolsHost")!.IsEnabled,
              $"Reject puts the committed edit back and deletes the proposal (live {w.IsLive}, ev {engine.Get("exposure_ev")} vs {committed}, " +
              $"file {File.Exists(proposed)}, compare {CompareState.Comparing}/{CompareState.Reference != null}, tools {window.FindControl<Control>("ToolsHost")?.IsEnabled})");

        // Approved: in the sidecar, one history step named for the agent.
        Propose(committed + 0.5f);
        Pump(() => w.IsLive);
        Commands.Run("approve-proposal");
        var sidecar = Sidecar.Read(photo)?.Develop is { } bytes ? DevelopJson.Decode(bytes) : null;
        Check(!w.IsLive && !File.Exists(proposed) && DevelopJson.Float(sidecar?["exposureEv"]) == committed + 0.5f
              && engine.Get("exposure_ev") == committed + 0.5f && EditHistory.UndoLabel == "Agent: exposureEv",
              $"Approve writes the proposal to the sidecar and records one step ({EditHistory.UndoLabel})");
        committed += 0.5f;

        // Mask rows carry names and matte ids the engine never holds, by position. A proposal
        // that drops the first of two rows must not leave the first's id on the survivor.
        string idA = "0b5f2a7e-0000-4000-8000-00000000000a", idB = "0b5f2a7e-0000-4000-8000-00000000000b";
        JsonObject Row(string id, string name) {
            var m = DevelopJson.DefaultMask();
            m["kind"] = 4; m["matteId"] = id; m["matteSource"] = "Subject"; m["name"] = name;
            return m;
        }
        void ProposeMasks(params JsonObject[] rows) {
            var state = engine.Session!.Capture();
            state["maskComponents"] = new JsonArray(rows);
            File.WriteAllBytes(proposed, DevelopJson.Encode(state));
            Pump(() => w.IsLive);
            Commands.Run("approve-proposal");
        }
        JsonArray? SavedRows() => (Sidecar.Read(photo)?.Develop is { } b ? DevelopJson.Decode(b) : null)?["maskComponents"] as JsonArray;
        ProposeMasks(Row(idA, "Sky"), Row(idB, "Face"));
        bool both = SavedRows() is [var r0, var r1] && r0!["matteId"]?.GetValue<string>() == idA && r1!["matteId"]?.GetValue<string>() == idB;
        ProposeMasks(Row(idB, "Face"));
        var left = SavedRows();
        Check(both && left is [var only] && only!["matteId"]?.GetValue<string>() == idB && only["name"]?.GetValue<string>() == "Face"
              && MaskGroup.Shared.Shapes is [{ MatteId: var held }] && held == idB,
              $"approving a proposal that removes the first mask row keeps the survivor's own matte id and name ({left?.ToJsonString()})");
        ProposeMasks();

        // An unreadable proposal, or one that is not a DevelopState (a key written twice), is no
        // proposal: it must not take the app down from the dispatcher.
        File.WriteAllText(proposed, "{}");
        File.SetUnixFileMode(proposed, UnixFileMode.None);
        bool survived = true;
        try { w.Check(); } catch (Exception) { survived = false; }
        Check(survived && !w.IsLive, "a proposal Orion may not read is treated as absent, not thrown");
        File.Delete(proposed);
        File.WriteAllText(proposed, "{\"exposureEv\": 1, \"exposureEv\": 2}");
        try { w.Check(); } catch (Exception) { survived = false; }
        Check(survived && !w.IsLive, "a proposal with a duplicate key is treated as absent, not thrown");
        File.Delete(proposed);
        Pump(() => false, 0.4);

        // Approved in chat: the file goes and the sidecar moved, so the sidecar is what shows.
        Propose(committed + 1);
        Pump(() => w.IsLive);
        var chat = engine.Session!.Capture();
        chat["exposureEv"] = committed + 1;
        Sidecar.Merge(photo, s => s.Develop = DevelopJson.Encode(chat));
        File.Delete(proposed);
        Pump(() => !w.IsLive);
        Check(!w.IsLive && engine.Get("exposure_ev") == committed + 1, "a proposal approved in chat leaves the sidecar's edit on the canvas");
        committed += 1;

        // A switch away puts the committed edit back on the photo left; coming back, it appears again against it.
        Propose(committed - 1);
        Pump(() => w.IsLive);
        engine.Next();
        Arrived(engine);
        bool away = !w.IsLive;
        engine.Previous();
        Arrived(engine);
        Pump(() => w.IsLive);
        Check(away && w.IsLive && w.Keys.SequenceEqual(["exposureEv"]), "leaving a photo mid-proposal puts its edit back; returning shows the proposal again");
        Commands.Run("reject-proposal");
        Check(engine.Get("exposure_ev") == committed, "and Reject after the return leaves the committed edit");

        // current.json: what the MCP server's current_photo reads.
        var when = new DateTime(2026, 10, 29, 12, 0, 0, DateTimeKind.Utc);
        var current = CurrentPhoto.Json(photo, CaptureInfo.Read(engine.Handle), when);
        Check(current["photo"]?.GetValue<string>() == photo && current["folder"]?.GetValue<string>() == Path.GetDirectoryName(photo)
              && current["updated"]?.GetValue<string>() == "2026-10-29T12:00:00Z" && current["capture"] is JsonObject
              && CurrentPhoto.Json(null, null, when).Select(p => p.Key).SequenceEqual(["updated"]),
              "current.json names the open photo, its folder and what the camera recorded; with none, only the time");
        window.Close();
    }
}
