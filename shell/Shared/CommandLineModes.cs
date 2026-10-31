using System.Runtime.InteropServices;
using OrionShell.Sheets;

namespace OrionShell;

/// `--batch-export`, `--hdr-merge` and `--stack`: the product's batch and merge run from
/// the command line, so the features are run rather than only unit-tested
/// (the Swift app's BatchExportDriver and HdrMergeDriver, #121). Each asserts
/// its own outcome in its exit code: tools/check-modes.py runs all three.
static partial class CommandLineModes {
    /// `Orion --batch-export <folder> <photo> [photo...]`: each photo with its
    /// sidecar's edit, at the export sheet's defaults and with no watermark (the
    /// command line never reads the photographer's mark, #284). Exits 1 when
    /// any photo failed, so "nothing failed" is the only 0.
    public static int BatchExport(string[] args) {
        int i = Array.IndexOf(args, "--batch-export");
        if (i < 0 || i + 2 >= args.Length) { Console.Error.WriteLine("usage: Orion --batch-export <folder> <photo> [photo...]"); return 2; }
        string folder = Path.GetFullPath(args[i + 1]);
        Directory.CreateDirectory(folder);
        var settings = new ExportSettings();
        var jobs = ExportRun.Plan(args[(i + 2)..].Select(Path.GetFullPath), folder, settings.Extension);
        int failed = 0;
        foreach (var (source, destination) in jobs) {
            // Open then restore, for every photo: one without the other exports each frame unedited, or all of them with the first one's grade.
            using var s = Session.OpenEdited(source, headless: true);
            string? why = s == null ? "could not open it"
                : s.Unreadable ?? (orion_engine_image_size(s.Handle, out uint w, out uint h) == 0
                                   && orion_engine_export(s.Handle, destination, settings.Options(w, h)) == 0 ? null : s.LastError);
            if (why == null) Console.Error.WriteLine($"  {Path.GetFileName(destination),-24} {new FileInfo(destination).Length / 1024.0,7:0.0} KB");
            else { Console.Error.WriteLine($"  FAILED {Path.GetFileName(source)} — {why}"); failed++; }
        }
        Console.Error.WriteLine($"orion: exported {jobs.Count - failed} of {jobs.Count}");
        return failed == 0 ? 0 : 1;
    }

    /// `Orion --hdr-merge <output.dng> <frame> <frame> [frame...]`. Its own
    /// oracle: the file it claims to have written must open and render like
    /// any photo, not merely have been returned from.
    public static int HdrMergeMode(string[] args) {
        int i = Array.IndexOf(args, "--hdr-merge");
        if (i < 0 || i + 3 >= args.Length) { Console.Error.WriteLine("usage: Orion --hdr-merge <output.dng> <frame> <frame> [frame...]"); return 2; }
        string output = Path.GetFullPath(args[i + 1]);
        var frames = args[(i + 2)..].Select(Path.GetFullPath).ToList();
        // Started from the pool: awaited on this thread, Run's continuation would
        // queue to Avalonia's dispatcher, which nothing pumps here — a deadlock.
        if (Task.Run(() => HdrMerge.Run(frames, 0, output)).GetAwaiter().GetResult() is { } why) { Console.Error.WriteLine($"orion: merge failed: {why}"); return 1; }
        using var merged = Session.Open(output, headless: true);
        if (merged == null || merged.Render(preview: false) != Graph.Full) {
            Console.Error.WriteLine($"orion: merged file does not open: {merged?.LastError ?? output}");
            return 1;
        }
        Console.Error.WriteLine($"orion: merged {frames.Count} frames into {output}");
        return 0;
    }

    /// `Orion --stack <output.dng> [--mode noise|stars|landscape] <frame> <frame> [frame...]`: frame 0 is the
    /// reference, rejection automatic, no weighting, no calibration. Landscape has no matte here, so the
    /// engine's refusal is the answer. Like --hdr-merge, the file it wrote must open and render.
    public static int StackMode(string[] args) {
        const string usage = "usage: Orion --stack <output.dng> [--mode noise|stars|landscape] [--force] <frame> <frame> [frame...]";
        int i = Array.IndexOf(args, "--stack");
        if (i < 0 || i + 1 >= args.Length) { Console.Error.WriteLine(usage); return 2; }
        string output = Path.GetFullPath(args[i + 1]);
        var rest = args[(i + 2)..].ToList();
        var mode = Stack.Mode.Noise;
        if (rest.IndexOf("--mode") is var m and >= 0) {
            if (m + 1 >= rest.Count || !Enum.GetNames<Stack.Mode>().Contains(rest[m + 1], StringComparer.OrdinalIgnoreCase)
                || !Enum.TryParse(rest[m + 1], true, out mode)) { Console.Error.WriteLine(usage); return 2; }
            rest.RemoveRange(m, 2);
        }
        bool force = rest.Remove("--force");   // run even over the memory available
        var frames = rest.Select(Path.GetFullPath).ToList();
        if (frames.Count < 2) { Console.Error.WriteLine(usage); return 2; }
        // Task.Run for the reason HdrMergeMode gives.
        if (Task.Run(() => Stack.Run(frames, 0, mode, -1, false, [], [], null, 0, 0, output, force)).GetAwaiter().GetResult() is { } why) {
            Console.Error.WriteLine($"orion: stack failed: {why}" + (Stack.LastStatus == Stack.NotEnoughMemory ? " (--force does that here.)" : ""));
            return 1;
        }
        using var stacked = Session.Open(output, headless: true);
        if (stacked == null || stacked.Render(preview: false) != Graph.Full) {
            Console.Error.WriteLine($"orion: stacked file does not open: {stacked?.LastError ?? output}");
            return 1;
        }
        Console.Error.WriteLine($"orion: stacked {frames.Count} frames into {output}");
        return 0;
    }

    [LibraryImport(Native.Lib)]
    private static partial int orion_engine_image_size(nint engine, out uint width, out uint height);

    [LibraryImport(Native.Lib, StringMarshalling = StringMarshalling.Utf8)]
    private static partial int orion_engine_export(nint engine, string path, in SheetsNative.ExportOptions options);
}
