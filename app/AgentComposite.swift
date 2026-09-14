import Foundation

/// Strict, range-checked validation for the composite `DevelopState` fields —
/// `curve`, the three `grade*` triples, the three eight-band arrays, `layers`,
/// `spots` and `maskComponents`. `AgentCLI.mergeEdits` used to refuse every one
/// of these outright; this is what replaced the refusal. Its own file because
/// `AgentCLI.swift` is already at the size a 3-file feature should stay under.
///
/// **A composite edit REPLACES the whole value** — no merge by index; the
/// model reads the current value from `apply`'s `state` and sends back the
/// complete new one. That is what makes strict decoding safe: every element
/// of every array must carry every field its struct has, so a value handed to
/// `DevelopState`'s own — deliberately lenient, per-field `try?` — decoder was
/// never missing a field to silently zero in the first place.
///
/// **Every numeric field's range is cited file:line to the `slider(...)` or
/// gesture call that draws the control**, the rule `AgentKeys.swift`
/// documents for scalars — a value accepted here is one the photographer's
/// own mouse could have produced.
enum AgentComposite {
    enum Failure: Error { case rejected(String) }

    /// The `DevelopState` top-level keys this file validates — `mergeEdits`
    /// routes exactly these here instead of down the scalar range-check path,
    /// and `keysJSON()` documents exactly these as the composite entries.
    static let fieldNames: Set<String> = [
        "curve", "gradeShadow", "gradeMidtone", "gradeHighlight",
        "hueShift", "satShift", "lumShift", "layers", "spots", "maskComponents",
    ]

    /// `photo` is only consulted for `maskComponents` (kind 4's matte-exists
    /// check); `AgentCLIDriver.runApply` always has one to pass.
    static func validate(key: String, value: Any, photo: URL?) throws {
        switch key {
        case "curve": try validateCurve(value)
        case "gradeShadow", "gradeMidtone", "gradeHighlight":
            try validateGradeWheel(value, name: key)
        case "hueShift", "satShift", "lumShift": try validateBand(value, name: key)
        case "layers": try validateLayers(value)
        case "spots": try validateSpots(value)
        case "maskComponents": try validateMaskComponents(value, photo: photo)
        default: break
        }
    }

    // MARK: curve — CurveSupport.swift's ToneCurve/CurvePoint

    static func validateCurve(_ value: Any) throws {
        guard let obj = value as? [String: Any] else { throw Failure.rejected("curve must be an object") }
        let keys = requiredKeys(ToneCurve())   // {"master","red","green","blue"}
        try checkComplete(obj, required: keys, allowed: keys, label: "curve")
        for channel in ["master", "red", "green", "blue"] {
            guard let points = obj[channel] as? [Any] else {
                throw Failure.rejected("curve.\(channel) must be an array of points")
            }
            try validateCurveChannel(points, label: "curve.\(channel)")
        }
    }

    private static func validateCurveChannel(_ points: [Any], label: String) throws {
        // CurveMath.swift:16-17 — under 2 points and the evaluator falls back
        // to the identity rather than the curve sent.
        guard points.count >= 2 else { throw Failure.rejected("\(label) needs at least 2 points") }
        let pointKeys = requiredKeys(CurvePoint(x: 0, y: 0))   // {"x","y"}
        var lastX: Double?
        for (i, raw) in points.enumerated() {
            guard let p = raw as? [String: Any] else {
                throw Failure.rejected("\(label)[\(i)] must be an object")
            }
            let pointLabel = "\(label)[\(i)]"
            try checkComplete(p, required: pointKeys, allowed: pointKeys, label: pointLabel)
            // CurveEditor.swift:338 — clamp01, both axes.
            try checkFieldRange(p, field: "x", range: 0...1, unit: "unitless",
                                 source: "CurveEditor.swift:338", label: pointLabel)
            try checkFieldRange(p, field: "y", range: 0...1, unit: "unitless",
                                 source: "CurveEditor.swift:338", label: pointLabel)
            let x = (p["x"] as! NSNumber).doubleValue
            // CurveEditor.swift:262-265 — a non-ascending curve is "malformed"
            // to the engine and silently falls back to the identity, so a
            // duplicate or descending x is refused here instead.
            if let lastX, x <= lastX {
                throw Failure.rejected("\(label) points must be sorted by x, strictly increasing "
                    + "(CurveEditor.swift:262-265)")
            }
            lastX = x
        }
    }

