import Foundation

/// Bringing a display-space-era state's masks into frame coordinates.
///
/// A sidecar or snapshot written before frame anchoring stores its parametric
/// masks normalized against the crop it also stores — so the conversion is
/// deterministic: the geometry the numbers were relative to travels in the
/// same blob. `DevelopState.maskSpace` is the marker (#112's rule: the marker
/// gates meaning, the numbers are never reinterpreted without it), and this
/// is the one place a legacy state crosses over, on the way in at the two
/// restore points in `Engine+Document`.
///
/// The mathematics is the engine's — `mask::placeToFrame` through
/// `orion_engine_mask_place_to_frame` — with the *state's own* geometry
/// passed explicitly, because a snapshot restored mid-session may carry a
/// crop the picture no longer shows. Centre and angle convert exactly; a
/// radial's semi-axes and a linear's length are exact under crops and quarter
/// turns and first order under a straighten or keystone — the accuracy every
/// render shipped before decision #137, so no migrated file renders worse
/// than it always had.
extension Engine {

    /// The state with its masks in frame coordinates, whichever space it
    /// arrived in. Idempotent by the marker; a state already at
    /// `maskSpace == 1` passes through untouched.
    ///
    /// ⚠ Returns the state **unconverted, marker intact**, when the engine
    /// has no photograph — the conversion needs the frame's dimensions and
    /// the EXIF turn. Stamping the marker without converting would be the
    /// silent reinterpretation the marker exists to prevent; leaving both
    /// alone lets a later attempt run. Both restore points sit behind
    /// `isLoaded`, so in the product this guard is theory.
    func migratedToFrameSpace(_ s: DevelopState) -> DevelopState {
        guard s.maskSpace == 0 else { return s }
        guard let handle, isLoaded else { return s }

        var out = s
        var geometry = OrionMaskLegacyGeometry(
            crop_x: s.cropX, crop_y: s.cropY, crop_w: s.cropW, crop_h: s.cropH,
            rotate_quarters: s.rotateQuarters,
            straighten_deg: s.straightenDeg,
            perspective_vertical: s.perspectiveVertical,
            perspective_horizontal: s.perspectiveHorizontal,
            perspective_aspect: s.perspectiveAspect)

        for i in out.maskComponents.indices {
            var c = out.maskComponents[i]
            var shape = OrionMaskShape(
                center_x: c.centerX, center_y: c.centerY,
                angle: c.angle, length: c.length,
                radius_x: c.radiusX, radius_y: c.radiusY,
                brush_radius: c.brushRadius)
            var dabs = c.brushStroke
            let pairs = Int32(dabs.count / 2)

            let status = dabs.withUnsafeMutableBufferPointer { buf in
                orion_engine_mask_place_to_frame(
                    handle, &geometry, c.kind, &shape,
                    pairs > 0 ? buf.baseAddress : nil, pairs)
            }
            // A refused conversion keeps the legacy numbers AND the legacy
            // marker, said out loud — better a mask that reads as it did than
            // one silently relabeled into a space it is not in.
            guard status == ORION_OK else {
                FileHandle.standardError.write(Data(
                    ("orion: mask \(i) could not be converted to frame "
                     + "coordinates and keeps its legacy reading\n").utf8))
                return s
            }

            c.centerX = shape.center_x
            c.centerY = shape.center_y
            c.angle = shape.angle
            c.length = shape.length
            c.radiusX = shape.radius_x
            c.radiusY = shape.radius_y
            c.brushRadius = shape.brush_radius
            c.brushStroke = dabs
            out.maskComponents[i] = c
        }

        out.maskSpace = 1
        return out
    }
}
