import Foundation

/// Saved looks — a named set of adjustments that can be dropped onto any photo.
///
/// ## A preset is a patch, not a state
///
/// ⚠ This is the decision the whole file turns on. A preset stores a full
/// `DevelopState` *and* the set of groups it is allowed to touch, and applying
/// it copies only those groups. The alternative — a preset that is simply a
/// state, assigned wholesale — is one line shorter and wrong in a way that only
/// shows up in use: dropping a black-and-white look onto a photograph would
/// silently reset its exposure, its crop and its dust spots, because those
/// fields are *in* the state whether the preset meant anything by them or not.
///
/// Lightroom's presets behave the same way and for the same reason. A look is
/// "make the color like this", not "make the photograph like this".
///
/// ## What a preset can never carry
///
/// Four things are excluded from every group, and none of it is an oversight:
///
/// - **Crop, straighten and rotation.** A preset that reframed every photograph
///   it touched would be unusable, and the geometry belongs to the shot.
/// - **Spot removal.** Dust is at a position on one sensor on one day.
/// - **Masks.** A gradient placed on one photograph's horizon means nothing on
///   another's. Worth revisiting if presets ever grow a "sky darken" that is
///   generated per photo rather than copied.
/// - **White balance as shot.** Temperature and tint *are* offerable, but the
///   camera's own reading is the starting point for the photograph rather than
///   part of a look, so the White Balance group is off by default.
enum PresetGroup: String, Codable, CaseIterable, Identifiable {
    case whiteBalance
    case light
    case color
    case curve
    case detail
    case effects

    /// What `.color` was spelled before decision #112, as it still sits in
    /// every `presets.json` written before it. Not a case: a seventh case would
    /// join `allCases`, put a duplicate "Color" row in the save sheet, and let
    /// two groups that mean the same thing disagree inside one `Set`.
    private static let legacyColourRawValue = "colour"

    /// ⚠ Reading a group is the one place in this file where a single unknown
    /// string used to cost every saved preset at once. `PresetStore.load`
    /// decoded the whole array or nothing, so a `presets.json` carrying the old
    /// `"colour"` would have thrown on element one and left the photographer
    /// with the four built-ins and no explanation. Both spellings are accepted
    /// here, and `PresetStore.load` now survives an element it cannot read at
    /// all — decision #112.
    ///
    /// Encoding stays synthesised, so only `"color"` is ever written.
    init(from decoder: Decoder) throws {
        let raw = try decoder.singleValueContainer().decode(String.self)
        if raw == PresetGroup.legacyColourRawValue {
            self = .color
        } else if let v = PresetGroup(rawValue: raw) {
            self = v
        } else {
            throw DecodingError.dataCorruptedError(
                in: try decoder.singleValueContainer(),
                debugDescription: "unknown preset group \"\(raw)\"")
        }
    }

    var id: String { rawValue }

    var title: String {
        switch self {
        case .whiteBalance: "White Balance"
        case .light:        "Light"
        case .color:        "Color"
        case .curve:        "Curve"
        case .detail:       "Detail"
        case .effects:      "Effects"
        }
    }

    /// What the group covers, for the interface — so a photographer can tell
    /// what a preset will disturb before applying it.
    var covers: String {
        switch self {
        case .whiteBalance: "temperature, tint"
        case .light:        "exposure, contrast, highlights, shadows, whites, blacks"
        case .color:        "vibrance, saturation, the mixer, color grading"
        case .curve:        "the tone curve"
        case .detail:       "sharpening, noise reduction, lens corrections"
        case .effects:      "clarity, dehaze, shadow lift, LUT strength"
        }
    }

    /// The groups a new preset offers by default: a look, without the
    /// photograph's own white balance and without touching sharpening or noise,
    /// which are properties of the camera and the ISO rather than of a look.
    static let defaultSelection: Set<PresetGroup> = [.light, .color, .curve, .effects]
}

struct Preset: Codable, Identifiable, Equatable {
    var id = UUID()
    var name: String
    /// Only these groups are copied when the preset is applied.
    var groups: Set<PresetGroup>
    /// The values. Fields outside `groups` are carried but never read — kept so
    /// a preset can be re-saved with more groups enabled without losing them.
    var state: DevelopState
    /// Built-ins cannot be deleted or overwritten.
    var builtIn = false

