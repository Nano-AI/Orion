import Foundation

/// The adjustments, defined once.
///
/// Every control was hand-listed where it was drawn: its range, its unit, its
/// decimals and its default, written out at the call site. That is fine for one
/// panel and stops being fine the moment the same adjustment appears twice —
/// which is what happened when a mask learned to do more than exposure, and the
/// Local section became a second copy of the Light section's list with
/// different numbers.
///
/// Two copies of "what a contrast slider is" drift. This is the one copy.
///
/// ## ⚠ Where a thing happens is information, not decoration
///
/// `stage` is the adjustment's position in the graph, and it is what answers
/// "what is applied before masking and what after". It is not a label: it is
/// **why** some adjustments can be local and others cannot.
///
/// - Everything at `.capture` runs before the demosaic or before the mask node,
///   so it is already baked into what the mask sees.
/// - `.local` is the mask node itself.
/// - Everything at `.display` runs after, on the combined result, so a mask
///   cannot reach it at all.
///
/// `research/masking.md` §2b is the long form.
enum AdjustmentStage: Int, CaseIterable, Sendable {
    case capture, detail, effects, tone, display, geometry

    var title: String {
        switch self {
        case .capture:  "Capture"
        case .detail:   "Detail"
        case .effects:  "Effects"
        case .tone:     "Tone and color"
        case .display:  "Display"
        case .geometry: "Geometry"
        }
    }

    /// What the photographer needs to know about ordering, in one line.
    var note: String {
        switch self {
        case .capture:
            "Before the demosaic or before the mask — already baked into what a mask sees."
        case .detail:
            "Before the mask, on the whole frame."
        case .effects:
            "Before the mask. These are multi-pass filters and cannot be local."
        case .tone:
            "The mask's own stage. These can be local."
        case .display:
            "After the mask, on the combined result."
        case .geometry:
            "Last, after everything."
        }
    }
}

/// One adjustment's identity. An enum rather than a string so the table that
/// binds these to the engine can be a `switch` with no `default` — a spec with
/// no binding then fails to compile instead of drawing nothing.
enum AdjustmentID: String, CaseIterable, Sendable {
    case exposure, contrast, highlights, shadows, whites, blacks
    case vibrance, saturation
    case temperature, tint
    case warmth, localTint
    case clarity, dehaze, fusion
    case grainAmount, grainSize
    case vignetteAmount, vignetteFieldAngle
    case lutStrength
}

/// What one adjustment is, in each scope it exists in.
struct AdjustmentSpec: Sendable {
    struct Scope: Sendable {
        var lower: Float
        var upper: Float
        var unit: String = ""
        var decimals: Int = 2
        /// ⚠ **Per scope, not per adjustment.** Global contrast runs in
        /// `develop_display`, *after* the mask node; local contrast runs inside
        /// it. The same named control therefore sits on opposite sides of the
        /// mask depending on which one is meant, and a single stage on the spec
        /// would have made the ordering readout state one of them falsely.
        var stage: AdjustmentStage
        var range: ClosedRange<Float> { lower...upper }
    }

    var id: AdjustmentID
    var title: String
    /// Absent when the adjustment has no global control of its own — Warmth is
    /// local-only, because globally the same job is white balance.
    var global: Scope?
    /// ⚠ Absent when the adjustment **cannot** be local, and the reason is
    /// always structural rather than a matter of taste. `research/masking.md`
    /// §2b: an adjustment can be local exactly when it is a function of the
    /// pixel alone, because the coverage scales the parameter.
    var local: Scope?
    /// Why it cannot be local, shown where someone would go looking for it.
    var localRefusal: String?
}

enum AdjustmentCatalogue {

