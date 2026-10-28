using Avalonia;
using Avalonia.Controls;
using Avalonia.Media;
using Avalonia.Media.Imaging;
using Avalonia.Platform;
using Avalonia.Rendering.Composition;
using Avalonia.Threading;

namespace OrionShell.CanvasSlice;

/// The engine's picture with no CPU copy (#317, decision #354). native/present.mm
/// copies each render on the GPU into an IOSurface, and this control's composition
/// visual imports it. They take turns over two MTLSharedEvents: the shim's copy
/// waits on the GPU for the consumer event, which Avalonia signals after its
/// snapshot; Avalonia's snapshot waits on the GPU for the producer event, which
/// the copy signals. One hand-off is in flight at a time; renders that land
/// meanwhile are handed over after it, the latest only.
///
/// Avalonia's Metal renderer only. Headless has no GPU interop, and the OpenGL
/// importer refuses an RGBA surface (AvaloniaNativeGlPlatformGraphics.cs), so
/// those keep the CPU copy, as does any import that fails: Shown.OnGpu stays false.
/// It sits under the canvas's own drawing (CanvasPanel.Draw), which still draws
/// the compare original over it, and a CPU copy whenever this does not hold the
/// shown picture.
sealed class SharedPicture : Control {
    const string IOSurface = KnownPlatformGraphicsExternalImageHandleTypes.IOSurfaceRef;
    const string SharedEvent = KnownPlatformGraphicsExternalSemaphoreHandleTypes.MetalSharedEvent;

    ICompositionGpuInterop? _interop;
    CompositionDrawingSurface? _surface;
    CompositionSurfaceVisual? _visual;
    ICompositionImportedGpuSemaphore? _producer, _consumer;
    nint _presenter;
    /// Per graph, the shim's surface generation and Avalonia's import of it.
    readonly Dictionary<Graph, (int Generation, ICompositionImportedGpuImage Image)> _images = [];
    /// The last hand-off: whose render, which graph, how many halvings, after which Shown change, its shape.
    (Session? Session, Graph Graph, int Level, int Version, double Aspect) _holds;
    int _version;
    double _pixels = double.PositiveInfinity;   // the picture's width on screen; full size until the canvas says
    bool _starting, _busy, _again;

    /// A hand-off was made: the canvas redraws, without its CPU copy if this now holds the picture.
    public event Action? Arrived;
    /// For the windowed proof: hand-offs made, whether the last placement held the shown picture, a fallback.
    public static int Presented { get; private set; }
    public static bool Showing { get; private set; }
    public static bool Failed { get; private set; }

    public SharedPicture() {
        ClipToBounds = true;   // the picture pans past the canvas's edge
        IsHitTestVisible = false;
    }

    protected override async void OnAttachedToVisualTree(VisualTreeAttachmentEventArgs e) {
        base.OnAttachedToVisualTree(e);
        if (_presenter != 0) { if (!Failed) Enable(true); return; }
        if (_starting || Failed || ElementComposition.GetElementVisual(this)?.Compositor is not { } compositor) return;
        _starting = true;
        try {
            _interop = await compositor.TryGetCompositionGpuInterop();
            if (_interop == null || !_interop.SupportedImageHandleTypes.Contains(IOSurface)
                || !_interop.SupportedSemaphoreTypes.Contains(SharedEvent)) return;   // headless, software
            _presenter = Native.present_create();
            if (_presenter == 0) return;
            _producer = _interop.ImportSemaphore(new PlatformHandle(Native.present_producer(_presenter), SharedEvent));
            _consumer = _interop.ImportSemaphore(new PlatformHandle(Native.present_consumer(_presenter), SharedEvent));
            await _producer.ImportCompleted;
            await _consumer.ImportCompleted;
            _surface = compositor.CreateDrawingSurface();
            _visual = compositor.CreateSurfaceVisual();
            _visual.Surface = _surface;
            _visual.Visible = false;
            ElementComposition.SetElementChildVisual(this, _visual);
            Enable(true);
        } catch (Exception x) { Fail(x); }
    }

    protected override void OnDetachedFromVisualTree(VisualTreeAttachmentEventArgs e) {
        base.OnDetachedFromVisualTree(e);
        Enable(false);
    }

