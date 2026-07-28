import SwiftUI

/// The tone curve, as a graph you draw on.
///
/// Sliders say "more shadows"; a curve says *which* shadows, and by how much
/// relative to everything either side of them. That is the whole reason the
/// control exists, and it only works if the graph is direct — drag a point and
/// the picture moves, click the line and a point appears there.
///
/// The interpolation drawn here is the same monotone cubic Hermite the engine
/// evaluates into its LUT (`pipe/ToneCurve.cpp`). Two implementations of one
/// spline is a real risk, so the shape is checked against the engine's in the
/// viewport suite rather than trusted to look right.
struct CurveEditor: View {
    @Bindable var engine: Engine
    /// Bins behind the curve, so you can see which tones you are moving.
    let histogram: [UInt32]

    @State private var channel: CurveChannel = .master
    @State private var dragging: Int?

    /// Measured rather than fixed: the graph is square and fills the panel, so
    /// it stays legible if the panel width is ever changed.
    @State private var size: CGFloat = 232
    private let grabRadius: CGFloat = 14

    var body: some View {
        VStack(alignment: .leading, spacing: 9) {
            header
            graph
            footnote
        }
    }

    // MARK: Pieces

    private var header: some View {
        HStack(spacing: 4) {
            ForEach(CurveChannel.allCases) { c in
                Button {
                    channel = c
                } label: {
                    Text(c.title)
                        .font(.system(size: 10, weight: channel == c ? .semibold : .regular))
                        .foregroundStyle(channel == c ? tint(c) : Palette.dim)
                        .frame(width: 30, height: 20)
                        .background(channel == c ? Palette.raised : .clear,
                                    in: RoundedRectangle(cornerRadius: 4))
                        .contentShape(RoundedRectangle(cornerRadius: 4))
                }
                .buttonStyle(.plain)
                .help(c == .master ? "All three channels" : "The \(c.title) channel alone")
            }

            Spacer()

            if !engine.curve.isIdentity {
                Button("Reset") {
                    engine.edit("Curve") { engine.curve = ToneCurve() }
                }
                .buttonStyle(.plain)
                .font(.system(size: 10))
                .foregroundStyle(Palette.dim)
            }
        }
    }

    private var graph: some View {
        ZStack(alignment: .topLeading) {
            Rectangle().fill(Palette.raised)

            histogramShape
            grid
            diagonal
            curveShape
            handles
        }
        .frame(height: size)
        .frame(maxWidth: .infinity)
        .background {
            GeometryReader { geo in
                Color.clear.onAppear { size = geo.size.width }
                    .onChange(of: geo.size.width) { _, w in size = w }
            }
        }
        .overlay(Rectangle().strokeBorder(Palette.line, lineWidth: 1))
        .contentShape(Rectangle())
        .gesture(drag)
        .onTapGesture(count: 2) { engine.edit("Curve") { engine.curve[channel] = ToneCurve.identity } }
    }

    /// Faint, and behind everything. It is orientation, not data you read off.
    private var histogramShape: some View {
        Path { p in
            guard histogram.count >= 384 else { return }
            let bins = 128
            let peak = max(1, (0..<bins).map { i in
                max(histogram[i], max(histogram[bins + i], histogram[2 * bins + i]))
            }.max() ?? 1)

            p.move(to: CGPoint(x: 0, y: size))
            for i in 0..<bins {
                let v = max(histogram[i], max(histogram[bins + i], histogram[2 * bins + i]))
                // Square root, because a linear axis buries everything that is
                // not the peak and the shape stops being readable.
                let t = sqrt(CGFloat(v) / CGFloat(peak))
                p.addLine(to: CGPoint(x: size * CGFloat(i) / CGFloat(bins - 1),
                                      y: size - t * size * 0.82))
            }
            p.addLine(to: CGPoint(x: size, y: size))
            p.closeSubpath()
        }
        .fill(Color.white.opacity(0.055))
        .allowsHitTesting(false)
    }

    private var grid: some View {
        Path { p in
            for i in 1..<4 {
                let t = size * CGFloat(i) / 4
                p.move(to: CGPoint(x: t, y: 0));    p.addLine(to: CGPoint(x: t, y: size))
                p.move(to: CGPoint(x: 0, y: t));    p.addLine(to: CGPoint(x: size, y: t))
            }
        }
        .stroke(Palette.line.opacity(0.7), lineWidth: 0.5)
        .allowsHitTesting(false)
    }

    /// Where the curve would be if it did nothing — the reference every curve
    /// panel draws, and the only way to read at a glance which way a section
    /// has been pushed.
    private var diagonal: some View {
        Path { p in
            p.move(to: CGPoint(x: 0, y: size))
            p.addLine(to: CGPoint(x: size, y: 0))
        }
        .stroke(Palette.dim.opacity(0.35), style: StrokeStyle(lineWidth: 0.5, dash: [3, 3]))
        .allowsHitTesting(false)
    }

