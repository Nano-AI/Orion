import AppKit
import SwiftUI

// The picture, and everything drawn over it.
//
// The three geometry helpers sit with the overlays that call them rather than
// in `CanvasLayout`, which is the maths: these are the *bindings* from this
// view's size to that maths, and an overlay placed against the wrong one of
// them is the recurring bug the comments below record. Adding an overlay means
// a slot in `canvas` and, if it needs to know where the picture is, a call to
// one of the three — one file, two adjacent edits.

extension Editor {
    var canvas: some View {
        GeometryReader { geo in
            ZStack(alignment: .bottomLeading) {
                Palette.surround

                if engine.isLoaded {
                    ImageCanvas(engine: engine, viewport: viewport,
                                targeted: targeted, generation: engine.generation)
                        // Held while a new photo decodes, and what the
                        // screenshot harness draws into — AppKit cannot capture
                        // a Metal layer, so the canvas has to be a still there.
                        // First in the chain, so every overlay below is drawn
                        // over it rather than hidden by it.
                        .overlay {
                            if let still = engine.placeholder {
                                Image(nsImage: still)
                                    .resizable()
                                    .interpolation(.high)
                                    .aspectRatio(contentMode: .fit)
                                    .allowsHitTesting(false)
                            }
                        }
                        // The crop overlay goes on before the padding, so its
                        // coordinate space is the Metal view's exactly. Applied
                        // after, it measured the padded box and every handle
                        // sat a few points off the pixels.
                        // The divider and its labels. Written weeks ago and
                        // never placed in the view, which is why compare had no
                        // handle to drag and nothing naming the two sides — the
                        // split itself was happening in the shader all along.
                        .overlay {
                            if tab == .crop {
                                GeometryReader { canvasGeo in
                                    CropOverlay(engine: engine,
                                                frame: photoFrame(in: canvasGeo.size),
                                                bounds: canvasGeo.size)
                                }
                                .clipped()
                            }
                        }
                        // The mask belongs to the panel that arms it, the same
                        // rule the color picker follows: leaving Light with a
                        // gradient still grabbing presses would mean a click on
                        // the photo did something the visible controls no
                        // longer explain. It goes on before the padding, so its
                        // coordinates are the Metal view's exactly — applied
                        // after, every handle sits twenty points off the pixels.
                        // Spots, on the tab that owns them. Drawn under the
                        // mask overlay's slot so a mask's handles win a press
                        // when both are somehow live — they cannot both be
                        // armed, since the two live on different tabs, and this
                        // is the belt to that brace.
                        .overlay {
                            if tab == .detail && !engine.spots.isEmpty || tab == .detail && engine.spotPlacing {
                                GeometryReader { canvasGeo in
                                    SpotOverlay(engine: engine,
                                                map: pictureMap(in: canvasGeo.size))
                                }
                                .clipped()
                            }
                        }
                        .overlay {
                            if tab == .mask && engine.maskKind != 0 {
                                GeometryReader { canvasGeo in
                                    MaskOverlay(engine: engine,
                                                map: pictureMap(in: canvasGeo.size))
                                }
                                .clipped()
                            }
                        }
                        // ⚠ **Compare goes on top of the editing overlays, and
                        // the order is the bug it fixes.** `MaskOverlay` takes
                        // `contentShape(Rectangle())` — the whole canvas, which
                        // it needs, since dragging a radial's body or painting a
                        // stroke can start anywhere on the picture. It sat above
                        // this, so with any mask active the divider could not be
                        // grabbed at all: reported as "I was messing around with
                        // masking and now I can't drag the compare".
                        //
                        // Reordering rather than disabling the mask overlay,
                        // because editing through a split is a thing people do.
                        // The divider only claims a 28-point strip, so every
                        // press outside it still falls through to the mask.
                        .overlay {
                            if engine.comparing {
                                GeometryReader { canvasGeo in
                                    CompareOverlay(engine: engine,
                                                   frame: drawnFrame(in: canvasGeo.size))
                                }
                            }
                        }
                        .padding(20)
                        .onChange(of: tab) { _, t in
                            engine.log.tab(t.rawValue)
                            engine.cropPreview = (t == .crop)
                            viewport.locked = (t == .crop)

                            // Anything armed and waiting for a click on the
                            // photo belongs to the panel that armed it. Leaving
                            // that panel with the color picker still live means
                            // the next click on the picture does something the
                            // visible controls no longer explain.
                            targeted.isActive = false
                            targeted.clearHover()
                            // Spot placing belongs to the panel that armed it,
                            // for the same reason.
                            engine.spotPlacing = false

                            // Compare holds a copy of the unedited render at the
                            // current geometry. Cropping changes that geometry
                            // under it, so the two halves stop being the same
                            // picture.
                            if t == .crop { engine.clearCompare() }
                        }
                        .onAppear {
                            engine.cropPreview = (tab == .crop)
                            viewport.locked = (tab == .crop)
                        }
                        .overlay { ColorLoupe(targeted: targeted) }
                        .onChange(of: targeted.lastPicked) { _, picked in
                            // Follow the pick, so the sliders below act on the
                            // band you just clicked rather than a stale one.
                            if let picked { band = picked }
                        }

                    HStack(alignment: .bottom, spacing: 10) {
                        if !viewport.isFit {
                            Navigator(imageWidth: engine.imageWidth,
                                      imageHeight: engine.imageHeight,
                                      viewport: viewport,
                                      viewAspect: geo.size.width / max(geo.size.height, 1))
                        }
                        // Off the photograph. This used to be a dark chip pinned
                        // over the lower-left corner of the picture — a caption
                        // sitting on the print, competing with the thing being
                        // judged, and covering whatever was in that corner. It
                        // reads in the footer instead, where the app already
                        // reports its own state.
                        Spacer(minLength: 10)

                        // Culling happens while looking at the picture, so the
                        // mark you are setting belongs over it — not only as
                        // four-pixel dots on a thumbnail you are not looking at.
                        if let current,
                           let photo = library.photos.first(where: { $0.url == current }) {
                            RatingBar(rating: photo.rating,
                                      rejected: photo.rejected,
                                      rate: { library.setRating($0, for: current) },
                                      toggleReject: { library.toggleRejected(current) })
                        }
                    }
                    .padding(14)
                } else {
                    VStack(spacing: 0) {
                        Text("Orion")
                            .font(.system(size: 52, weight: .regular, design: .serif))
                            .foregroundStyle(Palette.text)

                        Text("A darkroom for raw files.")
                            .font(.system(size: 13, design: .serif))
                            .italic()
                            .foregroundStyle(Palette.dim)
                            .padding(.top, 6)

                        Button("Open a raw file") { openFile() }
                            .buttonStyle(.plain)
                            .font(.system(size: 12))
                            .foregroundStyle(Palette.accent)
                            .padding(.horizontal, 16)
                            .padding(.vertical, 7)
                            .overlay(RoundedRectangle(cornerRadius: 5)
                                .stroke(Palette.accent.opacity(0.5), lineWidth: 1))
                            .padding(.top, 28)

                        Text("ARW, DNG, NEF, CR2, CR3, RAF, ORF, RW2 — Sony is the tested one.")
                            .font(.system(size: 10))
                            .foregroundStyle(Palette.faint)
                            .padding(.top, 14)
                    }
                    .frame(maxWidth: .infinity, maxHeight: .infinity)
                }
            }
        }
        .frame(maxWidth: .infinity, maxHeight: .infinity)
    }

