using System.Text.RegularExpressions;

namespace OrionShell;

/// The stage-0 contracts (#315) the three stage-1 streams build on: commands,
/// the photo list, the session and its stage-1b seams, the picture seam.
static partial class SelfTest {
    // Each stream adds its own checks in its own file (SelfTest.Speed.cs, ...),
    // so three worktrees never edit one file. Unimplemented, they compile away.
    static partial void SpeedChecks();
    static partial void NativeChecks();
    static partial void CopyChecks();
    static partial void WiringChecks();
    // Round 2 (the developer's UI pass): one hook per stream, each in its own file.
    static partial void LayoutChecks();
    static partial void CanvasUxChecks();
    static partial void ToolsChecks();
    static partial void MaskUxChecks();
    static partial void MotionChecks();
    // GitHub issue 23: sidecars and autosave.
    static partial void SidecarChecks();
    // GitHub issue 23, items 4-5: subject and sky mattes, sync settings.
    static partial void MatteChecks();
    // GitHub issue 23, the last item: the Swift app's settings carried over (SettingsImport).
    static partial void ImportChecks();
    static partial void AgentChecks();

    static void CommandChecks() {
        // Every name a control binds to (Commands.Run/CanRun("…") in this tree) is registered.
        var source = Path.GetFullPath(Path.Combine(AppContext.BaseDirectory, "..", "..", ".."));
        var bound = Directory.EnumerateFiles(source, "*.cs", SearchOption.AllDirectories)
            .Where(f => !f.Contains($"{Path.DirectorySeparatorChar}obj{Path.DirectorySeparatorChar}")
                        && !Path.GetFileName(f).StartsWith("SelfTest"))   // its probes name commands on purpose
            .SelectMany(f => Regex.Matches(File.ReadAllText(f), "Commands\\.(?:Run|CanRun)\\(\"([a-z0-9-]+)\"\\)"))
            .Select(m => m.Groups[1].Value).Distinct().ToList();
        var unknown = bound.Where(n => Commands.Find(n) == null).ToList();
        Check(bound.Count >= 10 && unknown.Count == 0,
              $"all {bound.Count} command names the controls bind to are registered" +
              (unknown.Count > 0 ? $"; unknown {string.Join(", ", unknown)}" : ""));

        // No two commands share a name or a key; aliases of one key count as one.
        static Command C(string name, string? keys) => new(name, name, keys, "X.swift", () => true, null);
        Check(Throws<InvalidOperationException>(() => Commands.Index([C("twin", null), C("twin", "G")]), "twin"),
              "a duplicate command name throws");
        Check(Throws<InvalidOperationException>(() => Commands.Index([C("a", "Enter"), C("b", "Return")]), "a and b"),
              "two commands on one key throw, naming both");
        Check(Commands.All.Count >= 35, $"{Commands.All.Count} commands registered");
        // Frozen at #380, the Swift `menu` scene's job: a deleted command fails
        // here even when only a key reaches it. A new one is added to the list.
        string[] registered = ["tab-light", "tab-color", "tab-detail", "tab-optics", "tab-mask", "tab-crop", "tab-presets", "gallery",
            "compare", "swap-split", "fit", "actual-size", "open-photo", "open-folder", "export", "export-all", "hdr-merge", "stop-batch",
            "reveal-session-log", "undo", "redo", "reset-adjustments", "rotate-left", "rotate-right", "reject", "rate-0", "rate-1", "rate-2",
            "rate-3", "rate-4", "rate-5", "next-photo", "previous-photo", "select-all", "deselect", "trash", "delete-rejected", "apply-crop",
            "cancel-crop", "mask-overlay", "mask-feather-less", "mask-feather-more", "delete-mask-shape", "toggle-assistant",
            "copy-mcp-setup", "approve-proposal", "reject-proposal", "check-for-updates", "restart-to-update"];
        var gone = registered.Where(n => Commands.Find(n) == null).ToList();
        var added = Commands.All.Select(c => c.Name).Except(registered).ToList();
        Check(gone.Count == 0 && added.Count == 0, $"the {registered.Length} commands are the frozen list (gone: {string.Join(", ", gone)}; new: {string.Join(", ", added)})");

        // A stub says what it is on the status line instead of doing nothing.
        var stub = Commands.All.First(c => !c.Built);
        string said = Commands.Run(stub.Name);
        Check(said == $"{stub.Title} isn't in this version yet." && !said.Contains(".swift") && EngineModel.Shared.Status == said,
              $"a stub command says what is missing in the user's words, with no Swift file on screen ('{said}')");
        Check(Commands.Run("no-such-command").StartsWith("no command"), "an unknown command is refused by name");
    }

