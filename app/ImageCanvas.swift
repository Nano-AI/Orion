import MetalKit
import SwiftUI

/// The canvas. The engine's output MTLTexture is sampled straight into the
/// drawable — no readback, no copy, no CPU touching pixels.
///
/// Zooming needs no extra machinery: the pipeline already renders at full
/// resolution, so magnifying is just sampling a smaller rectangle of that
/// texture. At 100% you are looking at real pixels, not an upscale. That is
/// what Lightroom's chunked rendering buys with far more effort — we get it for
/// free by never working at preview resolution in the first place.
struct ImageCanvas: NSViewRepresentable {

    let engine: Engine
    let viewport: Viewport
    let targeted: TargetedAdjust
    /// Changes whenever a new frame is ready; drives the redraw.
    let generation: UInt64


    func makeCoordinator() -> Renderer {
        Renderer(engine: engine, viewport: viewport, targeted: targeted)
    }

    func makeNSView(context: Context) -> MTKView {
        let view = TrackingMTKView()
        view.device = engine.metalDevice
        view.delegate = context.coordinator
        view.colorPixelFormat = .bgra8Unorm
        view.framebufferOnly = true
        view.autoResizeDrawable = true
        view.autoresizingMask = [.width, .height]
        // Redraw on demand rather than at 60 Hz forever — nothing moves unless
        // an adjustment or the viewport changed.
        view.isPaused = true
        view.enableSetNeedsDisplay = true
        let s = Orion.Components.surround
        view.clearColor = MTLClearColorMake(Double(s.x), Double(s.y), Double(s.z), 1.0)
        view.coordinator = context.coordinator
        context.coordinator.attach(to: view)
        return view
    }

    func updateNSView(_ view: MTKView, context: Context) {
        view.needsDisplay = true
    }

    /// MTKView subclass that forwards scroll and magnify gestures. SwiftUI
    /// exposes no direct trackpad magnification event, so the canvas has to be
    /// an AppKit view to get pinch-zoom at all.
    final class TrackingMTKView: MTKView {
        weak var coordinator: Renderer?

        override var acceptsFirstResponder: Bool { true }



        /// Flipped so y grows downward, matching image coordinates. Mouse
        /// deltas in AppKit's default orientation are the reason drag felt
        /// inverted; working in a flipped view removes the ambiguity.
        override var isFlipped: Bool { true }

        override func scrollWheel(with event: NSEvent) {
            coordinator?.handleScroll(event, in: self)
        }

        override func magnify(with event: NSEvent) {
            coordinator?.handleMagnify(event, in: self)
        }

        override func mouseDown(with event: NSEvent) {
            coordinator?.beginDrag(at: convert(event.locationInWindow, from: nil), in: self)
        }

        override func mouseUp(with event: NSEvent) {
            coordinator?.endDrag()
        }

        override func mouseDragged(with event: NSEvent) {
            coordinator?.handleDrag(to: convert(event.locationInWindow, from: nil), in: self)
        }

        override func rightMouseDown(with event: NSEvent) {
            coordinator?.resetView(in: self)
        }

        override func mouseMoved(with event: NSEvent) {
            coordinator?.hover(at: convert(event.locationInWindow, from: nil), in: self)
        }

        override func mouseExited(with event: NSEvent) {
            coordinator?.clearHover()
        }

        // A tracking area is what makes mouseMoved fire at all.
        override func updateTrackingAreas() {
            super.updateTrackingAreas()
            trackingAreas.forEach(removeTrackingArea)
            addTrackingArea(NSTrackingArea(
                rect: bounds,
                options: [.mouseMoved, .mouseEnteredAndExited, .activeInKeyWindow, .inVisibleRect],
                owner: self))
        }
    }

    // MARK: - Renderer

    final class Renderer: NSObject, MTKViewDelegate {
        private let engine: Engine
        private let viewport: Viewport
        private let targeted: TargetedAdjust
        private var queue: MTLCommandQueue?
        private var pipeline: MTLRenderPipelineState?
        private var sampler: MTLSamplerState?

        /// The mip chain the blit actually samples, and the render it was built
        /// from. See `minified(_:)`.
        private var mipped: MTLTexture?
        private var mippedFrom: UInt64 = .max
        private var mippedSource: ObjectIdentifier?
        private var halve: MTLComputePipelineState?