    /// The photo's rectangle within the canvas, from the same function the
    /// renderer uses, so the overlay lands on the pixels rather than near them.
    private func photoFrame(in size: CGSize) -> CGRect {
        guard engine.imageWidth > 0, engine.imageHeight > 0 else { return .zero }
        return CanvasLayout.frameRect(
            imageAspect: CGFloat(engine.imageWidth) / CGFloat(engine.imageHeight),
            in: size)
    }

    /// Where the picture is on screen right now, zoom included.
    ///
    /// The compare divider lives in view space — it stays put while the image
    /// pans under it — so it has to be placed against the rectangle the picture
    /// actually covers, not the one it covers at fit. Zoomed in, those are not
    /// the same rectangle, and the divider drawn on one while the split happens
    /// on the other is the whole of the bug.
    private func drawnFrame(in size: CGSize) -> CGRect {
        guard engine.imageWidth > 0, engine.imageHeight > 0, size.height > 0 else {
            return .zero
        }
        let quad = viewport.quadScale(
            imageAspect: CGFloat(engine.imageWidth) / CGFloat(engine.imageHeight),
            viewAspect: size.width / size.height)
        return CanvasLayout.drawnRect(quadScale: quad, in: size)
    }

    /// The map an overlay places itself with: normalized picture coordinates to
    /// view points, zoom and pan included.
    ///
    /// Built from the viewport's own `quadScale` and `visibleFraction` — the
    /// two numbers the vertex shader is handed — so an overlay cannot disagree
    /// with the picture about where the picture is.
    private func pictureMap(in size: CGSize) -> CanvasLayout.PictureMap {
        guard engine.imageWidth > 0, engine.imageHeight > 0, size.height > 0 else {
            return CanvasLayout.PictureMap()
        }
        let imageAspect = CGFloat(engine.imageWidth) / CGFloat(engine.imageHeight)
        let viewAspect = size.width / size.height
        return CanvasLayout.pictureMap(
            quadScale: viewport.quadScale(imageAspect: imageAspect, viewAspect: viewAspect),
            visible: viewport.visibleFraction(imageAspect: imageAspect, viewAspect: viewAspect),
            center: viewport.center,
            in: size)
    }
}
