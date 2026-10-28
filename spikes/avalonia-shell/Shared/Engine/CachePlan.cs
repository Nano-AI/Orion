namespace OrionShell;

/// How a full tier picks what to drop: lowest score first.
public enum Eviction {
    /// Decision #318: distance from the photo on screen, cheaper along the bias.
    /// In a list whose order is known, the photo needed furthest in the future
    /// is the furthest along the way the hand is going: Belady's optimal, guessed.
    Ahead,
    /// Ahead, except that a photo visited in the last 8 visits costs at most
    /// what one 8 photos away does: the pictures tier (20) keeps the photo an
    /// A/B comparison keeps going back to, the decoded tier (2-4) is unchanged.
    AheadRecent,
    /// Least recently visited or decoded.
    Recent,
    /// Fewest visits, halved every 16 visits; recency breaks ties.
    Frequent,
}

/// The decisions of the #318 cache with no photos in it: where the hand is
/// going, what to decode next, what a tier drops. SessionCache runs it over
/// sessions and pictures; CacheReplay runs it over traces with nothing decoded.
/// What is predicted (and so prefetched) is always the bias-weighted nearest;
/// the policy only chooses what leaves.
public sealed class CachePlan(Func<int> count, Eviction policy = Eviction.AheadRecent, int pictureSlots = 20) {
    readonly Dictionary<int, long> _used = new();   // tick of the last visit or decode
    readonly Dictionary<int, int> _visits = new();
    readonly Dictionary<int, long> _visited = new();  // hint number of the last visit
    double _rate, _lastPress = double.NegativeInfinity;   // presses per second, smoothed; seconds
    long _tick, _hints;

    public int Current { get; private set; } = -1;
    /// Where the hand is going, in [-1, 1]: +1 forward.
    public double Bias { get; private set; }
    /// Decoded neighbours beside the photo on screen: 2 at rest, 4 at key-repeat speed.
    public int SessionSlots => 2 + Math.Min(2, (int)(_rate / 4));
    public int PictureSlots => pictureSlots;

    /// The photo now shown, the direction of travel (0 for a jump), and the
    /// time in seconds: each press moves the bias halfway toward its direction.
    public void Hint(int index, int direction, double seconds) {
        double gap = seconds - _lastPress;
        _lastPress = seconds;
        if (direction == 0) (Bias, _rate) = (0, 0);
        else {
            Bias = (Bias + Math.Sign(direction)) / 2;
            _rate = gap > 1 ? 0 : (_rate + 1 / Math.Max(gap, 0.01)) / 2;
        }
        Current = index;
        _visited[index] = _hints + 1;
        _used[index] = ++_tick;
        _visits[index] = _visits.GetValueOrDefault(index) + 1;
        if (++_hints % 16 == 0)
            foreach (int i in _visits.Keys.ToList()) _visits[i] /= 2;
    }

    /// A photo was decoded: recent, for Recent.
    public void Decoded(int index) => _used[index] = ++_tick;

    /// The `slots` neighbours a tier should hold, and the photo on screen:
    /// nearest first, cheaper along the bias, inside the list.
    public IEnumerable<int> Predicted(int slots) {
        int reach = 3 * slots + 3, n = count();
        return Enumerable.Range(Math.Max(0, Current - reach), Math.Min(n, Current + reach + 1) - Math.Max(0, Current - reach))
            .OrderBy(Cost).Take(slots + 1);
    }

    /// Whether a decode of `index` belongs in a tier of `members`: predicted
    /// for it, missing from it, and not the first thing it would drop.
    public bool Wants(IEnumerable<int> members, int index, int slots) {
        if (members.Contains(index) || !Predicted(slots).Contains(index)) return false;
        var others = members.Where(i => i != Current).ToList();
        return others.Count < slots || others.Min(i => Score(i)) < Score(index, decoding: true);
    }

    /// What a tier of `members` drops to hold `slots` beside the photo on screen.
    public List<int> Over(IEnumerable<int> members, int slots) {
        var others = members.Where(i => i != Current).OrderBy(i => Score(i)).ToList();
        return others.Take(Math.Max(0, others.Count - slots)).ToList();
    }

    /// The next photo worth decoding for the decoded tier, else for the pictures.
    public int? Next(IEnumerable<int> sessions, IEnumerable<int> pictures, Func<int, bool> skip) {
        foreach (int i in Predicted(SessionSlots)) if (!skip(i) && Wants(sessions, i, SessionSlots)) return i;
        foreach (int i in Predicted(pictureSlots)) if (!skip(i) && Wants(pictures, i, pictureSlots)) return i;
        return null;
    }

    double Score(int i, bool decoding = false) {
        double used = decoding ? _tick + 1 : _used.GetValueOrDefault(i);
        return policy switch {
            Eviction.Ahead => -Cost(i),
            Eviction.AheadRecent => -(_hints - _visited.GetValueOrDefault(i, long.MinValue / 2) < 8 ? Math.Min(Cost(i), 8) : Cost(i)),
            Eviction.Recent => used,
            _ => _visits.GetValueOrDefault(i) + used / (_tick + 2.0),
        };
    }

    /// Distance from the photo on screen, weighted by the bias: at full lean a
    /// photo ahead costs half its distance and one behind one and a half.
    double Cost(int i) {
        int d = i - Current;
        bool along = d != 0 && Math.Sign(d) == (Bias >= 0 ? 1 : -1);
        return Math.Abs(d) * (along ? 1 - Math.Abs(Bias) / 2 : 1 + Math.Abs(Bias) / 2) - (along ? 1e-6 : 0);
    }
}
