using System.Runtime.InteropServices;
using Avalonia.Media.Imaging;

namespace OrionShell.Sheets;

/// Engine.watermarkPreview (WatermarkPanel.swift): the open photo exported at
/// 1024 px with this mark and read back, through the same writer every export
/// uses. The mark is drawn whether or not it is switched on: the editor shows
/// what it would put on a photo.
static partial class WatermarkPreview {
    [LibraryImport(Native.Lib, StringMarshalling = StringMarshalling.Utf8)]
    private static partial int orion_engine_export(nint engine, string path, in SheetsNative.ExportOptions options);

    public static Bitmap? Render(Watermark mark) {
        nint engine = EngineModel.Shared.Handle;
        var (sw, sh) = SheetsNative.ImageSize();
        if (engine == 0 || sw == 0 || sh == 0) return null;
        var settings = new ExportSettings { Size = Array.FindIndex(ExportSettings.Sizes, c => c.Code == 1024) };
        var size = settings.Dimensions(sw, sh);
        var options = settings.Options(sw, sh) with { Quality = 0.9f, BitDepth = 8 };
        string path = Path.Combine(Path.GetTempPath(), $"orion-watermark-preview-{Guid.NewGuid()}.jpg");
        try {
            if (WatermarkRaster.With(WatermarkRaster.Mask(mark, (int)size.W, (int)size.H), options, size,
                                     o => orion_engine_export(engine, path, o)) != 0) return null;
            using var file = File.OpenRead(path);
            return new Bitmap(file);
        } catch (IOException) {
            return null;
        } finally {
            File.Delete(path);
        }
    }
}
