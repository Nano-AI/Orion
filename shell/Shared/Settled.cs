using System.Diagnostics;
using Avalonia.Threading;

namespace OrionShell;

/// The edit at rest, for what reads the whole render back (the histogram, the
/// flat-frame check). A render outside a drag after `After` of quiet fires at
/// once: a swap's full render, one typed value. One inside a burst (a held key,
/// typed values, the drag's last tick) waits until `After` past the last, as
/// the Swift app's scheduleHistogram does, so a burst reads once.
static class Settled {
    public static readonly TimeSpan After = TimeSpan.FromMilliseconds(160);
    public static event Action? Fired;
    /// A render is waiting out `After`: a headless still pumps until it has fired.
    public static bool Pending => _fired != _rendered;

    static int _rendered, _fired;
    static bool _quiet = true;
    // From the end of the last render of any kind, a drag's included, to the start of the next: a slow
    // GPU (WARP) spends longer than After inside each render of a burst, and that is not a pause.
    static readonly Stopwatch Since = new();

    // A delay posted back, not a DispatcherTimer: headless, only Dispatcher.MainLoop fires
    // timers (SelfTest.CanvasUx's Frames), and the selftest and the stills pump with RunJobs.
    static Settled() {
        EngineModel.Shared.Rendering += () => _quiet = !Since.IsRunning || Since.Elapsed >= After;
        EngineModel.Shared.Rendered += () => {
            Settle();
            Since.Restart();   // after Fire: its readers' wait on the GPU is no pause either
        };
    }

    static void Settle() {
        if (EngineModel.Shared.Interacting) return;
        int render = ++_rendered;
        if (_quiet && _fired == render - 1) { Fire(render); return; }
        Task.Delay(After).ContinueWith(_ => Dispatcher.UIThread.Post(() => {
            if (render == _rendered) Fire(render);   // a later render waits its own After
        }));
    }

    static void Fire(int render) {
        _fired = render;
        Fired?.Invoke();
    }
}
