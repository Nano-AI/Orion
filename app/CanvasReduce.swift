import Metal

/// Averaging the render down for a canvas that is showing it smaller than it is.
///
/// ⚠ **This is not an optimisation, it is the difference between a photograph
/// and an aliased one.** `MTLSamplerMinMagFilter.linear` chooses between the
/// texels *within* one mip level; on a chain of one it is a 2x2 tap however far
/// the picture is being reduced. Fitting a 7968 x 5320 frame into a window is a
/// 5x reduction, so the sampler read 4 of every 28 texels and discarded the
/// rest — point sampling with a wide stride. Sensor noise survived at full
/// amplitude instead of averaging down by the square root of the footprint, and
/// every edge in the picture aliased. The photograph on screen was measurably
/// grainier and harder than the same render written to a file, which is the
/// "noisier and sharper than Darktable and Affinity" report, and it got worse
/// with the sensor because the reduction ratio does.
///
/// Separate from `CanvasBlit` so both the app and `orion-viewport-tests` can
/// build it: the blit's transform takes an `Engine`, and this must be assertable
/// without one. See `testCanvasReductionAverages` — `Renderer.draw` is reachable
/// from neither suite, so without a file like this the canvas's own sampling has
/// no oracle but a person looking at the screen, which is exactly how it shipped.
enum CanvasReduce {

    /// One 2x2 average — the base of the chain.
    ///
    /// Hand-written rather than left to `generateMipmaps`, because there is
    /// nothing to leave to it: level 0 *is* the copy and a blit cannot scale.
    /// Doing the first halving here is also what makes the whole chain cost a
    /// quarter of the render rather than a third more than it — 56 MB against
    /// 226 MB at 42 Mpx, on a memory budget #162 already has the developer
    /// choosing between tiling and lower precision.
    static let source = """
    #include <metal_stdlib>
    using namespace metal;

    kernel void orionHalve(texture2d<float, access::read>  src [[texture(0)]],
                           texture2d<float, access::write> dst [[texture(1)]],
                           uint2 gid [[thread_position_in_grid]]) {
        if (gid.x >= dst.get_width() || gid.y >= dst.get_height()) return;
        const uint2 m = uint2(src.get_width() - 1, src.get_height() - 1);
        const uint2 p = gid * 2;
        const float4 a = src.read(min(p, m));
        const float4 b = src.read(min(p + uint2(1, 0), m));
        const float4 c = src.read(min(p + uint2(0, 1), m));
        const float4 d = src.read(min(p + uint2(1, 1), m));
        dst.write((a + b + c + d) * 0.25, gid);
    }
    """

    static func makePipeline(device: MTLDevice) -> MTLComputePipelineState? {
        do {
            let library = try device.makeLibrary(source: source, options: nil)
            guard let fn = library.makeFunction(name: "orionHalve") else { return nil }
            return try device.makeComputePipelineState(function: fn)
        } catch {
            NSLog("Orion: could not build the canvas reduction — \(error)")
            return nil
        }
    }

    /// `source`'s valid rectangle at half resolution, carrying a mip chain.
    ///
    /// ⚠ **Only the valid rectangle.** The orientation node writes into a square
    /// texture with a live top-left corner, and reducing the whole thing would
    /// fold the dead remainder into every level and fringe the picture's right
    /// and bottom edges with it — a defect that draws a plausible image rather
    /// than an obviously broken one. A tight base also makes the blit's
    /// `validU`/`validV` exactly 1.
    ///
    /// ⚠ The levels average in the **display encoding**, which is what this
    /// texture holds, and that is decision #40's reasoning verbatim: averaging
    /// unbounded scene-linear values blooms a specular edge, which is why
    /// `geometry.slang` resamples display-encoded pixels too.
    ///
    /// ⚠ `generateMipmaps` was refused for the grain plate (#81) because its
    /// filter is implementation-defined and two machines must render one edit
    /// identically. Nothing here is ever exported — this texture lives for the
    /// length of one draw — so that argument does not reach it.
    static func reduced(_ source: MTLTexture, valid: (width: Int, height: Int),
                        device: MTLDevice, queue: MTLCommandQueue,
                        halve: MTLComputePipelineState) -> MTLTexture? {
        let w = (valid.width + 1) / 2, h = (valid.height + 1) / 2
        guard w > 1, h > 1,
              valid.width <= source.width, valid.height <= source.height
        else { return nil }

        let desc = MTLTextureDescriptor.texture2DDescriptor(
            pixelFormat: source.pixelFormat, width: w, height: h, mipmapped: true)
        // `shaderWrite` for the reduction that fills level 0, `renderTarget`
        // because `generateMipmaps` reduces by rendering and refuses a texture
        // that cannot be one.
        desc.usage = [.shaderRead, .shaderWrite, .renderTarget]
        desc.storageMode = .private

        guard let target = device.makeTexture(descriptor: desc),
              // Bound as a one-level view so the kernel's `write` lands on level
              // 0 rather than on whichever level the driver would pick.
              let base = target.makeTextureView(pixelFormat: target.pixelFormat,
                                                textureType: .type2D,
                                                levels: 0..<1, slices: 0..<1),
              let buffer = queue.makeCommandBuffer(),
              let compute = buffer.makeComputeCommandEncoder()
        else { return nil }

        compute.setComputePipelineState(halve)
        compute.setTexture(source, index: 0)
        compute.setTexture(base, index: 1)
        compute.dispatchThreadgroups(
            MTLSize(width: (w + 15) / 16, height: (h + 15) / 16, depth: 1),
            threadsPerThreadgroup: MTLSize(width: 16, height: 16, depth: 1))
        compute.endEncoding()

        guard let blit = buffer.makeBlitCommandEncoder() else { return nil }
        blit.generateMipmaps(for: target)
        blit.endEncoding()
        buffer.commit()
        return target
    }
}
