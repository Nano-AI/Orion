using System.Diagnostics;
using Avalonia.Media.Imaging;
using Avalonia.Threading;

namespace OrionShell.CanvasSlice;

/// The canvas's picture wherever SharedPicture does not hold it: headless, the
/// OpenGL renderer, Windows and Linux, a failed import (decision #386). Each
/// render is copied to the CPU at the size the canvas shows it: reduced and
/// converted on the GPU, read back without waiting (orion_engine_display_*), and
/// written straight into one bitmap. The canvas draws the newest copy that has
/// landed and never queues behind the engine: while a hand drags it does not wait
/// for a copy in flight, shows the one before (a render behind at most) and looks
/// again a moment later; at rest, or with no copy of this photo yet, it waits for
/// the copy of the render on screen, a screen-sized readback.
sealed class CopiedPicture {
    WriteableBitmap? _bitmap;
    Session? _session, _held;   // whose render was copied last; whose copy the bitmap holds
    Graph _graph;
    int _level, _renders;
    ulong _begun, _taken;
    double _aspect, _pixels = double.PositiveInfinity;
    bool _looking;
    readonly int[] _renderOf = new int[4];   // by ticket % 4: which render a copy in flight is of

    /// A copy landed after the canvas last looked: it draws again.
    public event Action? Arrived;

    /// For the bench: copies begun and taken, this thread's milliseconds in the
    /// native calls, and how many renders the picture drawn trails the engine.
    public static int Begun { get; private set; }
    public static int Taken { get; private set; }
    public static double Milliseconds { get; private set; }
    public static int Behind { get; private set; }
    public static Avalonia.PixelSize Size { get; private set; }

    public CopiedPicture() => EngineModel.Shared.Shown.Changed += Changed;

    void Changed() {
        _renders++;
        var shown = EngineModel.Shared.Shown;
        if (!shown.OnGpu && shown.Live is { Handle: not 0 } s) Begin(s, shown.Graph);
    }

    void Begin(Session s, Graph graph) {
        var sw = Stopwatch.StartNew();
        _level = Level(s.Size(graph).Width, _pixels);
        _begun = s.BeginCopy(graph, _level);
        (_session, _graph) = (s, graph);
        _renderOf[_begun % 4] = _renders;
        Begun++;
        Milliseconds += sw.Elapsed.TotalMilliseconds;
    }

    /// From CanvasPanel.Draw: the newest copy of the shown render, `pixels` wide
    /// on screen, or null where this does not hold one (a kept picture, the shared
    /// surface's, a crop that changed the shape) and the canvas draws Shown.Bitmap.
    public Bitmap? For(ShownPicture shown, double pixels, bool interacting) {
        _pixels = pixels;
        if (shown.OnGpu || shown.Live is not { Handle: not 0 } s) return null;
        var size = s.Size(shown.Graph);
        if (size.Width <= 0 || size.Height <= 0) return null;
        // A render nobody copied (the shared surface let it go), or a canvas that crossed a halving: copy it now.
        if (s != _session || shown.Graph != _graph || Level(size.Width, pixels) != _level) Begin(s, shown.Graph);
        if (_taken != _begun || _held != s) Take(s, wait: !interacting || _held != s);
        bool holds = _held == s && Math.Abs(_aspect * size.Height / size.Width - 1) < 0.01;
        return holds ? _bitmap : null;
    }

    void Take(Session s, bool wait) {
        var sw = Stopwatch.StartNew();
        ulong ticket = s.TakeCopy(wait, ref _bitmap);
        Milliseconds += sw.Elapsed.TotalMilliseconds;
        if (ticket != 0) {
            (_taken, _held) = (ticket, s);
            _aspect = (double)_bitmap!.PixelSize.Width / _bitmap.PixelSize.Height;
            Behind = _renders - _renderOf[ticket % 4];
            Size = _bitmap.PixelSize;
            Taken++;
        }
        if (_taken == _begun || _looking) return;
        // In flight: look again in a moment rather than wait for it.
        _looking = true;
        Task.Delay(2).ContinueWith(_ => Dispatcher.UIThread.Post(() => { _looking = false; Arrived?.Invoke(); }));
    }

    /// SharedPicture's rule, CanvasPanel's Halvings: the smallest halving still at least `pixels` wide.
    static int Level(int width, double pixels) {
        int k = 0;
        while (k < 12 && (width >> (k + 1)) >= pixels) k++;
        return k;
    }
}
