import SwiftUI

/// Shows where the viewport sits inside the whole frame.
///
/// Once you are past fit, the canvas alone gives you no idea which part of the
/// photo you are looking at. This is the smallest thing that answers that —
/// the frame's outline with the visible region drawn inside it.
struct Navigator: View {
    let imageWidth: UInt32
    let imageHeight: UInt32
    let viewport: Viewport
    /// The canvas's own aspect, needed to work out the visible fraction.
    let viewAspect: CGFloat

    private let boxWidth: CGFloat = 92

    var body: some View {
        let imageAspect = CGFloat(imageWidth) / CGFloat(max(imageHeight, 1))
        let boxHeight = boxWidth / max(imageAspect, 0.01)
        let visible = viewport.visibleFraction(imageAspect: imageAspect,
                                               viewAspect: viewAspect)

        ZStack(alignment: .topLeading) {
            RoundedRectangle(cornerRadius: 2)
                .fill(Color.black.opacity(0.45))
            RoundedRectangle(cornerRadius: 2)
                .strokeBorder(Color.white.opacity(0.28), lineWidth: 1)

            Rectangle()
                .strokeBorder(Palette.accent, lineWidth: 1.5)
                .frame(width: boxWidth * min(visible.width, 1),
                       height: boxHeight * min(visible.height, 1))
                .offset(x: boxWidth * max(0, viewport.center.x - visible.width / 2),
                        y: boxHeight * max(0, viewport.center.y - visible.height / 2))
        }
        .frame(width: boxWidth, height: boxHeight)
        .allowsHitTesting(false)
    }
}
