import AppKit
import SwiftUI

/// The adjustment track: an engraved channel, an index scale, and a milled
/// thumb that detents where the control's default is.
///
/// A camera's controls tell you where you are without being read. An exposure
/// compensation dial has an index mark at zero you can find by feel, a scale
/// you can count against, and a click when you cross the middle. A flat
/// rectangle with a dot on it tells you none of that — you have to look at the
/// number to know you have moved.
///
/// So the scale is drawn, the default gets its own taller mark in the accent,
/// and the value snaps to it when it comes close. Everything read at a glance:
/// how far the throw goes, where neutral is, and whether you are past it.
///
/// Linear, not rotary. A round dial is the more literal translation, but a
/// panel of nineteen of them is twice as tall, harder to scan down, and rotary
/// dragging is less precise than linear for the fine work this control is for.
struct AnalogTrack: View {
    let value: Float
    let range: ClosedRange<Float>

    /// Where the detent sits — the value this control returns to.
    let base: Float

    /// Called with each new value. The caller routes it through history, so
    /// a whole drag lands as one undo step.
    let set: (Float) -> Void

    @Environment(\.accessibilityReduceMotion) private var reduceMotion

    @State private var dragging = false
    @State private var startValue: Float?
    @State private var startFraction: CGFloat = 0

    private let height: CGFloat = 21
    private let thumbWidth: CGFloat = 19
    private let thumbHeight: CGFloat = 13
    private let grooveHeight: CGFloat = 5
    private let scaleHeight: CGFloat = 5

    /// Where the groove's centerline sits, leaving the scale room above it.
    private var grooveY: CGFloat { scaleHeight + 3 + grooveHeight / 2 }

    private var span: Float { max(range.upperBound - range.lowerBound, 1e-6) }

    private func fraction(_ v: Float) -> CGFloat {
        CGFloat((min(max(v, range.lowerBound), range.upperBound) - range.lowerBound) / span)
    }

    var body: some View {
        GeometryReader { geo in
            let width = max(geo.size.width, thumbWidth + 1)
            let travel = width - thumbWidth
            let thumbX = thumbWidth / 2 + fraction(value) * travel

            ZStack(alignment: .topLeading) {
                scale(width: width, travel: travel)
                groove(width: width, travel: travel, thumbX: thumbX)
                thumb.position(x: thumbX, y: grooveY)
            }
            .frame(width: geo.size.width, height: height, alignment: .topLeading)
            .contentShape(Rectangle())
            .gesture(drag(width: width, travel: travel))
        }
        .frame(height: height)
        .accessibilityElement()
        .accessibilityValue(Text(String(format: "%.2f", value)))
        .accessibilityAdjustableAction { direction in
            // A hundredth of the throw per press, which matches the step the
            // readouts print to.
            let step = span / 100
            switch direction {
            case .increment: commit(value + step, snapping: false)
            case .decrement: commit(value - step, snapping: false)
            @unknown default: break
            }
        }
    }

    // MARK: The engraving

    /// The index scale. Five major marks and four minor between each, which is
    /// enough to read a quarter of the throw off without counting.
    ///
    /// Deliberately unlabeled. The numbers under a camera's scale are the ones
    /// the scale is *for*; here the readout above already carries the exact
    /// value, and repeating it in five places would be noise rather than
    /// information.
    private func scale(width: CGFloat, travel: CGFloat) -> some View {
        Canvas { context, _ in
            let left = thumbWidth / 2
            let divisions = 20

            for i in 0...divisions {
                let t = CGFloat(i) / CGFloat(divisions)
                let x = left + t * travel
                let major = i % 5 == 0
                let h = major ? scaleHeight : scaleHeight * 0.42

                var line = Path()
                line.move(to: CGPoint(x: x, y: scaleHeight - h))
                line.addLine(to: CGPoint(x: x, y: scaleHeight))
                context.stroke(line, with: .color(major ? Palette.dim : Palette.faint),
                               lineWidth: 1)
            }

            // The index mark. Taller than a major tick and in the accent, so
            // where the control started is findable without moving it.
            let x = left + fraction(base) * travel
            var index = Path()
            index.move(to: CGPoint(x: x, y: 0))
            index.addLine(to: CGPoint(x: x, y: scaleHeight + 2))
            context.stroke(index, with: .color(Palette.accent), lineWidth: 1.5)
        }
        .frame(width: width, height: scaleHeight + 1.5)
        .allowsHitTesting(false)
    }

