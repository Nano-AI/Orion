using System.Diagnostics;
using Avalonia.Media.Imaging;
using Avalonia.Threading;

namespace OrionShell;

/// Decision #318: photo navigation caches in two tiers steered by a direction
/// vector. Around the photo on screen it keeps screen-sized pictures (the
/// preview graph's, ~6 MB) for `pictureSlots` neighbours and decoded sessions
/// (115 MB each) for 2-4, both placed by Bias: each arrow press moves it
/// halfway toward its direction, and pressing faster adds decoded neighbours.
///
/// Threading: a decode opens a session nothing else has seen, renders its
/// preview on a worker (Session.cs says why that is safe), and posts it to the
/// UI thread; a session dropped from both tiers closes on a worker too.
/// Every field here belongs to the UI thread.
public sealed class SessionCache(PhotoList photos, int pictureSlots = 20) : ISessionSource {
    readonly Dictionary<int, Session> _kept = new();
    readonly Dictionary<int, Bitmap> _pictures = new();
    readonly Dictionary<Session, int> _lent = new();     // acquired: never closed here before Release
    readonly Dictionary<int, Task> _decoding = new();    // any generation; one decode per index at a time
    readonly HashSet<int> _failed = new();
    (int Index, Action<Session?> Ready)? _request;
    Task _closing = Task.CompletedTask;
    int _generation, _current = -1;
    double _rate;                                         // arrow presses per second, smoothed
    long _lastPress;

    /// Where the hand is going, in [-1, 1]: +1 forward.
    public double Bias { get; private set; }
    /// Decoded neighbours kept beside the photo on screen: 2 at rest, 4 at key-repeat speed.
    public int SessionSlots => 2 + Math.Min(2, (int)(_rate / 4));
    public int Sessions => _kept.Count;
    public int Pictures => _pictures.Count;
    public bool Busy => _decoding.Count > 0;

    public Session? Acquire(int index) {
        if (!_kept.Remove(index, out var s)) s = Session.Open(photos.Paths[index]);
        if (s != null) _lent[s] = index;
        return s;
    }

    public void Request(int index, Action<Session?> ready) {
        _request = null;
        if (_kept.Remove(index, out var s)) {
            _lent[s] = index;
            ready(s);
            return;
        }
        _request = (index, ready);
        Pump();
    }

    public Bitmap? Picture(int index) => _pictures.GetValueOrDefault(index);

    public void Release(Session s) {
        if (!_lent.Remove(s, out int index)) { Close(s); return; }   // lent before a Clear
        // Edited since its last preview: render one so the kept picture shows the edit.
        if (s.RenderedFull && s.Render(preview: true) == Graph.Preview) s.Read(Graph.Preview);
        if (s.Picture is { } p) _pictures[index] = p;
        Keep(index, s);
        Evict();
        Pump();
    }

    public void Hint(int index, int direction) {
        long now = Stopwatch.GetTimestamp();
        double seconds = Stopwatch.GetElapsedTime(_lastPress, now).TotalSeconds;
        _lastPress = now;
        if (direction == 0) (Bias, _rate) = (0, 0);   // a jump: no direction, no speed
        else {
            Bias = (Bias + Math.Sign(direction)) / 2;
            _rate = seconds > 1 ? 0 : (_rate + 1 / Math.Max(seconds, 0.01)) / 2;
        }
        _current = index;
        Evict();
        Pump();
    }

    public void Clear() {
        _generation++;
        foreach (var s in _kept.Values) Close(s);
        _kept.Clear();
        _pictures.Clear();
        _lent.Clear();
        _failed.Clear();
        _request = null;
        (_current, Bias, _rate) = (-1, 0, 0);
    }

    /// Waits for the decodes and closes in flight, so none is inside the engine
    /// when the process exits.
    public void Drain() => Task.WaitAll([.. _decoding.Values, _closing]);

    /// One at a time, off the UI thread: shim_close takes 5-9 ms, a third of an arrow press.
    void Close(Session s) => _closing = _closing.ContinueWith(_ => s.Dispose());

