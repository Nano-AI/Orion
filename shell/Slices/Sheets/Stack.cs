using System.Runtime.InteropServices;
using OrionShell.Chrome;

namespace OrionShell.Sheets;

/// The native call behind stacking (#391): frames of one scene into one linear DNG beside the
/// reference. As HdrMerge: an engine of its own on a worker thread, so the UI goes on rendering,
/// with progress and cancel from any thread.
static partial class Stack {
    public enum Mode { Noise = 0, Stars = 1, Landscape = 2 }

    [LibraryImport(Native.Lib)] private static partial int orion_engine_create(out nint engine);
    [LibraryImport(Native.Lib)] private static partial void orion_engine_destroy(nint engine);
    [LibraryImport(Native.Lib, StringMarshalling = StringMarshalling.Utf8)]
    private static partial int orion_engine_stack(nint engine, string[] paths, int count, int reference, int mode, int rejection, int weight,
                                                  string[]? darks, int darkCount, string[]? flats, int flatCount,
                                                  float[]? skyMatte, int matteWidth, int matteHeight, string output);
    [LibraryImport(Native.Lib)] private static partial int orion_engine_stack_progress(nint engine, out float progress);
    [LibraryImport(Native.Lib)] private static partial int orion_engine_stack_cancel(nint engine);

    static nint _running;   // the stacking engine, while one runs

    /// `<reference stem>-stack.dng`, then -stack-2, -stack-3, never over a file.
    public static string OutputPath(string reference, Func<string, bool>? exists = null) {
        exists ??= File.Exists;
        string folder = Path.GetDirectoryName(reference)!, stem = Path.GetFileNameWithoutExtension(reference) + "-stack";
        string path = Path.Combine(folder, stem + ".dng");
        for (int n = 2; exists(path); n++) path = Path.Combine(folder, $"{stem}-{n}.dng");
        return path;
    }

    /// 0…1 while a stack runs, null otherwise.
    public static float? Progress => _running != 0 && orion_engine_stack_progress(_running, out float p) == 0 ? p : null;

    /// Stop: the stack fails at its next stage and leaves no partial file.
    public static void Cancel() { if (_running != 0) orion_engine_stack_cancel(_running); }

    /// The stack, off the UI thread. Null when it wrote `output`, else the engine's reason.
    /// rejection: -1 auto, 0 none, 1 percentile, 2 sigma, 3 winsorized, 4 linear fit. skyMatte: Landscape only, upright, 1 = sky.
    public static async Task<string?> Run(IReadOnlyList<string> paths, int reference, Mode mode, int rejection, bool weight,
                                          IReadOnlyList<string> darks, IReadOnlyList<string> flats,
                                          float[]? skyMatte, int matteWidth, int matteHeight, string output) {
        if (_running != 0) return "A stack is already running.";
        if (orion_engine_create(out nint engine) != 0 || engine == 0) return "no engine for the stack";
        _running = engine;
        try {
            return await Task.Run(() => orion_engine_stack(engine, paths.ToArray(), paths.Count, reference, (int)mode, rejection, weight ? 1 : 0,
                    darks.Count > 0 ? darks.ToArray() : null, darks.Count, flats.Count > 0 ? flats.ToArray() : null, flats.Count,
                    skyMatte, matteWidth, matteHeight, output) == 0 ? null
                : Marshal.PtrToStringUTF8(Native.orion_last_error(engine)) is { Length: > 0 } why ? why : "the stack failed");
        } finally {
            _running = 0;
            orion_engine_destroy(engine);
        }
    }
}
