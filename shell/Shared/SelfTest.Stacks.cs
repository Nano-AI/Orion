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
    }
}
