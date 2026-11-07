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
        Avalonia.Size Frame() { var r = engine.Session!.ImageSize; return new(r.Width, r.Height); }   // the picture, which the canvas draws a copy into
        // A reduced copy's mean, sampled every 16th pixel, drifts a few levels from the whole
        // render's on a busy picture (3.4 on CI's generated photo); a stale one is off by tens.
        static bool Same(double copy, double whole) => Math.Abs(copy - whole) < Math.Max(3, whole * 0.05);
        try {
            // A surface attached after a copy, edits made through it, then it failed.
            using (var copied = new CopiedPicture()) {
                shown.OnGpu = false;
                engine.Set("exposure_ev", -2);
                double before = MeanLuma(copied.For(shown, Frame(), 400, live: false));
                shown.OnGpu = true;
                for (int i = 1; i <= 4; i++) engine.Set("exposure_ev", -2 + i);
                shown.OnGpu = false;
                double after = MeanLuma(copied.For(shown, Frame(), 400, live: true)), want = Whole();
                Check(after > before + 10 && Same(after, want),
                      $"after the shared surface fails the canvas copies the latest edit, not the one before it ({before:0.0} → {after:0.0}, the render {want:0.0})");
            }

            // A copy that will not begin: nothing held, and the canvas reads the picture whole.
            using (var refused = new CopiedPicture { BeginCopy = (_, _, _) => 0 }) {
                int looks = 0;
                refused.Arrived += () => looks++;
                engine.Set("exposure_ev", 0.5f);
                var got = refused.For(shown, Frame(), 400, live: true);
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
                bool had = lost.For(shown, Frame(), 400, live: false) != null;
                var take = lost.TakeCopy;
                lost.TakeCopy = (Session _, bool _, ref WriteableBitmap? _) => -1;
                engine.Set("exposure_ev", 1);
                var stale = lost.For(shown, Frame(), 400, live: true);
                Pump(50);
                Check(had && stale == null && !lost.Looking && looks == 0,
                      $"a copy that fails holds no stale picture and does not look again ({looks} looks)");
                lost.TakeCopy = take;
                double back = MeanLuma(lost.For(shown, Frame(), 400, live: false)), want = Whole();
                Check(Same(back, want), $"and the next copy is the render on screen ({back:0.0}, the render {want:0.0})");
            }

            // Past fit a region renders without waiting for the GPU: the canvas draws the copy it
            // holds, where that copy's pixels belong, until the region's lands, then that.
            using (var held = new CopiedPicture()) {
                engine.Set("exposure_ev", -2);
                double whole = MeanLuma(held.For(shown, Frame(), 400, live: false));
                var part = new Avalonia.Rect(0.25, 0.25, 0.25, 0.25);
                engine.View = (part, new Avalonia.PixelSize(1600, (int)(1600 / Frame().AspectRatio)));
                CanvasState.Viewport.ZoomBy(4, new Avalonia.Point(0.5, 0.5));   // past fit, as the canvas's ZoomedPastFit follows it
                engine.Render(preview: false);   // the view's region
                var drawn = held.For(shown, Frame(), 400, live: false);
                Check(shown.Graph == Graph.Region && drawn != null && Same(MeanLuma(drawn), whole) && held.Uv == new Avalonia.Rect(0, 0, 1, 1) && held.Looking,
                      $"a region past fit returns before its copy lands: the canvas draws the whole picture it held, in its place, and looks again ({shown.Graph}, {held.Uv}, looking {held.Looking})");
                for (var sw = System.Diagnostics.Stopwatch.StartNew(); held.Uv != shown.Uv && sw.Elapsed.TotalSeconds < 5; Pump(5))
                    held.For(shown, Frame(), 400, live: false);
                Check(held.Uv == shown.Uv && CopiedPicture.Behind == 0,
                      $"and once it lands, the region's copy, at the region's place, no render behind ({held.Uv}, {CopiedPicture.Behind} behind)");
                CanvasState.Viewport.Reset();
                engine.View = null;
            }
        } finally {
            shown.OnGpu = onGpu;
            engine.Set("exposure_ev", start);
        }
    }

}
