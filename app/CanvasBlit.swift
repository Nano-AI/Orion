import CoreGraphics
import Metal

/// The display blit: the engine's output texture onto the drawable, with the
/// compare split composited in.
///
/// Factored out of `ImageCanvas` so that it has exactly one implementation.
/// Compare composites *two* textures here, in the fragment shader, and nothing
/// upstream of this file knows that — which is why a scenario measuring
/// `engine.outputTexture` could pass while the split on screen was wrong. The
/// runner now renders through this same pipeline offscreen (`composite`), so a
/// compare bug has nowhere left to hide: there is one shader string and both
/// paths use it.
enum CanvasBlit {

    /// Shader-side view transform. The field order and padding must match the
    /// `Transform` struct in `source` exactly.
    struct Transform {
        var quadScale = SIMD2<Float>(1, 1)
        var uvMin = SIMD2<Float>(0, 0)
        var uvSize = SIMD2<Float>(1, 1)
        var split: Float = 1
        var vertical: UInt32 = 1
        var surround = Orion.Components.surround
        var pad: Float = 0
    }

    /// Compiled at runtime because it belongs to the view, not the pipeline —
    /// keeping it out of the engine's shader library keeps that purely about
    /// pixels.
    static let source = """
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

    static func makePipeline(device: MTLDevice,
                             pixelFormat: MTLPixelFormat) -> MTLRenderPipelineState? {
        do {
            let library = try device.makeLibrary(source: source, options: nil)
            let desc = MTLRenderPipelineDescriptor()
            desc.vertexFunction = library.makeFunction(name: "orionBlitVertex")
            desc.fragmentFunction = library.makeFunction(name: "orionBlitFragment")
            desc.colorAttachments[0].pixelFormat = pixelFormat
            return try device.makeRenderPipelineState(descriptor: desc)
        } catch {
            NSLog("Orion: could not build the display pipeline — \(error)")
            return nil
        }
    }

    /// Where the picture lands in the drawable, and which part of the (square)
    /// orientation texture is live.
    ///
    /// Writes `viewport.fitScale` and clamps it, because both are functions of
    /// the drawable size and this is the only place that knows it.
    static func transform(engine: Engine, viewport: Viewport,
                          drawableSize: CGSize, texture: MTLTexture) -> Transform {
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
        let viewAspect = drawableSize.width / max(drawableSize.height, 1)

        // Report true magnification so the toolbar can show a real percent.
        viewport.fitScale = min(drawableSize.width / validW,
                                drawableSize.height / validH)

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

    /// The canvas as the screen would show it, rendered offscreen.
    ///
    /// For the scenario runner, which has no window. Fit, unzoomed, at the
    /// picture's own pixel size, so a region given in normalized coordinates
    /// means the same thing it means to `Screenshot.regionStats`.
    ///
    /// Deliberately the *whole* path — the same shader, the same transform, the
    /// same two texture bindings the view sets — because the point is to catch
    /// a fault in the compositing, and a hand-rolled CPU stand-in for it would
    /// be a second implementation with its own bugs and none of the first's.
    static func composite(engine: Engine, viewport: Viewport = Viewport()) -> MTLTexture? {
        guard let device = engine.metalDevice,
              let edited = engine.outputTexture,
              engine.imageWidth > 0, engine.imageHeight > 0
        else { return nil }

        let w = Int(engine.imageWidth), h = Int(engine.imageHeight)
        let format: MTLPixelFormat = .rgba16Float

        let desc = MTLTextureDescriptor.texture2DDescriptor(
            pixelFormat: format, width: w, height: h, mipmapped: false)
        desc.usage = [.renderTarget, .shaderRead]
        desc.storageMode = .shared

        guard let target = device.makeTexture(descriptor: desc),
              let pipeline = makePipeline(device: device, pixelFormat: format),
              let queue = device.makeCommandQueue(),
              let buffer = queue.makeCommandBuffer()
        else { return nil }

        let sd = MTLSamplerDescriptor()
        sd.minFilter = .linear
        sd.magFilter = .linear
        sd.sAddressMode = .clampToEdge
        sd.tAddressMode = .clampToEdge
        guard let sampler = device.makeSamplerState(descriptor: sd) else { return nil }

        let pass = MTLRenderPassDescriptor()
        pass.colorAttachments[0].texture = target
        pass.colorAttachments[0].loadAction = .clear
        pass.colorAttachments[0].storeAction = .store
        let s = Orion.Components.surround
        pass.colorAttachments[0].clearColor =
            MTLClearColorMake(Double(s.x), Double(s.y), Double(s.z), 1.0)

        guard let encoder = buffer.makeRenderCommandEncoder(descriptor: pass) else {
            return nil
        }

        var t = transform(engine: engine, viewport: viewport,
                          drawableSize: CGSize(width: w, height: h), texture: edited)
        t.split = Float(engine.compareSplit)
        t.vertical = engine.compareVertical ? 1 : 0

        encoder.setRenderPipelineState(pipeline)
        encoder.setVertexBytes(&t, length: MemoryLayout<Transform>.stride, index: 0)
        encoder.setFragmentBytes(&t, length: MemoryLayout<Transform>.stride, index: 0)
        encoder.setFragmentTexture(edited, index: 0)
        encoder.setFragmentTexture(engine.originalTexture ?? edited, index: 1)
        encoder.setFragmentSamplerState(sampler, index: 0)
        encoder.drawPrimitives(type: .triangleStrip, vertexStart: 0, vertexCount: 4)
        encoder.endEncoding()

        buffer.commit()
        buffer.waitUntilCompleted()
        return target
    }
}