        /// Shader-side view transform. Shared with the offscreen compositor —
        /// see `CanvasBlit`.
        private typealias Transform = CanvasBlit.Transform

        init(engine: Engine, viewport: Viewport, targeted: TargetedAdjust) {
            self.engine = engine
            self.viewport = viewport
            self.targeted = targeted
            super.init()
        }

        /// View point to normalized image coordinates, honouring letterbox and
        /// zoom. Returns nil outside the image.
        private func imageUV(_ point: CGPoint, in view: NSView) -> CGPoint? {
            guard engine.imageWidth > 0, view.bounds.width > 0 else { return nil }
            let imageAspect = CGFloat(engine.imageWidth) / CGFloat(engine.imageHeight)
            let viewAspect = view.bounds.width / max(view.bounds.height, 1)

            let quad = viewport.quadScale(imageAspect: imageAspect, viewAspect: viewAspect)
            let visible = viewport.visibleFraction(imageAspect: imageAspect,
                                                   viewAspect: viewAspect)

            // Undo the letterbox: map the point into the drawn rectangle.
            let nx = point.x / max(view.bounds.width, 1)
            let ny = point.y / max(view.bounds.height, 1)
            let insetX = (1 - quad.width) / 2
            let insetY = (1 - quad.height) / 2
            guard quad.width > 0, quad.height > 0 else { return nil }

            let qx = (nx - insetX) / quad.width
            let qy = (ny - insetY) / quad.height
            guard qx >= 0, qx <= 1, qy >= 0, qy <= 1 else { return nil }

            return CGPoint(x: viewport.center.x - visible.width / 2 + qx * visible.width,
                           y: viewport.center.y - visible.height / 2 + qy * visible.height)
        }

        func attach(to view: MTKView) {
            guard let device = view.device else { return }
            queue = device.makeCommandQueue()

            pipeline = CanvasBlit.makePipeline(device: device,
                                               pixelFormat: view.colorPixelFormat)

            let sd = MTLSamplerDescriptor()
            sd.minFilter = .linear
            sd.magFilter = .linear   // swapped to nearest past 400%, below
            // ⚠ **The mip filter is the whole point** — see `minified(_:)`.
            // `minFilter` only chooses between texels *within* one level, so on
            // a chain of one it is a 2x2 tap however far the picture is being
            // shrunk. Fitting a 42 Mpx frame to a 1600 pt canvas is a 4.9x
            // reduction: the tap reads 4 of every 24 texels and throws the rest
            // away, which is point sampling with extra steps. Sensor noise
            // survives at full amplitude instead of averaging down, and every
            // edge aliases — the picture reads harsher and grainier on screen
            // than the same render exported to a file, which is what a
            // photographer comparing Orion to Darktable actually sees.
            sd.mipFilter = .linear
            sd.sAddressMode = .clampToEdge
            sd.tAddressMode = .clampToEdge
            sampler = device.makeSamplerState(descriptor: sd)
        }

