import Foundation

/// Copy, paste and sync — one photograph's settings applied to others.
///
/// Reuses `PresetGroup` and `Preset.applied(to:)` wholesale. A paste *is* a
/// preset that was never named: the same patch-not-state question, with the
/// groups chosen at paste time instead of at save time. Two implementations of
/// "apply some groups over a state" would be two places to fix the day a field
/// is added.
///
/// ## ⚠ Sync does not open the photographs it writes to
///
/// The obvious implementation opens each target, applies, saves and moves on.
/// At roughly a quarter-second of RAW decode apiece that is a progress bar over
/// a folder of three hundred, and every one of those decodes is thrown away.
///
/// The sidecar is the source of truth (`CLAUDE.md`), so sync edits it directly.
/// But not by decoding it into a `DevelopState` and re-encoding — and that is
/// the part worth reading twice.
///
/// **A photograph with no sidecar has no stored white balance**, because its
/// white balance is whatever the camera recorded and is only known once the
/// file is decoded. Decode the sidecar into a `DevelopState` and the missing
/// keys come back as the struct's *defaults* — 5500 K, tint 0 — and writing
/// that back would silently rewhite-balance every untouched photograph in the
/// selection to a number nobody chose.
///
/// So the patch is applied **at the level of the JSON keys**: read the target's
/// develop object as a dictionary, overwrite only the keys the pasted groups
/// cover, write it back. A key the paste does not mention and the target never
/// had stays absent, and stays absent all the way to `Engine.open`, which fills
/// it from the camera.
enum SyncSettings {

    /// The JSON keys each group owns.
    ///
    /// ⚠ Hand-written, and it has to match `Preset.applied(to:)` field for
    /// field — the two are the same decision expressed against a struct and
    /// against its encoding. `orion-viewport-tests` asserts they agree by
    /// applying both to the same state and comparing, which is the only thing
    /// that will notice when someone adds a field to one and not the other.
    static func keys(for group: PresetGroup) -> [String] {
        switch group {
        case .whiteBalance:
            return ["temperatureK", "tint"]
        case .light:
            return ["exposureEv", "contrast", "highlights", "shadows",
                    "whites", "blacks", "highlightRecovery"]
        case .colour:
            return ["vibrance", "saturation", "hueShift", "satShift", "lumShift",
                    "gradeShadow", "gradeMidtone", "gradeHighlight",
                    "gradeBalance"]
        case .curve:
            return ["curve"]
        case .detail:
            return ["sharpenAmount", "sharpenRadius", "sharpenMasking",
                    "denoiseLuma", "denoiseColor",
                    "lensDistortion", "lensVignette", "lensCaRed", "lensCaBlue"]
        case .effects:
            return ["clarity", "dehaze", "fusion", "lutStrength",
                    "grainAmount", "grainSize",
                    "vignetteAmount", "vignetteFieldAngle"]
        }
    }

    /// Every key any group owns — the ones a paste may ever write.
    static var allSyncableKeys: Set<String> {
        Set(PresetGroup.allCases.flatMap { keys(for: $0) })
    }

    /// Merges the source's chosen groups into one target's stored develop JSON.
    ///
    /// `stored` is the target's existing develop data, or nil when it has none.
    /// Returns the JSON to store, or nil when there is nothing to write.
    static func patched(stored: Data?, source: DevelopState,
                        groups: Set<PresetGroup>) -> Data? {
        guard !groups.isEmpty else { return nil }
        guard let sourceData = try? JSONEncoder().encode(source),
              let sourceObj = try? JSONSerialization.jsonObject(with: sourceData)
                as? [String: Any]
        else { return nil }

        var target: [String: Any] = [:]
        if let stored,
           let obj = try? JSONSerialization.jsonObject(with: stored) as? [String: Any] {
            target = obj
        }

        for group in groups {
            for key in keys(for: group) {
                // A key the *source* does not carry is skipped rather than
                // written as null. Encoding a `DevelopState` always produces
                // every key, so this only fires if the two drift — and writing
                // null would put a value into the target that decodes to
                // nothing and reads as corruption.
                guard let v = sourceObj[key] else { continue }
                target[key] = v
            }
        }

        return try? JSONSerialization.data(withJSONObject: target,
                                           options: [.sortedKeys])
    }

    /// What a paste does to the photo that is currently open.
    ///
    /// The in-memory path, where the state is known in full and there is no
    /// as-shot question to worry about — so it goes through the same
    /// `Preset.applied(to:)` a saved look does.
    static func pasted(source: DevelopState, onto base: DevelopState,
                       groups: Set<PresetGroup>) -> DevelopState {
        Preset(name: "Paste", groups: groups, state: source).applied(to: base)
    }

    /// Syncs to a list of photographs, none of which is opened.
    ///
    /// Returns how many were written. The currently open photo is the caller's
    /// problem — it has unsaved state in memory, so writing its sidecar behind
    /// its back would be overwritten by the next autosave.
    @discardableResult
    static func sync(source: DevelopState, groups: Set<PresetGroup>,
                     to photos: [URL],
                     read: (URL) -> Data? = { Sidecar.read(for: $0)?.develop },
                     write: (URL, Data) -> Void = { url, data in
                         Sidecar.merge(into: url) { $0.develop = data }
                     }) -> Int {
        guard !groups.isEmpty else { return 0 }
        var written = 0
        for photo in photos {
            guard let data = patched(stored: read(photo), source: source,
                                     groups: groups) else { continue }
            write(photo, data)
            written += 1
        }
        return written
    }
}
