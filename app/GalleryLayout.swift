import CoreGraphics
import Foundation

/// The gallery grid's value logic - column count and 2D keyboard navigation.
///
/// Pure, in its own file with no SwiftUI and no AppKit, so
/// `orion-viewport-tests` can pin it without a GPU or a folder of raws - the
/// same split `PhotoSelection`, `CanvasLayout` and `BatchExport` already use.
/// `GalleryView` owns the pixels; this owns the decisions a key press or a
/// resize forces, which is exactly the part a person cannot check by looking.
enum GalleryLayout {

    /// The cell-size slider's range, named here so the view and the tests
    /// share one truth. 200 is the smallest cell on which framing is still
    /// judgeable; 400 at 2× is the 800 px a 1024-edge thumbnail comfortably
    /// covers (`PhotoIndex.thumbnailLongEdge`).
    static let minCell: CGFloat = 200
    static let maxCell: CGFloat = 400
    static let defaultCell: CGFloat = 260

    /// The gap between cells, and between the grid and its edges.
    static let spacing: CGFloat = 10

    /// A cell's frame is wider than tall - 3:2 is what most sensors are, so
    /// most photographs letterbox least in it.
    static let cellAspect: CGFloat = 1.5

    enum Direction {
        case left, right, up, down
    }

    /// How many columns fit a grid of the given width.
    ///
    /// The floor of what fits, and never less than one - a window narrower
    /// than a cell still shows a column, just a squeezed one, rather than
    /// none at all.
    static func columns(width: CGFloat, cell: CGFloat,
                        spacing: CGFloat = GalleryLayout.spacing) -> Int {
        guard width > 0, cell > 0 else { return 1 }
        // n cells and (n - 1) gaps: n·cell + (n − 1)·spacing ≤ width.
        let n = Int((width + spacing) / (cell + spacing))
        return max(1, n)
    }

    /// Where an arrow key moves the focus.
    ///
    /// Clamped, never wrapped: right at the end of a row does not jump to the
    /// next row's start, because during a cull the hand is holding an arrow
    /// key down and a wrap turns "the end of the row" into "somewhere else
    /// entirely". Down from the second-to-last row into a ragged last row
    /// lands on the last photograph rather than dying on a cell that is not
    /// there; down from the last row, and up from the first, stay put.
    static func move(from index: Int, direction: Direction,
                     columns: Int, count: Int) -> Int {
        guard count > 0 else { return 0 }
        guard index >= 0, index < count, columns > 0 else {
            return min(max(index, 0), count - 1)
        }

        switch direction {
        case .left:
            return max(index - 1, 0)
        case .right:
            return min(index + 1, count - 1)
        case .up:
            let up = index - columns
            return up >= 0 ? up : index
        case .down:
            let down = index + columns
            if down < count { return down }
            // A ragged last row: only fall into it if there is a row below at
            // all - otherwise the key does nothing, which is what "the bottom"
            // should feel like.
            let lastRowStart = ((count - 1) / columns) * columns
            return index < lastRowStart ? count - 1 : index
        }
    }
}