        /// The render, with a mip chain, so minification averages rather than
        /// decimates.
        ///
        /// The engine's output cannot carry one itself: it is the target of a
        /// compute write and is allocated per graph, so the levels are built
        /// here into a texture the canvas owns.
        ///
        /// ⚠ Rebuilt on a **new render**, not on every draw. `Engine.generation`
        /// ticks once per published frame, so panning and zooming — which redraw
        /// constantly and change no pixels — reuse the chain. Without that this
        /// copies and reduces 42 Mpx on every scroll event.
        ///
        /// ⚠ And the levels are averaged in the **display encoding**, which is
        /// what this texture holds. That is deliberate and is decision #40's
        /// reasoning verbatim: averaging unbounded scene-linear values blooms a
        /// specular edge, which is why `geometry.slang` resamples display-encoded
        /// pixels too.
        ///
        /// ⚠ **Only the valid rectangle is copied**, and the chain is built over
        /// a texture of exactly that size. The orientation node writes into a
        /// square texture with only its top-left rectangle live, so reducing the
        /// whole thing would fold the dead region into every level and fringe the
        /// right and bottom edges of the picture with it — a defect that draws a
        /// plausible image rather than an obviously broken one. A tight copy also
        /// makes `validU`/`validV` in `CanvasBlit.transform` exactly 1.
        ///
        /// ⚠ `generateMipmaps` was refused for the grain plate (#81) because its
        /// filter is implementation-defined and the plate has to be identical on
        /// every machine or two exports of one edit differ. Nothing here is ever
        /// exported — this texture exists for the length of one draw — so that
        /// argument does not reach it.
        ///
        /// ⚠ **The chain starts at half the render, not at the render**, and that
        /// is a memory decision rather than a quality one. A full-resolution base
        /// plus its levels is 226 MB at 42 Mpx and 452 MB in wide output, on a
        /// budget that #162 already has the developer choosing between tiling and
        /// lower precision. Halving first costs 56 MB and gives up nothing that
        /// is used: the caller only asks for this at a reduction of 2x or more,
        /// where a half-resolution base is at worst being drawn 1:1.
        private func minified(_ source: MTLTexture,
                              valid: (width: UInt32, height: UInt32),
                              device: MTLDevice?) -> MTLTexture? {
            let w = (Int(valid.width) + 1) / 2, h = (Int(valid.height) + 1) / 2
            guard w > 1, h > 1,
                  Int(valid.width) <= source.width,
                  Int(valid.height) <= source.height else { return nil }

            let id = ObjectIdentifier(source)
            if let mipped, mippedFrom == engine.generation, mippedSource == id,
               mipped.width == w, mipped.height == h {
                return mipped
            }
            guard let device, let queue else { return nil }
            if halve == nil { halve = CanvasReduce.makePipeline(device: device) }
            guard let halve,
                  let target = CanvasReduce.reduced(
                      source, valid: (Int(valid.width), Int(valid.height)),
                      device: device, queue: queue, halve: halve)
            else { return nil }

            mipped = target
            mippedFrom = engine.generation
            mippedSource = id
            return target
        }

        // MARK: Input

        private func normalizedPoint(_ event: NSEvent, in view: NSView) -> CGPoint {
            let p = view.convert(event.locationInWindow, from: nil)
            // The view is flipped, so y already grows downward like image space.
            return CGPoint(x: p.x / max(view.bounds.width, 1),
                           y: p.y / max(view.bounds.height, 1))
        }

        func handleScroll(_ event: NSEvent, in view: NSView) {
            let visible = currentVisible(for: view)
            if event.modifierFlags.contains(.command) || event.hasPreciseScrollingDeltas == false {
                let factor = 1 + event.scrollingDeltaY * 0.01
                viewport.zoomBy(factor, anchor: normalizedPoint(event, in: view), visible: visible)
            } else {
                // Two-finger scroll pans, which is what a trackpad user expects
                // once they are zoomed in.
                viewport.pan(by: CGSize(width: event.scrollingDeltaX / max(view.bounds.width, 1),
                                        height: event.scrollingDeltaY / max(view.bounds.height, 1)),
                             visible: visible)  // trackpad scroll already matches content direction
            }
            viewport.clamp(to: currentVisible(for: view))
            view.needsDisplay = true
        }

        func handleMagnify(_ event: NSEvent, in view: NSView) {
            viewport.zoomBy(1 + event.magnification,
                            anchor: normalizedPoint(event, in: view),
                            visible: currentVisible(for: view))
            viewport.clamp(to: currentVisible(for: view))
            view.needsDisplay = true
        }

        private var dragAnchor: CGPoint?

        func beginDrag(at point: CGPoint, in view: NSView) {
            dragAnchor = point

            // The mask color picker, armed from the Mask panel. It consumes
            // the click outright — the photograph must not also pan — and
            // disarms, because it is a pick, not a mode.
            if engine.tool == .maskColor, let uv = imageUV(point, in: view) {
                dragAnchor = nil
                engine.pickMaskColor(at: uv)
                engine.tool = .none
                view.needsDisplay = true
                return
            }

            // ⚠ Spot placement is **not** here any more. It was a click that
            // placed a spot and chose its source for you, with no way to see
            // either afterwards; it is a press-and-drag on `SpotOverlay` now,
            // which owns the whole gesture. Two handlers for one press is how a
            // single drag places two spots.

            guard engine.tool == .targeted,
                  let uv = imageUV(point, in: view),
                  let s = engine.sample(u: Float(uv.x), v: Float(uv.y)),
                  let hue = TargetedAdjust.hue(r: s.scene.r, g: s.scene.g, b: s.scene.b)
            else { return }

            targeted.begin(band: TargetedAdjust.band(forHue: hue), hue: hue)
        }

        func endDrag() { dragAnchor = nil; targeted.end() }

