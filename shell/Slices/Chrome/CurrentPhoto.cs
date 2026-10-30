using System.Globalization;
using System.Text.Json.Nodes;

namespace OrionShell.Chrome;

/// Editor.publishCurrentPhoto and Proposal.currentJSON: Orion says which photo
/// it is on in `current.json` beside its other files, so the MCP server's
/// current_photo (mcp/server.ts) knows what "this photo" means without being
/// told. Written atomically on every open and when the last photo goes; only
/// the app at a window writes it (AppFolder.Writable).
static class CurrentPhoto {
    static bool _installed;

    public static void Install() {
        if (_installed) return;
        _installed = true;
        var e = EngineModel.Shared;
        e.Opened += Publish;
        e.PropertyChanged += (_, p) => { if (p.PropertyName == nameof(EngineModel.Photos) && e.Photos.Count == 0) Publish(); };
        Publish();
    }

    static void Publish() {
        if (!AppFolder.Writable || AppFolder.File("current.json") is not { } file) return;
        var e = EngineModel.Shared;
        string? photo = e.Session?.Path;
        try {
            AppFolder.WriteAtomically(file, Json(photo, photo != null ? CaptureInfo.Read(e.Handle) : null, DateTime.UtcNow).ToJsonString());
        } catch (Exception ex) when (ex is IOException or UnauthorizedAccessException) {
            Console.Error.WriteLine($"orion: current.json not written ({ex.Message})");
        }
    }

    /// CurrentPhoto's encoding: photo and folder absent with no photo, `capture` only with one.
    public static JsonObject Json(string? photo, CaptureInfo? capture, DateTime now) {
        var o = new JsonObject();
        if (photo != null) (o["photo"], o["folder"]) = (photo, Path.GetDirectoryName(photo));
        o["updated"] = now.ToUniversalTime().ToString("yyyy-MM-dd'T'HH:mm:ss'Z'", CultureInfo.InvariantCulture);
        if (photo != null && capture != null) o["capture"] = Wire(capture);
        return o;
    }

    /// CaptureInfo.Wire: a field the file does not record is absent, never 0 or "".
    internal static JsonObject Wire(CaptureInfo c) {
        var o = new JsonObject();
        static double? Number(float f) => f > 0 && float.IsFinite(f) ? double.Parse(f.ToString("R", CultureInfo.InvariantCulture), CultureInfo.InvariantCulture) : null;
        if (c.Camera.Length > 0) o["camera"] = c.Camera;
        if (c.Lens.Length > 0) o["lens"] = c.Lens;
        foreach (var (key, value) in new[] { ("focalLength", c.FocalLength), ("aperture", c.Aperture), ("shutter", c.Shutter), ("iso", c.Iso) })
            if (Number(value) is { } n) o[key] = n;
        if (c.Captured is { } when) o["captured"] = when.ToUniversalTime().ToString("yyyy-MM-dd'T'HH:mm:ss'Z'", CultureInfo.InvariantCulture);
        return o;
    }
}
