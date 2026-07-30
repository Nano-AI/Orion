import SwiftUI

/// The histogram, built as an instrument rather than a decoration.
///
/// This is the interface's signature element, and it earns that by being the one
/// piece of chrome a photographer reads as often as the photograph. Everything
/// else in the panel is a control; this is a readout, so it is built like the
/// meter on a camera body: a recessed plate, an engraved rail with index marks,
/// and flags that fly only when something is happening.
///
/// Drawn as filled channel curves with screen blending, so overlapping channels
/// go white where all three agree — which is how every darkroom instrument
/// behaves and how a photographer already knows to read one.
///
/// ⚠️ **The rail is not marked in stops, and that is deliberate.** The obvious
/// instrument would engrave EV below the curve. The pipeline's output is
/// AgX-mapped and carries *no* sRGB encode (see the note at the end of
/// `develop_display.slang`), so turning a code value back into stops means
/// inverting the AgX polynomial here — a second copy of the display transform,
/// living in the interface, drifting from the shader the first time either is
/// touched. This codebase has been bitten by exactly that twice. The rail marks
/// quarters of the display range, which is what the axis honestly is; if stops
/// are wanted, the engine should report the mapping rather than the UI guessing
/// it.
struct Histogram: View {
    let bins: [UInt32]
    let height: CGFloat

    private var binCount: Int { bins.count / 3 }

    /// Below this share of the frame, a clipping flag is reporting sensor noise
    /// and a stray pixel rather than a decision anyone can act on — at 24 MP a
    /// single black pixel is 0.000004% of the frame, and a flag that is always
    /// lit is a flag nobody reads. Orion's own number, recorded in
    /// `research/UNSOURCED.md` §19 with this reasoning.
    private static let clipThreshold = 0.001

    var body: some View {
        VStack(alignment: .leading, spacing: 4) {
            flags
            plate
            rail
        }
        .accessibilityElement(children: .combine)
        .accessibilityLabel(accessibilityText)
    }

    // MARK: The curve, in its recess

    private var plate: some View {
        ZStack {
            Rectangle().fill(Palette.filmBase)

            if binCount > 1 {
                GeometryReader { geo in
                    // A shared ceiling across channels, so relative heights
                    // stay meaningful.
                    let ceiling = max(curveCeiling(), 1)

                    ZStack {
                        grid(in: geo.size)

                        ForEach(0..<3, id: \.self) { channel in
                            channelPath(channel, in: geo.size, ceiling: ceiling)
                                .fill(color(channel).opacity(0.85))
                                .blendMode(.screen)
                        }
                    }
                }
                .padding(1)
            }
        }
        .frame(height: height)
        .overlay(
            // Hairline on all four sides: this is a well cut into the panel, and
            // the border is what says the black inside it is a recess rather
            // than a hole in the window.
            Rectangle().strokeBorder(Palette.line, lineWidth: 1)
        )
    }

    // MARK: Flags

    /// Clipping, at both ends, as a share of the frame.
    ///
    /// Position carries which end, so both flags are cut in the same amber and
    /// no second ink is invented to say "shadow" against "highlight".
    private var flags: some View {
        HStack(spacing: 4) {
            flag(clipped(atStart: true), pointsLeft: true)
            Spacer(minLength: 0)
            Engraved.Label(text: "Levels", color: Palette.faint, size: 9)
            Spacer(minLength: 0)
            flag(clipped(atStart: false), pointsLeft: false)
        }
    }

    @ViewBuilder private func flag(_ share: Double, pointsLeft: Bool) -> some View {
        let lit = share >= Self.clipThreshold
        HStack(spacing: 3) {
            if !pointsLeft, lit { percent(share) }
            Triangle(pointsLeft: pointsLeft)
                .fill(lit ? Palette.star : Palette.faint.opacity(0.35))
                .frame(width: 4, height: 7)
            if pointsLeft, lit { percent(share) }
        }
        // The flag holds its slot whether it is lit or not, so the row does not
        // reflow as an edit pushes pixels into or out of clipping.
        .frame(minWidth: 44, alignment: pointsLeft ? .leading : .trailing)
        .help(pointsLeft
              ? "Shadow clipping: \(percentText(share)) of pixels at black"
              : "Highlight clipping: \(percentText(share)) of pixels at white")
    }

    private func percent(_ share: Double) -> some View {
        Engraved.Readout(text: percentText(share), color: Palette.star, size: 9)
    }

    private func percentText(_ share: Double) -> String {
        share <= 0 ? "0%"
            : share < 0.0001 ? "<0.01%"
            : String(format: "%.2f%%", share * 100)
    }

    /// A flag, not an arrowhead glyph: SF Symbols would put a font's idea of a
    /// triangle next to hand-drawn index marks.
    private struct Triangle: Shape {
        let pointsLeft: Bool
        func path(in r: CGRect) -> Path {
            Path { p in
                if pointsLeft {
                    p.move(to: CGPoint(x: r.maxX, y: r.minY))
                    p.addLine(to: CGPoint(x: r.minX, y: r.midY))
                    p.addLine(to: CGPoint(x: r.maxX, y: r.maxY))
                } else {
                    p.move(to: CGPoint(x: r.minX, y: r.minY))
                    p.addLine(to: CGPoint(x: r.maxX, y: r.midY))
                    p.addLine(to: CGPoint(x: r.minX, y: r.maxY))
                }
                p.closeSubpath()
            }
        }
    }