    void Enable(bool on) {
        var shown = EngineModel.Shared.Shown;
        shown.Changed -= Changed;
        if (on) shown.Changed += Changed;
        shown.OnGpu = on;
        if (!on) _holds = default;
    }

    /// Puts the shared picture where the canvas draws the whole picture and says
    /// whether it holds the shown one: the same photo at the same shape. False,
    /// and hidden, for a kept picture, a photo not handed over yet, a crop that
    /// changed the shape, or null: the canvas draws its CPU copy then. Called from
    /// CanvasPanel.Draw, so the visual moves in the same composition batch as
    /// everything drawn over it.
    public bool Place(ShownPicture? shown, Rect whole, BitmapInterpolationMode mode, double pixels) {
        if (_visual == null) return false;
        _pixels = pixels;
        var s = shown?.Live is { Handle: not 0 } live ? live : null;
        bool holds = shown is { OnGpu: true } && s != null && _holds.Session == s && whole.Width > 0
                     && Math.Abs(_holds.Aspect * whole.Height / whole.Width - 1) < 0.01;
        Showing = _visual.Visible = holds;
        if (!holds) return false;
        _visual.Offset = new Vector3D(whole.X, whole.Y, 0);
        _visual.Size = new Vector(whole.Width, whole.Height);
        _visual.RenderOptions = new RenderOptions { BitmapInterpolationMode = mode };
        if (Level(s!.Size(shown!.Graph).Width, pixels) != _holds.Level) Dispatcher.UIThread.Post(Present);   // zoomed across a halving
        return true;
    }

    void Changed() {
        _version++;
        Present();
    }

    void Present() {
        if (_busy) _again = true;
        else _ = Hand();
    }

    /// Hands the shown render over, then again while newer ones landed meanwhile.
    async Task Hand() {
        _busy = true;
        try {
            do {
                _again = false;
                var shown = EngineModel.Shared.Shown;
                if (!shown.OnGpu || shown.Live is not { Handle: not 0 } s) break;   // a kept picture: the canvas draws it
                var graph = shown.Graph;
                int level = Level(s.Size(graph).Width, _pixels), version = _version;
                if (_holds.Session == s && _holds.Graph == graph && _holds.Level == level && _holds.Version == version) break;
                if (s.Present(_presenter, graph, level, out nint io, out int generation, out int w, out int h, out ulong n) == 0)
                    throw new InvalidOperationException($"the shim has no RGBA8 {graph} render to hand over");
                Presented++;
                try {
                    var image = await Image(graph, io, generation, w, h);
                    var update = _surface!.UpdateWithTimelineSemaphoresAsync(image, _producer!, n, _consumer!, n);
                    _holds = (s, graph, level, version, (double)w / h);
                    Arrived?.Invoke();
                    await update;   // snapshotted, and the consumer signal is on the GPU: the next may follow
                } catch {
                    Native.present_release(_presenter, n);
                    throw;
                }
            } while (_again);
        } catch (Exception x) {
            Fail(x);
        } finally {
            _busy = false;
        }
    }

    /// Avalonia's import of the shim's surface for `graph`, made again when the shim made a new one.
    async Task<ICompositionImportedGpuImage> Image(Graph graph, nint io, int generation, int w, int h) {
        if (_images.TryGetValue(graph, out var had) && had.Generation == generation) return had.Image;
        if (had.Image != null) await had.Image.DisposeAsync();
        var image = _interop!.ImportImage(new PlatformHandle(io, IOSurface), new PlatformGraphicsExternalImageProperties {
            Width = w, Height = h, Format = PlatformGraphicsExternalImageFormat.R8G8B8A8UNorm, TopLeftOrigin = true,
        });
        _images[graph] = (generation, image);
        await image.ImportCompleted;
        return image;
    }

    void Fail(Exception x) {
        Console.Error.WriteLine($"shared picture: {x.GetType().Name}: {x.Message} The canvas copies through the CPU from now on.");
        Failed = true;
        Enable(false);
        Arrived?.Invoke();
    }

    /// CanvasPanel's Halvings rule: the smallest halving still at least `pixels` wide.
    static int Level(int width, double pixels) {
        int k = 0;
        while (k < 12 && (width >> (k + 1)) >= pixels) k++;
        return k;
    }
}
