using Avalonia.Controls;
using Avalonia.Media.Imaging;

namespace OrionShell;

/// `--selftest`: the contract every slice relies on, checked in one process.
/// The integrator runs it after each merge; a slice that breaks a shared
/// assumption fails here rather than in a screenshot nobody reads.
static partial class SelfTest {
    static int _passed, _failed;

    static void Check(bool ok, string what) {
        if (ok) _passed++;
        else { _failed++; Console.Error.WriteLine("FAIL " + what); }
    }

    static bool Throws<T>(Action act, string mustMention) where T : Exception {
        try { act(); return false; }
        catch (T e) { return e.Message.Contains(mustMention); }
    }

    sealed class Boom : Control { public Boom() => throw new InvalidOperationException("boom"); }

    public static int Run() {
        // Motion as designed whatever the machine says: CI runners report Reduce
        // Motion on, and the checks that want it reduced force it (#376).
        Motion.Forced = false;
        // Registry: a duplicate name fails loudly and names both types.
        Check(Throws<InvalidOperationException>(() => Panels.Index([
                (new PanelAttribute("twin", Region.Tools), typeof(TextBlock)),
                (new PanelAttribute("twin", Region.Tools), typeof(Border))]), "Avalonia.Controls.Border"),
              "duplicate panel names throw, naming both types");

        // A tab that is not one of ToolTab's, or a tab outside the tool column.
        Check(Throws<InvalidOperationException>(() => Panels.Index([
                (new PanelAttribute("stray", Region.Tools) { Tab = "Lights" }, typeof(TextBlock))]), "Lights"),
              "an unknown tab name throws");
        Check(Throws<InvalidOperationException>(() => Panels.Index([
                (new PanelAttribute("stray", Region.Canvas) { Tab = "Light" }, typeof(TextBlock))]), "Region.Tools"),
              "a tab outside the tool column throws");

        // A throwing constructor becomes a visible error, not a crash.
        Check(Panels.Create(typeof(Boom)) is TextBlock { Tag: Panels.Failed, Text: { } t } && t.Contains("boom"),
              "a panel whose constructor throws shows its exception");

        // Every registered panel constructs.
        Check(Panels.All.Count > 0, "panels are registered");
        foreach (var e in Panels.All)
            Check(Panels.Create(e.Type).Tag as string != Panels.Failed, $"panel '{e.Name}' ({e.Type.Name}) constructs");

        // AdjustmentMath, against AdjustmentMath.swift's rules.
        Check(AdjustmentMath.Parsed(" 2,5 EV", " EV", -5, 5) == 2.5f, "typed '2,5 EV' parses to 2.5");
        Check(AdjustmentMath.Parsed("9", "", -1, 1) == 1f, "typed values clamp to the range");
        Check(AdjustmentMath.Parsed("abc", "", -1, 1) == null, "nonsense is refused, not zeroed");
        Check(AdjustmentMath.Nudged("0.25", 10, "", 2, -1, 1) == 0.35f, "Shift-Up nudges ten of the last digit");
        Check(!AdjustmentMath.IsModified(1.4501f, 1.45f, 2) && AdjustmentMath.IsModified(1.46f, 1.45f, 2),
              "modified means a tenth of the last shown digit");
        Check(AdjustmentMath.Detented(0.01f, 0, -1, 1, departing: false) == 0 &&
              AdjustmentMath.Detented(0.01f, 0, -1, 1, departing: true) == 0.01f, "the detent snaps unless departing");

        // Every SF Symbol name written into an Icons.Create call has a drawing
        // (ternaries too). The retired Swift UI's names were the oracle until #380;
        // a name passed through a variable is the end-of-run Icons.Missing check's.
        var source = Path.GetFullPath(Path.Combine(AppContext.BaseDirectory, "..", "..", ".."));   // this tree's shell/, not the build dir's
        var symbols = Directory.EnumerateFiles(source, "*.cs", SearchOption.AllDirectories)
            .Where(f => !f.Contains($"{Path.DirectorySeparatorChar}obj{Path.DirectorySeparatorChar}")).Select(File.ReadAllText)
            .SelectMany(s => System.Text.RegularExpressions.Regex.Matches(s, @"Icons\.Create\((.*?),\s*[\w.]+,\s*[\w.]+\)"))
            .SelectMany(m => System.Text.RegularExpressions.Regex.Matches(m.Groups[1].Value, "\"([a-z0-9.]+)\"").Select(l => l.Groups[1].Value))
            .Distinct().ToList();
        var missing = symbols.Where(s => !Icons.Has(s)).ToList();
        Check(symbols.Count >= 10 && missing.Count == 0,
              $"Icons draws all {symbols.Count} SF Symbols the app names" + (missing.Count > 0 ? $"; missing {string.Join(", ", missing)}" : ""));

        CommandChecks();
        PhotoListNavigation();

        // The field table came from orion.h.
        Check(EngineModel.Fields.Count(f => f.Length == 1) >= 50, "at least 50 scalar fields from orion.h");

        var engine = EngineModel.Shared;
        if (engine.Handle == 0) {
            // Every check below drives the photo; without one they would crash, not
            // report (#376: Linux CI, where no CPU Vulkan driver gives the engine a GPU).
            Console.Error.WriteLine("FAIL selftest needs a photo: OrionShell <raw> --selftest");
            _failed++;
            Console.WriteLine($"selftest: {_passed} checks passed, {_failed} failed (stopped: no photo)");
            return _failed;
        } else {
            Check(Throws<ArgumentException>(() => engine.Set("exposre_ev", 1), "exposre_ev"),
                  "a misspelt field throws and names it");
            Check(Throws<ArgumentException>(() => engine.Get("hue_shift", 8), "hue_shift[8]"),
                  "an index past the array throws");

            int roundTrips = 0;
            foreach (var (name, length) in EngineModel.Fields)
                for (int i = 0; i < length; i++) {
                    float v = engine.Get(name, i);
                    engine.Set(name, v, i);
                    if (engine.Get(name, i) == v) roundTrips++;
                    else Check(false, $"{name}[{i}] round-trips");
                }
            Check(roundTrips == EngineModel.Fields.Sum(f => f.Length), $"all {roundTrips} field slots round-trip");

            // Wiring, not maths: a stop of exposure must brighten what the canvas shows.
            float start = engine.Get("exposure_ev");
            engine.Set("exposure_ev", 0);
            double dark = MeanLuma(engine.Picture);
            engine.Set("exposure_ev", 1);
            double bright = MeanLuma(engine.Picture);
            engine.Set("exposure_ev", start);
            Check(bright > dark * 1.2, $"+1 EV brightens the picture ({dark:0.0} → {bright:0.0})");
            SessionContracts(engine);
        }
        SpeedChecks();
        NativeChecks();
        CopyChecks();
        WiringChecks();
        DesignChecks();
        LayoutChecks();
        CanvasUxChecks();
        ToolsChecks();
        MaskUxChecks();
        MotionChecks();
        SidecarChecks();
        MatteChecks();      // before LibraryChecks, which leaves the photo list on its own deleted folder
        LibraryChecks();
        ParityChecks();
        ImportChecks();
        AgentChecks();
        // Asked for while every check above built and drove the panels.
        Check(Icons.Missing.Count == 0, "every icon the run asked for has a drawing"
              + (Icons.Missing.Count > 0 ? $"; missing {string.Join(", ", Icons.Missing)}" : ""));
        if (_stubs.Count > 0) Console.WriteLine($"selftest: session seams not built yet (stage 1b): {string.Join(", ", _stubs)}");

        Console.WriteLine($"selftest: {_passed} checks passed, {_failed} failed");
        return _failed;
    }

    static unsafe double MeanLuma(Bitmap? picture) {
        if (picture is not WriteableBitmap bitmap) return 0;
        using var fb = bitmap.Lock();
        double sum = 0;
        long n = 0;
        for (int y = 0; y < fb.Size.Height; y += 16) {
            byte* row = (byte*)fb.Address + (long)y * fb.RowBytes;
            for (int x = 0; x < fb.Size.Width; x += 16, n++)
                sum += 0.2126 * row[x * 4] + 0.7152 * row[x * 4 + 1] + 0.0722 * row[x * 4 + 2];
        }
        return n == 0 ? 0 : sum / n;
    }
}
