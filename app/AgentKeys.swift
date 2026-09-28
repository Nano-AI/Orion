import Foundation

/// The scalar `DevelopState` fields `AgentCLI.mergeEdits` may write, and the
/// range each one is checked against — the vocabulary `Orion --agent keys`
/// prints and the numbers `apply` validates a value against before it ever
/// reaches the engine.
///
/// **Every range is transcribed from the literal `slider(...)` call that
/// draws the control today**, cited file:line in each entry's comment — not
/// re-derived from `AdjustmentCatalogue.swift`. The catalogue's `global`
/// scope agrees for the fields it covers, but the global develop panels
/// (`DevelopPanels+Light.swift`, `+Color.swift`, `+Detail.swift`,
/// `+Optics.swift`, `OrionApp+Tools.swift`) are hand-written literals, not
/// driven by the catalogue — only the *local* (mask) panel goes through
/// `AdjustmentGroup` and the catalogue. Citing the literal call is what a
/// photographer's mouse is actually bounded by, and what stays checkable
/// against the file that would clip a drag.
///
/// This shipped after #244/#245: `apply` accepted `temperatureK: 150` —
/// meant as a +150 K delta, taken as an absolute 150 K, four hundred kelvin
/// under the coldest the Temperature slider can reach — rendered a flat black
/// proxy, and was then committed to the sidecar anyway.
enum AgentKeys {
    struct Spec {
        /// Human-readable unit for the `keys` verb and for error text.
        /// `"unitless"` for a plain multiplier or a -1…1 strength; `""` only
        /// for the two non-numeric/marker fields that also carry no range.
        var unit: String
        /// `nil` means the product defines no bound for this key — `apply`
        /// accepts whatever the JSON type allows, per the instruction not to
        /// invent one, and `keys` documents why in `note`.
        var range: ClosedRange<Double>?
        var note: String?
    }

