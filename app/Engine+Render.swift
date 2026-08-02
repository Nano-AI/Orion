import Foundation
import Metal
import MetalKit
import AppKit

/// Getting a frame onto the screen: the textures, the preview graph, the push,
/// the render itself, and the readbacks that hang off it.
///
/// `render()` calls `refreshOriginal()`, which is in `Engine+Compare.swift`.
/// That is deliberate and is the only edge between the two files: covering
/// every route into a geometry change from one place is what stopped the held
/// original going stale. See the comment on `refreshOriginal`.

extension Engine {

    /// The developed picture with its geometry neutralised, for something that
    /// wants to *analyze* it rather than show it — today, segmentation.
    ///
    /// ⚠ The crop, the straighten and the user's rotation are reset around the
    /// render, so the only difference between what comes back and the frame the
    /// mask kernel works in is the EXIF quarter turn — which is an exact
    /// permutation and needs no resample. Neutralising rather than correcting
    /// is what stops a matte having no data outside the crop rectangle.
    /// research/masking.md §5.
    ///
    /// Two renders and a readback, and it restores the caller's state exactly —
    /// the same shape as `captureOriginal`, for the same reason.
    func renderForAnalysis(longEdge: Int) -> CGImage? {
        guard isLoaded else { return nil }

        let held = state
        var neutral = held
        neutral.cropX = 0; neutral.cropY = 0
        neutral.cropW = 1; neutral.cropH = 1
        neutral.straightenDeg = 0
        neutral.rotateQuarters = 0
        // ⚠ And the perspective, for exactly the reason the other three are
        // here: a raster matte is stored in FRAME coordinates, and a model
        // handed a keystone-corrected picture would return a selection that
        // fits the corrected picture and nothing else. The mask kernel does no
        // correction of its own by design, so the correction has to be off on
        // the way in.
        neutral.perspectiveVertical = 0
        neutral.perspectiveHorizontal = 0
        neutral.perspectiveAspect = 0
        let heldPreview = cropPreview

        // ⚠ And the coverage overlay, for the same reason `export` turns it
        // off: it tints the render red wherever the group covers, and this
        // render is fed to a segmentation model. "Show mask" is on exactly when
        // a photographer is working with masks, which is exactly when they
        // press Subject — so the failure is the common case, not a corner, and
        // it would look like the model being bad at this photograph.
        let heldOverlay = maskOverlay

        maskOverlay = false
        cropPreview = false
        apply(neutral)
        defer {
            apply(held)
            cropPreview = heldPreview
            maskOverlay = heldOverlay
        }

        let size = MatteGeometry.previewSize(frameWidth: Int(imageWidth),
                                             frameHeight: Int(imageHeight),
                                             longEdge: longEdge)
        guard size.width > 0, size.height > 0 else { return nil }
        return Screenshot.developedCGImage(self, fitting: size)
    }

    var metalDevice: MTLDevice? {
        guard let handle, let raw = orion_engine_metal_device(handle) else { return nil }
        return Unmanaged<AnyObject>.fromOpaque(raw).takeUnretainedValue() as? MTLDevice
    }

    var outputTexture: MTLTexture? {
        guard let handle, let raw = orion_engine_output_texture(handle) else { return nil }
        return Unmanaged<AnyObject>.fromOpaque(raw).takeUnretainedValue() as? MTLTexture
    }

    /// The preview graph's output, or nil when there is none.
    var previewTexture: MTLTexture? {
        guard let handle, let raw = orion_engine_preview_texture(handle) else {
            return nil
        }
        return Unmanaged<AnyObject>.fromOpaque(raw).takeUnretainedValue() as? MTLTexture
    }

    /// The texture the canvas should show right now.
    ///
    /// ⚠ Only the canvas asks. `outputTexture` is what export, the histogram,
    /// the eyedropper and the screenshot harness read, and all of them must
    /// keep reading it — a preview-resolution export is a mistake only the
    /// person receiving the file would find.
    var displayTexture: MTLTexture? {
        (interacting ? previewTexture : nil) ?? outputTexture
    }

    var previewSize: (width: UInt32, height: UInt32) {
        guard let handle else { return (0, 0) }
        var w: UInt32 = 0, h: UInt32 = 0
        guard orion_engine_preview_size(handle, &w, &h) == ORION_OK else {
            return (0, 0)
        }
        return (w, h)
    }

    /// A drag started. Renders go to the preview graph until `endInteraction`.
    func beginInteraction() {
        // ⚠ **Not while comparing.** The preview graph renders at a quarter
        // linear, and the split samples the held original and the edited render
        // through *one* set of UVs taken from the edited one — so with a
        // preview-sized edited texture the two no longer line up, and the
        // canvas dealt with that by suspending the split for the length of the
        // drag. Which means the Original side showed the edited picture while a
        // slider moved: the exact thing compare exists to prevent, at the exact
        // moment someone is using it to judge a change.
        //
        // Compare is a deliberate, short-lived viewing mode. Paying
        // full-resolution latency inside it is the right trade against a split
        // that lies.
        guard !comparing else { return }
        guard isLoaded, previewTexture != nil, !interacting else { return }
        log.interacting(true)
        interacting = true
    }

    /// The hand stopped. Renders the full graph once and goes back to it.
    func endInteraction() {
        guard interacting else { return }
        log.interacting(false)
        interacting = false
        // ⚠ The full graph is *stale* at this point — every tick of the drag
        // went to the preview. This render is not a refinement of what is on
        // screen, it is the first time the full graph has seen these values,
        // and skipping it would leave the photograph at whatever it looked like
        // when the drag began.
        pushAndRender()
    }

