using System.Text.Json.Nodes;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Layout;
using Avalonia.Media;

namespace OrionShell.Sheets;

/// Copy's look onto every other photo in view: a port of the Swift app's SyncSettings.swift
/// and OrionApp+Files.swift's `runSync`, behind OrionApp.swift's counted alert.
///
/// The photos are not opened. Each sidecar's develop JSON is patched at the
/// level of its keys: only the keys the ticked groups own are written, so a
/// key a photo never had stays absent, and a photo with no sidecar keeps the
/// camera's white balance (Session.Restore fills it from the as-shot state)
/// instead of 5500 K nobody chose. The open photo takes the same groups
/// through the ordinary edit path, as one undo step, because its unsaved
/// state would overwrite a sidecar written behind its back.
static class SyncSettings {
    /// The develop JSON keys each PresetGroup owns, in PresetStore.Groups order.
    /// Hand-kept against PresetStore.Groups' C fields, as Swift keeps it against
    /// Preset.applied; the selftest holds the two to the same fields.
    public static string[] Keys(int group) => group switch {
        0 => ["temperatureK", "tint"],
        1 => ["exposureEv", "contrast", "highlights", "shadows", "whites", "blacks", "highlightRecovery"],
        2 => ["vibrance", "saturation", "hueShift", "satShift", "lumShift", "gradeShadow", "gradeMidtone", "gradeHighlight", "gradeBalance"],
        3 => ["curve"],
        4 => ["sharpenAmount", "sharpenRadius", "sharpenMasking", "denoiseLuma", "denoiseColor", "denoiseDetail", "denoiseNlm",
              "denoiseSmoothness", "lensDistortion", "lensVignette", "lensCaRed", "lensCaBlue"],
        5 => ["clarity", "texture", "dehaze", "fusion", "lutStrength", "grainAmount", "grainSize", "vignetteAmount", "vignetteFieldAngle"],
        _ => [],
    };

    /// The keys whose meaning `process` decides: absent means the first tone bands wherever one is nonzero.
    static readonly string[] ToneGated = ["highlights", "shadows", "whites", "blacks", "layers"];

    /// The source's keys over one target's stored develop JSON, keys sorted.
    /// Null when there is nothing to write. Throws FormatException when the
    /// stored blob is not a JSON object: Swift would overwrite it with the patch
    /// alone and lose the rest of that edit, so Sync leaves it and reports it.
    ///
    /// `process` rides along under AgentSync.swift's `committed` rule: only where
    /// the target never stated one and has no tone values of its own to
    /// reinterpret. Otherwise a photo with no sidecar would read the source's
    /// highlights under the first tone bands, as absent means (EditHistory.swift).
    public static byte[]? Patched(byte[]? stored, JsonObject source, IEnumerable<string> keys) {
        var target = new JsonObject();
        if (stored != null) {
            try { target = JsonNode.Parse(stored) as JsonObject ?? throw new System.Text.Json.JsonException(); }
            catch (System.Text.Json.JsonException) { throw new FormatException("the saved edit is not a JSON object"); }
        }
        var list = keys.ToList();
        if (list.Any(ToneGated.Contains) && !list.Contains("process") && target["process"] is null && !UsesToneBands(stored))
            list.Add("process");
        keys = list;
        bool any = false;
        foreach (var key in keys)
            if (source[key] is { } v) { target[key] = v.DeepClone(); any = true; }   // a key the source lacks is skipped, never nulled
        if (!any) return null;
        var sorted = new JsonObject();
        foreach (var (key, value) in target.OrderBy(p => p.Key, StringComparer.Ordinal)) sorted[key] = value?.DeepClone();
        return DevelopJson.Encode(sorted);
    }

    /// DevelopState.usesToneBands, on the target's own edit.
    static bool UsesToneBands(byte[]? stored) {
        if (stored == null || DevelopJson.Decode(stored) is not { } state) return false;
        string[] tone = ["highlights", "shadows", "whites", "blacks"];
        return tone.Any(k => DevelopJson.Float(state[k]) is { } v && v != 0)
            || ((JsonArray)state["layers"]!).Any(l => tone.Any(k => DevelopJson.Float(l![k]) is { } v && v != 0));
    }

