#pragma once

#include <cstdint>
#include <vector>

#include "gpu/MetalDevice.h"
#include "pipe/DevelopPipeline.h"
#include "pipe/LensDatabase.h"
#include "raw/RawImage.h"
#include "util/ImageWriter.h"

#include <memory>
#include <string>

namespace orion {

/// The engine proper. C++ with exceptions, RAII, the lot — all of which stops
/// at the C facade in CApi.cpp.
class Engine {
public:
    Engine();
    ~Engine();

    [[nodiscard]] const gpu::DeviceInfo& deviceInfo() const noexcept {
        return device_->info();
    }

    void openRaw(const std::string& path);
    void setAdjustments(const pipe::Adjustments&);
    double render();

    // ── Degrade-then-refine (ROADMAP M1, Interaction) ─────────────────────
    //
    // A second graph over a quarter-linear mosaic, for rendering while a
    // slider is moving. Sixteen times fewer pixels, so the two controls that
    // made the report — clarity at 66 ms a tick and dehaze at 116 — come back
    // inside a frame. The full graph renders when the hand stops.
    //
    // ⚠ **Both graphs get every adjustment**, always. Pushing only to the one
    // being rendered would leave the other stale, and the stale one is the one
    // that gets read the instant the drag ends: the photograph would settle to
    // whatever it looked like several gestures ago. `setAdjustments` feeds both
    // and that is not optional.
    //
    // ⚠ **Nothing but the canvas may read the preview.** Export, the histogram
    // and the eyedropper all go through `develop()`, which is always the full
    // graph — a preview-resolution export is the kind of mistake that is only
    // discovered by the person who receives the file.

    /// Renders the preview graph. Returns GPU milliseconds.
    double renderPreview();

    // ── State that is not an adjustment ───────────────────────────────────
    //
    // ⚠ A brush stroke, a raster matte and a creative LUT are all graph state
    // that `Adjustments` does not carry, and every one of them has to reach
    // **both** graphs for the same reason `setAdjustments` does. Routed through
    // here rather than through `developMutable()`, which is the full graph
    // only: a stroke sent to that one alone renders correctly when the hand is
    // still and vanishes for the length of every drag.
    void setBrushStroke(int component, const float* xy, int count);
    bool setMaskMatte(int component, const float* alpha, int width, int height);
    void setCreativeLut(const pipe::CubeLut&);
    void clearCreativeLut();

    /// True once a preview graph exists for the open photo.
    [[nodiscard]] bool hasPreview() const noexcept { return preview_ != nullptr; }

    /// The preview graph's output, for the canvas to show mid-drag.
    [[nodiscard]] const pipe::DevelopPipeline& previewDevelop() const;

    /// Rendered color at normalized image coordinates, 0..1 per channel.
    /// Samples at normalized *oriented* coordinates.
    ///
    /// Returns two things because they answer different questions.
    /// `outDisplay` is what is on screen, which is what a swatch must show.
    /// `outScene` is the image before any user adjustment, which is what the
    /// hue band must be derived from — reading the edited result would mean
    /// adjusting a band changes which band you would pick next time.
    /// Either pointer may be null.
    void sampleAt(float u, float v, float* outDisplay, float* outScene) const;

    /// Per-channel histogram of the rendered image, `bins` entries each,
    /// packed R then G then B. Sampled on a stride: this drives a 90-pixel-tall
    /// readout, not a measurement, and 24 million samples would be waste.
    void histogram(std::uint32_t* out, std::uint32_t bins) const;

    /// Measures the picture and writes the sliders auto-enhance is allowed to
    /// move, in place. Renders several times — it is a one-click action, not
    /// something on the interaction path. research/auto-enhance.md.
    void autoEnhance(pipe::Adjustments& adj);
    void exportImage(const std::string& path, const util::ExportOptions&);

    /// Encodes with these options and reports the byte count without writing.
    /// The export panel shows a measured size rather than an estimate, which
    /// is the whole reason the number is worth showing.
    [[nodiscard]] std::size_t exportedSize(const util::ExportOptions&);

private:
    /// The developed output as 16-bit unsigned, which is what the image
    /// formats want. The graph ends in half float.
    /// The output as normalized float, whichever format the tail is in.
    /// Screen readers use this; only export widens the graph and reads 16.
    [[nodiscard]] std::vector<float> readOutputFloat(std::uint32_t w,
                                                     std::uint32_t h) const;

    [[nodiscard]] std::vector<std::uint16_t> readOutput16(std::uint32_t w,
                                                          std::uint32_t h) const;
public:

    [[nodiscard]] bool hasImage() const noexcept { return develop_ != nullptr; }
    [[nodiscard]] const pipe::DevelopPipeline& develop() const;

    /// Mutable access, for the few C-API entry points that change graph state
    /// rather than adjustments — the output width being one of them.
    [[nodiscard]] pipe::DevelopPipeline& developMutable();
    [[nodiscard]] const std::string& camera() const noexcept { return camera_; }

    /// The lens profile for the open photo, looked up when it was opened.
    /// `found` is false when the lens is unknown, which includes every manual
    /// lens — those report no name in EXIF at all.
    [[nodiscard]] const pipe::LensProfile& lensProfile() const noexcept {
        return lensProfile_;
    }
    [[nodiscard]] gpu::Device& device() noexcept { return *device_; }

    void setError(std::string message) { lastError_ = std::move(message); }
    [[nodiscard]] const char* lastError() const noexcept { return lastError_.c_str(); }

private:
    /// How much smaller the preview mosaic is, per axis.
    ///
    /// Four, because the target is a filter that costs 116 ms coming back
    /// inside a 16 ms frame and four is the first power of two that manages it
    /// — sixteen times fewer pixels. Two would only be four times, which leaves
    /// dehaze at 29 ms. Its cost is one more set of intermediates at 1/16 the
    /// size: about 400 MiB against the full graph's 6.4 GiB.
    static constexpr int kPreviewScale = 4;

    std::unique_ptr<gpu::Device>           device_;
    std::unique_ptr<pipe::DevelopPipeline> develop_;
    std::unique_ptr<pipe::DevelopPipeline> preview_;
    std::string                            camera_;
    /// The RAW this was opened from. Export lifts its EXIF, so the file
    /// written carries the exposure, lens and date the picture was taken with.
    std::string                            sourcePath_;
    std::string                            lastError_;

    /// One copy for the process. Five megabytes of XML, parsed once — a
    /// per-photo parse would put a tenth of a second in front of every open.
    static const pipe::LensDatabase& lensDatabase();
    pipe::LensProfile                      lensProfile_;
};

}  // namespace orion