    struct Sample {
        /// What is on screen. This is what a swatch must show.
        var display: (r: Double, g: Double, b: Double)
        /// The color before any user adjustment. Hue bands derive from this,
        /// so adjusting a band cannot change which band you pick next.
        var scene: (r: Double, g: Double, b: Double)
    }

    func sample(u: Float, v: Float) -> Sample? {
        guard let handle, isLoaded else { return nil }
        var display = [Float](repeating: 0, count: 3)
        var scene = [Float](repeating: 0, count: 3)
        guard orion_engine_sample(handle, u, v, &display, &scene) == ORION_OK else {
            return nil
        }
        return Sample(
            display: (Double(display[0]), Double(display[1]), Double(display[2])),
            scene: (Double(scene[0]), Double(scene[1]), Double(scene[2])))
    }

    /// Per-channel histogram of the rendered image.
    func histogram(bins: Int = 128) -> [UInt32]? {
        guard let handle, isLoaded else { return nil }
        var out = [UInt32](repeating: 0, count: bins * 3)
        guard orion_engine_histogram(handle, &out, UInt32(bins)) == ORION_OK else {
            return nil
        }
        return out
    }

    func generationBump() { generation &+= 1 }

    /// Sixteen bits out of the tail of the graph instead of eight.
    ///
    /// The screen path is eight bits: the drawable is `bgra8Unorm`, so wider is
    /// bytes moved for precision nothing can show, and it costs about 3.5 ms of
    /// a 16 ms budget. Export widens the tail itself. This is here for the
    /// screenshot harness, which reads the output texture directly and measures
    /// to four decimal places — quantised to 8 bits, the differences this
    /// codebase hunts (0.0001 chroma) round to nothing.
    ///
    /// Reallocates two full-resolution textures. Not for a slider.
    func setWideOutput(_ wide: Bool) {
        guard let handle, isLoaded else { return }
        _ = orion_engine_set_wide_output(handle, wide ? 1 : 0)
        pushAndRender()
    }

    func pushAndRender() {
        guard isLoaded, !suspended, let handle else { return }
        var adj = cAdjustments()
        // Both graphs, always — the engine fans this out. A preview left stale
        // is the graph read the instant the drag ends.
        //
        // ⚠ **The status is read.** It was discarded, and the failure that
        // produces is the worst-looking one in this file: the push is refused,
        // the render below then succeeds *on the previous adjustments*, and the
        // photographer gets a fast, correct-looking frame of the wrong picture.
        // Every number on the bar is green. Swift imports C functions as
        // implicitly discardable, so nothing warned.
        let pushed = orion_engine_set_adjustments(handle, &adj)
        guard pushed == ORION_OK else {
            let why = errorText(pushed)
            lastFailure = why
            FileHandle.standardError.write(
                Data("orion: the edit did not reach the engine — \(why)\n".utf8))
            generation &+= 1
            return
        }

        if interacting {
            renderPreview()
        } else {
            render()
        }
        onEdit?(state)
    }

    /// Renders the quarter-linear graph and publishes a frame.
    ///
    /// Deliberately does *not* re-read `imageWidth`, recompute the histogram or
    /// drop the compare original: those all describe the full render, and the
    /// preview is a stand-in for looking at, not a new state of the document.
    func renderPreview() {
        guard let handle else { return }
        var ms: Double = 0
        guard orion_engine_render_preview(handle, &ms) == ORION_OK else {
            // No preview graph on this machine. Fall back rather than showing
            // nothing — degrade-then-refine is a comfort, not a requirement.
            render()
            return
        }
        lastFailure = nil
        lastRenderMs = ms
        generation &+= 1
    }

    func render() {
        guard let handle else { return }
        var ms: Double = 0
        let status = orion_engine_render(handle, &ms)
        guard status == ORION_OK else {
            // ⚠ Say so. See `lastFailure`: this used to return silently and
            // leave a black canvas reading 0.0 ms.
            let why = errorText(status)
            lastFailure = why
            FileHandle.standardError.write(Data("orion: render failed — \(why)\n".utf8))
            generation &+= 1
            return
        }
        lastFailure = nil

        // Re-read the size every frame. A quarter turn swaps width and height,
        // and the canvas uses these to work out which part of the (square)
        // orientation texture is valid — stale values there tear the image.
        var w: UInt32 = 0, h: UInt32 = 0
        if orion_engine_image_size(handle, &w, &h) == ORION_OK {
            imageWidth = w
            imageHeight = h
        }
        var fw: UInt32 = 0, fh: UInt32 = 0
        if orion_engine_frame_size(handle, &fw, &fh) == ORION_OK {
            frameWidth = fw
            frameHeight = fh
        }

        lastRenderMs = ms
        refreshOriginal()
        generation &+= 1
        scheduleHistogram()
    }

    func showPlaceholder(_ image: NSImage?) {
        placeholder = image
        generation &+= 1
    }

    func clearPlaceholder() {
        guard placeholder != nil else { return }
        placeholder = nil
        generation &+= 1
    }

    /// The histogram reads back the whole output texture — ~96 MB at 24 MP —
    /// so recomputing it per render added tens of milliseconds to every slider
    /// tick. It is a readout nobody watches mid-drag, so it updates once the
    /// values settle instead.
    private func scheduleHistogram() {
        histogramTask?.cancel()
        histogramTask = Task { [weak self] in
            try? await Task.sleep(for: .milliseconds(160))
            guard !Task.isCancelled, let self else { return }
            if let bins = self.histogram() { self.histogramBins = bins }
        }
    }
}
