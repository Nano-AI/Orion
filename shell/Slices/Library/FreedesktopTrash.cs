using System.Globalization;
using System.Runtime.InteropServices;

namespace OrionShell.Library;

/// The Trash on Linux, as the freedesktop.org Trash specification 1.0 lays it out
/// (https://specifications.freedesktop.org/trash-spec/1.0/), so a file manager can
/// show and restore what Orion put there (#394). A file on the home trash's
/// filesystem goes to $XDG_DATA_HOME/Trash; one on another mount to that mount's
/// $topdir/.Trash/$uid (only when the administrator made .Trash, sticky and not a
/// link) or else $topdir/.Trash-$uid. Each move is a rename, never a copy, and a
/// file that cannot be renamed stays where it is.
[System.Runtime.Versioning.SupportedOSPlatform("linux")]
static partial class FreedesktopTrash {
    const UnixFileMode Private = UnixFileMode.UserRead | UnixFileMode.UserWrite | UnixFileMode.UserExecute;

    /// Null when it went, else why not.
    public static string? Move(string path) {
        string full = Path.GetFullPath(path);
        if (!File.Exists(full) && !Directory.Exists(full)) return "it is no longer there";
        string folder = RealPath(Path.GetDirectoryName(full)!), top = MountOf(folder);
        string data = Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData, Environment.SpecialFolderOption.DoNotVerify);
        if (data.Length > 0 && MountOf(Existing(data)) == top)
            return Into(Path.Combine(data, "Trash"), full, full);   // the home trash: the original path is absolute
        // Another mount: a path relative to its top directory, so the trash survives being mounted elsewhere.
        string relative = Path.GetRelativePath(top, Path.Combine(folder, Path.GetFileName(full)));
        string shared = Path.Combine(top, ".Trash");
        if (Directory.Exists(shared) && new DirectoryInfo(shared).LinkTarget == null && File.GetUnixFileMode(shared).HasFlag(UnixFileMode.StickyBit)
            && Into(Path.Combine(shared, geteuid().ToString(CultureInfo.InvariantCulture)), full, relative) is null) return null;
        string own = Path.Combine(top, $".Trash-{geteuid()}");
        if (Directory.Exists(own) && new DirectoryInfo(own).LinkTarget != null) return $"{own} is a link, which the Trash may not be";
        return Into(own, full, relative);
    }

    /// The .trashinfo first, claimed by creating it (O_EXCL), then the rename beside it:
    /// a name another file already holds is never written over.
    static string? Into(string trash, string full, string original) {
        string files = Path.Combine(trash, "files"), info = Path.Combine(trash, "info");
        try {
            foreach (var dir in new[] { trash, files, info }) Directory.CreateDirectory(dir, Private);
        } catch (Exception e) when (e is IOException or UnauthorizedAccessException) {
            return $"there is no Trash Orion may use on this disk ({e.Message})";
        }
        string name = Path.GetFileName(full), stem = Path.GetFileNameWithoutExtension(name), ext = Path.GetExtension(name);
        string text = $"[Trash Info]\nPath={Uri.EscapeDataString(original).Replace("%2F", "/")}\n"
                    + $"DeletionDate={DateTime.Now.ToString("yyyy-MM-ddTHH:mm:ss", CultureInfo.InvariantCulture)}\n";
        for (int n = 1; n < 10_000; n++) {
            string candidate = n == 1 ? name : $"{stem}.{n}{ext}", record = Path.Combine(info, candidate + ".trashinfo");
            try {
                using (var stream = new FileStream(record, FileMode.CreateNew, FileAccess.Write)) stream.Write(System.Text.Encoding.UTF8.GetBytes(text));
            } catch (IOException) when (File.Exists(record)) {
                continue;   // taken: the next name
            } catch (Exception e) when (e is IOException or UnauthorizedAccessException) {
                return $"the Trash will not take it ({e.Message})";
            }
            string target = Path.Combine(files, candidate);
            try {
                if (File.Exists(target) || Directory.Exists(target)) { File.Delete(record); continue; }   // a stray file without its record
                if (Directory.Exists(full)) Directory.Move(full, target);
                else File.Move(full, target);
                return null;
            } catch (Exception e) when (e is IOException or UnauthorizedAccessException) {
                File.Delete(record);
                return $"it could not be moved into the Trash ({e.Message})";
            }
        }
        return "the Trash has no free name for it";
    }

    /// The deepest mount point holding `path` (a real path), from /proc/self/mountinfo,
    /// whose fifth field is the mount point with spaces and the like written \ooo.
    static string MountOf(string path) {
        string best = "/";
        foreach (var line in File.ReadLines("/proc/self/mountinfo")) {
            var fields = line.Split(' ');
            if (fields.Length < 5) continue;
            string mount = System.Text.RegularExpressions.Regex.Replace(fields[4], @"\\([0-7]{3})", m => ((char)Convert.ToInt32(m.Groups[1].Value, 8)).ToString());
            if (mount.Length > best.Length && (path == mount || path.StartsWith(mount + "/"))) best = mount;
        }
        return best;
    }

    /// The nearest folder of `path` that exists, resolved: where the home trash will be made.
    static string Existing(string path) {
        while (!Directory.Exists(path) && Path.GetDirectoryName(path) is { } up) path = up;
        return RealPath(path);
    }

    static string RealPath(string path) {
        nint resolved = realpath(path, 0);
        if (resolved == 0) return path;
        try { return Marshal.PtrToStringUTF8(resolved)!; } finally { free(resolved); }
    }

    [LibraryImport("libc", StringMarshalling = StringMarshalling.Utf8)] private static partial nint realpath(string path, nint resolved);
    [LibraryImport("libc")] private static partial void free(nint pointer);
    [LibraryImport("libc")] private static partial uint geteuid();
}
