import SwiftUI

/// A set of adjustments, drawn from the catalogue rather than from a list at
/// the call site.
///
/// The global panel and a mask's local panel render **the same specs through
/// the same view**, so they cannot drift in look, in behaviour or in what they
/// offer. That was the ask: one set of controls, defined once, pointed at
/// whichever scope is being edited.
///
/// It is also what stage 2 of the layer plan needs — N layers each showing
/// their own adjustments is this view N times, with no new controls to build.
struct AdjustmentGroup: View {
    @Bindable var engine: Engine
    let specs: [AdjustmentSpec]
    let scope: Scope

    enum Scope { case global, local }

    var body: some View {
        ForEach(specs, id: \.id.rawValue) { spec in
            if let s = scope == .global ? spec.global : spec.local {
                AdjustmentSlider(name: spec.title,
                                 value: binding(spec.id, scope),
                                 range: s.range, unit: s.unit,
                                 decimals: s.decimals,
                                 base: reset(spec.id, scope),
                                 engine: engine)
            }
        }
    }

    // MARK: The one place a spec becomes a control

    /// ⚠ A `switch` with no `default`, on purpose. Adding a case to
    /// `AdjustmentID` without binding it here is a **compile error** rather
    /// than a control that draws nothing — which is exactly how `lutStrength`
    /// once shipped a dead slider.
    private func binding(_ id: AdjustmentID, _ scope: Scope) -> Binding<Float> {
        switch (id, scope) {
        case (.exposure, .global):    $engine.exposureEv
        case (.exposure, .local):     $engine.localExposureEv
        case (.contrast, .global):    $engine.contrast
        case (.contrast, .local):     $engine.localContrast
        case (.highlights, _):        $engine.highlights
        case (.shadows, _):           $engine.shadows
        case (.whites, _):            $engine.whites
        case (.blacks, _):            $engine.blacks
        case (.vibrance, _):          $engine.vibrance
        case (.saturation, .global):  $engine.saturation
        case (.saturation, .local):   $engine.localSaturation
        case (.temperature, _):       $engine.temperatureK
        case (.tint, _):              $engine.tint
        case (.warmth, _):            $engine.localWarmth
        case (.localTint, _):         $engine.localTint
        case (.clarity, _):           $engine.clarity
        case (.dehaze, _):            $engine.dehaze
        case (.fusion, _):            $engine.fusion
        case (.lutStrength, _):       $engine.lutStrength
        }
    }

    /// What the control returns to for *this* photograph. White balance resets
    /// to the camera's own reading rather than to a constant, which is why this
    /// asks the engine instead of the spec.
    private func reset(_ id: AdjustmentID, _ scope: Scope) -> Float {
        let d = engine.defaults
        switch (id, scope) {
        case (.exposure, .global):    return d.exposureEv
        case (.exposure, .local):     return d.localExposureEv
        case (.contrast, .global):    return d.contrast
        case (.contrast, .local):     return d.localContrast
        case (.highlights, _):        return d.highlights
        case (.shadows, _):           return d.shadows
        case (.whites, _):            return d.whites
        case (.blacks, _):            return d.blacks
        case (.vibrance, _):          return d.vibrance
        case (.saturation, .global):  return d.saturation
        case (.saturation, .local):   return d.localSaturation
        case (.temperature, _):       return d.temperatureK
        case (.tint, _):              return d.tint
        case (.warmth, _):            return d.localWarmth
        case (.localTint, _):         return d.localTint
        case (.clarity, _):           return d.clarity
        case (.dehaze, _):            return d.dehaze
        case (.fusion, _):            return d.fusion
        case (.lutStrength, _):       return d.lutStrength
        }
    }
}

/// What a mask cannot reach, and why — shown where someone goes looking for it.
///
/// ⚠ Generated from the catalogue rather than written out, so a control that
/// stops being local-able says so the day it changes. An empty list here would
/// mean the refusals had been silently dropped, which is worse than the
/// refusals.
struct LocalRefusals: View {
    var body: some View {
        let items = AdjustmentCatalogue.refusedLocally
        VStack(alignment: .leading, spacing: 2) {
            Engraved.Label(text: "Not available on a mask", color: Palette.faint, size: 9)
            ForEach(items, id: \.id.rawValue) { spec in
                Text("\(spec.title) — \(spec.localRefusal ?? "")")
                    .font(.system(size: 10))
                    .foregroundStyle(Palette.faint)
                    .fixedSize(horizontal: false, vertical: true)
            }
        }
    }
}

/// Where the mask sits in the pipeline, named rather than implied.
///
/// The panel is organised by *kind* of adjustment — Light, Colour, Detail — and
/// that is the right way to find a control. It is the wrong way to understand
/// why some of them can be local and others cannot, because that is decided by
/// **order**, and order is invisible in a tab bar.
///
/// ⚠ Built from `AdjustmentStage`, so it cannot drift from the table that
/// decides what is offered. A hand-written sentence here would be a second
/// claim about the graph, and this codebase has been bitten by a second copy of
/// a claim more than once.
struct PipelineOrder: View {
    var body: some View {
        // ⚠ Read from each adjustment's **global** stage, because that is the
        // one that says where it sits relative to the mask. Contrast is the
        // case that makes the distinction real: globally it is after, locally
        // it is here.
        let before = AdjustmentCatalogue.all.filter {
            ($0.global?.stage.rawValue ?? .max) < AdjustmentStage.tone.rawValue
        }
        let after = AdjustmentCatalogue.all.filter {
            ($0.global?.stage.rawValue ?? -1) > AdjustmentStage.tone.rawValue
        }

        VStack(alignment: .leading, spacing: 2) {
            Engraved.Label(text: "Where the mask sits", color: Palette.faint, size: 9)
            line("Before", before, "already in what the mask sees")
            line("Here", AdjustmentCatalogue.localSet, "scaled by the coverage")
            line("After", after, "on the combined result")
        }
    }

    @ViewBuilder
    private func line(_ label: String, _ specs: [AdjustmentSpec],
                      _ note: String) -> some View {
        if !specs.isEmpty {
            (Text(label).foregroundStyle(Palette.dim)
             + Text("  " + specs.map(\.title).joined(separator: ", "))
                .foregroundStyle(Palette.faint)
             + Text("  — " + note).foregroundStyle(Palette.faint.opacity(0.7)))
                .font(.system(size: 10))
                .fixedSize(horizontal: false, vertical: true)
        }
    }
}
