using Avalonia;
using Avalonia.Media.Imaging;

namespace OrionShell;

/// The camera's embedded JPEG through the facade (orion_read_thumbnail via the
/// shim), decoded and stood upright: the library's thumbnails, and the canvas's
/// picture of a photo whose session is still decoding.
public static class EmbeddedPreview {
    /// The preview, decoded `width` wide and turned upright, through `buffer`,
    /// which grows for a larger one. Null when the file carries none.
    public static Bitmap? Read(string path, int width, ref byte[] buffer) =>
        Decode(path, ref buffer, (stream, _) => Bitmap.DecodeToWidth(stream, width));

    /// The preview at its own size when that is at most `widest`, else halved
    /// until it fits, which the JPEG decoder does in its transform: a resample
    /// to any other width costs ten times the decode (1920 to 1600 px, 112
    /// against 12 ms here).
    public static Bitmap? ReadFitting(string path, int widest, ref byte[] buffer) =>
        Decode(path, ref buffer, (stream, jpegWidth) => {
            int width = jpegWidth;
            while (width > widest) width = (width + 1) / 2;
            return width == jpegWidth || jpegWidth <= 0 ? new Bitmap(stream) : Bitmap.DecodeToWidth(stream, width);
        });

    /// ReadFitting, centred on the photo's own frame: a camera that records
    /// a 16:9 preview of a 3:2 frame crops it from the middle, so the bands it
    /// left out stay empty and the picture sits where the render will put it.
    public static Bitmap? ReadFramed(string path, int widest, ref byte[] buffer) {
        if (ReadFitting(path, widest, ref buffer) is not { } preview) return null;
        var info = new byte[288];
        if (Native.orion_read_info(path, info) != 0) return preview;
        double frame = (double)BitConverter.ToUInt32(info, 0) / Math.Max(BitConverter.ToUInt32(info, 4), 1u);
        int w = preview.PixelSize.Width, h = preview.PixelSize.Height;
        var size = frame > 0 && frame < (double)w / h ? new PixelSize(w, (int)Math.Round(w / frame)) : new PixelSize((int)Math.Round(h * frame), h);
        if (frame <= 0 || Math.Abs(size.Width - w) + Math.Abs(size.Height - h) <= 2) return preview;
        using var src = preview;
        var framed = new WriteableBitmap(size, src.Dpi, src.Format ?? Avalonia.Platform.PixelFormat.Bgra8888, src.AlphaFormat ?? Avalonia.Platform.AlphaFormat.Premul);
        using var fb = framed.Lock();
        unsafe { new Span<byte>((void*)fb.Address, fb.RowBytes * size.Height).Clear(); }   // transparent: the canvas's ground
        int x = (size.Width - w) / 2, y = (size.Height - h) / 2;
        src.CopyPixels(new PixelRect(0, 0, w, h), fb.Address + y * fb.RowBytes + x * 4, fb.RowBytes * (size.Height - y) - x * 4, fb.RowBytes);
        return framed;
    }

    static unsafe Bitmap? Decode(string path, ref byte[] buffer, Func<Stream, int, Bitmap> decode) {
        int size, turns;
        fixed (byte* p = buffer) size = Native.shim_thumbnail(path, p, buffer.Length, out turns);
        if (size < 0) {
            buffer = new byte[-size];
            fixed (byte* p = buffer) size = Native.shim_thumbnail(path, p, buffer.Length, out turns);
        }
        if (size <= 0) return null;
        using var stream = new MemoryStream(buffer, 0, size);
        return Upright(decode(stream, JpegWidth(buffer.AsSpan(0, size))), turns);
    }

    /// The width in a JPEG's start-of-frame header, 0 when there is none to read.
    internal static int JpegWidth(ReadOnlySpan<byte> jpeg) {
        for (int i = 2; i + 9 < jpeg.Length;) {
            if (jpeg[i] != 0xFF) return 0;
            byte marker = jpeg[i + 1];
            int length = jpeg[i + 2] << 8 | jpeg[i + 3];
            // SOF0 to SOF15, less DHT (C4), JPG (C8) and DAC (CC), which share the range.
            if (marker is >= 0xC0 and <= 0xCF and not 0xC4 and not 0xC8 and not 0xCC) return jpeg[i + 7] << 8 | jpeg[i + 8];
            i += 2 + length;
        }
        return 0;
    }

    /// The JPEG carries no orientation; `turns` clockwise quarter turns stand
    /// it up (orion.h). Done once on the pixels, so every view sees the true
    /// aspect instead of a landscape box with a rotated picture in it. Moved
    /// pixel by pixel straight into the new bitmap rather than drawn, so it
    /// needs no renderer and runs on any thread.
    static Bitmap Upright(Bitmap bitmap, int turns) {
        turns = ((turns % 4) + 4) % 4;
        if (turns == 0) return bitmap;
        using var src = bitmap;
        int w = src.PixelSize.Width, h = src.PixelSize.Height;
        bool swap = turns % 2 == 1;
        var upright = new WriteableBitmap(swap ? new PixelSize(h, w) : src.PixelSize, src.Dpi,
                                          src.Format ?? Avalonia.Platform.PixelFormat.Bgra8888, src.AlphaFormat);
        var pixels = System.Buffers.ArrayPool<uint>.Shared.Rent(w * h);
        try {
            unsafe {
                fixed (uint* p = pixels) src.CopyPixels(new PixelRect(0, 0, w, h), (nint)p, w * h * 4, w * 4);
                using var fb = upright.Lock();
                byte* dst = (byte*)fb.Address;
                for (int y = 0; y < h; y++)
                    for (int x = 0; x < w; x++) {
                        var (tx, ty) = turns switch { 1 => (h - 1 - y, x), 2 => (w - 1 - x, h - 1 - y), _ => (y, w - 1 - x) };
                        *(uint*)(dst + ty * fb.RowBytes + tx * 4) = pixels[y * w + x];
                    }
            }
        } finally {
            System.Buffers.ArrayPool<uint>.Shared.Return(pixels);
        }
        return upright;
    }
}