    // MARK: grade wheels — ColorWheel.swift

    static func validateGradeWheel(_ value: Any, name: String) throws {
        guard let arr = value as? [Any], arr.count == 3,
              let x = (arr[0] as? NSNumber)?.doubleValue,
              let y = (arr[1] as? NSNumber)?.doubleValue,
              let lum = (arr[2] as? NSNumber)?.doubleValue else {
            throw Failure.rejected("\(name) must be an array of exactly 3 numbers [x, y, luminance]")
        }
        // ColorWheel.swift:165-166 — the puck is clamped into the unit disc;
        // past the rim the radius stops meaning anything.
        guard x * x + y * y <= 1 else {
            throw Failure.rejected("\(name) [x=\(AgentKeys.format(x)), y=\(AgentKeys.format(y))] is "
                + "outside the unit disc, x²+y² ≤ 1 (ColorWheel.swift:165-166)")
        }
        // ColorWheel.swift:66 — the luminance track under each wheel.
        try checkScalarRange(lum, range: -0.5...0.5, unit: "unitless",
                              source: "ColorWheel.swift:66", label: "\(name)[2] (luminance)")
    }

    // MARK: hue/sat/lum bands — DevelopPanels+Color.swift:113,115,117

    static func validateBand(_ value: Any, name: String) throws {
        guard let arr = value as? [Any], arr.count == 8 else {
            throw Failure.rejected("\(name) must be an array of exactly 8 numbers")
        }
        for (i, raw) in arr.enumerated() {
            guard let n = raw as? NSNumber else { throw Failure.rejected("\(name)[\(i)] must be a number") }
            try checkScalarRange(n.doubleValue, range: -1...1, unit: "unitless",
                                  source: "DevelopPanels+Color.swift:113,115,117", label: "\(name)[\(i)]")
        }
    }

    // MARK: layers — EditHistory.swift's LocalAdjustState

    /// ⚠ Ranges come from `AdjustmentCatalogue`'s *local* scope, not
    /// `AgentKeys`' global one: local exposure is -3…3 EV where global is
    /// -5…5, and local contrast is an additive -1…1 gain where global is a
    /// 0.5…2 multiplier (AdjustmentCatalogue.swift:108-120). Reusing the
    /// global numbers would accept a local exposureEv the panel's own slider
    /// cannot produce.
    static func validateLayers(_ value: Any) throws {
        guard let arr = value as? [Any] else { throw Failure.rejected("layers must be an array") }
        let required = requiredKeys(LocalAdjustState())
        let fields: [(name: String, id: AdjustmentID, source: String)] = [
            ("exposureEv", .exposure, "AdjustmentCatalogue.swift:108-110"),
            ("contrast", .contrast, "AdjustmentCatalogue.swift:118-120"),
            ("saturation", .saturation, "AdjustmentCatalogue.swift:143-145"),
            ("warmth", .warmth, "AdjustmentCatalogue.swift:159-160"),
            ("tint", .localTint, "AdjustmentCatalogue.swift:161-162"),
            ("highlights", .highlights, "AdjustmentCatalogue.swift:127-129"),
            ("shadows", .shadows, "AdjustmentCatalogue.swift:130-132"),
            ("whites", .whites, "AdjustmentCatalogue.swift:133-135"),
            ("blacks", .blacks, "AdjustmentCatalogue.swift:136-138")]
        for (i, raw) in arr.enumerated() {
            guard let dict = raw as? [String: Any] else {
                throw Failure.rejected("layers[\(i)] must be an object")
            }
            let label = "layers[\(i)]"
            try checkComplete(dict, required: required, allowed: required, label: label)
            for field in fields {
                guard let scope = AdjustmentCatalogue.spec(field.id).local else { continue }
                let unit = scope.unit.trimmingCharacters(in: .whitespaces)
                try checkFieldRange(dict, field: field.name, range: Double(scope.lower)...Double(scope.upper),
                                     unit: unit.isEmpty ? "unitless" : unit, source: field.source, label: label)
            }
        }
    }