    /// The channel the thumb runs in, and the throw taken out of it.
    private func groove(width: CGFloat, travel: CGFloat, thumbX: CGFloat) -> some View {
        let left = thumbWidth / 2
        let baseX = left + fraction(base) * travel
        let lo = min(baseX, thumbX)
        let hi = max(baseX, thumbX)

        return ZStack(alignment: .topLeading) {
            // Engraved: darker than the panel it sits in, with a hairline on
            // the upper edge where a milled channel catches the light.
            Capsule()
                .fill(Palette.ground)
                .frame(width: width, height: grooveHeight)
                .overlay(
                    Capsule().strokeBorder(Palette.line, lineWidth: 0.5)
                )

            // Filled from the index mark, not from the left end. What the
            // control is doing is a departure from its default, and a bar that
            // grows from the far edge says the wrong thing about a value like
            // exposure, whose neutral is in the middle.
            Capsule()
                .fill(Palette.accent.opacity(0.55))
                .frame(width: max(hi - lo, 0), height: grooveHeight - 1.5)
                .offset(x: lo, y: 0.75)
        }
        .frame(width: width, height: grooveHeight, alignment: .topLeading)
        .offset(y: grooveY - grooveHeight / 2)
        .allowsHitTesting(false)
    }

    /// A milled cylinder: lit from above, knurled across its face.
    private var thumb: some View {
        ZStack {
            RoundedRectangle(cornerRadius: 3, style: .continuous)
                .fill(LinearGradient(
                    colors: [Color(white: 0.78), Color(white: 0.55), Color(white: 0.68)],
                    startPoint: .top, endPoint: .bottom))

            Canvas { context, size in
                // Knurling. Five grooves, so it reads as machined at this size
                // rather than as a hatch pattern.
                for i in 0..<5 {
                    let x = size.width / 2 + CGFloat(i - 2) * 3
                    var line = Path()
                    line.move(to: CGPoint(x: x, y: 3))
                    line.addLine(to: CGPoint(x: x, y: size.height - 3))
                    context.stroke(line, with: .color(.black.opacity(0.28)), lineWidth: 0.75)

                    var light = Path()
                    light.move(to: CGPoint(x: x + 0.75, y: 3))
                    light.addLine(to: CGPoint(x: x + 0.75, y: size.height - 3))
                    context.stroke(light, with: .color(.white.opacity(0.35)), lineWidth: 0.75)
                }
            }

            RoundedRectangle(cornerRadius: 3, style: .continuous)
                .strokeBorder(.black.opacity(0.35), lineWidth: 0.5)
        }
        .frame(width: thumbWidth, height: thumbHeight)
        .shadow(color: .black.opacity(dragging ? 0.5 : 0.35),
                radius: dragging ? 3 : 1.5, y: 1)
        .scaleEffect(dragging ? 1.06 : 1)
        .animation(reduceMotion ? nil : .easeOut(duration: 0.12), value: dragging)
    }

    // MARK: Dragging

    private func drag(width: CGFloat, travel: CGFloat) -> some Gesture {
        DragGesture(minimumDistance: 0)
            .onChanged { g in
                let unit = (g.location.x - thumbWidth / 2) / max(travel, 1)

                if startValue == nil {
                    dragging = true
                    // A press away from the thumb jumps to it, the way clicking
                    // a track does everywhere on this platform; a press on the
                    // thumb picks it up where it stands.
                    let thumbFraction = fraction(value)
                    let onThumb = abs(unit - thumbFraction) * travel < thumbWidth
                    startFraction = onThumb ? thumbFraction : min(max(unit, 0), 1)
                    startValue = range.lowerBound + Float(startFraction) * span
                    if !onThumb { commit(startValue ?? value, snapping: true) }
                    return
                }

                // Option slows the throw to a fifth, for the last hundredth of
                // a value that a full-width drag cannot resolve.
                let fine = NSEvent.modifierFlags.contains(.option)
                let moved = g.translation.width / max(travel, 1) * (fine ? 0.2 : 1)
                let target = range.lowerBound + Float(startFraction + moved) * span
                commit(target, snapping: !fine)
            }
            .onEnded { _ in
                startValue = nil
                dragging = false
            }
    }

    /// Clamps, detents, and hands the value up.
    ///
    /// The detent is what makes the default findable by drag rather than by
    /// eye, and it snaps to the base *exactly* — so a control dragged back to
    /// neutral stops reporting itself as modified, instead of sitting at
    /// 0.0004 and claiming an edit that is not visible in the picture.
    private func commit(_ raw: Float, snapping: Bool) {
        var v = min(max(raw, range.lowerBound), range.upperBound)
        if snapping, abs(v - base) < span * 0.015 { v = base }
        guard v != value else { return }
        set(v)
    }
}
