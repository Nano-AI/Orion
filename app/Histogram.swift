import SwiftUI

/// The histogram — the instrument a photographer actually reads.
///
/// This is the page's signature element, and it earns that by being true to the
/// subject rather than decorative: it is the one piece of chrome a photographer
/// looks at as often as the photo. Everything else in the panel is a control;
/// this is a readout.
///
/// Drawn as filled channel curves with screen blending, so overlapping channels
/// go white where all three agree — which is how every darkroom instrument
/// behaves and how a photographer already knows to read one.
struct Histogram: View {
    let bins: [UInt32]
    let height: CGFloat

    private var binCount: Int { bins.count / 3 }

    var body: some View {
        ZStack {
            RoundedRectangle(cornerRadius: 3)
                .fill(Color(red: 0.063, green: 0.063, blue: 0.071))

            if binCount > 1 {
                GeometryReader { geo in
                    // A shared ceiling across channels, so relative heights
                    // stay meaningful. The 99th percentile rather than the max:
                    // one blown highlight bin would otherwise flatten the
                    // entire curve into the baseline.
                    let ceiling = max(percentileCeiling(), 1)

                    ZStack {
                        grid(in: geo.size)

                        ForEach(0..<3, id: \.self) { channel in
                            channelPath(channel, in: geo.size, ceiling: ceiling)
                                .fill(colour(channel).opacity(0.85))
                                .blendMode(.screen)
                        }
                    }
                }
                .padding(1)
            }
        }
        .frame(height: height)
        .accessibilityLabel("Histogram")
    }

    private func colour(_ channel: Int) -> Color {
        switch channel {
        case 0:  Color(red: 0.769, green: 0.263, blue: 0.227)
        case 1:  Color(red: 0.227, green: 0.659, blue: 0.310)
        default: Color(red: 0.227, green: 0.435, blue: 0.769)
        }
    }

    /// Quarter divisions. Photographers reason in stops and zones, so the
    /// marks encode something real rather than tidying the box.
    private func grid(in size: CGSize) -> some View {
        Path { p in
            for i in 1..<4 {
                let x = size.width * CGFloat(i) / 4
                p.move(to: CGPoint(x: x, y: 0))
                p.addLine(to: CGPoint(x: x, y: size.height))
            }
        }
        .stroke(Color.white.opacity(0.08), lineWidth: 0.5)
    }

    private func percentileCeiling() -> UInt32 {
        var all = bins
        all.sort()
        let index = Int(Double(all.count) * 0.99)
        return all[min(index, all.count - 1)]
    }

    private func channelPath(_ channel: Int, in size: CGSize, ceiling: UInt32) -> Path {
        Path { p in
            p.move(to: CGPoint(x: 0, y: size.height))
            for i in 0..<binCount {
                let value = Double(bins[channel * binCount + i]) / Double(ceiling)
                let x = size.width * CGFloat(i) / CGFloat(binCount - 1)
                let y = size.height * (1 - CGFloat(min(value, 1)))
                p.addLine(to: CGPoint(x: x, y: y))
            }
            p.addLine(to: CGPoint(x: size.width, y: size.height))
            p.closeSubpath()
        }
    }
}
