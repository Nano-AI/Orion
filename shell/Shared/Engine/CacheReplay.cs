namespace OrionShell;

/// --bench's trace replay: CachePlan's decisions over a list of 200 photos with
/// nothing decoded, one eviction policy at a time, at the tiers' real sizes.
/// A decode is counted, not done: the photo moved to when its session is not
/// kept, and each prefetch the pause before a move pays for at 300 ms a decode
/// (#319's cold opens are 94-330 ms).
static class CacheReplay {
    const int Count = 200;
    const double DecodeSeconds = 0.3;

    public static void Print() {
        Console.WriteLine($"bench replay: {Count} photos, {DecodeSeconds * 1000:0} ms a decode · % of moves that found a picture / a session kept · decodes per 100 moves");
        Console.WriteLine($"  {"trace",-28}{"policy",-13}{"pictures",9}{"sessions",9}{"decodes",9}");
        var totals = new Dictionary<Eviction, (double P, double S, double D)>();
        foreach (var (name, moves) in Traces())
            foreach (var policy in Enum.GetValues<Eviction>()) {
                var (p, s, d) = Run(policy, moves);
                Console.WriteLine($"  {name,-28}{policy,-13}{p,9:0.0}{s,9:0.0}{d,9:0.0}");
                var t = totals.GetValueOrDefault(policy);
                totals[policy] = (t.P + p, t.S + s, t.D + d);
            }
        int traces = Traces().Count();
        foreach (var (policy, t) in totals)
            Console.WriteLine($"  {"mean of " + traces + " traces",-28}{policy,-13}{t.P / traces,9:0.0}{t.S / traces,9:0.0}{t.D / traces,9:0.0}");
    }

    /// Moves as (photo, seconds since the move before).
    static IEnumerable<(string, List<(int, double)>)> Traces() {
        yield return ("forward 1-200, 1.5 s", Enumerable.Range(0, Count).Select(i => (i, 1.5)).ToList());

        var cull = new List<(int, double)>();
        for (int at = 0; at + 5 < Count;) {
            for (int k = 0; k < 5; k++) cull.Add((++at, 1.0));
            for (int k = 0; k < 2; k++) cull.Add((--at, 1.0));
        }
        yield return ("cull: 5 fwd, 2 back, 1 s", cull);

        var held = new List<(int, double)> { (10, 0), (11, 3) };   // settled, then held
        for (int i = 12; i < 72; i++) held.Add((i, 0.03));
        for (int i = 72; i < 77; i++) held.Add((i, 1.5));
        yield return ("held arrow: 60 × 30 ms", held);

        var flip = new List<(int, double)> { (100, 0) };
        for (int at = 100; at < 140;) {
            flip.Add((++at, 1.0));
            flip.Add((++at, 1.0));
            flip.Add((60, 1.0));
            flip.Add((90, 1.0));
            flip.Add((60, 1.0));
            flip.Add((at, 1.0));
        }
        yield return ("A/B 60|90 amid fwd, 1 s", flip);

        var random = new Random(318);
        yield return ("random jumps, 2 s", Enumerable.Range(0, 150).Select(_ => (random.Next(Count), 2.0)).ToList());
    }

    static (double Pictures, double Sessions, double Decodes) Run(Eviction policy, List<(int Index, double Gap)> moves) {
        var plan = new CachePlan(() => Count, policy);
        var sessions = new HashSet<int>();
        var pictures = new HashSet<int>();
        int from = -1, pictured = 0, kept = 0, decodes = 0;
        double now = 0, budget = 0;
        void Fit() {
            foreach (int x in plan.Over(sessions, plan.SessionSlots)) sessions.Remove(x);
            foreach (int x in plan.Over(pictures, plan.PictureSlots)) pictures.Remove(x);
        }
        void Decode(int i, bool asked) {
            decodes++;
            plan.Decoded(i);
            if (asked || plan.Wants(pictures, i, plan.PictureSlots)) pictures.Add(i);
            if (asked || plan.Wants(sessions, i, plan.SessionSlots)) sessions.Add(i);
            Fit();
        }
        foreach (var (index, gap) in moves) {
            // The pause before this move: one prefetch at a time, as the worker does.
            now += gap;
            for (budget = plan.Current < 0 ? 0 : budget + gap / DecodeSeconds; budget >= 1; budget--)
                if (plan.Next(sessions, pictures, _ => false) is int next) Decode(next, asked: false);
                else { budget = 0; break; }   // idle time is not banked
            int step = index - from;
            plan.Hint(index, from >= 0 && Math.Abs(step) == 1 ? step : 0, now);
            Fit();
            if (pictures.Contains(index)) pictured++;
            if (sessions.Contains(index)) kept++;
            else Decode(index, asked: true);
            from = index;
        }
        double n = moves.Count;
        return (100 * pictured / n, 100 * kept / n, 100 * decodes / n);
    }
}
