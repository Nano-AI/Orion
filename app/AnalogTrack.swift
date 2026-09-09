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

    /// Called when the drag starts and again when it ends.
    ///
    /// ⚠ This is what arms degrade-then-refine, and it is the control that
    /// says so rather than the engine guessing from how fast values arrive. A
    /// timer-based guess renders the *first* tick of every drag at full
    /// resolution — the expensive one, since it is the tick that dirties the
    /// graph — and gets a keyboard nudge wrong in the other direction.
    var interacting: (Bool) -> Void = { _ in }

    /// A label gradient for the groove — what each extreme would do, painted
    /// where the throw goes. See `TrackTint`. Nil renders exactly the plain
    /// control; a tinted track trades the accent departure fill for the
    /// gradient revealed at full strength over the throw, because the color
    /// under the thumb *is* the departure.
    var tint: Gradient?

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

    /// The index scale: quarters of the throw, and the index mark.
    ///
    /// Deliberately unlabeled. The numbers under a camera's scale are the ones
    /// the scale is *for*; here the readout above already carries the exact
    /// value, and repeating it in five places would be noise rather than
    /// information.
    ///
    /// ⚠ **The sixteen minor marks between them are gone, and the same argument
    /// is why.** The comment above was right about labels and wrong to stop
    /// there: twenty-one teeth under a control that prints its own value is the
    /// noise it warns about, and a panel stacks ten of these — two hundred marks
    /// on one screen, none of which is read. Quarters are what the doc claimed
    /// the scale was for, and the accent index mark is the one a photographer
    /// actually looks for. Majors dropped to `faint` for the same reason: the
    /// index mark has to be the brightest thing on the rule or it is not an
    /// index mark.
    private func scale(width: CGFloat, travel: CGFloat) -> some View {
        Canvas { context, _ in
            let left = thumbWidth / 2

            for i in 0...4 {
                let x = left + CGFloat(i) / 4 * travel

                var line = Path()
                line.move(to: CGPoint(x: x, y: 0))
                line.addLine(to: CGPoint(x: x, y: scaleHeight))
                context.stroke(line, with: .color(Palette.faint), lineWidth: 1)
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

            if let tint {
                // The label, faint across the whole throw so both extremes can
                // be read before moving anything…
                Capsule()
                    .fill(LinearGradient(gradient: tint,
                                         startPoint: .leading, endPoint: .trailing))
                    .opacity(0.45)
                    .frame(width: width, height: grooveHeight - 1.5)
                    .offset(y: 0.75)

                // …and at full strength over the departure, in place of the
                // accent bar: the throw *reveals* the color it is applying,
                // still growing from the index mark, not the far edge.
                Capsule()
                    .fill(LinearGradient(gradient: tint,
                                         startPoint: .leading, endPoint: .trailing))
                    .opacity(0.85)
                    .frame(width: width, height: grooveHeight - 1.5)
                    .offset(y: 0.75)
                    .mask(alignment: .topLeading) {
                        Rectangle()
                            .frame(width: max(hi - lo, 0))
                            .offset(x: lo)
                    }
            } else {
                // Filled from the index mark, not from the left end. What the
                // control is doing is a departure from its default, and a bar
                // that grows from the far edge says the wrong thing about a
                // value like exposure, whose neutral is in the middle.
                Capsule()
                    .fill(Palette.accent.opacity(0.55))
                    .frame(width: max(hi - lo, 0), height: grooveHeight - 1.5)
                    .offset(x: lo, y: 0.75)
            }
        }
        .frame(width: width, height: grooveHeight, alignment: .topLeading)
        .offset(y: grooveY - grooveHeight / 2)
        .allowsHitTesting(false)
    }

    /// A milled cylinder: lit from above, knurled across its face.
    ///
    /// ⚠ **Quieted rather than removed.** The knurl was five grooves drawn
    /// twice each — a black line at 0.28 and a white one beside it at 0.35 —
    /// over a three-stop gradient, inside nineteen points. At render size that
    /// is not a machined face, it is a hash: this control appears about forty
    /// times in one panel, which made the thumbs the loudest thing in a window
    /// whose whole job is to be quieter than the photograph. Three grooves at a
    /// third of the contrast still read as milled at a glance and stop
    /// competing with the picture. The three-stop gradient went with them —
    /// the light one at the bottom was reading as a second highlight and
    /// muddying the middle.
    private var thumb: some View {
        ZStack {
            RoundedRectangle(cornerRadius: 3, style: .continuous)
                .fill(LinearGradient(
                    colors: [Color(white: 0.74), Color(white: 0.52)],
                    startPoint: .top, endPoint: .bottom))

            Canvas { context, size in
                for i in 0..<3 {
                    let x = size.width / 2 + CGFloat(i - 1) * 3.5
                    var line = Path()
                    line.move(to: CGPoint(x: x, y: 3.5))
                    line.addLine(to: CGPoint(x: x, y: size.height - 3.5))
                    context.stroke(line, with: .color(.black.opacity(0.20)), lineWidth: 0.75)

                    var light = Path()
                    light.move(to: CGPoint(x: x + 0.75, y: 3.5))
                    light.addLine(to: CGPoint(x: x + 0.75, y: size.height - 3.5))
                    context.stroke(light, with: .color(.white.opacity(0.12)), lineWidth: 0.75)
                }
            }

            RoundedRectangle(cornerRadius: 3, style: .continuous)
                .strokeBorder(.black.opacity(0.30), lineWidth: 0.5)
        }
        .frame(width: thumbWidth, height: thumbHeight)
        .shadow(color: .black.opacity(dragging ? 0.45 : 0.28),
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
                    interacting(true)
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
                interacting(false)
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
