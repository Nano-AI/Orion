import AppKit
import SwiftUI

// The toolbar's middle slot: what the camera recorded for the open photo.
//
// It used to print the camera alone, and nothing anywhere in the interface
// showed the ISO, shutter, aperture, focal length, lens or capture time,
// although the engine read every one of them on open. The line says as much as
// the toolbar has room for; the popover behind it says the rest.

extension Editor {
    /// The capture line for the photograph on the canvas, or the empty label
    /// it stands in for while nothing is open.
    var captureLine: some View {
        CaptureLine(capture: engine.isLoaded ? engine.capture : nil, file: current)
    }
}

/// One dim line, as long as the toolbar allows, that opens the full list.
///
/// Its own view rather than a computed property on `Editor` because the
/// popover's open flag is state, and `Editor`'s state lives in its struct body.
struct CaptureLine: View {
    let capture: CaptureInfo?
    let file: URL?

    @State private var showing = false
    @State private var hovering = false

    var body: some View {
        if let capture, !capture.line.isEmpty {
            Button { showing.toggle() } label: {
                // Widest first: camera and all, then the exposure with its
                // focal length, then the three numbers that set it. A window
                // narrow enough to lose even those truncates the last.
                ViewThatFits(in: .horizontal) {
                    label(capture.line)
                    label(capture.compactLine)
                    label(capture.exposureLine)
                }
            }
            .buttonStyle(.plain)
            .frame(maxWidth: .infinity)
            .onHover { hovering = $0 }
            .help("Capture settings")
            .popover(isPresented: $showing, arrowEdge: .bottom) {
                CapturePopover(capture: capture, file: file)
            }
        } else {
            // The same frame with nothing in it, so the controls either side
            // stay where they are while a photo opens or closes.
            Self.empty
        }
    }

    /// What the slot draws with no photograph, or one whose file records none
    /// of it. `capture-line` compares the two.
    static var empty: some View {
        Text("").font(.system(size: 12)).frame(maxWidth: .infinity)
    }

    private func label(_ text: String) -> some View {
        Text(text)
            .font(.system(size: 12))
            .monospacedDigit()
            .lineLimit(1)
            .truncationMode(.tail)
            .foregroundStyle(hovering || showing ? Palette.text : Palette.dim)
            .contentShape(Rectangle())
    }
}

/// Every field the file records, label beside value, and the file itself last.
struct CapturePopover: View {
    let capture: CaptureInfo
    let file: URL?

    var body: some View {
        let rows = capture.rows + fileRow
        Grid(alignment: .leading, horizontalSpacing: 14, verticalSpacing: 6) {
            ForEach(rows.indices, id: \.self) { i in
                GridRow {
                    Text(rows[i].0)
                        .foregroundStyle(Palette.dim)
                        .gridColumnAlignment(.trailing)
                    Text(rows[i].1)
                        .foregroundStyle(Palette.text)
                        .textSelection(.enabled)
                }
            }
        }
        .font(.system(size: 12))
        .monospacedDigit()
        .padding(14)
        .frame(minWidth: 240, alignment: .leading)
    }

    /// The file name and its size on disk. Read when the popover draws, which
    /// is when somebody asked: a stat per open would be a cost paid for a
    /// popover nobody opened.
    private var fileRow: [(String, String)] {
        guard let file else { return [] }
        let bytes = (try? file.resourceValues(forKeys: [.fileSizeKey]))?.fileSize
        let size = bytes.map {
            ByteCountFormatter.string(fromByteCount: Int64($0), countStyle: .file)
        }
        return [("File", [file.lastPathComponent, size].compactMap { $0 }
            .joined(separator: CaptureInfo.separator))]
    }
}
