using System.Runtime.InteropServices;

namespace OrionShell.Develop;

/// The orion_* calls the develop panels make that EngineModel does not
/// (engine/include/orion/orion.h), against EngineModel.Handle. 0 is ORION_OK.
static partial class DevelopFacade {
    const string Lib = Native.Lib;

    /// out_bins takes bins*3 entries, red then green then blue.
    [LibraryImport(Lib)]
    public static partial int orion_engine_histogram(nint engine, [Out] uint[] outBins, uint bins);

    /// OrionLensProfile: int32 found, int32 approximate, char lens[128], char maker[64], int32 source.
    [LibraryImport(Lib)]
    public static partial int orion_engine_lens_profile(nint engine, [Out] byte[] profile);

    [LibraryImport(Lib)]
    public static partial nint orion_engine_lens(nint engine);

    [LibraryImport(Lib)]
    public static partial int orion_lens_name_count();

    [LibraryImport(Lib)]
    public static partial nint orion_lens_name_at(int index);

    [LibraryImport(Lib, StringMarshalling = StringMarshalling.Utf8)]
    public static partial int orion_engine_set_lens_choice(nint engine, string name);

    [LibraryImport(Lib)]
    public static partial nint orion_engine_lens_choice(nint engine);

    [LibraryImport(Lib, StringMarshalling = StringMarshalling.Utf8)]
    public static partial int orion_engine_load_lut(nint engine, string path);

    [LibraryImport(Lib)]
    public static partial int orion_engine_clear_lut(nint engine);

    [LibraryImport(Lib)]
    public static partial int orion_engine_lut_title(nint engine, [Out] byte[] title, int capacity);

    [LibraryImport(Lib)]
    public static partial nint orion_status_string(int status);

    public static string Text(nint cString) => cString == 0 ? "" : Marshal.PtrToStringUTF8(cString) ?? "";

    /// Engine.errorText: the engine's own reason, else the status name.
    public static string ErrorText(int status) {
        string detail = Text(Native.orion_last_error(EngineModel.Shared.Handle));
        return detail.Length > 0 ? detail : Text(orion_status_string(status));
    }

    /// orion.h ORION_LENS_SOURCE_*: where a lens profile came from.
    public enum LensSource { None = 0, Lensfun = 1, Dng = 3 }

    /// Engine.refreshLensProfile: the matched profile, "" when none.
    /// ⚠ 204 bytes, the whole struct: the engine writes all of it, `source` included.
    public static (string Name, bool Approximate, LensSource Source) LensProfile() {
        var engine = EngineModel.Shared.Handle;
        var raw = new byte[204];
        if (engine == 0 || orion_engine_lens_profile(engine, raw) != 0 || BitConverter.ToInt32(raw, 0) == 0) return ("", false, LensSource.None);
        string lens = CString(raw.AsSpan(8, 128)), maker = CString(raw.AsSpan(136, 64));
        return (lens.StartsWith(maker) || maker.Length == 0 ? lens : $"{maker} {lens}", BitConverter.ToInt32(raw, 4) != 0,
                (LensSource)BitConverter.ToInt32(raw, 200));
    }

    public static string CString(ReadOnlySpan<byte> bytes) {
        int end = bytes.IndexOf((byte)0);
        return System.Text.Encoding.UTF8.GetString(end < 0 ? bytes : bytes[..end]);
    }

    /// Engine.lensCatalogue, fetched once: it crosses the facade ~1,450 times.
    public static readonly Lazy<string[]> LensCatalogue = new(() =>
        Enumerable.Range(0, orion_lens_name_count()).Select(i => Text(orion_lens_name_at(i))).ToArray());
}
