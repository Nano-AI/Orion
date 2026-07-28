import SwiftUI

/// The compare divider — draggable, with the two sides named.
///
/// Labelling matters more than it looks. A split with no labels makes you stop
/// and work out which half is which every time you glance at it, and on a
/// subtle edit the two sides can be genuinely hard to tell apart.
struct CompareOverlay: View {
    @Bindable var engine: Engine
    /// The photo's rectangle inside the canvas, in view coordinates.
    let frame: CGRect

    /// Where the split was when the drag started. Dragging works from a
    /// translation rather than from an absolute location because the handle is
    /// positioned, and a positioned view's own coordinate space is not the
    /// canvas's.
    @State private var startSplit: CGFloat?

    private let grabWidth: CGFloat = 28

    var body: some View {
        if engine.comparing, frame.width > 0, frame.height > 0 {
            ZStack(alignment: .topLeading) {
                label("Original", at: beforeAnchor)
                label("Edited", at: afterAnchor)
                handle
            }
        }
    }

    // MARK: Geometry

    private var split: CGFloat { CGFloat(engine.compareSplit) }

    private var dividerPosition: CGPoint {
        engine.compareVertical
            ? CGPoint(x: frame.minX + frame.width * split, y: frame.midY)
            : CGPoint(x: frame.midX, y: frame.minY + frame.height * split)
    }

    /// Centred in each half, so a label never sits under the divider.
    private var beforeAnchor: CGPoint {
        engine.compareVertical
            ? CGPoint(x: frame.minX + frame.width * split * 0.5, y: frame.minY + 22)
            : CGPoint(x: frame.midX, y: frame.minY + frame.height * split * 0.5)
    }

    private var afterAnchor: CGPoint {
        engine.compareVertical
            ? CGPoint(x: frame.minX + frame.width * (split + (1 - split) * 0.5),
                      y: frame.minY + 22)
            : CGPoint(x: frame.midX,
                      y: frame.minY + frame.height * (split + (1 - split) * 0.5))
    }

    // MARK: Pieces

    private func label(_ text: String, at point: CGPoint) -> some View {
        Text(text.uppercased())
            .font(.system(size: 9, weight: .semibold))
            .tracking(0.9)
            .foregroundStyle(.white)
            .padding(.horizontal, 7)
            .padding(.vertical, 3)
            .background(Color.black.opacity(0.6), in: Capsule())
            .position(point)
            .allowsHitTesting(false)
            // Hide a label once its half is too narrow to hold it.
            .opacity(halfIsWideEnough(text) ? 1 : 0)
    }

    private func halfIsWideEnough(_ text: String) -> Bool {
        let extent = engine.compareVertical ? frame.width : frame.height
        let share = text == "Original" ? split : 1 - split
        return extent * share > 90
    }

    /// The grab box: a band the width of the divider's reach, the length of
    /// the frame.
    private var grabSize: CGSize {
        engine.compareVertical
            ? CGSize(width: grabWidth, height: frame.height)
            : CGSize(width: frame.width, height: grabWidth)
    }

    private var handle: some View {
        ZStack {
            if engine.compareVertical {
                Rectangle().fill(.white).frame(width: 1.5)
            } else {
                Rectangle().fill(.white).frame(height: 1.5)
            }

            // A grip at the midpoint, so the divider reads as draggable rather
            // than as a rule that happens to be there.
            Circle()
                .fill(.white)
                .frame(width: 24, height: 24)
                .overlay {
                    Image(systemName: engine.compareVertical
                          ? "arrow.left.and.right" : "arrow.up.and.down")
                        .font(.system(size: 10, weight: .bold))
                        .foregroundStyle(.black.opacity(0.7))
                }
                .shadow(color: .black.opacity(0.4), radius: 2)

            // The whole band is the target. Sized here, before `.position`:
            // a modifier after it applies to a view that fills the parent, so
            // the old contentShape put the grab area in the canvas's corner
            // rather than on the divider. Same defect the crop handles had.
            Color.clear.contentShape(Rectangle())
        }
        .frame(width: grabSize.width, height: grabSize.height)
        .position(dividerPosition)
        .gesture(
            DragGesture(minimumDistance: 0)
                .onChanged { value in
                    // From a translation, not a location. A positioned view's
                    // coordinate space is its own, so `value.location` is not
                    // in canvas coordinates and reading it moved the divider to
                    // wherever the grab box's origin happened to be.
                    let from = startSplit ?? split
                    if startSplit == nil { startSplit = from }

                    let delta = engine.compareVertical
                        ? value.translation.width / max(frame.width, 1)
                        : value.translation.height / max(frame.height, 1)

                    engine.setCompare(split: Double(min(max(from + delta, 0.02), 0.98)))
                }
                .onEnded { _ in startSplit = nil }
        )
    }
}
