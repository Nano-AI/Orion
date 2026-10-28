using System.Diagnostics;
using Avalonia.Threading;

namespace OrionShell;

/// Stage 1a (#318): the cache's window and steering, and that it never hands
/// out, or keeps, a session it closed. Runs real decodes of the selftest photo.
static partial class SelfTest {
    static partial void SpeedChecks() {
        // The window: `slots` neighbours, leaning with the bias, inside the list.
        Check(SessionCache.Window(15, 30, 20, 0) == (5, 25), "cache window: unbiased, ±10");
        Check(SessionCache.Window(15, 30, 20, -0.5) == (0, 20), "cache window: leaning back, 15 behind and 5 ahead");
        Check(SessionCache.Window(5, 30, 4, 1) == (4, 8), "cache window: leaning hard, the photo just left is still kept");
        Check(SessionCache.Window(0, 30, 20, -1) == (0, 20) && SessionCache.Window(29, 30, 20, 1) == (9, 29)
              && SessionCache.Window(1, 3, 20, 0) == (0, 2), "cache window: clamps at the list ends, keeping its size where it can");

        if (EngineModel.Shared.Photos.Count == 0) return;
        // Runs the UI thread's queue, where background decodes land, until `done`.
        static void Settle(Func<bool> done) {
            var sw = Stopwatch.StartNew();
            while (!done() && sw.Elapsed.TotalSeconds < 30) {
                Dispatcher.UIThread.RunJobs();
                Thread.Sleep(2);
            }
        }
        var list = new PhotoList();
        list.Load(Enumerable.Repeat(EngineModel.Shared.Photos[0], 5));
        var cache = new SessionCache(list, pictureSlots: 2);

        // Steering: each press moves the bias halfway toward its direction; fast presses widen the decoded tier.
        cache.Hint(1, 1); cache.Hint(2, 1); cache.Hint(3, 1);
        double right = cache.Bias;
        int widened = cache.SessionSlots;
        cache.Hint(2, -1);
        Check(right == 0.875 && cache.Bias == -0.0625, $"bias moves halfway with each press ({right}, then {cache.Bias} after one back)");
        Check(widened == 4, $"presses in quick succession widen the decoded tier to 4 neighbours ({widened})");
        cache.Hint(2, 0);
        Check(cache.Bias == 0 && cache.SessionSlots == 2, "a jump has no direction and no speed");

        // A requested photo arrives from a background decode, and the prefetch fills both tiers around it.
        Session? lent = null;
        bool answered = false;
        cache.Request(2, s => (lent, answered) = (s, true));
        Settle(() => answered && !cache.Busy);
        Check(lent is { Handle: not 0 } && cache.Sessions == 2 && cache.Pictures == 3,
              $"Request answers with an open session; photos 1 and 3 decoded beside it, 3 pictures ({cache.Sessions}, {cache.Pictures})");

        // Acquire never hands out a closed session.
        var kept = cache.Acquire(3)!;
        Check(kept.Handle != 0 && cache.Sessions == 1, "Acquire takes a kept session, open, without decoding");
        cache.Release(kept);
        cache.Hint(0, 0);   // window [0, 2]: photo 3 falls out
        Settle(() => !cache.Busy);
        cache.Drain();
        Check(kept.Handle == 0 && lent!.Handle != 0, "leaving the window closes a kept session, never a lent one");
        var again = cache.Acquire(3)!;
        Check(again.Handle != 0 && !ReferenceEquals(again, kept), "Acquire never hands out a closed session: photo 3 decodes afresh");

        // Clear closes everything kept and nothing lent. A decode in flight across it is never handed
        // out, not even to the same index of the next list (here a file that will not open).
        cache.Hint(4, 0);
        cache.Request(4, _ => { });
        cache.Clear();
        Check(cache.Sessions == 0 && cache.Pictures == 0 && lent.Handle != 0 && again.Handle != 0,
              "Clear drops both tiers and closes no lent session");
        list.Load(Enumerable.Repeat("no-such-photo.arw", 5));
        Session? stale = lent;
        answered = false;
        cache.Hint(4, 0);
        cache.Request(4, s => (stale, answered) = (s, true));
        Settle(() => answered && !cache.Busy);
        Check(answered && stale == null, "a decode in flight across Clear is not handed to the next list; a photo that will not open answers null");
        cache.Release(lent);
        cache.Release(again);
        cache.Drain();
        Check(cache.Sessions == 0 && lent.Handle == 0 && again.Handle == 0, "sessions lent before Clear close when they come back");
    }
}