    /// Library.neighbor: clamps at both ends, and starts at the first photo when none is current.
    static void PhotoListNavigation() {
        var list = new PhotoList();
        list.Load(["a.arw", "b.arw", "c.arw"]);
        Check(list.Current == -1 && list.Next() && list.Current == 0, "Next from nothing opens the first photo");
        Check(list.Go(2) && !list.Next() && list.Current == 2, "Next at the last photo stays put (clamps, no wrap)");
        Check(list.Previous() && list.Current == 1 && list.Go(0) && !list.Previous() && list.Current == 0,
              "Previous at the first photo stays put");
        Check(!list.Go(3) && !list.Go(-1) && list.Current == 0, "Go past either end is refused");
        list.Load(["d.arw"]);
        Check(list.Current == -1, "a new list has nothing current");
        var empty = new PhotoList();
        Check(!empty.Next() && !empty.Previous() && empty.Current == -1, "an empty list goes nowhere");
    }

    static void SessionContracts(EngineModel engine) {
        var session = engine.Session!;
        int trips = 0, slots = 0;
        foreach (var (name, length) in EngineModel.Fields)
            for (int i = 0; i < length; i++, slots++) {
                float v = session.Get(name, i);
                session.Set(name, v, i);
                if (session.Get(name, i) == v) trips++;
            }
        Check(trips == slots, $"Session: all {trips} of {slots} field slots round-trip without a render");
        Check(Throws<ArgumentException>(() => session.Set("exposre_ev", 1), "exposre_ev"), "Session: a misspelt field throws");

        // Stage 1b's seams: not built yet, or built and honest. Either way, never silent.
        NotBuiltOr("curve", () => {
            (float, float)[] drawn = [(0, 0), (0.25f, 0.2f), (0.75f, 0.85f), (1, 1)];
            session.SetCurve(2, drawn);
            bool ok = session.Curve(2).SequenceEqual(drawn) && session.Curve(0).Count >= 2;
            session.SetCurve(2, [(0, 0), (1, 1)]);   // later checks expect an untouched photo
            return ok;
        });
        Check(Throws<ArgumentOutOfRangeException>(() => session.Curve(4), "channel"), "Session: curve channel 4 is out of range");
        NotBuiltOr("preview size", () => session.PreviewSize is { Width: > 0, Height: > 0 } p
                                         && Math.Abs((double)p.Width / p.Height - (double)session.ImageSize.Width / session.ImageSize.Height) < 0.01);
        NotBuiltOr("release render memory", () => { session.ReleaseRenderMemory(); return engine.Render(false).Render > 0; });
        NotBuiltOr("mask group", () => { session.SetMaskGroup([]); return true; });
        NotBuiltOr("auto-enhance", () => {
            string[] auto = ["exposure_ev", "whites", "blacks", "fusion", "clarity"];
            var before = auto.Select(f => session.Get(f)).ToArray();
            session.AutoEnhance();
            bool moved = auto.Where((f, i) => session.Get(f) != before[i]).Any();
            for (int i = 0; i < auto.Length; i++) session.Set(auto[i], before[i]);
            return moved;
        });

        // A seam that is not built reaches the status line through Apply, not a crash.
        Check(!engine.Apply(_ => throw new NotBuiltException("probe", "Engine.swift")) &&
              engine.Status == "Probe isn't in this version yet.", "Apply turns NotBuiltException into the status line");

        // The picture seam carries the graph a picture came from.
        int shown = 0;
        void Count() => shown++;
        engine.Shown.Changed += Count;
        float start = engine.Get("exposure_ev");
        engine.BeginInteraction();
        engine.Set("exposure_ev", start + 0.1f);
        bool preview = engine.Shown.Graph == Graph.Preview;
        engine.EndInteraction();
        engine.Set("exposure_ev", start);
        engine.Shown.Changed -= Count;
        Check(preview && engine.Shown.Graph == Graph.Full && shown == 3 && ReferenceEquals(engine.Picture, engine.Shown.Bitmap),
              "the picture seam names its graph and changes once per render");

        // One photo: there is nowhere to step, and stepping does not reopen it.
        if (engine.Photos.Count == 1) {
            nint handle = engine.Handle;
            Check(!engine.Next() && !engine.Previous() && engine.Handle == handle, "Next/Previous clamp on a one-photo list");
        }
    }

    static void NotBuiltOr(string what, Func<bool> builtAndRight) {
        try {
            Check(builtAndRight(), $"Session seam '{what}' is built and right");
        } catch (NotBuiltException e) {
            Check(e.Message.EndsWith("isn't in this version yet."), $"Session seam '{what}' says it is not built");
            _stubs.Add(what);
        }
    }

    static readonly List<string> _stubs = new();
}
