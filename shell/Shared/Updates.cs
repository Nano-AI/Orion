using System.Runtime.InteropServices;
using Avalonia.Threading;
using Velopack;
using Velopack.Sources;

namespace OrionShell;

/// Updates from GitHub Releases through Velopack (#389): one silent check a few
/// seconds after an interactive launch, the download in the background, then one
/// notice in the footer with a Restart button (Chrome.UpdateNotice). Pre-releases
/// count, since every Orion build so far is one. A build Velopack did not install
/// (`dotnet run`, bin/, the headless modes) checks nothing, and nor does the
/// Microsoft Store's MSIX (tools/package-msix.sh): the Store updates it, and its
/// folder is read-only.
public static class Updates {
    public const string Repo = "https://github.com/Nano-AI/Orion";
    public const string NotInstalled = "Updates come with the installed app.";
    public const string FromStore = "The Microsoft Store updates Orion.";
    public const string Failed = "Couldn't check for updates: ";

    /// Running from an MSIX, with a package identity. Settable for the selftest.
    public static bool Packaged { get; set; } = PackageFullName() != null;

    static UpdateManager? _manager;

    /// The version Velopack installed, the release tag without its v (0.6.0-alpha.2); the MSIX's
    /// from its full name (Orion_6.0.999.0_x64__hash); "dev" for a build neither installed.
    /// Read when first asked, after VelopackApp's Run: Program.Main reads Packaged before it.
    public static string Version => _version ??=
        (Velopack.Locators.VelopackLocator.IsCurrentSet ? Velopack.Locators.VelopackLocator.Current.CurrentlyInstalledVersion?.ToString() : null)
        ?? PackageFullName()?.Split('_')[1] ?? "dev";
    static string? _version;

    /// Downloaded and waiting for a restart; Velopack also applies it on the next launch.
    public static UpdateInfo? Ready { get; private set; }
    public static event Action? Changed;

    /// The interactive launch: off the UI thread, after the window is up. Silent either way.
    public static void Start() => Task.Delay(TimeSpan.FromSeconds(5)).ContinueWith(_ => Check());

    /// "Check for updates": the answer goes on the status line.
    public static void CheckNow() => Check().ContinueWith(t => Dispatcher.UIThread.Post(() => EngineModel.Shared.Say(t.Result)));

    /// Checks, downloads what it finds, and says what happened. Never throws: offline is an answer.
    public static async Task<string> Check(UpdateManager? manager = null) {
        if (Packaged) return FromStore;
        try {
            manager ??= _manager ??= new UpdateManager(new GithubSource(Repo, null, prerelease: true));
            if (!manager.IsInstalled) return NotInstalled;
            if (await manager.CheckForUpdatesAsync().ConfigureAwait(false) is not { } info) return "Orion is up to date.";
            await manager.DownloadUpdatesAsync(info).ConfigureAwait(false);
            Dispatcher.UIThread.Post(() => { Ready = info; Changed?.Invoke(); });
            return $"Orion {info.TargetFullRelease.Version} is ready. Restart to apply.";
        } catch (Exception e) {
            return Failed + e.Message;
        }
    }

    /// Writes what is owed (Velopack exits without the window's Exit event), then swaps the app and starts it again.
    public static void Restart() {
        if (Ready is not { } info || _manager is not { } manager) return;
        Autosave.Shared.Flush();
        manager.ApplyUpdatesAndRestart(info.TargetFullRelease);
    }

    /// Null when the process has no package identity, and off Windows.
    static string? PackageFullName() {
        if (!OperatingSystem.IsWindows()) return null;
        uint length = 0;
        if (GetCurrentPackageFullName(ref length, null) != 122) return null;   // not ERROR_INSUFFICIENT_BUFFER: APPMODEL_ERROR_NO_PACKAGE
        var name = new char[length];
        return GetCurrentPackageFullName(ref length, name) == 0 ? new string(name, 0, (int)length - 1) : null;
    }

    [DllImport("kernel32.dll", CharSet = CharSet.Unicode)]
    static extern int GetCurrentPackageFullName(ref uint length, char[]? name);
}
