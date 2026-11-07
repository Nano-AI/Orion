using Avalonia;
using Avalonia.Media;
using Avalonia.Media.Imaging;
using Avalonia.Platform;
using OrionShell.Develop;

namespace OrionShell.CanvasSlice;

/// Lightroom's clipping warning: the pixels the histogram's flags count, painted
/// on the photo, highlights in ClipHigh and shadows in ClipLow. J shows both
/// ends, each flag under the histogram shows its own, and Alt held while a tone
/// slider drags shows both for the drag. A viewing aid the canvas draws over the
/// picture (ClippingMask): the engine never renders it, so no export, sidecar,
/// undo step or histogram reading can carry it.
static class Clipping {
    static bool _shadows, _highlights, _held;
    public static event Action? Changed;

    public static bool Shadows { get => _shadows; set => Change(ref _shadows, value); }
    public static bool Highlights { get => _highlights; set => Change(ref _highlights, value); }
    /// A tone slider drags with Alt held.
    public static bool Held { get => _held; set => Change(ref _held, value); }

    public static bool ShowsShadows => _shadows || _held;
    public static bool ShowsHighlights => _highlights || _held;
    public static bool Any => ShowsShadows || ShowsHighlights;

    /// The sliders whose Alt-drag shows the overlay, as Lightroom's Basic panel does.
    public static readonly IReadOnlySet<string> Tones = new HashSet<string> { "exposure_ev", "highlights", "shadows", "whites", "blacks" };

    /// J: both ends on, or both off when either is on.
    public static void Toggle() {
        bool on = !(_shadows || _highlights);
        (_shadows, _highlights) = (on, on);
        Changed?.Invoke();
    }

    static void Change(ref bool field, bool value) {
        if (field == value) return;
        field = value;
        Changed?.Invoke();
    }
}

/// The overlay for one drawn picture: its clipped pixels opaque, the rest clear,
/// at the picture's own size so it lands on it pixel for pixel. Rebuilt only
/// when the picture, its pixels or the ends shown change.
sealed class ClippingMask {
    WriteableBitmap? _mask;
    Bitmap? _for;
    int _version;
    (bool, bool) _ends;

    /// What a pixel's channel byte says, by Levels.Bin: 1 crushed, 2 blown, 0 neither.
    static readonly byte[] End = Enumerable.Range(0, 256)
        .Select(v => (byte)(Levels.Bin((byte)v) == 0 ? 1 : Levels.Bin((byte)v) == Levels.BinCount - 1 ? 2 : 0)).ToArray();

    public Bitmap? For(Bitmap picture, int version) {
        if (picture is not WriteableBitmap source) return null;
        var ends = (Clipping.ShowsShadows, Clipping.ShowsHighlights);
        if (picture == _for && version == _version && ends == _ends && _mask != null) return _mask;
        (_for, _version, _ends) = (picture, version, ends);
        Paint(source, ends.Item1, ends.Item2);
        return _mask;
    }

    unsafe void Paint(WriteableBitmap source, bool shadows, bool highlights) {
        using var s = source.Lock();
        if (_mask == null || _mask.PixelSize != s.Size) {
            _mask?.Dispose();
            _mask = new WriteableBitmap(s.Size, new Vector(96, 96), PixelFormat.Rgba8888, AlphaFormat.Premul);
        }
        using var d = _mask.Lock();
        uint high = Packed(Palette.ClipHigh), low = Packed(Palette.ClipLow);
        nint from = s.Address, to = d.Address;
        int w = s.Size.Width, ss = s.RowBytes, ds = d.RowBytes;
        Parallel.For(0, s.Size.Height, y => {
            byte* a = (byte*)from + (long)y * ss;
            uint* o = (uint*)((byte*)to + (long)y * ds);
            for (int x = 0; x < w; x++, a += 4) {
                int any = End[a[0]] | End[a[1]] | End[a[2]];   // alpha is the fourth byte in RGBA and BGRA alike
                // A pixel both blown and crushed (a pure primary) reads as blown: it is the end an edit can still lose.
                o[x] = highlights && (any & 2) != 0 ? high : shadows && (any & 1) != 0 ? low : 0;
            }
        });
    }

    static uint Packed(IBrush brush) {
        var c = ((ISolidColorBrush)brush).Color;
        return BitConverter.IsLittleEndian ? (uint)(c.R | c.G << 8 | c.B << 16 | 0xFF << 24) : (uint)(c.R << 24 | c.G << 16 | c.B << 8 | 0xFF);
    }
}
