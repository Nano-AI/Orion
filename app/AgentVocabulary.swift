import Foundation

/// The `keys` verb's payload - the edit vocabulary `describe_edits` (mcp/)
/// reads before a model proposes anything.
///
/// Its own file because the notes are the product here: a composite entry has
/// no `min`/`max`/`default` to lean on, so everything a caller needs in order
/// to write one has to be said in prose. Split out of `AgentCLI.swift` when
/// the mask note grew past the size that file is allowed to be.
///
/// A scalar entry's fields are `name, type, unit, min, max, default, absolute,
/// note`; a composite entry's are `name, type, absolute, example, note`. Both
/// shapes are fixed - `mcp/server.ts` and `tools/check-agent.py` read them.
extension AgentCLI {

    private static func isComposite(_ value: Any?) -> Bool {
        value is [Any] || value is [String: Any]
    }

    /// The top-level `DevelopState` fields `apply` may actually edit -
    /// scalars only. Derived from a fresh `DevelopState()` rather than
    /// hand-listed, so it can never drift from what `mergeEdits` accepts.
    static let scalarFieldNames: Set<String> = {
        guard let data = try? JSONEncoder().encode(DevelopState()),
              let object = try? JSONSerialization.jsonObject(with: data) as? [String: Any]
        else { return [] }
        return Set(object.keys.filter { !isComposite(object[$0]) })
    }()

    /// One entry per editable field - scalar and composite together, sorted by
    /// name - the same set `mergeEdits` allows, since both are built from
    /// `scalarFieldNames` / `AgentComposite.fieldNames`.
    static func keysJSON() -> [[String: Any]] {
        guard let data = try? JSONEncoder().encode(DevelopState()),
              let object = try? JSONSerialization.jsonObject(with: data) as? [String: Any]
        else { return [] }

        let scalars = scalarFieldNames.map { name -> [String: Any] in
            var entry: [String: Any] = ["name": name, "absolute": true]
            switch object[name] {
            case let s as String:
                entry["type"] = "string"
                entry["default"] = s
            case let n as NSNumber:
                entry["type"] = "number"
                entry["default"] = n.doubleValue
            default:
                entry["type"] = "number"
            }
            let spec = AgentKeys.specs[name]
            entry["unit"] = spec?.unit ?? "unitless"
            if let range = spec?.range {
                entry["min"] = range.lowerBound
                entry["max"] = range.upperBound
            }
            if let note = spec?.note { entry["note"] = note }
            return entry
        }
        return (scalars + compositeKeysJSON()).sorted {
            ($0["name"] as? String ?? "") < ($1["name"] as? String ?? "")
        }
    }

    /// The eight bands of `hueShift`/`satShift`/`lumShift`, in index order.
    /// `HueBand` (app/TargetedAdjust.swift:6-7) is the one declaration.
    private static let bandOrder = HueBand.allCases.map(\.name).joined(separator: ", ")

    /// One `keys` entry per composite field - `AgentComposite.fieldNames` -
    /// each carrying a complete, valid `example` (round-tripped through
    /// `JSONEncoder`/`JSONSerialization` from a real default instance rather
    /// than hand-typed, so it cannot drift from the structs) and a `note` that
    /// states the rule and repeats the replace-semantics reminder.
    static func compositeKeysJSON() -> [[String: Any]] {
        func json<T: Encodable>(_ value: T) -> Any {
            (try? JSONEncoder().encode(value))
                .flatMap { try? JSONSerialization.jsonObject(with: $0, options: [.fragmentsAllowed]) }
                ?? NSNull()
        }
        let sendComplete = " Send the complete value; read the current one from apply.state first."
        let bands = "Eight bands in HueBand order - \(bandOrder) - each -1…1."
        var radial = MaskComponentState()
        radial.kind = 2   // 0 is "no mask", never a kind to send.

        let entries: [(name: String, type: String, example: Any, note: String)] = [
            ("curve", "object", json(ToneCurve()),
             "Each channel (master, red, green, blue) needs at least 2 points, x and y "
                 + "each 0…1, sorted strictly ascending by x." + sendComplete),
            ("gradeShadow", "array", json([Float](repeating: 0, count: 3)), gradeNote),
            ("gradeMidtone", "array", json([Float](repeating: 0, count: 3)), gradeNote),
            ("gradeHighlight", "array", json([Float](repeating: 0, count: 3)), gradeNote),
            ("hueShift", "array", json([Float](repeating: 0, count: 8)), bands + sendComplete),
            ("satShift", "array", json([Float](repeating: 0, count: 8)), bands + sendComplete),
            ("lumShift", "array", json([Float](repeating: 0, count: 8)), bands + sendComplete),
            ("layers", "array", json([LocalAdjustState()]), layersNote + sendComplete),
            ("spots", "array", json([SpotState()]),
             "Every element needs every SpotState field (destX, destY, srcX, srcY, "
                 + "radius, feather, heal); positions 0…1, radius and feather in the "
                 + "product's ranges." + sendComplete),
            ("maskComponents", "array", json([radial]), maskNote + sendComplete),
        ]
        return entries.map {
            ["name": $0.name, "type": $0.type, "absolute": true, "example": $0.example, "note": $0.note]
        }
    }

