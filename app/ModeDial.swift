import SwiftUI

/// The tool selector, as the top edge of a camera's mode dial.
///
/// Seen edge-on, a mode dial shows you the setting you are on, the two either
/// side of it, and the knurled rim you would turn to get there. That is exactly
/// the information a tab bar carries — where you are, what is next to it — so
/// the shape is doing a job rather than being a costume.
///
/// The perspective is real, not painted: each mode is rotated about the Y axis
/// by its angle on the cylinder and scaled by the cosine of that angle, so it
/// foreshortens the way a curved surface does. Turning the dial animates the
/// whole set through their angles at once, and the mode you land on comes
/// square to the viewer.
///
/// Legibility comes first. The selected mode is face-on, at full contrast, with
/// its name written out; the neighbors are angled and dimmed but still whole
/// words, and every one of them is a full-height click target. A dial you have
/// to squint at would be a worse tab bar with a better story.
struct ModeDial: View {
    @Binding var tab: ToolTab

    @Environment(\.accessibilityReduceMotion) private var reduceMotion

    /// Degrees of turn per mode away from the selection. Shallow on purpose:
    /// the far mode is three steps out, and anything past about sixty degrees
    /// stops being a readable word.
    private let step: Double = 17

    private let height: CGFloat = 46

    private var index: Int {
        ToolTab.allCases.firstIndex(of: tab) ?? 0
    }

    var body: some View {
        // Exactly the shape the plain tab bar used, because that one laid out
        // correctly and three cleverer arrangements did not: placing the modes
        // by sin() of their angle put the far one edge-on and unclickable;
        // `.position` inside a ZStack stacked all four in one spot, since a
        // positioned child lands in whatever bounds the stack settled on; and
        // adding an outer `.frame(maxWidth:)` left them crammed against the
        // panel's leading edge.
        //
        // The dial was never in the placement. It is the rim behind the row and
        // the rotation on each mode, both of which sit on top of a layout that
        // works.
        HStack(spacing: 2) {
            ForEach(Array(ToolTab.allCases.enumerated()), id: \.element) { i, t in
                mode(t, offset: Double(i - index))
            }
        }
        .padding(.horizontal, 10)
        .frame(height: height)
        .background(rim)
        .accessibilityElement(children: .contain)
        .accessibilityLabel(Text("Tool"))
    }

    /// The knurled rim, and the shading that makes it a cylinder.
    ///
    /// Vertical grooves, densest at the edges where the surface turns away —
    /// which is what a photograph of a knurled dial looks like, and what a
    /// flat, evenly spaced hatch does not.
    private var rim: some View {
        ZStack {
            LinearGradient(
                stops: [.init(color: Color(white: 0.10), location: 0.0),
                        .init(color: Color(white: 0.20), location: 0.34),
                        .init(color: Color(white: 0.24), location: 0.5),
                        .init(color: Color(white: 0.17), location: 0.72),
                        .init(color: Color(white: 0.09), location: 1.0)],
                startPoint: .top, endPoint: .bottom)

            Canvas { context, size in
                // Spacing follows cos of the position across the rim, so the
                // grooves crowd toward both ends the way they do on a cylinder.
                let count = 90
                for i in 0...count {
                    let t = Double(i) / Double(count)
                    let angle = (t - 0.5) * .pi          // −90°…90° across
                    let x = size.width * (0.5 + sin(angle) / 2)
                    let lit = cos(angle)

                    var line = Path()
                    line.move(to: CGPoint(x: x, y: 3))
                    line.addLine(to: CGPoint(x: x, y: size.height - 3))
                    context.stroke(line,
                                   with: .color(.white.opacity(0.05 + 0.05 * lit)),
                                   lineWidth: 0.75)
                }
            }
        }
        .overlay(alignment: .bottom) {
            Rectangle().fill(Palette.line).frame(height: 1)
        }
        .allowsHitTesting(false)
    }

    /// One engraved mode on the rim.
    private func mode(_ t: ToolTab, offset: Double) -> some View {
        let angle = offset * step
        let facing = cos(angle * .pi / 180)
        let selected = t == tab

        // Past the horizon a mode would be edge-on and unreadable, so it fades
        // out rather than collapsing to a line.
        let visible = max(facing, 0)

        return Button {
            let turn = Animation.spring(response: 0.34, dampingFraction: 0.78)
            if reduceMotion { tab = t } else { withAnimation(turn) { tab = t } }
        } label: {
            VStack(spacing: 1) {
                // The index mark, in the selected mode's own slot. On a camera
                // the dial turns under a fixed mark; here the modes have to
                // stay put to stay clickable, so the mark travels instead — and
                // carrying it inside the slot means it cannot drift off the
                // thing it points at.
                Triangle()
                    .fill(Palette.accent)
                    .frame(width: 7, height: 4)
                    .opacity(selected ? 1 : 0)

                Image(systemName: t.symbol)
                    .font(.system(size: selected ? 13 : 11))
                Text(t.title)
                    .font(.system(size: selected ? 10 : 9,
                                  weight: selected ? .semibold : .regular))
                    .tracking(0.5)
                    .lineLimit(1)
                    .fixedSize()
            }
            .foregroundStyle(selected ? Palette.accent : Palette.dim)
            .opacity(0.45 + 0.55 * visible)
            .frame(maxWidth: .infinity)
            .frame(height: height)
            .contentShape(Rectangle())
        }
        .buttonStyle(.plain)
        // Scaled by how much of the face is turned toward the viewer, which is
        // the whole of the perspective — the rotation alone reads as a skew.
        .scaleEffect(x: 0.74 + 0.26 * visible, y: 0.93 + 0.07 * visible)
        // ⚠️ Verified live, not in the harness. A 3D transform does not survive
        // AppKit's offscreen `cacheDisplay`, so the screenshot suite renders
        // every mode square-on — the same class of blind spot STATUS records
        // for the Metal canvas. The horizontal scale above is deliberately
        // redundant with this: it carries the foreshortening on its own, so
        // what a screenshot *can* check is still the right shape.
        .rotation3DEffect(.degrees(angle), axis: (x: 0, y: 1, z: 0),
                          perspective: 0.5)
        .help(t.title)
    }
}

/// The dial's index mark.
private struct Triangle: Shape {
    func path(in rect: CGRect) -> Path {
        var p = Path()
        p.move(to: CGPoint(x: rect.midX, y: rect.maxY))
        p.addLine(to: CGPoint(x: rect.minX, y: rect.minY))
        p.addLine(to: CGPoint(x: rect.maxX, y: rect.minY))
        p.closeSubpath()
        return p
    }
}