    /// What a sync did: writes that landed, and the photos that were not written.
    public sealed record Outcome(int Written, List<string> Failed) {
        /// The status line's apology, or null when there is none to make.
        public string? Complaint => Failed.Count == 0 ? null
            : Failed.Count == 1 ? $"{Path.GetFileName(Failed[0])} could not be written."
            : $"{Failed.Count} photos could not be written, starting with {Path.GetFileName(Failed[0])}.";
    }

    /// Patches each photo's sidecar; none is opened. `read` and `write` default to the sidecar.
    public static Outcome Sync(JsonObject source, IReadOnlySet<int> groups, IEnumerable<string> photos,
                               Func<string, byte[]?>? read = null, Func<string, byte[], bool>? write = null) {
        read ??= p => Sidecar.Read(p)?.Develop;
        write ??= (p, data) => Sidecar.Merge(p, s => s.Develop = data);
        var keys = groups.Order().SelectMany(Keys).ToList();
        int written = 0;
        var failed = new List<string>();
        if (keys.Count == 0) return new(0, failed);
        foreach (var photo in photos) {
            byte[]? data;
            try { data = Patched(read(photo), source, keys); }
            catch (FormatException) { failed.Add(photo); continue; }
            if (data == null) continue;   // nothing to write is not a failure
            if (write(photo, data)) written++;
            else failed.Add(photo);
        }
        return new(written, failed);
    }

    /// `syncWarning`: the groups by name, and how many photos, from which list.
    public static string Warning(IReadOnlySet<int> groups, int photos) =>
        $"Writes {string.Join(", ", groups.Order().Select(g => PresetStore.Groups[g].Title))} to all {photos} photos in view. " +
        "This cannot be undone for photos other than the one open.";

    /// The counted question, built apart from showing it so the selftest can press its buttons.
    public static Window Dialog(Preset copied, JsonObject state, IReadOnlySet<int> groups) {
        int n = EngineModel.Shared.Photos.Count;
        var window = new Window {
            Title = $"Sync to {n} photos?", SizeToContent = SizeToContent.WidthAndHeight, CanResize = false,
            Background = Palette.Panel, Tag = "sync-confirmation",
        };
        StateButton Choice(string title, bool destructive, Action act) => new(null, title, () => { window.Close(); act(); }) {
            MinWidth = 96, Height = 28, Ink = destructive ? Palette.Reject : null,
        };
        var chosen = groups.ToHashSet();   // what was ticked when asked, not when answered
        window.Content = new StackPanel {
            Width = 320, Margin = new Thickness(Space.Lg), Spacing = Space.Md,
            Children = {
                new TextBlock { Text = window.Title, FontWeight = FontWeight.SemiBold, TextWrapping = TextWrapping.Wrap },
                new TextBlock { Text = Warning(chosen, n), Foreground = Palette.Dim, TextWrapping = TextWrapping.Wrap },
                new StackPanel { Orientation = Orientation.Horizontal, Spacing = Space.Sm, HorizontalAlignment = HorizontalAlignment.Right,
                                 Margin = new Thickness(0, Space.Sm, 0, 0), Children = {
                    Choice("Cancel", false, () => { }),
                    Choice("Sync", true, () => Run(copied, state, chosen)),
                } },
            },
        };
        return window;
    }

    public static void Confirm(Preset copied, JsonObject state, IReadOnlySet<int> groups) {
        if (App.Window is { } owner) Dialog(copied, state, groups).ShowDialog(owner);
    }

    /// `runSync`: every other photo's sidecar, then the open photo through the edit path.
    public static Outcome Run(Preset copied, JsonObject state, IReadOnlySet<int> groups) {
        var engine = EngineModel.Shared;
        Autosave.Shared.Flush();   // an owed write lands first, so nothing queued can land over the sync
        string? open = engine.Session?.Path;
        var outcome = Sync(state, groups, engine.Photos.Where(p => p != open).ToList());
        // Kept sessions and pictures hold the edits the sync just replaced: one opened later
        // would show the old look, and its autosave would write it back over the sync.
        engine.Cache.Clear();
        if (engine.Session != null) PresetStore.Apply(copied with { Name = "Sync", Groups = [.. groups] });
        string done = outcome.Written == 1 ? "Synced 1 other photo." : $"Synced {outcome.Written} other photos.";
        engine.Say(outcome.Complaint is { } complaint ? $"{done} {complaint}" : done);
        return outcome;
    }
}