    /// The photos a tier keeps around `index`: `slots` neighbours, more of them
    /// on the side `bias` leans to but one on the other once there are two,
    /// shifted to stay inside a list of `count`.
    public static (int Lo, int Hi) Window(int index, int count, int slots, double bias) {
        int lean = (int)Math.Round(slots * (1 + Math.Abs(bias)) / 2, MidpointRounding.AwayFromZero);
        if (slots >= 2) lean = Math.Min(lean, slots - 1);
        var (ahead, behind) = bias >= 0 ? (lean, slots - lean) : (slots - lean, lean);
        int lo = index - behind, hi = index + ahead;
        if (lo < 0) (lo, hi) = (0, hi - lo);
        if (hi > count - 1) (lo, hi) = (lo - (hi - count + 1), count - 1);
        return (Math.Max(lo, 0), hi);
    }

    bool InWindow(int i, int slots) => Window(_current, photos.Paths.Count, slots, Bias) is var (lo, hi) && i >= lo && i <= hi;

    /// The window's photos, nearest first, the side Bias leans to first on a tie.
    IEnumerable<int> Nearest(int slots) {
        var (lo, hi) = Window(_current, photos.Paths.Count, slots, Bias);
        int lean = Bias >= 0 ? 1 : -1;
        return Enumerable.Range(lo, hi - lo + 1).OrderBy(i => 2 * Math.Abs(i - _current) - (Math.Sign(i - _current) == lean ? 1 : 0));
    }

    void Evict() {
        if (_current < 0) return;
        foreach (int i in _kept.Keys.Where(i => !InWindow(i, SessionSlots)).ToList()) {
            _kept.Remove(i, out var s);
            Close(s!);
        }
        foreach (int i in _pictures.Keys.Where(i => !InWindow(i, pictureSlots)).ToList()) _pictures.Remove(i);
    }

    /// Into the decoded tier when it is wanted there and light, else closed.
    void Keep(int index, Session s) {
        if (InWindow(index, SessionSlots) && !_kept.ContainsKey(index) && Light(s)) _kept[index] = s;
        else Close(s);
    }

    /// Drops the graphs' intermediates (stage 1b). Until that is built, a
    /// session that rendered full holds 1.35 GB more (#318): not worth keeping.
    static bool Light(Session s) {
        try {
            s.ReleaseRenderMemory();
            return true;
        } catch (NotBuiltException) {
            return !s.RenderedFull;
        }
    }

    /// The photo asked for starts at once, beside at most one other decode; a
    /// prefetch starts only when nothing runs: the decoded tier before the
    /// pictures, nearest first.
    void Pump() {
        if (_request is { Index: var r } && !_decoding.ContainsKey(r) && _decoding.Count < 2) Start(r);
        if (_decoding.Count > 0 || _current < 0) return;
        foreach (int i in Nearest(SessionSlots))
            if (Wanted(i) && !_kept.ContainsKey(i)) { Start(i); return; }
        foreach (int i in Nearest(pictureSlots))
            if (Wanted(i) && !_pictures.ContainsKey(i)) { Start(i); return; }
    }

    bool Wanted(int i) => !_lent.ContainsValue(i) && !_failed.Contains(i);

    void Start(int index) {
        int generation = _generation;
        string path = photos.Paths[index];
        _decoding[index] = Task.Run(() => {
            var s = Session.Open(path);
            try {
                if (s?.Render(preview: true) == Graph.Preview) s.Read(Graph.Preview);   // the picture, off the UI thread
            } catch (Exception e) {
                Console.Error.WriteLine($"cache: {path}: {e.Message}");
                s?.Dispose();
                s = null;
            }
            Dispatcher.UIThread.Post(() => Done(index, generation, s));
        });
    }

    void Done(int index, int generation, Session? s) {
        _decoding.Remove(index);
        if (generation != _generation) { if (s != null) Close(s); }   // the list changed under it
        else {
            if (s == null) _failed.Add(index);
            else if (s.Picture is { } p) _pictures[index] = p;
            if (_request is { } r && r.Index == index) {
                _request = null;
                if (s != null) _lent[s] = index;
                r.Ready(s);
            } else if (s != null) Keep(index, s);
        }
        Evict();
        Pump();
    }
}
