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
        view.clearColor = MTLClearColorMake(0.165, 0.165, 0.173, 1.0)  // --surround
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

        /// Shader-side view transform.
        private struct Transform {
            var quadScale = SIMD2<Float>(1, 1)
            var uvMin = SIMD2<Float>(0, 0)
            var uvSize = SIMD2<Float>(1, 1)
            var split: Float = 1
            var vertical: UInt32 = 1
            var surround = SIMD3<Float>(0.165, 0.165, 0.173)   // --surround
            var pad: Float = 0
        }

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

            // A fullscreen triangle. Compiled at runtime because it belongs to
            // the view, not the pipeline — keeping it out of the engine's
            // shader library keeps that purely about pixels.
            let source = """
            #include <metal_stdlib>
            using namespace metal;

            struct Transform {
                float2 quadScale;
                float2 uvMin;
                float2 uvSize;
                float  split;      // 0..1 along the axis; 1 = no split
                uint   vertical;   // 1 splits left/right, 0 splits top/bottom
                float3 surround;   // what empty space blends to
                float  _pad;
            };

            struct VOut {
                float4 pos [[position]];
                float2 uv;
                // Position across the drawn quad, 0..1, y downward — the same
                // sense the panel's own geometry uses. Carried rather than
                // recovered from uv, because uv is flipped in y and scaled into
                // a sub-rectangle of the texture, and unpicking that in the
                // fragment shader is what put the top/bottom split upside down.
                float2 quad;
            };

            // A quad as a triangle strip, NOT the usual oversized fullscreen
            // triangle. That trick relies on the triangle extending past the
            // viewport so its hypotenuse is clipped away — scale it down to
            // letterbox and the hypotenuse becomes visible as a diagonal edge
            // with black on the far side.
            vertex VOut orionBlitVertex(uint vid [[vertex_id]],
                                        constant Transform& t [[buffer(0)]]) {
                const float2 p = float2(float(vid & 1u), float(vid >> 1u));
                VOut out;
                out.pos  = float4((p * 2.0 - 1.0) * t.quadScale, 0.0, 1.0);
                out.uv   = t.uvMin + float2(p.x, 1.0 - p.y) * t.uvSize;
                out.quad = float2(p.x, 1.0 - p.y);
                return out;
            }

            fragment float4 orionBlitFragment(VOut in [[stage_in]],
                                              texture2d<float> image [[texture(0)]],
                                              texture2d<float> original [[texture(1)]],
                                              sampler samp [[sampler(0)]],
                                              constant Transform& t [[buffer(0)]]) {
                float4 edited = image.sample(samp, in.uv);

                // Alpha is zero wherever the geometry pass found nothing —
                // outside a rotated frame, for instance. Blending to the
                // surround rather than showing black is what removes the
                // apparent edge of a viewport: the picture just sits on the
                // background like a print on a table.
                edited.rgb = mix(t.surround, edited.rgb, edited.a);
                edited.a = 1.0;

                if (t.split >= 0.999) return edited;

                // Which side of the divider this pixel is on. The divider runs
                // across the drawn quad, not across the image, so it stays put
                // while the image pans underneath — and it lands where the
                // panel draws it, which at any zoom other than fit is not the
                // same rectangle the image occupies when fitted.
                const float where_ = (t.vertical != 0u) ? in.quad.x : in.quad.y;

                if (where_ > t.split) return edited;

                // A hairline so the boundary is visible against similar tones.
                if (abs(where_ - t.split) < 0.0015) {
                    return float4(1.0, 1.0, 1.0, 1.0);
                }
                float4 before = original.sample(samp, in.uv);
                before.rgb = mix(t.surround, before.rgb, before.a);
                return float4(before.rgb, 1.0);
            }
            """

            do {
                let library = try device.makeLibrary(source: source, options: nil)
                let desc = MTLRenderPipelineDescriptor()
                desc.vertexFunction = library.makeFunction(name: "orionBlitVertex")
                desc.fragmentFunction = library.makeFunction(name: "orionBlitFragment")
                desc.colorAttachments[0].pixelFormat = view.colorPixelFormat
                pipeline = try device.makeRenderPipelineState(descriptor: desc)
            } catch {
                NSLog("Orion: could not build the display pipeline — \(error)")
            }

            let sd = MTLSamplerDescriptor()
            sd.minFilter = .linear
            sd.magFilter = .linear   // swapped to nearest past 400%, below
            sd.sAddressMode = .clampToEdge
            sd.tAddressMode = .clampToEdge
            sampler = device.makeSamplerState(descriptor: sd)
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

            guard targeted.isActive,
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
            if targeted.isActive, let band = targeted.activeBand {
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
        func hover(at point: CGPoint, in view: NSView) {
            guard targeted.isActive else {
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
            if let hue = TargetedAdjust.hue(r: s.scene.r, g: s.scene.g, b: s.scene.b) {
                targeted.hoverBand = TargetedAdjust.band(forHue: hue)
                targeted.hoverIsNeutral = false
            } else {
                targeted.hoverBand = nil
                targeted.hoverIsNeutral = true
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
            // The orientation node writes into a square texture so a rotation
            // never needs the graph recompiled — only the top-left rectangle is
            // valid, and its size is what the engine reports.
            let validW = CGFloat(engine.imageWidth)
            let validH = CGFloat(engine.imageHeight)
            guard validW > 0, validH > 0 else { return Transform() }

            let validU = validW / CGFloat(texture.width)
            let validV = validH / CGFloat(texture.height)

            // The crop preview is rendered into a texture of the frame's own
            // aspect — the extra context lives inside the picture rather than
            // spilling past it — so there is nothing special to do here. The
            // output is fitted to the view, cropping or not.
            let imageAspect = validW / validH
            let viewAspect = view.drawableSize.width / max(view.drawableSize.height, 1)

            // Report true magnification so the toolbar can show a real percent.
            viewport.fitScale = min(view.drawableSize.width / validW,
                                    view.drawableSize.height / validH)

            let visible = viewport.visibleFraction(imageAspect: imageAspect, viewAspect: viewAspect)
            viewport.clamp(to: visible)

            let quad = viewport.quadScale(imageAspect: imageAspect, viewAspect: viewAspect)

            var t = Transform()
            t.quadScale = SIMD2<Float>(Float(quad.width), Float(quad.height))
            // Scale image-space UVs into the valid sub-rectangle of the texture.
            t.uvSize = SIMD2<Float>(Float(visible.width * validU),
                                    Float(visible.height * validV))
            t.uvMin = SIMD2<Float>(Float((viewport.center.x - visible.width / 2) * validU),
                                   Float((viewport.center.y - visible.height / 2) * validV))
            return t
        }

        // MARK: Draw

        func mtkView(_ view: MTKView, drawableSizeWillChange size: CGSize) {
            view.needsDisplay = true
        }

        func draw(in view: MTKView) {
            guard let queue, let pipeline,
                  let texture = engine.outputTexture,
                  let descriptor = view.currentRenderPassDescriptor,
                  let drawable = view.currentDrawable,
                  let buffer = queue.makeCommandBuffer(),
                  let encoder = buffer.makeRenderCommandEncoder(descriptor: descriptor)
            else { return }

            var t = transform(for: view, texture: texture)

            // Past 4x, linear filtering just smears — a photographer inspecting
            // at that magnification wants to see the actual pixels.
            let magnified = viewport.fitScale * viewport.zoom > 4.0
            let samp = magnified ? nearestSampler(view.device) : sampler

            t.split = Float(engine.compareSplit)
            t.vertical = engine.compareVertical ? 1 : 0

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
