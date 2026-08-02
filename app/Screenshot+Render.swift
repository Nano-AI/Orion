import AppKit
import SwiftUI

/// A SwiftUI hierarchy to a PNG, offscreen.
///
/// The window is real but off screen, because SwiftUI defers a good deal of
/// work until a view is in one and a detached hierarchy renders empty. The
/// scroll is real too — see `scrollToEnd`, which is `detail-tail`'s mechanism
/// and lives here because it is a walk over `NSView`, not a scene.
extension Screenshot {

    // MARK: Rendering

    /// Scrolls the interface's tallest scrolling region to its end, and says how
    /// far it went.
    ///
    /// The panel column is a plain SwiftUI `ScrollView`, which AppKit backs with
    /// an `NSScrollView` — so the scroll is the real one, performed on the real
    /// view, and what the capture shows afterwards is what a photographer sees
    /// after a flick of the wheel. Chosen by overflow rather than by name: the
    /// filmstrip is the other scroll view in the hierarchy and it scrolls
    /// sideways, so its document is exactly as tall as its clip and it can never
    /// win.
    ///
    /// ⚠ Returns nil when the hierarchy holds no scroll view at all, and 0 when
    /// nothing overflows. **Both are refused by the caller**, because a scene
    /// whose whole purpose is to photograph what is past the fold and which
    /// finds no fold has stopped covering anything — which is the failure this
    /// scene was written against, and it must not be able to reappear as a pass.
    /// The height of the scrolling region `scrollToEnd` last moved, so the
    /// diagnostic can say what fraction of the panel a frame accounts for.
    private static var panelClip: CGFloat = 0

    private static func scrollToEnd(_ root: NSView) -> CGFloat? {
        var best: (scroll: NSScrollView, overflow: CGFloat)?
        func walk(_ v: NSView) {
            if let s = v as? NSScrollView, let doc = s.documentView {
                let overflow = doc.frame.height - s.contentView.bounds.height
                if best == nil || overflow > best!.overflow { best = (s, overflow) }
            }
            for sub in v.subviews { walk(sub) }
        }
        walk(root)

        guard let found = best else { return nil }
        panelClip = found.scroll.contentView.bounds.height
        guard found.overflow > 1, let doc = found.scroll.documentView else { return 0 }
        // A flipped document counts down from the top, so its end is the
        // overflow; an unflipped one counts up from the bottom, so its end is 0.
        found.scroll.contentView.scroll(to: NSPoint(x: 0,
                                                    y: doc.isFlipped ? found.overflow : 0))
        found.scroll.reflectScrolledClipView(found.scroll.contentView)
        return found.overflow
    }

    static func render<V: View>(_ view: V, size: CGSize,
                                        scrolledToBottom: Bool = false) -> Data? {
        let hosting = NSHostingView(rootView: view)
        hosting.frame = CGRect(origin: .zero, size: size)

        // A window, even an invisible one: SwiftUI defers a good deal of work
        // until a view is in one, and a detached hierarchy renders empty.
        let window = NSWindow(contentRect: hosting.frame,
                              styleMask: [.borderless],
                              backing: .buffered, defer: false)
        window.contentView = hosting
        window.appearance = NSAppearance(named: .darkAqua)
        window.setFrameOrigin(NSPoint(x: -20000, y: -20000))
        window.orderFront(nil)

        // SwiftUI lays out and loads asynchronously. Turn the runloop until it
        // settles; capturing on the first pass gives a half-built interface.
        for _ in 0..<12 {
            RunLoop.current.run(until: Date().addingTimeInterval(0.05))
        }
        hosting.layoutSubtreeIfNeeded()
        hosting.displayIfNeeded()

        if scrolledToBottom {
            guard let overflow = scrollToEnd(hosting) else {
                fail("no scrolling region in the interface — this scene "
                     + "photographs what is past the fold and there is no fold")
            }
            guard overflow > 1 else {
                fail("nothing overflows the panel column, so a scene that "
                     + "captures it scrolled captures the same thing as the "
                     + "scene that does not")
            }
            FileHandle.standardError.write(Data(
                String(format: "orion: scrolled the panel column %.1f points "
                       + "(%.1f of content past a %.1f-point window)\n",
                       overflow, overflow + panelClip, panelClip).utf8))
            // The scroll is a layout change like any other: settle again before
            // the capture, or the frame is the interface mid-move.
            for _ in 0..<6 {
                RunLoop.current.run(until: Date().addingTimeInterval(0.05))
            }
            hosting.layoutSubtreeIfNeeded()
            hosting.displayIfNeeded()
        }

        guard let rep = hosting.bitmapImageRepForCachingDisplay(in: hosting.bounds) else {
            return nil
        }
        hosting.cacheDisplay(in: hosting.bounds, to: rep)
        window.orderOut(nil)
        return rep.representation(using: .png, properties: [:])
    }
}