        /// Drags move the photo with the cursor: grab it and it follows. In a
        /// flipped view both axes point the same way as image space, so the
        /// delta needs no sign juggling.
        func handleDrag(to point: CGPoint, in view: NSView) {
            guard let anchor = dragAnchor else { dragAnchor = point; return }
            let dx = (point.x - anchor.x) / max(view.bounds.width, 1)
            let dy = (point.y - anchor.y) / max(view.bounds.height, 1)
            dragAnchor = point

            // With the tool armed, a vertical drag adjusts the band under the
            // cursor rather than panning. Up increases, matching every other
            // drag-to-adjust control on the platform.
            if engine.tool == .targeted, let band = targeted.activeBand {
                let delta = Float(-dy) * 2.0
                let i = band.rawValue
                switch targeted.mode {
                case .hue:        engine.hueShift[i] = clampUnit(engine.hueShift[i] + delta)
                case .saturation: engine.satShift[i] = clampUnit(engine.satShift[i] + delta)
                case .luminance:  engine.lumShift[i] = clampUnit(engine.lumShift[i] + delta)
                }
                return
            }

            let visible = currentVisible(for: view)
            viewport.pan(by: CGSize(width: dx, height: dy), visible: visible)
            viewport.clamp(to: visible)
            view.needsDisplay = true
        }

        /// Publishes the color under the cursor so the loupe can show it.
        /// Serves both pickers: the targeted tool reads the band caption, the
        /// mask color picker just needs to not click blind.
        func hover(at point: CGPoint, in view: NSView) {
            guard engine.tool.wantsLoupe else {
                if targeted.hoverPoint != nil { targeted.clearHover() }
                return
            }
            guard let uv = imageUV(point, in: view),
                  let s = engine.sample(u: Float(uv.x), v: Float(uv.y)) else {
                targeted.clearHover()
                return
            }

            targeted.hoverPoint = point

            // The swatch shows the *rendered* color, already display-encoded,
            // so it matches the pixel under the cursor exactly. Showing the
            // scene-linear value instead would look nothing like the photo.
            targeted.hoverColor = CGColor(red: s.display.r, green: s.display.g,
                                          blue: s.display.b, alpha: 1)

            // The band comes from the scene color, which does not move as you
            // edit.
            //
            // Written only when they change. An `@Observable` property notifies
            // on every assignment, equal value or not, and this runs on every
            // mouse-moved event — but the band under the cursor holds still for
            // most of a gesture, so most of those notifications were asking the
            // loupe's caption to redraw the word it was already showing.
            let hue = TargetedAdjust.hue(r: s.scene.r, g: s.scene.g, b: s.scene.b)
            let band = hue.map(TargetedAdjust.band(forHue:))
            if targeted.hoverBand != band { targeted.hoverBand = band }
            if targeted.hoverIsNeutral != (hue == nil) {
                targeted.hoverIsNeutral = (hue == nil)
            }
        }

        func clearHover() { targeted.clearHover() }

        func resetView(in view: NSView) {
            viewport.reset()
            view.needsDisplay = true
        }

        // MARK: Geometry

        private func currentVisible(for view: NSView) -> CGSize {
            guard engine.imageHeight > 0, view.bounds.height > 0 else {
                return CGSize(width: 1, height: 1)
            }
            let imageAspect = CGFloat(engine.imageWidth) / CGFloat(engine.imageHeight)
            let viewAspect = view.bounds.width / view.bounds.height
            return viewport.visibleFraction(imageAspect: imageAspect, viewAspect: viewAspect)
        }

        private func transform(for view: MTKView, texture: MTLTexture) -> Transform {
            // The preview's valid rectangle is its own, not the full graph's.
            let valid: (width: UInt32, height: UInt32)? =
                engine.interacting ? engine.previewSize : nil
            return CanvasBlit.transform(engine: engine, viewport: viewport,
                                        drawableSize: view.drawableSize,
                                        texture: texture, valid: valid)
        }

        // MARK: Draw

        func mtkView(_ view: MTKView, drawableSizeWillChange size: CGSize) {
            view.needsDisplay = true
        }