    /// Applies this preset over an existing state, returning the result.
    ///
    /// ⚠ Field by field, by group. There is no way to write this generically in
    /// Swift without reflection, and reflection here would mean a field added to
    /// `DevelopState` silently joining whichever group its name happened to
    /// sort into. The explicit list is longer and is the only version where
    /// adding a field is a decision rather than an accident.
    func applied(to base: DevelopState) -> DevelopState {
        var out = base

        if groups.contains(.whiteBalance) {
            out.temperatureK = state.temperatureK
            out.tint = state.tint
        }
        if groups.contains(.light) {
            out.exposureEv = state.exposureEv
            out.contrast = state.contrast
            out.highlights = state.highlights
            out.shadows = state.shadows
            out.whites = state.whites
            out.blacks = state.blacks
            out.highlightRecovery = state.highlightRecovery
        }
        if groups.contains(.color) {
            out.vibrance = state.vibrance
            out.saturation = state.saturation
            out.hueShift = state.hueShift
            out.satShift = state.satShift
            out.lumShift = state.lumShift
            out.gradeShadow = state.gradeShadow
            out.gradeMidtone = state.gradeMidtone
            out.gradeHighlight = state.gradeHighlight
            // With the wheels — a look copied without the split point they were
            // graded against is not the same look.
            out.gradeBalance = state.gradeBalance
        }
        if groups.contains(.curve) {
            out.curve = state.curve
        }
        if groups.contains(.detail) {
            out.sharpenAmount = state.sharpenAmount
            out.sharpenRadius = state.sharpenRadius
            out.sharpenMasking = state.sharpenMasking
            out.denoiseLuma = state.denoiseLuma
            out.denoiseColor = state.denoiseColor
            out.lensDistortion = state.lensDistortion
            out.lensVignette = state.lensVignette
            out.lensCaRed = state.lensCaRed
            out.lensCaBlue = state.lensCaBlue
        }
        if groups.contains(.effects) {
            out.clarity = state.clarity
            out.dehaze = state.dehaze
            out.fusion = state.fusion
            out.lutStrength = state.lutStrength
            out.grainAmount = state.grainAmount
            out.grainSize = state.grainSize
            out.vignetteAmount = state.vignetteAmount
            out.vignetteFieldAngle = state.vignetteFieldAngle
        }

        // ⚠ Never copied, under any group. See the note on PresetGroup: the
        // geometry, the dust and the masks belong to one photograph.
        //
        // **These lines are a backstop and change nothing today** — `out`
        // started as `base`, and no group above assigns any of them, so
        // deleting the block leaves every test passing. That was checked by
        // mutation rather than assumed, and they stay for the same reason the
        // brush kernel keeps its radius cutoff: the next person to add a group
        // is one `out.cropX = state.cropX` away from a preset that reframes
        // every photograph it touches, and this is where that gets undone.
        //
        // A test cannot distinguish a backstop from the thing it backs up. What
        // the suite does pin is the *property* — that applying a preset with
        // every group enabled leaves the frame, the dust and the masks exactly
        // as they were.
        out.rotateQuarters = base.rotateQuarters
        out.straightenDeg = base.straightenDeg
        // Perspective is framing, and framing is per photograph. A keystone
        // measured on one building corrects nothing on the next.
        out.perspectiveVertical = base.perspectiveVertical
        out.perspectiveHorizontal = base.perspectiveHorizontal
        out.perspectiveAspect = base.perspectiveAspect
        out.cropX = base.cropX; out.cropY = base.cropY
        out.cropW = base.cropW; out.cropH = base.cropH
        out.spots = base.spots
        out.maskComponents = base.maskComponents
        out.maskRefine = base.maskRefine
        // ⚠ Layers travel with the mask, not with the look — a preset that
        // carried them would apply one photograph's local grade to another
        // photograph's subject.
        out.layers = base.layers

        return out
    }
}

/// Where presets live, and the built-in looks.
///
/// JSON on disk, one file for the lot, in Application Support. Not a database:
/// there will be tens of these, a photographer should be able to read and mail
/// one, and `Sidecar` already establishes JSON as this project's answer for
/// small structured data.
@Observable
final class PresetStore {

    private(set) var presets: [Preset] = []

    private let url: URL?

    init(url: URL? = PresetStore.defaultURL()) {
        self.url = url
        load()
    }

    static func defaultURL() -> URL? {
        guard let base = FileManager.default.urls(for: .applicationSupportDirectory,
                                                  in: .userDomainMask).first
        else { return nil }
        let dir = base.appendingPathComponent("Orion", isDirectory: true)
        try? FileManager.default.createDirectory(at: dir, withIntermediateDirectories: true)
        return dir.appendingPathComponent("presets.json")
    }

