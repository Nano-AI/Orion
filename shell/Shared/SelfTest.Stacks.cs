using OrionShell.Library;
using OrionShell.Sheets;

namespace OrionShell;

/// Finding stacks in a folder from capture metadata (#391): runs of identical settings at a steady cadence.
static partial class SelfTest {
    static readonly DateTime T0 = new(2026, 10, 30, 22, 0, 0);

    /// n frames, `gap` s apart, optionally rewritten per frame index (1-based).
    static List<Shot> Frames(int n, float shutter, float iso, double gap, Func<int, Shot, Shot>? edit = null) =>
        Enumerable.Range(1, n).Select(i => {
            var s = new Shot($"f{i:00}", "Sony A7", iso, shutter, 2.8f, 24, T0.AddSeconds(i * gap));
            return edit?.Invoke(i, s) ?? s;
        }).ToList();

    static string Sizes(List<StackGroup> g) => string.Join("+", g.Select(x => x.Paths.Count));

    static void StackGroupChecks() {
        var sky = StackGroups.Find(Frames(14, 20, 3200, 25));
        Check(sky.Count == 1 && sky[0].Astro && sky[0].Paths.Count == 14 && StackGroups.Suggestion(sky[0]) == "14 frames look like a stack. Stack them?",
              $"14 frames of 20 s at ISO 3200, 25 s apart, are one astro stack ({Sizes(sky)})");

        var iso = StackGroups.Find(Frames(14, 20, 3200, 25, (i, s) => i == 8 ? s with { Iso = 1600 } : s));
        Check(Sizes(iso) == "7+6" && iso.All(g => !g.Paths.Contains("f08")), $"one frame at another ISO splits the run and is left out ({Sizes(iso)})");

        // The Library's line (Task 10): the first group not dismissed this session, by the bar's own Pick.
        var dismissed = new HashSet<string>();
        bool firstShown = StackSuggestion.Pick(iso, dismissed) == iso[0];
        dismissed.Add(iso[0].Paths[0]);
        bool secondShown = StackSuggestion.Pick(iso, dismissed) == iso[1];
        dismissed.Add(iso[1].Paths[0]);
        Check(firstShown && secondShown && StackSuggestion.Pick(iso, dismissed) == null && StackSuggestion.Pick([], new HashSet<string>()) == null,
              "the suggestion shows the first group, the second once the first is dismissed, none once both are");
        // Stack… makes the group a selection: the sheet must still see it is the astro group.
        Check(StackSheet.DefaultMode(sky[0].Paths.Reverse().ToList(), sky) == Stack.Mode.Stars
              && StackSheet.DefaultMode(sky[0].Paths.Take(5).ToList(), sky) == Stack.Mode.Noise
              && StackSheet.DefaultMode(sky[0].Paths, null) == Stack.Mode.Noise,
              "a selection equal to a cached astro group, in any order, opens the sheet in Stars; part of one or no scan, Noise");

        var gap = StackGroups.Find(Frames(14, 20, 3200, 25, (i, s) => s with { Captured = T0.AddSeconds(i * 25 + (i > 5 ? 35 : 0)) }));
        Check(Sizes(gap) == "5+9", $"a 60 s gap after frame 5 splits the run ({Sizes(gap)})");

        Check(StackGroups.Find(Frames(4, 20, 3200, 25)).Count == 0, "four frames are not a stack");

        var burst = StackGroups.Find(Frames(8, 0.005f, 100, 0));
        Check(burst.Count == 1 && !burst[0].Astro && burst[0].Paths.Count == 8, $"a handheld burst in one second is a stack, not astro ({Sizes(burst)})");
        var steady = StackGroups.Find(Frames(8, 0.005f, 100, 0, (i, s) => s with { Captured = T0.AddSeconds(i / 3) }));
        Check(steady.Count == 1, "a burst across second boundaries stays one group (timestamps are whole seconds)");

        Check(StackGroups.Find(Frames(8, 0, 100, 5)).Count == 0 && StackGroups.Find(Frames(8, 0.005f, 0, 0)).Count == 0,
              "frames with an unknown shutter or ISO make no group");

        var calls = new List<string>();
        var bright = StackGroups.Find(Frames(14, 20, 3200, 25), p => { calls.Add(p); return false; });
        Check(bright.Count == 1 && !bright[0].Astro && calls.Count == 1 && calls[0] == "f08", $"a bright middle frame is not astro, asked once of the middle ({string.Join(",", calls)})");
        Check(StackGroups.Find(Frames(14, 2, 3200, 25), p => true) is [{ Astro: false }], "a 2 s exposure is not a candidate astro group");
        Check(StackGroups.Find(Frames(14, 20, 400, 25), p => true) is [{ Astro: false }], "ISO 400 is not a candidate astro group");

        Check(StackGroups.Find(Frames(14, 20, 3200, 25, (i, s) => s with { Captured = null })).Count == 0, "frames without a capture time make no group");

        var shuffled = StackGroups.Find(Frames(14, 20, 3200, 25).OrderBy(s => s.Path.Last() % 3).ThenByDescending(s => s.Path).ToList());
        Check(shuffled.Count == 1 && shuffled[0].Paths.SequenceEqual(sky[0].Paths), "input order does not matter, groups are in capture order");

        // A stack never writes over a file: a.ARW -> a-stack.dng, then a-stack-2.dng.
        var taken = new HashSet<string> { Path.Combine("d", "a-stack.dng") };
        Check(Stack.OutputPath(Path.Combine("d", "a.ARW"), _ => false) == Path.Combine("d", "a-stack.dng")
              && Stack.OutputPath(Path.Combine("d", "a.ARW"), taken.Contains) == Path.Combine("d", "a-stack-2.dng"),
              "a stack is named <stem>-stack.dng, then -stack-2, never over a file");

        // Of / Cached: one scan shared by the Library and the Stack sheet, keyed by the list instance.
        IReadOnlyList<string> none = new List<string>(), other = new List<string>();
        var first = StackGroups.Of(none);
        Check(ReferenceEquals(first, StackGroups.Of(none)) && ReferenceEquals(first, StackGroups.Cached(none)) && StackGroups.Cached(other) == null,
              "Of caches by list instance: twice is the same result, Cached of another list is null");

        StackSheetChecks();
    }

