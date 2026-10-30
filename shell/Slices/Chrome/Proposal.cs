using System.Text.Json.Nodes;
using System.Text.RegularExpressions;

namespace OrionShell.Chrome;

/// A port of the Swift app's Proposal.swift: data in, data out. Where an agent's proposal
/// lives, what it changes, and the phase machine that turns a filesystem
/// event into the actions ProposalWatcher performs, so the watcher is a thin
/// interpreter over the policy written down here.
static partial class Proposal {
    /// `<raw stem>.proposed.json` beside the raw: mcp/server.ts's proposedPath
    /// (`raw.replace(/\.[^.]+$/, ".proposed.json")`).
    public static string ProposedPath(string raw) =>
        Path.Combine(Path.GetDirectoryName(raw)!, Path.GetFileNameWithoutExtension(raw) + ".proposed.json");

    /// The top-level DevelopState keys whose values differ, sorted: what the footer names.
    public static List<string> ChangedKeys(JsonObject? from, JsonObject? to) =>
        from == null || to == null ? []
            : to.Select(p => p.Key).Where(k => !JsonNode.DeepEquals(from[k], to[k])).Order(StringComparer.Ordinal).ToList();

    static readonly Dictionary<string, string> Labels = new() {
        ["exposureEv"] = "Exposure", ["temperatureK"] = "Temperature", ["highlightRecovery"] = "Highlight recovery",
        ["fusion"] = "Shadow lift", ["gradeShadow"] = "Shadows grading", ["gradeMidtone"] = "Midtones grading",
        ["gradeHighlight"] = "Highlights grading", ["gradeBalance"] = "Grading balance", ["hueShift"] = "Color mixer hue",
        ["satShift"] = "Color mixer saturation", ["lumShift"] = "Color mixer luminance", ["curve"] = "Tone curve",
        ["denoiseLuma"] = "Luminance noise", ["denoiseColor"] = "Color noise", ["denoiseDetail"] = "Noise detail",
        ["denoiseNlm"] = "Non-local noise", ["denoiseSmoothness"] = "Color smoothness", ["texture"] = "Texture",
        ["sharpenAmount"] = "Sharpening", ["sharpenRadius"] = "Sharpening radius", ["sharpenMasking"] = "Sharpening masking",
        ["rotateQuarters"] = "Rotation", ["straightenDeg"] = "Straighten", ["cropX"] = "Crop", ["cropY"] = "Crop",
        ["cropW"] = "Crop", ["cropH"] = "Crop", ["maskComponents"] = "Mask shapes", ["layers"] = "Local adjustments",
        ["spots"] = "Spot removal", ["lutStrength"] = "LUT strength", ["vignetteAmount"] = "Vignette",
        ["vignetteFieldAngle"] = "Vignette field angle",
    };

    /// The names on the develop panels, not persistence keys: "hueShift" is "Color mixer hue",
    /// and a key with no name of its own splits at its humps ("lensChoice" is "Lens choice").
    public static string ProductLabel(string key) {
        if (Labels.TryGetValue(key, out var label)) return label;
        string words = Humps().Replace(key, "$1 $2");
        return words.Length == 0 ? words : char.ToUpperInvariant(words[0]) + words[1..].ToLowerInvariant();
    }

    [GeneratedRegex("([a-z])([A-Z])")] private static partial Regex Humps();

    // ── The phase machine ───────────────────────────────────────────────────

    /// Idle, or previewing with the keys the footer names.
    public sealed record Phase(IReadOnlyList<string>? Keys) {
        public static readonly Phase Idle = new((IReadOnlyList<string>?)null);
        public bool Previewing => Keys != null;
    }

    public enum EventKind { Appeared, Changed, Approved, Rejected, Vanished, SwitchedAway }

    /// What the watcher saw, or what a footer button did. `SidecarChanged`, for
    /// Vanished: the sidecar's develop now differs from the committed state,
    /// which means approve_edit ran in chat.
    public sealed record Event(EventKind Kind, IReadOnlyList<string>? Keys = null, bool SidecarChanged = false);

    public enum Act { CaptureOriginal, StopAutosave, RestoreProposed, SetCompare, RecordHistory, BeginAutosave,
                      NoteAndFlush, DeleteProposedFile, RestoreCommitted, RestoreSidecar, ClearCompare }

    /// One step, in order. Label is RecordHistory's.
    public sealed record Action(Act Kind, string? Label = null);

    /// The whole feature as a table, Proposal.transition's: an event against the
    /// phase it applies to gives the next phase and the actions, in order. An
    /// event that does not apply leaves the phase alone and performs nothing.
    ///
    /// ⚠ StopAutosave first: capturing the original renders, and with autosave
    /// armed a render notes a state that is not the photographer's (806a00d).
    /// ⚠ SetCompare before RestoreProposed: the restore's render refreshes the
    /// original, which must already know it is comparing, or it is re-taken
    /// against the proposal's own state.
    public static (Phase, List<Action>) Transition(Phase phase, Event e) {
        Action A(Act a, string? label = null) => new(a, label);
        List<Action> preview = [A(Act.StopAutosave), A(Act.CaptureOriginal), A(Act.SetCompare), A(Act.RestoreProposed)];
        switch (e.Kind) {
            case EventKind.Appeared:
                return (new Phase(e.Keys ?? []), preview);
            case EventKind.Changed when phase.Previewing:
                return (new Phase(e.Keys ?? []), preview);
            case EventKind.Approved when phase.Previewing:
                return (Phase.Idle, [A(Act.RecordHistory, $"Agent: {string.Join(", ", phase.Keys!)}"),
                                     A(Act.BeginAutosave), A(Act.NoteAndFlush), A(Act.DeleteProposedFile), A(Act.ClearCompare)]);
            case EventKind.Rejected when phase.Previewing:
                return (Phase.Idle, [A(Act.RestoreCommitted), A(Act.DeleteProposedFile), A(Act.BeginAutosave), A(Act.ClearCompare)]);
            case EventKind.Vanished when phase.Previewing:
                List<Action> acts = e.SidecarChanged
                    ? [A(Act.RestoreSidecar), A(Act.RecordHistory, "Agent: approved in chat")]
                    : [A(Act.RestoreCommitted)];
                return (Phase.Idle, [.. acts, A(Act.ClearCompare), A(Act.BeginAutosave)]);
            case EventKind.SwitchedAway when phase.Previewing:
                return (Phase.Idle, [A(Act.RestoreCommitted), A(Act.ClearCompare), A(Act.BeginAutosave)]);
            default:
                return (phase, []);
        }
    }
}