        func draw(in view: MTKView) {
            guard let queue, let pipeline,
                  // ⚠ `displayTexture`, not `outputTexture`: the canvas is the
                  // one consumer allowed to see the quarter-linear preview, and
                  // only while a control is being dragged. Export, the
                  // histogram, the eyedropper and the screenshot harness all
                  // read the full graph and must keep doing so.
                  let rendered = engine.displayTexture,
                  let descriptor = view.currentRenderPassDescriptor,
                  let drawable = view.currentDrawable,
                  let buffer = queue.makeCommandBuffer(),
                  let encoder = buffer.makeRenderCommandEncoder(descriptor: descriptor)
            else { return }

            // ⚠ `transform` writes `viewport.fitScale`, and the magnification
            // below reads it, so it has to run before the choice of texture — and
            // the choice of texture is what decides whether the picture is
            // averaged down or decimated. See `minified`.
            var t = transform(for: view, texture: rendered)

            // Past 4x, linear filtering just smears — a photographer inspecting
            // at that magnification wants to see the actual pixels.
            let magnified = viewport.fitScale * viewport.zoom > 4.0
            let samp = magnified ? nearestSampler(view.device) : sampler

            // Shrinking by 2x or more is the case the chain exists for, and the
            // threshold is the half-resolution base it starts from — above it
            // there is nothing to average and the base would be magnified.
            // Compare holds the *full-resolution* original and both sides are
            // read through one set of UVs, so the split keeps the untouched pair.
            let valid: (width: UInt32, height: UInt32) =
                engine.interacting ? engine.previewSize
                                   : (engine.imageWidth, engine.imageHeight)
            let shrinking = viewport.fitScale * viewport.zoom <= 0.5
            let texture = (shrinking && !magnified && engine.compareSplit >= 0.999)
                ? (minified(rendered, valid: valid, device: view.device) ?? rendered)
                : rendered

            // The reduced base is exactly the valid rectangle, so the scaling
            // that fitted image UVs into a corner of the square render is the
            // identity here and has to come back out. ⚠ Recomputing the whole
            // transform against the smaller texture would also rewrite
            // `viewport.fitScale`, which is the *picture's* magnification and is
            // what the toolbar percentage and the 400% nearest-sampling switch
            // both read — it would read double.
            if texture !== rendered, rendered.width > 0, rendered.height > 0 {
                let u = Float(valid.width) / Float(rendered.width)
                let v = Float(valid.height) / Float(rendered.height)
                if u > 0, v > 0 {
                    t.uvMin  = SIMD2<Float>(t.uvMin.x / u,  t.uvMin.y / v)
                    t.uvSize = SIMD2<Float>(t.uvSize.x / u, t.uvSize.y / v)
                }
            }

            t.split = Float(engine.compareSplit)
            t.vertical = engine.compareVertical ? 1 : 0

            // ⚠ Before the bytes are copied, not after. The held original is a
            // *full-resolution* copy and the blit samples both textures through
            // one set of UVs, so compositing it against a quarter-linear
            // preview would read it through the wrong window — the same defect
            // the geometry changes caused, by a different route. The split is
            // suspended for the length of a drag instead.
            // ⚠ Belt to `beginInteraction`'s brace: interaction is refused
            // while comparing, so this should never fire. It stays because the
            // failure it guards — a split sampling two differently-sized
            // textures through one set of UVs — draws a plausible picture
            // rather than an obviously broken one.
            if engine.interacting { t.split = 1 }

            encoder.setRenderPipelineState(pipeline)
            encoder.setVertexBytes(&t, length: MemoryLayout<Transform>.stride, index: 0)
            encoder.setFragmentBytes(&t, length: MemoryLayout<Transform>.stride, index: 0)
            encoder.setFragmentTexture(texture, index: 0)
            encoder.setFragmentTexture(engine.originalTexture ?? texture, index: 1)
            encoder.setFragmentSamplerState(samp, index: 0)
            encoder.drawPrimitives(type: .triangleStrip, vertexStart: 0, vertexCount: 4)
            encoder.endEncoding()

            buffer.present(drawable)
            buffer.commit()
        }

        private func clampUnit(_ v: Float) -> Float { min(max(v, -1), 1) }

        private var cachedNearest: MTLSamplerState?
        private func nearestSampler(_ device: MTLDevice?) -> MTLSamplerState? {
            if let cachedNearest { return cachedNearest }
            guard let device else { return sampler }
            let sd = MTLSamplerDescriptor()
            sd.minFilter = .nearest
            sd.magFilter = .nearest
            sd.sAddressMode = .clampToEdge
            sd.tAddressMode = .clampToEdge
            cachedNearest = device.makeSamplerState(descriptor: sd)
            return cachedNearest
        }
    }
}
