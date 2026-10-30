using Avalonia.Media.Imaging;
using OrionShell.CanvasSlice;

namespace OrionShell;

/// The canvas's copy where the shared surface does not hold the picture (#386):
/// it shows the render on screen after the surface lets go of it, and a copy that
/// will not begin or fails is no picture held and no endless look again.
static partial class SelfTest {
    static partial void CopyChecks() {
        var engine = EngineModel.Shared;
        if (engine.Session == null) return;   // SelfTest.Run already failed for want of a photo
        var shown = engine.Shown;
        bool onGpu = shown.OnGpu;
        float start = engine.Get("exposure_ev");
        double Whole() => MeanLuma(shown.Bitmap);   // the render on screen, read whole and waited for
        try {
            // A surface attached after a copy, edits made through it, then it failed.
            using (var copied = new CopiedPicture()) {
                shown.OnGpu = false;
                engine.Set("exposure_ev", -2);
                double before = MeanLuma(copied.For(shown, 400, interacting: false));
                shown.OnGpu = true;
                for (int i = 1; i <= 4; i++) engine.Set("exposure_ev", -2 + i);
                shown.OnGpu = false;
                double after = MeanLuma(copied.For(shown, 400, interacting: true)), want = Whole();
                Check(after > before + 10 && Math.Abs(after - want) < 3,
                      $"after the shared surface fails the canvas copies the latest edit, not the one before it ({before:0.0} → {after:0.0}, the render {want:0.0})");
            }

            // A copy that will not begin: nothing held, and the canvas reads the picture whole.
            using (var refused = new CopiedPicture { BeginCopy = (_, _, _) => 0 }) {
                int looks = 0;
                refused.Arrived += () => looks++;
                engine.Set("exposure_ev", 0.5f);
                var got = refused.For(shown, 400, interacting: true);
                Pump(50);
                Check(got == null && !refused.Looking && looks == 0,
                      "a copy that will not begin holds no picture and does not look again");
            }

            // A copy that fails in flight (a refused map, a GPU reset): no stale picture, no endless
            // redraw, and the next copy that works is the render on screen.
            using (var lost = new CopiedPicture()) {
                int looks = 0;
                lost.Arrived += () => looks++;
                engine.Set("exposure_ev", -1);
                bool had = lost.For(shown, 400, interacting: false) != null;
                var take = lost.TakeCopy;
                lost.TakeCopy = (Session _, bool _, ref WriteableBitmap? _) => -1;
                engine.Set("exposure_ev", 1);
                var stale = lost.For(shown, 400, interacting: true);
                Pump(50);
                Check(had && stale == null && !lost.Looking && looks == 0,
                      $"a copy that fails holds no stale picture and does not look again ({looks} looks)");
                lost.TakeCopy = take;
                double back = MeanLuma(lost.For(shown, 400, interacting: false)), want = Whole();
                Check(Math.Abs(back - want) < 3, $"and the next copy is the render on screen ({back:0.0}, the render {want:0.0})");
            }
        } finally {
            shown.OnGpu = onGpu;
            engine.Set("exposure_ev", start);
        }
    }

}