    /// User presets, then the built-ins. Built-ins are appended at load rather
    /// than written to disk, so improving one in a later release reaches
    /// everybody instead of only new installs.
    private func load() {
        var user: [Preset] = []
        if let url, let data = try? Data(contentsOf: url) {
            user = PresetStore.decode(data)
        }
        presets = user.filter { !$0.builtIn } + PresetStore.builtIns
    }

    /// A preset that cannot be read costs that preset, and no other.
    ///
    /// ⚠ **This was `try? JSONDecoder().decode([Preset].self)`, which is all or
    /// nothing** — one unreadable element and the photographer opens Orion to
    /// the four built-ins and every saved look gone, with nothing written
    /// anywhere to say why. That is a defect on its own terms and decision #112
    /// is where it became visible: the group rename is exactly the change that
    /// would have triggered it, on every installation at once, on first launch.
    ///
    /// `Failable` decodes each element inside its own `init(from:)` and never
    /// throws, so the array always decodes and the unreadable slots come back
    /// `nil`. Element-wise `try? container.decode(...)` is the obvious
    /// alternative and is wrong: `UnkeyedDecodingContainer` does not promise to
    /// advance `currentIndex` on a throw, so the loop either spins forever or
    /// eats the element after the bad one, depending on the Foundation build.
    ///
    /// ⚠ Skipping is right for a *preset* and would be wrong for a **group**.
    /// Dropping an unreadable group silently narrows what the preset touches,
    /// which turns "this look changed nothing" into a mystery; dropping the
    /// preset loses one named row the photographer can see is missing.
    static func decode(_ data: Data) -> [Preset] {
        struct Failable: Decodable {
            let value: Preset?
            init(from decoder: Decoder) throws { value = try? Preset(from: decoder) }
        }
        guard let rows = try? JSONDecoder().decode([Failable].self, from: data)
        else { return [] }
        return rows.compactMap(\.value)
    }

    private func save() {
        guard let url else { return }
        let user = presets.filter { !$0.builtIn }
        guard let data = try? JSONEncoder().encode(user) else { return }
        try? data.write(to: url, options: .atomic)
    }

    /// Adds a preset, replacing a user one of the same name.
    ///
    /// Replacing rather than appending: saving "My Look" twice should leave one
    /// entry, which is what every other application does and what a
    /// photographer means. A built-in of that name is never touched — the new
    /// one shadows it in the list instead.
    @discardableResult
    func add(name: String, groups: Set<PresetGroup>, state: DevelopState) -> Bool {
        let trimmed = name.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !trimmed.isEmpty, !groups.isEmpty else { return false }

        let p = Preset(name: trimmed, groups: groups, state: state)
        if let i = presets.firstIndex(where: { $0.name == trimmed && !$0.builtIn }) {
            presets[i] = p
        } else {
            presets.insert(p, at: presets.firstIndex(where: { $0.builtIn }) ?? presets.count)
        }
        save()
        return true
    }

    func remove(_ preset: Preset) {
        guard !preset.builtIn else { return }
        presets.removeAll { $0.id == preset.id }
        save()
    }

    // MARK: The built-ins

    /// Four, deliberately few.
    ///
    /// ⚠ Every value here is a *look* and none of it is claimed as a correction
    /// — they are taste, they are Orion's own, and `UNSOURCED.md` does not need
    /// an entry for them for the same reason it does not need one for the
    /// interface's colours. What would need defending is a built-in that
    /// claimed to emulate a named film stock, and none of these does.
    static let builtIns: [Preset] = {
        func look(_ name: String, _ build: (inout DevelopState) -> Void) -> Preset {
            var s = DevelopState()
            build(&s)
            return Preset(name: name,
                          groups: [.light, .color, .curve, .effects],
                          state: s, builtIn: true)
        }

        return [
            look("Punch") { s in
                s.contrast = 1.62
                s.blacks = -0.18
                s.whites = 0.12
                s.vibrance = 0.30
                s.clarity = 0.25
            },
            look("Soft") { s in
                s.contrast = 1.24
                s.blacks = 0.14
                s.highlights = -0.20
                s.vibrance = 0.10
                s.clarity = -0.15
            },
            look("Monochrome") { s in
                s.saturation = -1.0
                s.contrast = 1.55
                s.clarity = 0.20
            },
            look("Lift the shadows") { s in
                s.fusion = 0.55
                s.shadows = 0.25
                s.blacks = 0.08
            },
        ]
    }()
}
