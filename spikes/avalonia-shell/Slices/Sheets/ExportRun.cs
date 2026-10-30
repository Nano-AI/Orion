using System.Runtime.InteropServices;
using Avalonia.Platform.Storage;
using Avalonia.Threading;
using OrionShell.Library;

namespace OrionShell.Sheets;

/// The writing half of OrionApp+Files.swift: `exportFile` (the sheet's Export
/// button) and `runBatchExport` with BatchExport.swift's plan, progress and Stop.
///
/// The shell keeps no sidecars, so a photo that is not open exports as it opens
/// (the default look); the open one exports with its edits in memory.
static partial class ExportRun {
    [LibraryImport(Native.Lib, StringMarshalling = StringMarshalling.Utf8)]
    private static partial int orion_engine_export(nint engine, string path, in SheetsNative.ExportOptions options);

    /// Done of total while a batch runs, else null: the toolbar's readout and Stop.
    public static (int Done, int Total)? Progress { get; private set; }
    public static event Action? Changed;
    static bool _cancelled;

    static EngineModel E => EngineModel.Shared;
    static ExportSettings S => ExportSettings.Shared;
    static string Extension => S.Extension;

    /// The photo's own options: its rating and the metadata the sheet chose.
    static SheetsNative.ExportOptions Options(nint engine, int rating) {
        uint w = 0, h = 0;
        SheetsNative.orion_engine_image_size(engine, out w, out h);
        return S.Options(w, h) with { Rating = rating, Metadata = ExportSettings.Metadatas[S.Metadata].Code };
    }

    /// With the photographer's watermark, when it is on (WatermarkRaster.Marked).
    static string? Write(nint engine, string path, int rating) {
        SheetsNative.orion_engine_image_size(engine, out uint w, out uint h);
        return WatermarkRaster.Marked(Options(engine, rating), S.Dimensions(w, h), o => orion_engine_export(engine, path, o)) == 0 ? null
            : Marshal.PtrToStringUTF8(Native.orion_last_error(engine)) ?? "the engine refused it";
    }

    static IStorageProvider? Picker => App.Window?.StorageProvider is { CanSave: true } p ? p : null;

    /// `exportFile`: a save panel named after the photo, then one write.
    public static async void ExportOpen() {
        if (E.Handle == 0 || Picker is not { } picker) return;
        string stem = Path.GetFileNameWithoutExtension(E.Photos[E.Current]);
        var file = await picker.SaveFilePickerAsync(new FilePickerSaveOptions {
            Title = "Export", SuggestedFileName = $"{stem}.{Extension}", DefaultExtension = Extension,
        });
        if (file?.TryGetLocalPath() is not { } path || E.Handle == 0) return;
        if (Write(E.Handle, path, LibraryModel.Shared.Open?.Rating ?? 0) is { } why) E.Say(why);
    }

    /// `exportTargets`: the selection, or what is in view, without the rejects
    /// unless they were asked for, by selecting them or by the Rejected filter.
    public static List<LibraryModel.Photo> Targets() {
        var library = LibraryModel.Shared;
        bool asked = library.HasExplicitSelection || library.Current == LibraryModel.Filter.Rejected;
        return library.Targets.Where(p => !p.Rejected || asked).ToList();
    }

    /// BatchExport.plan: `stem.ext`, then `stem-2.ext`, never over a file already there.
    public static List<(string Source, string Destination)> Plan(IEnumerable<string> sources, string folder, string ext,
                                                                 Func<string, bool>? exists = null) {
        exists ??= File.Exists;
        var taken = new HashSet<string>();
        var jobs = new List<(string, string)>();
        foreach (var source in sources) {
            string stem = Path.GetFileNameWithoutExtension(source), candidate = Path.Combine(folder, $"{stem}.{ext}");
            for (int n = 2; taken.Contains(candidate) || exists(candidate); n++) candidate = Path.Combine(folder, $"{stem}-{n}.{ext}");
            taken.Add(candidate);
            jobs.Add((source, candidate));
        }
        return jobs;
    }

    /// `runBatchExport`: a folder, then one photo at a time on the UI thread,
    /// yielding between them so the readout paints and Stop is heard.
    public static async void ExportAll() {
        var targets = Targets();
        int skipped = LibraryModel.Shared.Targets.Count - targets.Count;
        if (E.Handle == 0 || targets.Count == 0 || Progress != null || App.Window?.StorageProvider is not { CanOpen: true } picker) return;
        // The skipped count is said on the panel that chooses the folder: the last moment before files are written.
        string rejects = skipped == 0 ? "" : skipped == 1 ? " 1 rejected photo is not included." : $" {skipped} rejected photos are not included.";
        var chosen = await picker.OpenFolderPickerAsync(new FolderPickerOpenOptions {
            Title = $"Export Here: choose a folder for {targets.Count} exported photos.{rejects}", AllowMultiple = false,
        });
        if (chosen.Count == 0 || chosen[0].TryGetLocalPath() is not { } folder) return;
        E.Say(await Run(Plan(targets.Select(p => p.Path), folder, Extension)));
    }

    /// BatchExport.run and its summary.
    public static async Task<string> Run(List<(string Source, string Destination)> jobs) {
        var ratings = LibraryModel.Shared.Photos.ToDictionary(p => p.Path, p => p.Rating);
        int written = 0, failed = 0;
        _cancelled = false;
        for (int i = 0; i < jobs.Count && !_cancelled; i++) {
            Report(i, jobs.Count);
            await Dispatcher.UIThread.InvokeAsync(() => { }, DispatcherPriority.Background);
            var (source, destination) = jobs[i];
            bool open = E.Current >= 0 && E.Photos[E.Current] == source && E.Session != null;   // not while it decodes (#318)
            var session = open ? E.Session : Session.OpenEdited(source);
            try {
                if (session == null || Write(session.Handle, destination, ratings.GetValueOrDefault(source)) != null) failed++;
                else written++;
            } finally {
                if (!open) session?.Dispose();
            }
        }
        bool stopped = _cancelled && written + failed < jobs.Count;
        Progress = null;
        Changed?.Invoke();
        if (written == 0 && failed == 0 && !stopped) return "Nothing to export.";
        var parts = new List<string> { written == 1 ? "Exported 1 photo" : $"Exported {written} photos" };
        if (failed > 0) parts.Add(failed == 1 ? "1 failed" : $"{failed} failed");
        if (stopped) parts.Add("stopped early");
        return string.Join(", ", parts) + ".";
    }

    static void Report(int done, int total) { Progress = (done, total); Changed?.Invoke(); }

    /// Stop: the photo being written finishes, then the batch ends.
    public static void Stop() => _cancelled = true;
}
