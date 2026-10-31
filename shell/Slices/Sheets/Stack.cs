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
                                                  float[]? skyMatte, int matteWidth, int matteHeight, string output, int force);
    [LibraryImport(Native.Lib, StringMarshalling = StringMarshalling.Utf8)]
    private static partial int orion_stack_estimate(string[] paths, int count, int darkCount, int flatCount, out ulong bytes);
    [LibraryImport(Native.Lib)] private static partial int orion_available_memory(out ulong bytes);
    [LibraryImport(Native.Lib)] private static partial int orion_available_memory_override(ulong bytes);
    [LibraryImport(Native.Lib)] private static partial int orion_engine_stack_progress(nint engine, out float progress);
    [LibraryImport(Native.Lib)] private static partial int orion_engine_stack_cancel(nint engine);
    [LibraryImport(Native.Lib)] private static partial int orion_engine_stack_skipped(nint engine, out int skipped);

    // The stacking engine, while one runs. Read and cleared under Gate, so Progress and Cancel never
    // call into an engine Run's finally has destroyed.
    static readonly object Gate = new();
    static nint _running;

    /// `<reference stem>-stack.dng`, then -stack-2, -stack-3, never over a file.
    public static string OutputPath(string reference, Func<string, bool>? exists = null) {
        exists ??= File.Exists;
        string folder = Path.GetDirectoryName(reference)!, stem = Path.GetFileNameWithoutExtension(reference) + "-stack";
        string path = Path.Combine(folder, stem + ".dng");
        for (int n = 2; exists(path); n++) path = Path.Combine(folder, $"{stem}-{n}.dng");
        return path;
    }

    /// 0…1 while a stack runs, null otherwise.
    public static float? Progress { get { lock (Gate) return _running != 0 && orion_engine_stack_progress(_running, out float p) == 0 ? p : null; } }

    /// Test seam: the memory free the sheet's check sees, in place of the machine's (the selftest; --screenshot
    /// sets it from ORION_STACK_AVAILABLE). The engine's own check is not affected: see EngineAvailableOverride.
    public static ulong? AvailableOverride { get; set; }

    /// Test seam: what the engine's own check (and orion_available_memory) sees; 0 restores the OS.
    internal static void EngineAvailableOverride(ulong bytes) => orion_available_memory_override(bytes);

    /// What the stack would need and what is free now, off the UI thread and with no engine (the estimate reads
    /// metadata only); null when the frames will not read (the stack itself then says why). Free is 0 when the
    /// OS will not say.
    public static Task<(ulong Need, ulong Free)?> Memory(IReadOnlyList<string> paths, int darks, int flats) => Task.Run<(ulong, ulong)?>(() => {
        if (orion_stack_estimate(paths.ToArray(), paths.Count, darks, flats, out ulong need) != 0) return null;
        ulong free = AvailableOverride ?? (orion_available_memory(out ulong f) == 0 ? f : 0);
        return (need, free);
    });

    /// Test seam: whether the last Run was forced.
    public static bool LastForce { get; private set; }

    /// The last Run's OrionStatus (0 ok; 4 ORION_ERR_MEMORY: refused over the memory available, unforced).
    public static int LastStatus { get; private set; }
    public const int NotEnoughMemory = 4;

    /// Frames the last Run left out because they did not register.
    public static int Skipped { get; private set; }

    /// Stop: the stack fails at its next stage and leaves no partial file.
    public static void Cancel() { lock (Gate) if (_running != 0) orion_engine_stack_cancel(_running); }

    /// The stack, off the UI thread. Null when it wrote `output`, else the engine's reason.
    /// rejection: -1 auto, 0 none, 1 percentile, 2 sigma, 3 winsorized, 4 linear fit. skyMatte: Landscape only, upright, 1 = sky.
    /// force: run even over the memory available (else the engine refuses with a sentence).
    public static async Task<string?> Run(IReadOnlyList<string> paths, int reference, Mode mode, int rejection, bool weight,
                                          IReadOnlyList<string> darks, IReadOnlyList<string> flats,
                                          float[]? skyMatte, int matteWidth, int matteHeight, string output, bool force = false) {
        nint engine;
        lock (Gate) {
            if (_running != 0) return "A stack is already running.";
            if (orion_engine_create(out engine) != 0 || engine == 0) return "no engine for the stack";
            _running = engine;
        }
        try {
            Skipped = 0;
            LastForce = force;
            LastStatus = 0;
            return await Task.Run<string?>(() => {
                LastStatus = orion_engine_stack(engine, paths.ToArray(), paths.Count, reference, (int)mode, rejection, weight ? 1 : 0,
                        darks.Count > 0 ? darks.ToArray() : null, darks.Count, flats.Count > 0 ? flats.ToArray() : null, flats.Count,
                        skyMatte, matteWidth, matteHeight, output, force ? 1 : 0);
                if (LastStatus != 0)
                    return Marshal.PtrToStringUTF8(Native.orion_last_error(engine)) is { Length: > 0 } why ? why : "the stack failed";
                Skipped = orion_engine_stack_skipped(engine, out int skipped) == 0 ? skipped : 0;
                return null;
            });
        } finally {
            lock (Gate) _running = 0;
            orion_engine_destroy(engine);
        }
    }
}