    /// The Stack sheet (Task 9): who may stack, the command behind it, and the sky painter.
    static void StackSheetChecks() {
        static HdrMergePanel.Candidate C(string camera) => new("x.ARW", camera, 3200, 20, 2.8f);
        Check(StackSheet.Ineligibility([C("A")]) == "Select at least two frames to stack."
              && StackSheet.Ineligibility([C("B"), C("A")]) == "These photos come from different cameras: A, B."
              && StackSheet.Ineligibility([C("A"), C("A"), C("")]) == null, "the stack refuses one frame or two cameras");
        Check(Commands.Find("stack") is { Built: true, Title: "Stack photos…" }, "\"stack\" is a registered, built command");
        Check(StackSheet.AutoRejection(3) == "None" && StackSheet.AutoRejection(4) == "Winsorized"
              && StackSheet.AutoRejection(20) == "Winsorized" && StackSheet.AutoRejection(21) == "Linear fit",
              "Auto names what the engine picks for the count (Rejection.cpp autoRejection)");
        Check(StackCalibration.Count(3, 5) == "3 darks · 5 flats" && StackCalibration.Count(1, 0) == "1 dark"
              && StackCalibration.Count(0, 1) == "1 flat" && StackCalibration.Count(0, 0) == "none",
              "the calibration line counts darks and flats");

        // A disc of radius 2 around (4.5, 4.5) on a 9×9 matte: inside set, outside untouched.
        var m = Enumerable.Repeat(0.5f, 81).ToArray();
        StackSplit.Paint(m, 9, 9, 4.5f, 4.5f, 2, add: true);
        bool inside(int i) { float dx = i % 9 + 0.5f - 4.5f, dy = i / 9 + 0.5f - 4.5f; return dx * dx + dy * dy <= 4; }
        Check(Enumerable.Range(0, 81).All(i => m[i] == (inside(i) ? 1f : 0.5f)) && m[40] == 1 && m[0] == 0.5f,
              "Add sky paints 1 inside the disc and leaves the outside untouched");
        StackSplit.Paint(m, 9, 9, 4.5f, 4.5f, 2, add: false);
        Check(Enumerable.Range(0, 81).All(i => m[i] == (inside(i) ? 0f : 0.5f)), "Remove sky paints 0 inside the disc only");
        StackSplit.Paint(m, 9, 9, -50, -50, 3, add: true);
        Check(Enumerable.Range(0, 81).All(i => m[i] != 1f), "a disc off the picture paints nothing");

        // Seed: a calm sky with single-pixel stars over a busy ground. The 3×3 median
        // takes the stars out, so the sky is found down to the horizon at row 20.
        const int w = 60, h = 40;
        var rgb = new float[w * h * 3];
        for (int y = 0; y < h; y++)
            for (int x = 0; x < w; x++) {
                float v = y < 20 ? ((x * 7 + y * 13) % 23 == 0 ? 1f : 0.05f) : ((x / 2 + y / 2) % 2 == 0 ? 0.8f : 0.2f);
                for (int c = 0; c < 3; c++) rgb[(y * w + x) * 3 + c] = v;
            }
        var (seed, noSky) = StackSplit.Seed(rgb, w, h);
        int skyRows = Enumerable.Range(0, h).Count(y => Enumerable.Range(0, w).Count(x => seed[y * w + x] > 0.5f) > w / 2);
        Check(noSky == null && skyRows is >= 17 and <= 21, $"a starry sky over busy ground seeds the split at the horizon ({skyRows} sky rows, {noSky})");
        var (flat, why) = StackSplit.Seed(new float[w * h * 3], w, h);
        Check(why != null && flat.Length == w * h && flat.All(v => v == 0), "a picture with no horizon seeds an empty matte and says why");

        // The real route: the sample's embedded preview, read back small and seeded.
        if (EngineModel.Shared.Photos.FirstOrDefault() is { } photo) {
            var split = StackSplit.For(photo);
            Check(split is { MatteWidth: > 2, MatteHeight: > 2 } s && Math.Max(s.MatteWidth, s.MatteHeight) <= StackSplit.LongEdge
                  && s.Matte.Length == s.MatteWidth * s.MatteHeight && (s.NoSky != null) == s.Matte.All(v => v == 0),
                  $"the sample's preview seeds a matte of at most {StackSplit.LongEdge} px ({split?.MatteWidth}×{split?.MatteHeight}, {split?.NoSky ?? "sky found"})");
        }

        // --mode takes the names only: an enum's comma list and numerals are flag tricks, not modes.
        string dng = Path.Combine(Path.GetTempPath(), "orion-stack-mode.dng");
        Check(CommandLineModes.StackMode(["--stack", dng, "--mode", "noise,stars", "a.ARW", "b.ARW"]) == 2
              && CommandLineModes.StackMode(["--stack", dng, "--mode", "1", "a.ARW", "b.ARW"]) == 2
              && CommandLineModes.StackMode(["--stack", dng, "--mode", "STARS", "/nonexistent/a.ARW", "/nonexistent/b.ARW"]) == 1,
              "--mode accepts noise, stars or landscape in any case, and nothing else");
    }
}
