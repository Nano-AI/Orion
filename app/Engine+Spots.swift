import Foundation
import Metal
import MetalKit
import AppKit

/// Spot removal — research/spot-removal.md.
///
/// A spot is stored in **frame** coordinates and converted at the edge, so it
/// follows the subject through a later crop or quarter turn. A mask does the
/// opposite. Same transform, applied at a different moment.

extension Engine {

    /// Places a spot at a point on the displayed picture.
    ///
    /// ⚠ The source is chosen automatically, and the rule is Orion's own: the
    /// same distance away as two and a half radii, along whichever axis has
    /// room. Lightroom searches for a matching patch; that is a different and
    /// much larger feature, and for sensor dust — which sits on smooth
    /// backgrounds by definition — anywhere nearby is as good as anywhere else.
    /// The photographer can see the result immediately and place it again if
    /// the guess was poor.
    @discardableResult
    func addSpot(atFrame displayed: CGPoint) -> Bool {
        guard isLoaded, let handle, spots.count < Self.maxSpots else { return false }

        // ⚠ Converted here, once, rather than on every render. Dust is on the
        // sensor, so a spot has to follow the subject through a later crop or
        // quarter turn — the opposite of a mask, which stays where it was put on
        // screen. Same transform, applied at a different moment, and that
        // moment is the whole difference between the two behaviors.
        var fx: Float = 0, fy: Float = 0
        guard orion_engine_to_frame(handle, Float(displayed.x), Float(displayed.y),
                                    &fx, &fy) == ORION_OK else { return false }
        let p = CGPoint(x: CGFloat(fx), y: CGFloat(fy))

        let step = spotRadius * 2.5
        var sx = Float(p.x) + step
        if sx > 1 - spotRadius { sx = Float(p.x) - step }
        var sy = Float(p.y)
        // Nowhere to go sideways on a very small frame; try the other axis.
        if sx < spotRadius || sx > 1 - spotRadius {
            sx = Float(p.x)
            sy = Float(p.y) + step
            if sy > 1 - spotRadius { sy = Float(p.y) - step }
        }

        var s = SpotState()
        s.destX = Float(p.x); s.destY = Float(p.y)
        s.srcX = min(max(sx, 0), 1); s.srcY = min(max(sy, 0), 1)
        s.radius = spotRadius
        s.feather = spotFeather
        s.heal = spotHeal
        spots.append(s)
        selectedSpot = spots.count - 1
        pushAndRender()
        history.record(state, label: "Spot"); log.committed(state, label: "Spot")
        return true
    }

    /// A point in frame coordinates, as a point on the displayed picture.
    /// The inverse of the transform `addSpot` uses.
    func displayedPoint(fromFrame p: CGPoint) -> CGPoint? {
        guard isLoaded, let handle else { return nil }
        var x: Float = 0, y: Float = 0
        guard orion_engine_from_frame(handle, Float(p.x), Float(p.y),
                                      &x, &y) == ORION_OK else { return nil }
        return CGPoint(x: CGFloat(x), y: CGFloat(y))
    }

    /// The spots as the canvas handles them, in displayed coordinates.
    ///
    /// ⚠ Converted here and nowhere else. `CanvasLayout` and the overlay work
    /// entirely in displayed coordinates; giving either of them the frame
    /// transform would be a second copy of it.
    var spotPlacements: [CanvasLayout.SpotPlacement] {
        spots.compactMap { s in
            guard let d = displayedPoint(fromFrame: CGPoint(x: CGFloat(s.destX),
                                                            y: CGFloat(s.destY))),
                  let src = displayedPoint(fromFrame: CGPoint(x: CGFloat(s.srcX),
                                                              y: CGFloat(s.srcY)))
            else { return nil }
            return CanvasLayout.SpotPlacement(destination: d, source: src,
                                              radius: CGFloat(s.radius),
                                              heal: s.heal)
        }
    }

    /// Moves a spot's destination or its source, from a displayed point.
    ///
    /// Renders without recording, exactly as `setCrop` does — a drag is one
    /// history entry, committed on release, not sixty.
    func moveSpot(_ index: Int, destination: CGPoint?, source: CGPoint?) {
        guard isLoaded, let handle, spots.indices.contains(index) else { return }
        func toFrame(_ p: CGPoint) -> (Float, Float)? {
            var fx: Float = 0, fy: Float = 0
            guard orion_engine_to_frame(handle, Float(p.x), Float(p.y),
                                        &fx, &fy) == ORION_OK else { return nil }
            return (fx, fy)
        }
        if let d = destination, let (fx, fy) = toFrame(d) {
            spots[index].destX = fx; spots[index].destY = fy
        }
        if let src = source, let (fx, fy) = toFrame(src) {
            spots[index].srcX = fx; spots[index].srcY = fy
        }
        pushAndRender()
    }

    /// One history entry when a spot drag finishes.
    ///
    /// ⚠ **Labelled "Move spot", not "Spot".** `EditHistory` coalesces
    /// consecutive entries carrying the same label — which is what makes a
    /// slider drag one undo step instead of sixty — so a placement and a later
    /// move both labeled "Spot" merged into one entry, and undoing the move
    /// deleted the spot instead. Two different acts need two different names.
    ///
    /// The place-and-drag gesture is deliberately *not* two entries: `addSpot`
    /// has already recorded, and the overlay skips this call while the spot it
    /// is dragging the source of is the one it just created.
    func commitSpotEdit() { history.record(state, label: "Move spot"); log.committed(state, label: "Move spot") }

    /// Removes one spot by index, which is what a selected spot and a Delete
    /// key mean. `removeLastSpot` stays for the panel's Undo spot button.
    func removeSpot(_ index: Int) {
        guard spots.indices.contains(index) else { return }
        spots.remove(at: index)
        selectedSpot = -1
        pushAndRender()
        history.record(state, label: "Spot"); log.committed(state, label: "Spot")
    }

    func removeLastSpot() {
        guard !spots.isEmpty else { return }
        spots.removeLast()
        pushAndRender()
        history.record(state, label: "Spot"); log.committed(state, label: "Spot")
    }

    func clearSpots() {
        guard !spots.isEmpty else { return }
        spots.removeAll()
        pushAndRender()
        history.record(state, label: "Clear spots")
    }
}
