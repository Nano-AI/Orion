import SwiftUI

/// Rating and rejection, over the photo.
///
/// These used to live only as four-pixel dots in the corner of a thumbnail,
/// which is fine for reading a whole shoot at a glance and useless for the frame
/// you are actually looking at — the mark you are setting was the one you could
/// not see. Culling happens while looking at the picture, so the control belongs
/// there too.
///
/// Stars here rather than the filmstrip's dots. At thumbnail scale a five-
/// pointed star is mush and dots are countable; at this size the star is the
/// shape everyone already reads as a rating, and there is room to draw it
/// properly.
struct RatingBar: View {
    let rating: Int
    let rejected: Bool
    let rate: (Int) -> Void
    let toggleReject: () -> Void

    @State private var hovered: Int?

    var body: some View {
        HStack(spacing: 10) {
            HStack(spacing: 3) {
                ForEach(1...5, id: \.self) { n in
                    star(n)
                }
            }

            Rectangle()
                .fill(.white.opacity(0.18))
                .frame(width: 1, height: 14)

            Button(action: toggleReject) {
                HStack(spacing: 4) {
                    Image(systemName: rejected ? "arrow.uturn.backward" : "xmark")
                        .font(.system(size: 9, weight: .semibold))
                    Text(rejected ? "Keep" : "Reject")
                        .font(.system(size: 10))
                }
                .foregroundStyle(rejected ? Palette.rejected : .white.opacity(0.75))
                .frame(height: 18)
                .padding(.horizontal, 6)
                .contentShape(Rectangle())
            }
            .buttonStyle(.plain)
            .help(rejected ? "Keep this photo (R)" : "Reject this photo (R)")
        }
        .padding(.horizontal, 10)
        .padding(.vertical, 5)
        .background(.black.opacity(0.5), in: Capsule())
        .overlay(Capsule().strokeBorder(.white.opacity(0.1), lineWidth: 1))
    }

    private func star(_ n: Int) -> some View {
        // Hovering previews the rating the click would set, which is what makes
        // a five-target row hittable without counting first.
        let shown = hovered ?? rating
        let filled = n <= shown && !rejected

        return Button {
            // Clicking the star already set clears the rating, the way every
            // other rating control on the platform behaves. Without it there is
            // no way back to unrated except a menu.
            rate(n == rating ? 0 : n)
        } label: {
            Image(systemName: filled ? "star.fill" : "star")
                .font(.system(size: 12))
                .foregroundStyle(rejected ? .white.opacity(0.25)
                                 : filled ? Palette.rated : .white.opacity(0.4))
                .frame(width: 16, height: 18)
                .contentShape(Rectangle())
        }
        .buttonStyle(.plain)
        .onHover { inside in hovered = inside ? n : (hovered == n ? nil : hovered) }
        .help(n == rating ? "Clear the rating" : "\(n) star\(n == 1 ? "" : "s")  (\(n))")
        .disabled(rejected)
    }
}