    // MARK: spots — EditHistory.swift's SpotState

    static func validateSpots(_ value: Any) throws {
        guard let arr = value as? [Any] else { throw Failure.rejected("spots must be an array") }
        let required = requiredKeys(SpotState())
        for (i, raw) in arr.enumerated() {
            guard let dict = raw as? [String: Any] else { throw Failure.rejected("spots[\(i)] must be an object") }
            let label = "spots[\(i)]"
            try checkComplete(dict, required: required, allowed: required, label: label)
            // Engine+Spots.swift:50 — srcX/srcY are explicitly clamped 0…1;
            // destX/destY are the same frame-fraction convention.
            for field in ["destX", "destY", "srcX", "srcY"] {
                try checkFieldRange(dict, field: field, range: 0...1, unit: "fraction",
                                     source: "Engine+Spots.swift:50", label: label)
            }
            try checkFieldRange(dict, field: "radius", range: 0.004...0.12, unit: "fraction",
                                 source: "DevelopPanels+Detail.swift:39", label: label)
            try checkFieldRange(dict, field: "feather", range: 0...1, unit: "unitless",
                                 source: "DevelopPanels+Detail.swift:40", label: label)
            guard dict["heal"] is Bool else { throw Failure.rejected("\(label).heal must be a boolean") }
        }
    }

    // MARK: maskComponents — EditHistory.swift's MaskComponentState

    static func validateMaskComponents(_ value: Any, photo: URL?) throws {
        guard let arr = value as? [Any] else { throw Failure.rejected("maskComponents must be an array") }
        // required omits matteId/matteSource/name — the struct's only Optional
        // fields, so a JSON-encoded default omits them too; allowed is the
        // full roster, so a model *may* still send them. That is what lets
        // kind 4 name a matte without every other mask carrying a null one.
        let required = requiredKeys(MaskComponentState())
        let allowed = MaskComponentState.fieldRoster
        let numericFields: [(name: String, range: ClosedRange<Double>, unit: String, source: String)] = [
            ("centerX", 0...1, "unitless", "DevelopPanels+Mask.swift:457"),
            ("centerY", 0...1, "unitless", "DevelopPanels+Mask.swift:459"),
            ("angle", -3.15...3.15, "radians", "DevelopPanels+Mask.swift:461"),
            ("length", 0.05...1.5, "unitless", "DevelopPanels+Mask.swift:474"),
            ("radiusX", 0.02...1, "unitless", "DevelopPanels+Mask.swift:486"),
            ("radiusY", 0.02...1, "unitless", "DevelopPanels+Mask.swift:488"),
            ("feather", 0...1, "unitless", "DevelopPanels+Mask.swift:484"),
            ("roundness", 2...8, "unitless", "DevelopPanels+Mask.swift:490"),
            ("rangeLo", -8...8, "EV", "DevelopPanels+Mask.swift:371"),
            ("rangeHi", -8...8, "EV", "DevelopPanels+Mask.swift:373"),
            ("rangeSoft", 0.05...4, "EV", "DevelopPanels+Mask.swift:375"),
            ("colorTol", 0.01...0.8, "unitless", "DevelopPanels+Mask.swift:407"),
            ("colorSoft", 0.002...0.4, "unitless", "DevelopPanels+Mask.swift:409"),
            ("brushRadius", 0.01...0.4, "unitless", "DevelopPanels+Mask.swift:430"),
            ("brushFlow", 0.01...1, "unitless", "DevelopPanels+Mask.swift:439"),
            ("brushHardness", 0...1, "unitless", "DevelopPanels+Mask.swift:441"),
            // Add/Subtract/Intersect.
            ("compose", 0...2, "unitless", "DevelopPanels+Mask.swift:327-329")]

        for (i, raw) in arr.enumerated() {
            guard let dict = raw as? [String: Any] else {
                throw Failure.rejected("maskComponents[\(i)] must be an object")
            }
            let label = "maskComponents[\(i)]"
            try checkComplete(dict, required: required, allowed: allowed, label: label)
            guard let kindNumber = dict["kind"] as? NSNumber else {
                throw Failure.rejected("\(label).kind must be a number")
            }
            let kindValue = kindNumber.doubleValue
            guard kindValue == kindValue.rounded(), (1...6).contains(Int(kindValue)) else {
                // EditHistory.swift:102 — "1 linear, 2 radial, 3 brush, 4
                // matte, 5 range, 6 color". 0 is the struct's zero value,
                // meaning "no mask", never a kind a caller may name.
                throw Failure.rejected("\(label).kind \(AgentKeys.format(kindValue)) is outside 1…6 — "
                    + "1 linear, 2 radial, 3 brush, 4 matte, 5 range, 6 color (EditHistory.swift:102)")
            }
            let kind = Int(kindValue)

            for field in numericFields {
                try checkFieldRange(dict, field: field.name, range: field.range,
                                     unit: field.unit, source: field.source, label: label)
            }
            for field in ["invert", "hidden", "startsLayer"] {
                guard dict[field] is Bool else { throw Failure.rejected("\(label).\(field) must be a boolean") }
            }
            // colorR/G/B: scene-linear Rec.2020, set by an eyedropper click,
            // not a slider — no product range, only a type.
            for field in ["colorR", "colorG", "colorB"] {
                guard dict[field] is NSNumber else { throw Failure.rejected("\(label).\(field) must be a number") }
            }
            for field in ["brushStroke", "brushErase"] {
                guard let a = dict[field] as? [Any], a.allSatisfy({ $0 is NSNumber }) else {
                    throw Failure.rejected("\(label).\(field) must be an array of numbers")
                }
            }

            if kind == 4 {
                // A model cannot paint — only reference a matte a producer
                // (Vision, the sky detector) already saved. MatteStore.swift.
                guard let matteId = dict["matteId"] as? String, !matteId.isEmpty else {
                    throw Failure.rejected("\(label) kind 4 (raster matte) needs matteId — a model "
                        + "cannot paint one, only reference a matte already saved beside the photo")
                }
                guard let photo else {
                    throw Failure.rejected("\(label) kind 4 needs a photograph to check the matte against")
                }
                let matteURL = MatteStore.url(photo: photo, id: matteId)
                guard FileManager.default.fileExists(atPath: matteURL.path) else {
                    throw Failure.rejected("\(label) kind 4 names matte '\(matteId)' but "
                        + "\(matteURL.lastPathComponent) does not exist beside \(photo.lastPathComponent)")
                }
            }
        }
    }