    static let all: [AdjustmentSpec] = [
        .init(id: .exposure, title: "Exposure",
              global: .init(lower: -5, upper: 5, unit: " EV", stage: .tone),
              local: .init(lower: -3, upper: 3, unit: " EV", stage: .tone)),

        // ⚠ The two contrasts are not the same control, and they are not even
        // at the same point in the graph. Global contrast is the display
        // transform's slope, applied *after* the mask on the combined result;
        // local contrast is a gain on the pixel's distance from the pivot,
        // applied inside the mask node. One shared range or one shared stage
        // would have to lie about one of them.
        .init(id: .contrast, title: "Contrast",
              global: .init(lower: 0.5, upper: 2, stage: .display),
              local: .init(lower: -1, upper: 1, stage: .tone)),

        .init(id: .highlights, title: "Highlights",
              global: .init(lower: -1, upper: 1, stage: .tone), local: nil,
              localRefusal: "reads the guided-filter chain, which runs once for the frame"),
        .init(id: .shadows, title: "Shadows",
              global: .init(lower: -1, upper: 1, stage: .tone), local: nil,
              localRefusal: "reads the guided-filter chain, which runs once for the frame"),
        .init(id: .whites, title: "Whites",
              global: .init(lower: -1, upper: 1, stage: .tone), local: nil,
              localRefusal: "an endpoint, and an endpoint per region is not an endpoint"),
        .init(id: .blacks, title: "Blacks",
              global: .init(lower: -1, upper: 1, stage: .tone), local: nil,
              localRefusal: "an endpoint, and an endpoint per region is not an endpoint"),

        .init(id: .vibrance, title: "Vibrance",
              global: .init(lower: -1, upper: 1, stage: .tone), local: nil,
              localRefusal: "saturation covers it locally"),
        .init(id: .saturation, title: "Saturation",
              global: .init(lower: -1, upper: 1, stage: .tone),
              local: .init(lower: -1, upper: 1, stage: .tone)),

        // ⚠ The pair that makes the whole table worth having.
        .init(id: .temperature, title: "Temperature",
              global: .init(lower: 2000, upper: 12000, unit: " K", decimals: 0,
                            stage: .capture),
              local: nil,
              localRefusal: "applied before the demosaic — a local one would mean "
                          + "demosaicing the frame twice"),
        .init(id: .tint, title: "Tint",
              global: .init(lower: -1, upper: 1, stage: .capture), local: nil,
              localRefusal: "applied before the demosaic, with the temperature"),

        // Local-only: globally this job belongs to white balance.
        .init(id: .warmth, title: "Warmth",
              global: nil, local: .init(lower: -1, upper: 1, stage: .tone)),
        .init(id: .localTint, title: "Tint",
              global: nil, local: .init(lower: -1, upper: 1, stage: .tone)),

        .init(id: .clarity, title: "Clarity",
              global: .init(lower: -1, upper: 1, stage: .effects), local: nil,
              localRefusal: "a 32-node pyramid; scaling its parameter by coverage "
                          + "is not defined"),
        .init(id: .dehaze, title: "Dehaze",
              global: .init(lower: 0, upper: 1, stage: .effects), local: nil,
              localRefusal: "a 16-node pyramid, and it estimates one atmospheric "
                          + "light for the frame"),
        .init(id: .fusion, title: "Lift",
              global: .init(lower: 0, upper: 1, stage: .effects), local: nil,
              localRefusal: "a 32-node pyramid over a plan measured from the whole frame"),

        // After the mask, with the contrast: the creative LUT is applied by the
        // display node on the combined result.
        .init(id: .lutStrength, title: "Look",
              global: .init(lower: 0, upper: 1, stage: .display), local: nil,
              localRefusal: "applied by the display transform, after the mask"),

        // Last of all, and later than the display transform. Grain is added to
        // the finished picture because film grain is in the print, not in the
        // scene — #81 has the reasoning, including why scene-linear is the
        // wrong side of the transform for it.
        .init(id: .grainAmount, title: "Grain",
              global: .init(lower: 0, upper: 0.06, stage: .display), local: nil,
              localRefusal: "added after the display transform, past where a "
                          + "mask's coverage exists"),
        .init(id: .grainSize, title: "Grain size",
              global: .init(lower: 1.2, upper: 8, stage: .display), local: nil,
              localRefusal: "a property of the negative, not of a region of it"),

        // The creative vignette — research/vignette.md, decision #96. Fused
        // into the grading pass, which is after the mask node, so it is
        // `.display` for the same reason the LUT is.
        //
        // ⚠ Amount reads in **stops at the corner**, not in a 0–100 strength,
        // because it is an exposure change and calling it one is what makes the
        // number checkable. Negative darkens, which is the usual direction.
        .init(id: .vignetteAmount, title: "Vignette",
              global: .init(lower: -3, upper: 3, unit: " EV", decimals: 2,
                            stage: .display), local: nil,
              localRefusal: "a property of the whole composition, and it is "
                          + "already positioned by the crop"),
        .init(id: .vignetteFieldAngle, title: "Vignette field",
              global: .init(lower: 10, upper: 70, unit: "°", decimals: 0,
                            stage: .display), local: nil,
              localRefusal: "the shape of the falloff, not a region of it"),
    ]

    static func spec(_ id: AdjustmentID) -> AdjustmentSpec {
        // Every case of the enum has an entry, and the test below is what keeps
        // that true — a missing one would silently drop a control.
        all.first { $0.id == id }!
    }

    /// The adjustments a mask can carry, in the order the panel shows them.
    static var localSet: [AdjustmentSpec] { all.filter { $0.local != nil } }

    /// The ones someone will look for in a mask and not find, with the reason.
    static var refusedLocally: [AdjustmentSpec] {
        all.filter { $0.local == nil && $0.localRefusal != nil }
    }
}
