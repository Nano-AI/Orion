import SwiftUI

/// One grading wheel: a hue disc with a draggable puck, and the zone's
/// luminance under it.
///
/// The disc is the control surface every grading application uses, and it is
/// used here for the reason it is used there — hue and strength are one gesture
/// rather than two sliders, and the direction you push is the direction the
/// color goes. Angle picks the hue, distance from the center picks how far.
///
/// Brightness is deliberately *not* on the disc. The engine makes each wheel's
/// offset zero-sum, so pushing toward yellow tints without lifting; the slider
/// beneath is the only thing that changes the zone's luminance. Two controls,
/// two axes, no interaction between them — which is what makes a grade
/// recoverable when you overshoot.
struct ColorWheel: View {
    let title: String
    /// [x, y, luminance]. x and y are the puck in the unit disc.
    @Binding var value: [Float]
    let engine: Engine

    @State private var dragging = false

    private let size: CGFloat = 92
    private let puck: CGFloat = 11

    private var point: CGPoint {
        CGPoint(x: CGFloat(value.count > 0 ? value[0] : 0),
                y: CGFloat(value.count > 1 ? value[1] : 0))
    }

    private var modified: Bool {
        value.contains { abs($0) > 0.001 }
    }

    var body: some View {
        VStack(spacing: 6) {
            HStack(spacing: 4) {
                Text(title.uppercased())
                    .font(.system(size: 9, weight: .semibold))
                    .tracking(0.7)
                    .foregroundStyle(modified ? Palette.accent : Palette.dim)
                if modified {
                    Button {
                        engine.edit("Reset \(title)") { value = [0, 0, 0] }
                    } label: {
                        Image(systemName: "arrow.uturn.backward")
                            .font(.system(size: 8, weight: .semibold))
                            .foregroundStyle(Palette.accent)
                            .frame(width: 12, height: 12)
                            .contentShape(Rectangle())
                    }
                    .buttonStyle(.plain)
                    .help("Reset \(title)")
                }
            }

            disc

            AnalogTrack(value: value.count > 2 ? value[2] : 0,
                        range: -0.5...0.5, base: 0) { v in
                engine.edit("\(title) luminance") { value[2] = v }
            }
            .frame(width: size)
        }
    }

    private var disc: some View {
        ZStack {
            // The hue ring, wound to match the engine.
            //
            // `gradeOffsets` puts red at 0°, green at 120° and blue at 240°
            // measured counter-clockwise with y upward — the usual mathematical
            // convention. `AngularGradient` sweeps *clockwise* from three
            // o'clock in screen coordinates, where y runs downward, so the
            // familiar R-Y-G-C-B-M order paints green where the engine puts
            // blue and vice versa. Dragging toward what looked like green made
            // the picture blue.
            //
            // Reversing the order winds the ring the same way the maths does.
            // `testGradePrimaryAngles` pins the engine half of that; this
            // comment is the only thing holding the other half, so do not
            // "tidy" this back into alphabetical rainbow order.
            Circle()
                .fill(AngularGradient(
                    colors: [.red, Color(red: 1, green: 0, blue: 1), .blue,
                             .cyan, .green, .yellow, .red],
                    center: .center))
                // Saturation falls to nothing at the middle, so the center
                // reads as "no correction" rather than as a color you happen
                // to be sitting on.
                .overlay(
                    RadialGradient(colors: [Palette.panel, Palette.panel.opacity(0)],
                                   center: .center, startRadius: 0,
                                   endRadius: size / 2)
                )
                .overlay(Circle().strokeBorder(Palette.line, lineWidth: 1))

            // Crosshair, so the center is findable when the puck is away from
            // it and there is nothing else to judge the middle against.
            Path { p in
                p.move(to: CGPoint(x: size / 2 - 4, y: size / 2))
                p.addLine(to: CGPoint(x: size / 2 + 4, y: size / 2))
                p.move(to: CGPoint(x: size / 2, y: size / 2 - 4))
                p.addLine(to: CGPoint(x: size / 2, y: size / 2 + 4))
            }
            .stroke(.white.opacity(0.35), lineWidth: 1)

            Circle()
                .fill(.white)
                .frame(width: puck, height: puck)
                .overlay(Circle().strokeBorder(.black.opacity(0.5), lineWidth: 1))
                .shadow(color: .black.opacity(0.5), radius: dragging ? 3 : 1.5)
                .position(x: size / 2 + point.x * (size / 2 - puck / 2),
                          // Screen y grows downward; the wheel's does not, so
                          // dragging up has to move the puck up.
                          y: size / 2 - point.y * (size / 2 - puck / 2))
                .allowsHitTesting(false)
        }
        .frame(width: size, height: size)
        .contentShape(Circle())
        .gesture(
            DragGesture(minimumDistance: 0)
                .onChanged { g in
                    dragging = true
                    let r = size / 2 - puck / 2
                    var x = (g.location.x - size / 2) / r
                    var y = (size / 2 - g.location.y) / r

                    // Clamped to the disc, not the square that bounds it. Past
                    // the rim the angle is still meaningful and the radius is
                    // not, so the puck slides around the edge instead of
                    // sticking in a corner the wheel does not have.
                    let d = sqrt(x * x + y * y)
                    if d > 1 { x /= d; y /= d }

                    engine.edit(title) {
                        value[0] = Float(x)
                        value[1] = Float(y)
                    }
                }
                .onEnded { _ in dragging = false }
        )
        .accessibilityElement()
        .accessibilityLabel(Text(title))
    }
}
