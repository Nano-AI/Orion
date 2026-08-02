import SwiftUI

/// Shows the color under the cursor while the targeted tool is armed.
///
/// Without it you are clicking blind: the bands are 60° wide and a sky can
/// cross from aqua into blue without looking like it changed. This makes the
/// pick legible before you commit to it.
struct ColorLoupe: View {
    let targeted: TargetedAdjust

    private let diameter: CGFloat = 46

    var body: some View {
        if let point = targeted.hoverPoint, let color = targeted.hoverColor {
            VStack(spacing: 5) {
                ZStack {
                    Circle()
                        .fill(Color(cgColor: color))
                        .frame(width: diameter, height: diameter)

                    // Two rings: a dark one so the swatch reads against a pale
                    // photo, a light one so it reads against a dark photo.
                    Circle()
                        .strokeBorder(Color.black.opacity(0.55), lineWidth: 3)
                        .frame(width: diameter, height: diameter)
                    Circle()
                        .strokeBorder(Color.white.opacity(0.9), lineWidth: 1.5)
                        .frame(width: diameter, height: diameter)

                    // Crosshair marks the exact sampled pixel.
                    Path { p in
                        p.move(to: CGPoint(x: diameter / 2 - 7, y: diameter / 2))
                        p.addLine(to: CGPoint(x: diameter / 2 + 7, y: diameter / 2))
                        p.move(to: CGPoint(x: diameter / 2, y: diameter / 2 - 7))
                        p.addLine(to: CGPoint(x: diameter / 2, y: diameter / 2 + 7))
                    }
                    .stroke(Color.white.opacity(0.85), lineWidth: 1)
                    .frame(width: diameter, height: diameter)
                    .blendMode(.difference)
                }

                Text(label)
                    .font(.system(size: 10, weight: .medium))
                    .foregroundStyle(.white)
                    .padding(.horizontal, 7)
                    .padding(.vertical, 3)
                    .background(Color.black.opacity(0.72),
                                in: RoundedRectangle(cornerRadius: 4))
            }
            // Sit above and left of the cursor so the swatch is never hidden
            // under the pointer itself.
            .position(x: point.x, y: point.y - diameter)
            .allowsHitTesting(false)
            // ⚠️ No animation on the position, and this is the reported
            // "eyedropper latency". It carried `.linear(duration: 0.05)`, which
            // means the loupe is *always* 50 ms behind the pointer and never
            // arrives while the hand is still moving. The engine read behind it
            // costs 2.4 us — measured, `repro/eyedropper-latency.txt` — so
            // every millisecond of the lag was this line.
            //
            // It is also wrong rather than merely slow: the color and the band
            // change the instant the sample lands while the crosshair
            // interpolates, so a crosshair captioned "the exact sampled pixel"
            // sat somewhere the sample was not taken.
        }
    }

    private var label: String {
        if targeted.hoverIsNeutral { return "neutral — no hue" }
        return targeted.hoverBand?.name ?? "—"
    }
}
