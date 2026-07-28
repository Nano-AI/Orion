import MetalKit
import SwiftUI

/// The canvas. The engine's output MTLTexture is sampled straight into the
/// drawable — no readback, no copy, no CPU touching pixels. This is the whole
/// reason the shell is native (UI-DECISION.md).
struct ImageCanvas: NSViewRepresentable {

    let engine: Engine
    /// Changes whenever a new frame is ready; drives the redraw.
    let generation: UInt64

    func makeCoordinator() -> Renderer { Renderer(engine: engine) }

    func makeNSView(context: Context) -> MTKView {
        let view = MTKView()
        view.device = engine.metalDevice
        view.delegate = context.coordinator
        view.colorPixelFormat = .bgra8Unorm
        view.framebufferOnly = true
        // Redraw on demand rather than at 60 Hz forever — nothing moves unless
        // an adjustment changed, and a photo editor idling on the GPU is rude.
        view.isPaused = true
        view.enableSetNeedsDisplay = true
        view.clearColor = MTLClearColorMake(0.165, 0.165, 0.173, 1.0)  // --surround
        context.coordinator.attach(to: view)
        return view
    }

    func updateNSView(_ view: MTKView, context: Context) {
        view.needsDisplay = true
    }

    // MARK: - Renderer

    final class Renderer: NSObject, MTKViewDelegate {
        private let engine: Engine
        private var queue: MTLCommandQueue?
        private var pipeline: MTLRenderPipelineState?
        private var sampler: MTLSamplerState?

        init(engine: Engine) {
            self.engine = engine
            super.init()
        }

        func attach(to view: MTKView) {
            guard let device = view.device else { return }
            queue = device.makeCommandQueue()

            // A fullscreen triangle. Compiled at runtime because it belongs to
            // the view, not the pipeline — keeping it out of the engine's
            // metallib keeps that library purely about pixels.
            let source = """
            #include <metal_stdlib>
            using namespace metal;

            struct VOut {
                float4 pos [[position]];
                float2 uv;
            };

            vertex VOut orionBlitVertex(uint vid [[vertex_id]],
                                        constant float2& scale [[buffer(0)]]) {
                const float2 p = float2((vid << 1) & 2, vid & 2);
                VOut out;
                out.pos = float4((p * 2.0 - 1.0) * scale, 0.0, 1.0);
                out.uv  = float2(p.x, 1.0 - p.y);
                return out;
            }

            fragment float4 orionBlitFragment(VOut in [[stage_in]],
                                              texture2d<float> image [[texture(0)]],
                                              sampler samp [[sampler(0)]]) {
                return image.sample(samp, in.uv);
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
            sd.magFilter = .linear
            sd.sAddressMode = .clampToEdge
            sd.tAddressMode = .clampToEdge
            sampler = device.makeSamplerState(descriptor: sd)
        }

        func mtkView(_ view: MTKView, drawableSizeWillChange size: CGSize) {
            view.needsDisplay = true
        }

        func draw(in view: MTKView) {
            guard let queue, let pipeline, let sampler,
                  let texture = engine.outputTexture,
                  let descriptor = view.currentRenderPassDescriptor,
                  let drawable = view.currentDrawable,
                  let buffer = queue.makeCommandBuffer(),
                  let encoder = buffer.makeRenderCommandEncoder(descriptor: descriptor)
            else { return }

            // Letterbox: fit the photo inside the view without distorting it.
            let imageAspect = Float(texture.width) / Float(texture.height)
            let viewAspect = Float(view.drawableSize.width) /
                             Float(max(view.drawableSize.height, 1))
            var scale = SIMD2<Float>(1, 1)
            if imageAspect > viewAspect {
                scale.y = viewAspect / imageAspect
            } else {
                scale.x = imageAspect / viewAspect
            }

            encoder.setRenderPipelineState(pipeline)
            encoder.setVertexBytes(&scale, length: MemoryLayout<SIMD2<Float>>.size, index: 0)
            encoder.setFragmentTexture(texture, index: 0)
            encoder.setFragmentSamplerState(sampler, index: 0)
            encoder.drawPrimitives(type: .triangle, vertexStart: 0, vertexCount: 3)
            encoder.endEncoding()

            buffer.present(drawable)
            buffer.commit()
        }
    }
}
