using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

namespace OrionShell.Sheets;

/// The orion_* calls the sheets need that EngineModel does not wrap (SLICES.md
/// rule 10). Layouts mirror engine/include/orion/orion.h field for field.
static partial class SheetsNative {
    /// OrionExportOptions. The watermark mask is always null here: the raster
    /// that fills it (WatermarkRaster.swift) is reached only through export.
    [StructLayout(LayoutKind.Sequential)]
    public struct ExportOptions {
        public int Format;        // OrionImageFormat: PNG 0, JPEG 1, TIFF 2, JPEG XL 3
        public float Quality;
        public uint MaxDimension; // 0 keeps full resolution
        public int Space, Rating, Metadata, BitDepth, Sharpen;
        public nint WatermarkMask;
        public uint WatermarkWidth, WatermarkHeight;
        public float WatermarkR, WatermarkG, WatermarkB;
    }

    [InlineArray(256)]
    public struct Names { byte _first; }   // camera[128] + lens[128]

    /// OrionRawInfo.
    [StructLayout(LayoutKind.Sequential)]
    public struct RawInfo {
        public uint Width, Height;
        public Names CameraAndLens;
        public float Iso, Shutter, Aperture, FocalLength;
        public long Timestamp;
    }

    [LibraryImport(Native.Lib)]
    public static partial int orion_engine_image_size(nint engine, out uint width, out uint height);

    /// A real encode; reports the bytes without writing a file.
    [LibraryImport(Native.Lib)]
    public static partial int orion_engine_export_size(nint engine, in ExportOptions options, out ulong bytes);

    /// The same measurement in two halves: the snapshot renders (engine
    /// thread), the size encodes it (any thread). Free every snapshot.
    [LibraryImport(Native.Lib)]
    public static partial int orion_engine_export_snapshot(nint engine, in ExportOptions options, out nint snapshot);

    [LibraryImport(Native.Lib)]
    public static partial int orion_export_snapshot_size(nint snapshot, out ulong bytes);

    [LibraryImport(Native.Lib)]
    public static partial void orion_export_snapshot_free(nint snapshot);

    [LibraryImport(Native.Lib, StringMarshalling = StringMarshalling.Utf8)]
    public static partial int orion_read_info(string path, out RawInfo info);

    /// The open photo's rendered size, (0, 0) when nothing is open.
    public static (uint W, uint H) ImageSize() {
        uint w = 0, h = 0;
        nint engine = EngineModel.Shared.Handle;
        if (engine == 0 || orion_engine_image_size(engine, out w, out h) != 0) return (0, 0);
        return (w, h);
    }
}