    static let specs: [String: Spec] = [
        // MARK: White balance — DevelopPanels+Light.swift
        //
        // ⚠ Both defaults come from `DevelopState()` (temperatureK 5500, tint
        // 0), not from the camera. `Engine.asShotState()` (Engine.swift:
        // 652-665) seats the real as-shot reading on open, and the struct
        // default has no way to know it — that is the note below, and it is
        // why the photographer's floor was 150 K short of anything sane: the
        // model had no as-shot number to reason a delta from.
        "temperatureK": Spec(
            unit: "kelvin", range: 2000...12000,
            note: "default is DevelopState()'s 5500 K, not the camera's "
                + "as-shot reading — read the real one from `stats` or a "
                + "proposed state before changing it"),
        "tint": Spec(
            unit: "unitless", range: -1...1,
            note: "default is DevelopState()'s 0, not the camera's as-shot "
                + "reading — read the real one from `stats` or a proposed "
                + "state before changing it"),

        // DevelopPanels+Light.swift:35-40, :49
        "exposureEv": Spec(unit: "EV", range: -5...5),
        "contrast": Spec(unit: "unitless", range: 0.5...2),
        "highlights": Spec(unit: "unitless", range: -1...1),
        "shadows": Spec(unit: "unitless", range: -1...1),
        "whites": Spec(unit: "unitless", range: -1...1),
        "blacks": Spec(unit: "unitless", range: -1...1),
        "highlightRecovery": Spec(unit: "unitless", range: 0...1),

        // EditHistory.swift:391-402, decision #276. Not a slider: it is which
        // generation of tone bands the photograph is rendered under, and a
        // sidecar carries the one it was finished under.
        "process": Spec(
            unit: "generation", range: 1...2,
            note: "which generation of tone bands renders highlights, shadows, "
                + "whites and blacks: 2 is the current one - an identity band at "
                + "middle grey, so a face survives a highlights pull - and 1 is "
                + "the bands as shipped. A fresh photograph is 2; a sidecar "
                + "written before the field existed reopens at 1 so it renders "
                + "as it was finished. Leave it alone unless you mean to "
                + "re-render an old edit under the new bands"),

        // MARK: Color — DevelopPanels+Color.swift:11, :13, :37
        "vibrance": Spec(unit: "unitless", range: -1...1),
        "saturation": Spec(unit: "unitless", range: -1...1),
        "gradeBalance": Spec(unit: "unitless", range: -1...1),

        // MARK: Detail/Effects — DevelopPanels+Detail.swift
        "denoiseLuma": Spec(unit: "unitless", range: 0...4),          // :14
        "denoiseColor": Spec(unit: "unitless", range: 0...4),         // :15
        "lutStrength": Spec(unit: "unitless", range: 0...1),          // :98
        "fusion": Spec(unit: "unitless", range: 0...1),                // :106
        "grainAmount": Spec(unit: "unitless", range: 0...0.06),        // :114
        "grainSize": Spec(unit: "pixels", range: 1.2...8),             // :116
        "vignetteAmount": Spec(unit: "EV", range: -3...3),             // :126
        "vignetteFieldAngle": Spec(unit: "degrees", range: 10...70),   // :128
        "dehaze": Spec(unit: "unitless", range: 0...1),                // :135
        "clarity": Spec(unit: "unitless", range: -1...1),              // :141
        "sharpenAmount": Spec(unit: "unitless", range: 0...2),         // :145
        "sharpenRadius": Spec(unit: "pixels", range: 0.5...3),         // :146
        "sharpenMasking": Spec(unit: "unitless", range: 0...1),        // :147

        // DevelopPanels+Mask.swift:364 — the group-wide feather, not a
        // per-component field.
        "maskRefine": Spec(unit: "unitless", range: 0...1),

        // MARK: Optics — DevelopPanels+Optics.swift:85, :88, :91, :93
        "lensDistortion": Spec(unit: "unitless", range: -1...1),
        "lensVignette": Spec(unit: "unitless", range: -1...1),
        "lensCaRed": Spec(unit: "unitless", range: -1...1),
        "lensCaBlue": Spec(unit: "unitless", range: -1...1),

        // MARK: Geometry — OrionApp+Tools.swift:88, :122, :124, :126
        "straightenDeg": Spec(unit: "degrees", range: -90...90),
        "perspectiveVertical": Spec(unit: "unitless", range: -1...1),
        "perspectiveHorizontal": Spec(unit: "unitless", range: -1...1),
        "perspectiveAspect": Spec(unit: "unitless", range: -1...1),

        // Crop, normalized to the frame: default 0,0,1,1 (Engine.swift:
        // 192-195) and clamped to 0...1 by `moveCrop`/`constrainCrop`
        // (Engine+Geometry.swift:78-79). No slider — the crop overlay is a
        // drag rectangle, not a `slider(...)` call — so the range comes from
        // the engine's own clamp rather than a control literal.
        "cropX": Spec(unit: "fraction", range: 0...1),
        "cropY": Spec(unit: "fraction", range: 0...1),
        "cropW": Spec(unit: "fraction", range: 0...1),
        "cropH": Spec(unit: "fraction", range: 0...1),

        // MARK: No product range — the type is the only limit, per the
        // instruction not to invent a range where the product has none.
        "rotateQuarters": Spec(
            unit: "quarter turns", range: nil,
            note: "no slider — rotates 90° per quarter turn "
                + "(InteractionLog.swift:183-185 reads it mod 4); any "
                + "integer the JSON type allows is accepted"),
        "lensChoice": Spec(
            unit: "", range: nil,
            note: "a lens profile name from LensDatabase.names(), not an "
                + "index (EditHistory.swift:396-413); empty means the "
                + "profile came from the file's own EXIF; any string is "
                + "accepted here and simply fails to resolve to a profile "
                + "if it names none"),
        "maskSpace": Spec(
            unit: "", range: nil,
            note: "an internal marker, not a photographer-facing control — "
                + "0 is legacy display-space, 1 is frame-space "
                + "(EditHistory.swift:423-442, decision #112); apply accepts "
                + "any integer but only 0 and 1 mean anything to the engine"),
    ]

    /// `2000` rather than `2000.0`, `0.06` rather than `0.059999999…` —
    /// formats a `Double` the way the numbers above were written, for the
    /// range-violation message and the `keys` verb alike.
    static func format(_ value: Double) -> String {
        value == value.rounded() && abs(value) < 1e15
            ? String(Int64(value))
            : String(value)
    }
}
