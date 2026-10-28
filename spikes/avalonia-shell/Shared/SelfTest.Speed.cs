using System.Diagnostics;
using Avalonia.Threading;

namespace OrionShell;

/// Stage 1a (#318): the cache's prediction, steering and eviction, and that it
/// never hands out, or keeps, a session it closed. Runs real decodes of the selftest photo.
static partial class SelfTest {
    static partial void SpeedChecks() {
        // Prediction: `slots` neighbours, nearest first, leaning with the bias, inside the list.
        static List<int> At(int index, int direction, int slots, int count = 30, int presses = 1) {
            var plan = new CachePlan(() => count);
            for (int k = 0; k < presses; k++) plan.Hint(index, direction, k);   // a second apart: no speed
            return plan.Predicted(slots).Order().ToList();
        }
        Check(At(15, 0, 20).SequenceEqual(Enumerable.Range(5, 21)), "prediction: unbiased, ±10");
        var leaning = At(15, 1, 20, count: 100, presses: 3);
        Check(leaning.Count(i => i > 15) == 15 && leaning.Count(i => i < 15) == 5,
              $"prediction: three presses ahead lean it 15 ahead, 5 behind ({leaning.Count(i => i > 15)}, {leaning.Count(i => i < 15)})");
        Check(At(0, -1, 20, presses: 3).SequenceEqual(Enumerable.Range(0, 21)) && At(29, 1, 20, presses: 3).SequenceEqual(Enumerable.Range(9, 21))
              && At(1, 0, 20, count: 3).SequenceEqual([0, 1, 2]), "prediction: clamps at the list ends, keeping its size where it can");

        // Eviction: lowest score leaves first. Photo 10 on screen after a press right; 7 behind, 11 and 12 ahead.
        static int Drops(Eviction policy, params int[] visits) {
            var plan = new CachePlan(() => 30, policy);
            double t = 0;
            foreach (int v in visits) plan.Hint(v, 0, t += 5);
            plan.Hint(10, 1, t + 5);
            return plan.Over([7, 11, 12], 2).Single();
        }
        Check(Drops(Eviction.Ahead, 7, 11, 12) == 7, "Ahead drops the photo furthest against the way the hand is going");
        Check(Drops(Eviction.Recent, 11, 7, 12) == 11, "Recent drops the least recently visited");
        Check(Drops(Eviction.Frequent, 12, 7, 12, 7, 11) == 11, "Frequent drops the least visited");
        static int Reference(Eviction policy) {   // compared with 60, now at 100
            var plan = new CachePlan(() => 200, policy);
            plan.Hint(60, 0, 1);
            plan.Hint(100, 0, 2);
            return plan.Over([60, 85, 112], 2).Single();
        }
        Check(Reference(Eviction.Ahead) == 60 && Reference(Eviction.AheadRecent) == 85,
              "AheadRecent (the default) keeps a photo visited moments ago however far it is; Ahead drops it");

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

        // Through EngineModel, over five spellings of the selftest photo (dir/./name...) so paths differ.
        string photo = EngineModel.Shared.Photos[0];
        var spelt = Enumerable.Range(0, 5).Select(k => Path.Combine([Path.GetDirectoryName(photo)!, .. Enumerable.Repeat(".", k), Path.GetFileName(photo)])).ToList();
        var model = new EngineModel();
        model.Load(spelt);
        model.Open(0);
        Settle(() => !model.Opening && !model.Cache.Busy);
        model.Next();
        Check(model.Session != null && !model.Opening && model.Shown.Graph == Graph.Preview,
              "a kept neighbour is open before Next returns, rendered at preview size");
        model.Open(4);
        Check(model.Opening && model.Session == null && model.Shown.Bitmap != null,
              "a photo with a kept picture and no session shows the picture while it decodes");
        Settle(() => !model.Opening);
        var onScreen = model.Session!;
        Check(!model.Apply(_ => throw new ArgumentException("probe: refused")) && model.Status == "probe: refused",
              "Apply puts an ArgumentException on the status line");
        model.Remove([spelt[0], spelt[1]]);
        Check(onScreen.Handle != 0 && ReferenceEquals(model.Session, onScreen) && model.Current == 2 && model.Photos.Count == 3,
              "Remove keeps the photo on screen open, same session, at its new index");
        model.Remove([spelt[4]]);
        Check(model.Session == null && model.Current == -1 && model.Photos.Count == 2, "Remove closes the photo on screen when it goes");
        model.Open(0);
        bool opening = model.Opening;
        model.Load([photo]);
        Check(opening && !model.Opening, "a new list ends Opening, whose request it dropped");
        model.Cache.Drain();
        Check(onScreen.Handle == 0, "the removed photo's session closed");
    }
}
