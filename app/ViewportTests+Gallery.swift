import CoreGraphics
import Foundation

// The gallery grid's value logic - column math and 2D arrow navigation.
// These are the decisions a resize or a key press forces, and the part of the
// gallery nothing can check by looking at a capture: a focus ring that moves
// to the wrong cell looks exactly like one that moved to the right one.
extension ViewportTests {

    static func testGalleryColumnsFitTheWidth() {
        // Three 260pt cells and two 10pt gaps are exactly 800.
        report(GalleryLayout.columns(width: 800, cell: 260, spacing: 10) == 3,
               "three cells and two gaps fit exactly",
               "\(GalleryLayout.columns(width: 800, cell: 260, spacing: 10))")
        report(GalleryLayout.columns(width: 799, cell: 260, spacing: 10) == 2,
               "one point short drops a column",
               "\(GalleryLayout.columns(width: 799, cell: 260, spacing: 10))")
        report(GalleryLayout.columns(width: 100, cell: 260, spacing: 10) == 1,
               "a window narrower than a cell still shows one column")
        report(GalleryLayout.columns(width: 0, cell: 260, spacing: 10) == 1,
               "zero width is one column, not zero")
        report(GalleryLayout.columns(width: 800, cell: 0, spacing: 10) == 1,
               "a degenerate cell size cannot divide by zero")
    }

    static func testGalleryMoveClampsAtTheEdges() {
        // A 4-column grid over 10 photographs: rows of 4, 4 and a ragged 2.
        let cols = 4, count = 10

        report(GalleryLayout.move(from: 0, direction: .left,
                                  columns: cols, count: count) == 0,
               "left at the start stays")
        report(GalleryLayout.move(from: 3, direction: .right,
                                  columns: cols, count: count) == 4,
               "right crosses a row boundary by one, not by a wrap")
        report(GalleryLayout.move(from: 9, direction: .right,
                                  columns: cols, count: count) == 9,
               "right at the end stays")
        report(GalleryLayout.move(from: 2, direction: .up,
                                  columns: cols, count: count) == 2,
               "up from the first row stays")
        report(GalleryLayout.move(from: 6, direction: .up,
                                  columns: cols, count: count) == 2,
               "up moves one column")
        report(GalleryLayout.move(from: 2, direction: .down,
                                  columns: cols, count: count) == 6,
               "down moves one column")
    }

    static func testGalleryMoveHandlesTheRaggedLastRow() {
        let cols = 4, count = 10   // last row holds indices 8 and 9 only

        // Down from index 7 (second row, fourth column) has no cell below -
        // it clamps to the last photograph rather than dying or wrapping.
        report(GalleryLayout.move(from: 7, direction: .down,
                                  columns: cols, count: count) == 9,
               "down into the ragged row clamps to the last photograph",
               "\(GalleryLayout.move(from: 7, direction: .down, columns: cols, count: count))")
        report(GalleryLayout.move(from: 6, direction: .down,
                                  columns: cols, count: count) == 9,
               "down one short of the ragged edge clamps too")
        report(GalleryLayout.move(from: 4, direction: .down,
                                  columns: cols, count: count) == 8,
               "down onto a cell the ragged row does hold lands on it")
        report(GalleryLayout.move(from: 8, direction: .down,
                                  columns: cols, count: count) == 8,
               "down from the last row stays")
        report(GalleryLayout.move(from: 9, direction: .down,
                                  columns: cols, count: count) == 9,
               "down from the last photograph stays")
    }

    static func testGalleryMoveSurvivesDegenerateInput() {
        report(GalleryLayout.move(from: 0, direction: .down,
                                  columns: 4, count: 0) == 0,
               "an empty grid answers zero rather than trapping")
        report(GalleryLayout.move(from: 12, direction: .left,
                                  columns: 4, count: 10) == 9,
               "an index past the end clamps into range")
        report(GalleryLayout.move(from: -3, direction: .right,
                                  columns: 4, count: 10) == 0,
               "a negative index clamps into range")
        report(GalleryLayout.move(from: 5, direction: .up,
                                  columns: 0, count: 10) == 5,
               "zero columns moves nothing rather than dividing by it")
    }

    static func testGallerySliderConstantsHoldTheirOrder() {
        report(GalleryLayout.minCell < GalleryLayout.defaultCell
                && GalleryLayout.defaultCell < GalleryLayout.maxCell,
               "the slider's default sits inside its range")
        // The largest cell at a 2x display must be covered by the cached
        // thumbnail's long edge, or the gallery's top size shows upscaling -
        // this is the pairing that ties the slider to PhotoIndex.
        report(Int(GalleryLayout.maxCell) * 2 <= PhotoIndex.thumbnailLongEdge,
               "the largest cell at 2x fits inside a cached thumbnail",
               "\(Int(GalleryLayout.maxCell) * 2) against \(PhotoIndex.thumbnailLongEdge)")
    }
}
