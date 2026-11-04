using System.Globalization;
using System.Reflection;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Text;

namespace OrionShell.Chrome;

/// What the feedback form is told about this machine and session, read at the
/// click and never in the background. Null is unknown, and leaves the field out.
/// Never a path, a file name, the user's name, a serial or any EXIF but the camera.
public sealed partial record FeedbackFacts(string Os, string Version, string? OsVersion = null, string? Chip = null, string? Gpu = null,
                                   int? RamGb = null, string? Display = null, string? Build = null, int? Minutes = null,
                                   int? Photos = null, string? Camera = null, string? Format = null) {
    static readonly CultureInfo C = CultureInfo.InvariantCulture;

    public static FeedbackFacts Now() {
        var e = EngineModel.Shared;
        string? photo = e.Current >= 0 ? e.Photos[e.Current] : null;
        return new(Feedback.Os, Updates.Version, SystemVersion(), Processor(), Adapter(e.Handle),
                   (int)Math.Round(GC.GetGCMemoryInfo().TotalAvailableMemoryBytes / (double)(1L << 30)), Screen(), Commit(),
                   (int)Math.Round(FeedbackReminder.Minutes), FeedbackReminder.Photos,
                   CaptureInfo.Read(e.Handle)?.Camera, photo == null ? null : Path.GetExtension(photo).TrimStart('.').ToUpperInvariant());
    }

    static string? SystemVersion() {
        var v = Environment.OSVersion.Version;
        if (OperatingSystem.IsMacOS()) return "macOS " + v.ToString(v.Build > 0 ? 3 : 2);
        if (OperatingSystem.IsWindows()) return "Windows " + v.ToString(3);
        return Lines("/etc/os-release").FirstOrDefault(l => l.StartsWith("PRETTY_NAME="))?["PRETTY_NAME=".Length..].Trim('"')
               ?? RuntimeInformation.OSDescription;
    }

    /// sysctl on macOS, the registry on Windows, /proc/cpuinfo on Linux (an ARM board's may name no model).
    static string? Processor() {
        if (OperatingSystem.IsMacOS()) {
            var name = new byte[256];
            nint size = name.Length;
            return sysctlbyname("machdep.cpu.brand_string", name, ref size, 0, 0) == 0 ? Text(name) : null;
        }
        if (OperatingSystem.IsWindows())
            return Microsoft.Win32.Registry.GetValue(@"HKEY_LOCAL_MACHINE\HARDWARE\DESCRIPTION\System\CentralProcessor\0",
                                                      "ProcessorNameString", null) as string;
        return Lines("/proc/cpuinfo").FirstOrDefault(l => l.StartsWith("model name"))?.Split(':', 2)[1].Trim();
    }

    /// The device the engine bound: Metal's name, or Dawn's adapter. Unknown with no photo open.
    static string? Adapter(nint engine) => engine != 0 && orion_engine_device_info(engine, out var d) == 0 ? Text(d.Name) : null;

    /// The main window's screen in device pixels and its scale: 3024x1964@2x.
    static string? Screen() => App.Window?.Screens.ScreenFromWindow(App.Window) is { } s
        ? $"{s.Bounds.Width}x{s.Bounds.Height}@{s.Scaling.ToString("0.##", C)}x" : null;

    /// The commit the .NET SDK writes after the + of the informational version, when the build had one.
    static string? Commit() => typeof(FeedbackFacts).Assembly.GetCustomAttribute<AssemblyInformationalVersionAttribute>()?.InformationalVersion
        .Split('+') is [_, { Length: >= 7 } sha] ? sha[..7] : null;

    static string[] Lines(string path) {
        try { return File.ReadAllLines(path); } catch (Exception e) when (e is IOException or UnauthorizedAccessException) { return []; }
    }

    static string Text(ReadOnlySpan<byte> s) {
        int end = s.IndexOf((byte)0);
        return Encoding.UTF8.GetString(end < 0 ? s : s[..end]).Trim();
    }

    [InlineArray(128)] struct Chars128 { byte _first; }

    /// OrionDeviceInfo in orion.h, field for field.
    [StructLayout(LayoutKind.Sequential)]
    struct OrionDeviceInfo {
        public Chars128 Name;
        public ulong RecommendedWorkingSet, MaxBufferLength;
        public int HasUnifiedMemory, SupportsApple7;
    }

    [LibraryImport(Native.Lib)]
    private static partial int orion_engine_device_info(nint engine, out OrionDeviceInfo info);

    [LibraryImport("libc", StringMarshalling = StringMarshalling.Utf8)]
    private static partial int sysctlbyname(string name, byte[] value, ref nint size, nint newValue, nint newSize);
}
