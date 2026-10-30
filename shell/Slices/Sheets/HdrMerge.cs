using System.Runtime.InteropServices;
using OrionShell.Chrome;

namespace OrionShell.Sheets;

/// HdrMergeFlow.swift's eligibility and naming, and HdrMergeDriver.swift's call:
/// two or more brackets into one floating-point DNG beside the reference.
///
/// The merge runs on an engine of its own, on a worker thread: the facade
/// documents orion_engine_hdr_merge as blocking and its progress and cancel as
/// safe from any thread, and an engine of its own keeps the shim's one-thread
/// rule for the session on screen, which the UI goes on rendering meanwhile.
static partial class HdrMerge {
    [LibraryImport(Native.Lib)] private static partial int orion_engine_create(out nint engine);
    [LibraryImport(Native.Lib)] private static partial void orion_engine_destroy(nint engine);
    [LibraryImport(Native.Lib, StringMarshalling = StringMarshalling.Utf8)]
    private static partial int orion_engine_hdr_merge(nint engine, string[] paths, int count, int reference, string output);
    [LibraryImport(Native.Lib)] private static partial int orion_engine_hdr_merge_progress(nint engine, out float progress);
    [LibraryImport(Native.Lib)] private static partial int orion_engine_hdr_merge_cancel(nint engine);

    static nint _running;   // the merging engine, while one runs


    /// outputURL: `<reference stem>-HDR.dng`, then -HDR-2, -HDR-3, never over a file.
    public static string OutputPath(string reference, Func<string, bool>? exists = null) {
        exists ??= File.Exists;
        string folder = Path.GetDirectoryName(reference)!, stem = Path.GetFileNameWithoutExtension(reference) + "-HDR";
        string path = Path.Combine(folder, stem + ".dng");
        for (int n = 2; exists(path); n++) path = Path.Combine(folder, $"{stem}-{n}.dng");
        return path;
    }

    /// 0…1 while a merge runs, null otherwise.
    public static float? Progress => _running != 0 && orion_engine_hdr_merge_progress(_running, out float p) == 0 ? p : null;

    /// Stop: the merge fails at its next stage and leaves no partial file.
    public static void Cancel() { if (_running != 0) orion_engine_hdr_merge_cancel(_running); }

    /// The merge, off the UI thread. Null when it wrote `output`, else the engine's reason.
    public static async Task<string?> Run(IReadOnlyList<string> paths, int reference, string output) {
        if (_running != 0) return "A merge is already running.";
        if (orion_engine_create(out nint engine) != 0 || engine == 0) return "no engine for the merge";
        _running = engine;
        try {
            return await Task.Run(() => orion_engine_hdr_merge(engine, paths.ToArray(), paths.Count, reference, output) == 0 ? null
                : Marshal.PtrToStringUTF8(Native.orion_last_error(engine)) is { Length: > 0 } why ? why : "the merge failed");
        } finally {
            _running = 0;
            orion_engine_destroy(engine);
        }
    }
}