    // MARK: shared helpers

    /// The keys a JSON-encoded default instance carries — an Optional stored
    /// property with a nil default is omitted by Swift's synthesized encoder,
    /// so this is "every field this struct requires", not "every field it has".
    private static func requiredKeys<T: Encodable>(_ value: T) -> Set<String> {
        guard let data = try? JSONEncoder().encode(value),
              let object = try? JSONSerialization.jsonObject(with: data) as? [String: Any]
        else { return [] }
        return Set(object.keys)
    }

    private static func checkComplete(_ dict: [String: Any], required: Set<String>,
                                       allowed: Set<String>, label: String) throws {
        if let missing = required.subtracting(dict.keys).sorted().first {
            throw Failure.rejected("\(label) is missing '\(missing)'")
        }
        if let unknown = Set(dict.keys).subtracting(allowed).sorted().first {
            throw Failure.rejected("\(label) has an unknown field '\(unknown)'")
        }
    }

    private static func checkScalarRange(_ v: Double, range: ClosedRange<Double>,
                                          unit: String, source: String, label: String) throws {
        guard range.contains(v) else {
            throw Failure.rejected("\(label) \(AgentKeys.format(v)) is outside "
                + "\(AgentKeys.format(range.lowerBound))…\(AgentKeys.format(range.upperBound)) "
                + "(\(unit), \(source))")
        }
    }

    private static func checkFieldRange(_ dict: [String: Any], field: String, range: ClosedRange<Double>,
                                         unit: String, source: String, label: String) throws {
        guard let n = dict[field] as? NSNumber else { throw Failure.rejected("\(label).\(field) must be a number") }
        try checkScalarRange(n.doubleValue, range: range, unit: unit, source: source, label: "\(label).\(field)")
    }
}