    // MARK: The engraved rail

    /// Index marks at the quarters, with the ends and the middle named.
    ///
    /// The ends are named `BLACK` and `WHITE` rather than `SHADOWS` and
    /// `HIGHLIGHTS`: the axis really does run from the darkest value the file
    /// can hold to the brightest, and a mark that says shadows at the extreme
    /// left is naming a tonal region at the position of an absolute.
    private var rail: some View {
        VStack(alignment: .leading, spacing: 2) {
            GeometryReader { geo in
                ZStack(alignment: .topLeading) {
                    // Named marks carry more weight than the quarters between
                    // them — the ranking a scale is read by. Two passes because
                    // one stroke colour cannot say both.
                    marks(in: geo.size, every: 2, length: 6)
                        .stroke(Palette.dim, lineWidth: 1)
                    marks(in: geo.size, every: 1, length: 3, skipEvery: 2)
                        .stroke(Palette.faint, lineWidth: 1)
                }
            }
            .frame(height: 6)

            HStack(spacing: 0) {
                Engraved.Label(text: "Black", color: Palette.faint, size: 9)
                Spacer(minLength: 0)
                Engraved.Label(text: "Mid", color: Palette.faint, size: 9)
                Spacer(minLength: 0)
                Engraved.Label(text: "White", color: Palette.faint, size: 9)
            }
        }
    }

    /// Index marks at every `every`-th quarter, optionally skipping the ones a
    /// heavier pass already drew.
    private func marks(in size: CGSize, every: Int, length: CGFloat,
                       skipEvery: Int? = nil) -> Path {
        Path { p in
            for i in stride(from: 0, through: 4, by: every) {
                if let skip = skipEvery, i % skip == 0 { continue }
                // Inset by half the line width at the ends, or a mark on the
                // boundary is drawn half outside the rail and reads thinner
                // than the ones beside it.
                let t = CGFloat(i) / 4
                let x = 0.5 + (size.width - 1) * t
                p.move(to: CGPoint(x: x, y: 0))
                p.addLine(to: CGPoint(x: x, y: length))
            }
        }
    }

    // MARK: Reading the bins

    /// The share of pixels sitting in the extreme bin, worst channel.
    ///
    /// Worst rather than averaged: a blown red channel is clipped, and averaging
    /// it against two unclipped channels reports two thirds of a problem that is
    /// entirely there in the channel the photographer has to fix.
    private func clipped(atStart: Bool) -> Double {
        guard binCount > 1 else { return 0 }
        var worst = 0.0
        for channel in 0..<3 {
            let base = channel * binCount
            let total = bins[base..<(base + binCount)].reduce(0.0) { $0 + Double($1) }
            guard total > 0 else { continue }
            let extreme = Double(bins[atStart ? base : base + binCount - 1])
            worst = max(worst, extreme / total)
        }
        return worst
    }

    private func color(_ channel: Int) -> Color {
        switch channel {
        case 0:  Orion.Palette.chanR
        case 1:  Orion.Palette.chanG
        default: Orion.Palette.chanB
        }
    }

    /// Quarter divisions, matching the rail's index marks exactly — the grid
    /// inside the plate and the scale below it are the same scale, or the marks
    /// are decoration.
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

    /// The tallest bin the curve has to fit, ignoring the two clipped ends.
    ///
    /// This was the 99th percentile over every bin, on the reasoning that one
    /// blown highlight bin should not flatten the curve. It does not hold: with
    /// 3 × 256 bins the 99th percentile is still the eighth-largest value, so a
    /// night frame — 10% of its pixels at black — kept a ceiling set by the
    /// clipping spike and squashed the whole photograph into a band along the
    /// bottom. Which is what the developer meant by calling it a grey blob.
    ///
    /// The end bins are excluded instead of discounted, and that is now honest
    /// rather than a fudge: the flags above report exactly how much sits in them,
    /// as a number, so the curve does not have to carry that information too.
    /// The max of what remains, because within the picture's own range a tall bin
    /// is real content and clamping it would misreport the frame.
    private func curveCeiling() -> UInt32 {
        guard binCount > 2 else { return 1 }
        var peak: UInt32 = 0
        for channel in 0..<3 {
            let base = channel * binCount
            for i in 1..<(binCount - 1) {
                peak = max(peak, bins[base + i])
            }
        }
        return peak
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

    /// VoiceOver gets the numbers, not a description of a picture of them.
    private var accessibilityText: String {
        let lo = clipped(atStart: true), hi = clipped(atStart: false)
        return "Levels. Shadow clipping \(percentText(lo)), "
             + "highlight clipping \(percentText(hi))."
    }
}
