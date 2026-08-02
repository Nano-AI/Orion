// Split out of ViewportTests.swift 2026-07-31.

import CoreGraphics
import Foundation

// MARK: - Dust spots on the canvas
//
// A spot has two handles a couple of radii apart, and at any useful size they
// overlap. Which one a press grabs is the whole usability of the tool, and it is
// pure geometry — so it is pinned here rather than discovered by dragging.

extension ViewportTests {

    /// A map with the picture filling a 1000x1000 view at 1:1, so a normalized
    /// unit is 1000 points and the arithmetic below is readable.
    static var spotMap: CanvasLayout.PictureMap {
        var m = CanvasLayout.PictureMap()
        m.rect = CGRect(x: 0, y: 0, width: 1000, height: 1000)
        m.visible = CGSize(width: 1, height: 1)
        m.origin = .zero
        return m
    }

    /// ⚠ **The source wins where the two discs overlap.**
    ///
    /// A destination can also be reached by dragging the spot's body, and the
    /// automatic source is placed two and a half radii away — so at any radius
    /// worth using the two circles intersect. Whichever is tested first is the
    /// one that can always be grabbed, and the source is the one with no other
    /// route to it. Test destinations first and the source becomes unreachable
    /// exactly when the spot is large, which is when someone most wants it.
    static func testSpotHitPrefersTheSource() {
        var s = CanvasLayout.SpotPlacement()
        s.destination = CGPoint(x: 0.5, y: 0.5)
        s.source = CGPoint(x: 0.56, y: 0.5)     // 60 pt away
        s.radius = 0.05                          // 50 pt: the discs overlap

        // A point inside both.
        let both = CGPoint(x: 530, y: 500)
        report(CanvasLayout.spotHit(both, [s], in: spotMap) == .source(0),
               "where a spot's two discs overlap, the press takes the source",
               "\(String(describing: CanvasLayout.spotHit(both, [s], in: spotMap)))")

        // And the destination is still reachable where only it covers.
        let onlyDest = CGPoint(x: 465, y: 500)
        report(CanvasLayout.spotHit(onlyDest, [s], in: spotMap) == .destination(0),
               "and the destination answers where only it covers")

        // Empty canvas is nil, or an armed tool could never place a new spot.
        report(CanvasLayout.spotHit(CGPoint(x: 900, y: 100), [s], in: spotMap) == nil,
               "and open canvas grabs nothing")
    }

    /// Later spots are drawn over earlier ones, so a press has to answer with
    /// the one on top. Otherwise placing a spot on top of another makes the new
    /// one — the one being looked at — the only one that cannot be adjusted.
    static func testSpotHitPrefersTheTopmost() {
        var a = CanvasLayout.SpotPlacement()
        a.destination = CGPoint(x: 0.5, y: 0.5)
        a.source = CGPoint(x: 0.9, y: 0.9)
        a.radius = 0.04
        var b = a
        b.destination = CGPoint(x: 0.51, y: 0.5)
        b.source = CGPoint(x: 0.1, y: 0.1)

        let hit = CanvasLayout.spotHit(CGPoint(x: 505, y: 500), [a, b], in: spotMap)
        report(hit == .destination(1),
               "the spot drawn last is the spot a press finds",
               "\(String(describing: hit))")
    }

    /// ⚠ Dust is *supposed* to be small. The size slider goes down to 0.004 of
    /// the frame, which at fit zoom on a 24 MP photograph is about three view
    /// points — a target nobody can hit with a mouse. The handle has a floor
    /// even though the spot it draws does not.
    static func testSpotHandleHasAMinimumSize() {
        var s = CanvasLayout.SpotPlacement()
        s.destination = CGPoint(x: 0.5, y: 0.5)
        s.source = CGPoint(x: 0.9, y: 0.9)
        s.radius = 0.004                         // 4 pt on this map

        let near = CGPoint(x: 500 + 8, y: 500)   // outside the disc, inside the handle
        report(CanvasLayout.spotHit(near, [s], in: spotMap) == .destination(0),
               "a spot smaller than the handle floor is still grabbable",
               "radius \(CanvasLayout.spotRadius(s, in: spotMap)) pt, "
             + "floor \(CanvasLayout.spotHandleMin) pt")

        // But the floor is a floor, not a free-for-all: well outside still misses.
        report(CanvasLayout.spotHit(CGPoint(x: 540, y: 500), [s], in: spotMap) == nil,
               "and the floor does not make the whole picture one big handle")
    }

    /// A drag cannot push a spot off the photograph. Both centers are
    /// normalized picture coordinates and the shader clamps its reads, so an
    /// off-picture source would silently heal from the edge pixel — a spot that
    /// looks placed and quietly does something else.
    static func testSpotDragStaysOnThePicture() {
        let far = CanvasLayout.spotDrag(to: CGPoint(x: -240, y: 1400), in: spotMap)
        report(far == CGPoint(x: 0, y: 1),
               "a drag past the edge clamps to the picture", "\(far)")

        let inside = CanvasLayout.spotDrag(to: CGPoint(x: 250, y: 750), in: spotMap)
        report(abs(inside.x - 0.25) < 1e-6 && abs(inside.y - 0.75) < 1e-6,
               "and a drag inside it maps straight through", "\(inside)")
    }
}