    private var curveShape: some View {
        Path { p in
            let points = engine.curve[channel]
            let steps = 96
            for i in 0...steps {
                let x = Float(i) / Float(steps)
                let y = CurveMath.evaluate(points, at: x)
                let v = CGPoint(x: CGFloat(x) * size, y: size - CGFloat(y) * size)
                i == 0 ? p.move(to: v) : p.addLine(to: v)
            }
        }
        .stroke(tint(channel), style: StrokeStyle(lineWidth: 1.6, lineJoin: .round))
        .allowsHitTesting(false)
    }

    private var handles: some View {
        ForEach(Array(engine.curve[channel].enumerated()), id: \.offset) { index, point in
            Circle()
                .fill(dragging == index ? tint(channel) : Palette.text)
                .frame(width: 7, height: 7)
                .overlay(Circle().strokeBorder(Palette.raised, lineWidth: 1.5))
                .position(view(point))
                .allowsHitTesting(false)
        }
    }

    private var footnote: some View {
        Text(engine.curve[channel].count > 2
             ? "drag a point · double-click the graph to reset this channel"
             : "click the line to add a point")
            .font(.system(size: 10))
            .foregroundStyle(Palette.faint)
    }

    // MARK: Interaction

    private var drag: some Gesture {
        DragGesture(minimumDistance: 0)
            .onChanged { value in
                var points = engine.curve[channel]

                if dragging == nil {
                    // Grab the nearest point, or make one where the line was
                    // clicked. Adding on click rather than behind a modifier:
                    // the graph is the control, and reaching for a key to use
                    // it defeats the point of a direct one.
                    if let hit = nearest(to: value.location, in: points) {
                        dragging = hit
                    } else {
                        guard points.count < 16, points.count >= 2 else { return }

                        // Strictly between the two points it lands between —
                        // not merely inside the endpoints. The engine treats a
                        // non-ascending curve as malformed and falls back to
                        // the identity, so a duplicated x makes the whole curve
                        // silently snap back, which reads as the panel being
                        // broken rather than as input being rejected.
                        guard let placed = CurveMath.insertion(
                            of: Float(value.location.x / size), into: points)
                        else { return }

                        points.insert(CurvePoint(x: placed.x,
                                                 y: CurveMath.evaluate(points, at: placed.x)),
                                      at: placed.index)
                        dragging = placed.index
                        engine.curve[channel] = points
                    }
                }

                guard let index = dragging, index < points.count else { return }
                points = engine.curve[channel]

                // The ends stay pinned to their edges: they set the black and
                // white points, and letting them slide sideways would leave the
                // curve undefined past them.
                let isEnd = index == 0 || index == points.count - 1
                let x = isEnd ? points[index].x
                              : bounded(Float(value.location.x / size),
                                        between: points[index - 1].x, and: points[index + 1].x)
                let y = clamp01(Float(1 - value.location.y / size))

                points[index] = CurvePoint(x: x, y: y)
                engine.curve[channel] = points
            }
            .onEnded { value in
                // Dragged well clear of the graph: remove it. That is how a
                // point is deleted in every curve panel, and there is nowhere
                // sensible to put a delete button for a thing you are holding.
                if let index = dragging, index > 0 {
                    var points = engine.curve[channel]
                    let out = value.location.y < -grabRadius || value.location.y > size + grabRadius
                    if out, index < points.count - 1, points.count > 2 {
                        points.remove(at: index)
                        engine.curve[channel] = points
                    }
                }
                dragging = nil
                engine.history.record(engine.state, label: "Curve")
            }
    }

    private func nearest(to location: CGPoint, in points: [CurvePoint]) -> Int? {
        var best: (index: Int, distance: CGFloat)?
        for (i, p) in points.enumerated() {
            let v = view(p)
            let d = hypot(v.x - location.x, v.y - location.y)
            if d <= grabRadius, best == nil || d < best!.distance { best = (i, d) }
        }
        return best?.index
    }

    private func view(_ p: CurvePoint) -> CGPoint {
        CGPoint(x: CGFloat(p.x) * size, y: size - CGFloat(p.y) * size)
    }

    private func tint(_ c: CurveChannel) -> Color {
        switch c {
        case .master: Palette.accent
        case .red:    Color(red: 0.85, green: 0.36, blue: 0.34)
        case .green:  Color(red: 0.45, green: 0.76, blue: 0.44)
        case .blue:   Color(red: 0.38, green: 0.60, blue: 0.88)
        }
    }

    private func clamp01(_ v: Float) -> Float { min(max(v, 0), 1) }

    /// Keeps an interior point strictly between its neighbours, so the control
    /// points stay ascending — the interpolator relies on it.
    private func bounded(_ v: Float, between lower: Float, and upper: Float) -> Float {
        min(max(v, lower + 0.005), upper - 0.005)
    }
}