    // MARK: the three notes that are too long to inline

    /// ⚠ The axes here are **what `DevelopPipeline::gradeOffsets`
    /// (engine/src/pipe/DevelopPipeline.cpp:158-191) computes**, not what an
    /// agent guessed from two renders. It decomposes the puck's angle over
    /// three primaries 120° apart - red at 0°, green at 120°, blue at 240°,
    /// counter-clockwise with y upward, which is also the winding
    /// `ColorWheel.swift:77-86` draws the rim at and `testGradePrimaryAngles`
    /// pins. So +x is red and -x is cyan; +y is between yellow and green and
    /// -y between blue and magenta. An earlier agent's "+x magenta, +y yellow"
    /// is the same wheel read about 60° out.
    private static let gradeNote =
        "[x, y, luminance]. x²+y² ≤ 1 (the puck stays in the wheel); luminance -0.5…0.5. "
        + "The angle picks a hue the way the wheel is wound: red at 0°, green at 120°, "
        + "blue at 240°, counter-clockwise with y upward (DevelopPipeline.cpp:158-191). "
        + "So +x is red and -x cyan; +y is yellow-to-green and -y blue-to-magenta; "
        + "the radius is how far, 0 is neutral. The offset is zero-sum, so a wheel "
        + "tints without lifting; luminance is the separate track under it. "
        + "Send the complete value; read the current one from apply.state first."

    private static let layersNote =
        "One entry per mask LAYER, not per component: layers[0] is the run that starts "
        + "at maskComponents[0] (row 0 always begins a layer whatever its flag says), "
        + "layers[1] the run beginning at the next component with startsLayer true, and "
        + "so on (MaskLayers.group). Every element needs every LocalAdjustState field "
        + "(exposureEv, contrast, saturation, warmth, tint, highlights, shadows, whites, "
        + "blacks), each checked against the *local* panel's own range, not the global "
        + "scalar one - local exposure is -3…3 EV where global is -5…5."

    /// The per-kind field map, checked against `engine/shaders/
    /// mask_component.slang` (the kind branches at :306, :326, :347, :477,
    /// :487, :527) and `app/DevelopPanels+Mask.swift:456-492`, which is the
    /// list of sliders a photographer actually gets per kind.
    ///
    /// ⚠ Two traps this note exists to close, both of them a field that looks
    /// like it applies and does not: **a linear gradient ignores `feather`**
    /// (its Length *is* the feather - the panel says so at :465-473 and the
    /// measurement it cites came back bit-identical), and **a radial reads
    /// `angle`** (the shader rotates the superellipse by it at :334), which is
    /// easy to miss because Angle is drawn above the kind-1/kind-2 split.
    private static let maskNote =
        "Every element needs every MaskComponentState field except the optional "
        + "matteId/matteSource/name. ALL coordinates, radii and lengths are in FRAME "
        + "space - the sensor's un-turned frame, x and y each 0…1 from the top-left, "
        + "before crop, straighten, rotation and perspective (maskSpace 1, decision "
        + "#112) - NOT the display-space fractions get_proxy and get_stats' region use. "
        + "detect_faces returns both. kind is 1…6 and decides which fields are read: "
        + "1 linear gradient - centerX, centerY, angle, length; it ignores feather, "
        + "because the ramp runs the whole length. The ramp is centred on "
        + "(centerX, centerY) and covers the side the angle points to: at angle 0 the "
        + "right, at +1.571 rad (+90°) the BOTTOM, at -1.571 rad (-90°) the TOP, y "
        + "running downward, and length is how far the transition runs. "
        + "⚠ FRAME top is not display top on a turned photograph: a portrait frame off "
        + "a landscape sensor puts frame-top down the display's LEFT edge (measured), "
        + "so aim a gradient by carrying a point across with detect_faces, or propose "
        + "it and probe both edges with get_stats' region before believing it. "
        + "2 radial - centerX, centerY, radiusX, radiusY, feather, roundness, and angle, "
        + "which rotates the ellipse; roundness 2 is an ellipse and higher walks toward "
        + "a rounded rectangle; feather is how far inside the boundary the ramp starts. "
        + "3 brush - brushStroke (interleaved x,y dabs), brushErase, brushRadius, "
        + "brushFlow, brushHardness. A model cannot paint one: send no dabs and it "
        + "covers nothing, so use 2, 5 or 6 instead. "
        + "4 raster matte - matteId, which must name a PNG already saved beside the "
        + "photograph by a producer (Subject, Sky); a model cannot make one. "
        + "5 luminance range - rangeLo, rangeHi (the band's edges in EV relative to "
        + "middle grey, as displayed, so they follow the exposure slider) and rangeSoft "
        + "(how many EV each edge takes to ramp). "
        + "6 colour range - colorR, colorG, colorB (the target shade in scene-linear "
        + "Rec.2020), colorTol and colorSoft, measured in Oklab chromaticity, so "
        + "lightness does not enter: use kind 5 for that axis. "
        + "invert flips THIS component's own coverage before it folds into the group, "
        + "so an inverted radial covers everything outside its ellipse. compose (0 add, "
        + "1 subtract, 2 intersect) is how it folds; startsLayer begins a new layer, and "
        + "layers[i] holds the adjustments for the i-th layer in this list's order."
}
