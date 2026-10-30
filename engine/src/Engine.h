#pragma once

#include <cstdint>
#include <vector>

#include "gpu/MetalDevice.h"
#include "merge/HdrMerge.h"
#include "pipe/DevelopPipeline.h"
#include "pipe/LensDatabase.h"
#include "raw/RawImage.h"
#include "util/ImageWriter.h"

#include <memory>
#include <string>

namespace orion {

namespace pipe { class AdobeLensProfiles; }

/// The engine proper. C++ with exceptions, RAII, the lot — all of which stops
/// at the C facade in CApi.cpp.
class Engine {
public:
    Engine();
    ~Engine();

    [[nodiscard]] const gpu::DeviceInfo& deviceInfo() const noexcept {
        return device_->info();
    }

    /// How much smaller the preview mosaic is, per axis.
    ///
    /// Four, because the target is a filter that costs 116 ms coming back
    /// inside a 16 ms frame and four is the first power of two that manages it
    /// — sixteen times fewer pixels. Two would only be four times, which leaves
    /// dehaze at 29 ms. Its cost is one more set of intermediates at 1/16 the
    /// size: about 400 MiB against the full graph's 6.4 GiB.
    ///
    /// ⚠ Public because `orion-bench` builds the preview graph itself to profile
    /// it, and a second copy of this number in the bench would be a bench that
    /// silently stopped measuring the graph the app runs.
    static constexpr int kPreviewScale = 4;

    /// A process that develops and exits rather than one a person drags
    /// sliders in: `Orion --agent`, `--batch-export`. Decision #312.
    ///
    /// Two things change, both only worth doing when nothing is interactive.
    /// No preview graph is built, so the quarter-scale mosaic and its
    /// intermediates (~400 MiB on a 24 MP frame, more at 42) are never
    /// allocated and `setAdjustments` and the matte calls stop feeding a
    /// graph nobody renders. And the full graph releases every intermediate
    /// at its last reader on every render (`Pipeline::setReleaseEveryRender`)
    /// rather than once ever, which is what made a restore, a matte and an
    /// export peak at 13.7 GB.
    ///
    /// ⚠ **Set before `openRaw`.** The preview is decided at open; a call
    /// after it changes the release policy of the open graph but cannot
    /// unbuild a preview that already exists. Never on the app's engine -
    /// the canvas needs both things this turns off.
    void setHeadless(bool on) noexcept;
    [[nodiscard]] bool headless() const noexcept { return headless_; }

    void openRaw(const std::string& path);
    /// The graphs half of `openRaw`, for an image already in memory: the tests'
    /// synthetic mosaics. No file, so no metadata and no lens profile.
    void openDecoded(raw::DecodedImage image);
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

    // ── Region of interest (issue #22, decision #364) ─────────────────────
    //
    // A third graph, over the part of the mosaic the screen shows, decimated
    // to the zoom: a drag frame and a settled render at 100% cost what the
    // screen holds, not what the sensor holds, and are never softer than the
    // screen. Like the preview it is for the canvas only.

    /// Part of the displayed picture, normalized (0..1 of the cropped, turned
    /// picture), and the screen pixels it will cover.
    struct Region { float x = 0, y = 0, w = 1, h = 1; std::uint32_t outW = 0, outH = 0; };

    /// Renders `r`. Returns GPU milliseconds, or a negative number when the
    /// region graph cannot render this edit exactly
    /// (`DevelopPipeline::regionSupports`), the source is not a mosaic, or the
    /// engine is headless: the caller renders the whole graph instead.
    double renderRegion(const Region& r);

    /// The region graph, once `renderRegion` has built one.
    [[nodiscard]] bool hasRegion() const noexcept { return region_ != nullptr; }
    [[nodiscard]] const pipe::DevelopPipeline& regionDevelop() const;
    /// Frame pixels per region pixel, per axis, that the last region chose.
    [[nodiscard]] int regionStep() const noexcept { return regionStep_; }

    /// Both graphs give their render textures back to Metal and keep the
    /// decoded photo: what a cache does to a photo it holds but is not
    /// showing (decision #318). The next render of either rebuilds it.
    void releaseRenderMemory() noexcept;

    // ── State that is not an adjustment ───────────────────────────────────
    //
    // ⚠ A brush stroke, a raster matte and a creative LUT are all graph state
    // that `Adjustments` does not carry, and every one of them has to reach
    // **both** graphs for the same reason `setAdjustments` does. Routed through
    // here rather than through `developMutable()`, which is the full graph
    // only: a stroke sent to that one alone renders correctly when the hand is
    // still and vanishes for the length of every drag.
    void setBrushStroke(int component, const float* xy, const float* erase,
                        int count);
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
    /// formats want. Wide export reads half float; narrow export reads bytes.
    [[nodiscard]] std::vector<std::uint16_t> readOutput16(std::uint32_t w,
                                                          std::uint32_t h) const;
public:

    [[nodiscard]] bool hasImage() const noexcept { return develop_ != nullptr; }
    [[nodiscard]] const pipe::DevelopPipeline& develop() const;

    /// Mutable access, for the few C-API entry points that change graph state
    /// rather than adjustments — the output width being one of them.
    [[nodiscard]] pipe::DevelopPipeline& developMutable();
    [[nodiscard]] const std::string& camera() const noexcept { return camera_; }

    /// The lens name **as the file records it**, before any database lookup —
    /// empty when the file records none.
    ///
    /// ⚠ Separate from `lensProfile().lens`, which is the name of the *matched*
    /// database entry and is empty whenever nothing matched. The interface needs
    /// both to say anything useful: "no profile for the 24-70 your file names"
    /// and "your file names no lens at all, which every manual lens does" are
    /// different problems with different answers, and the panel said neither.
    [[nodiscard]] const std::string& photoLens() const noexcept { return info_.lens; }

    /// Everything the open file's metadata records - camera, lens, exposure,
    /// focal length, capture time - read once on open. All zero and empty
    /// before the first open.
    [[nodiscard]] const raw::RawInfo& info() const noexcept { return info_; }

    /// Choose a lens profile **by hand**, from a name `LensDatabase::names()`
    /// offered. Empty restores the EXIF match, which may itself be nothing.
    ///
    /// ⚠ **Not part of `Adjustments`, deliberately.** A lens choice is not a
    /// per-frame render parameter — it *selects* a profile, and that profile's
    /// coefficients already reach the shader through `Adjustments::lensModel`. Putting a string in the POD block would push it
    /// across the facade on every slider tick to say a thing that changes once
    /// a photograph. It belongs beside the profile it picks.
    ///
    /// Returns false when the name is not in the database, leaving the previous
    /// profile alone — a picker can seat a stale choice from a sidecar written
    /// against a database that has since changed, and dropping the correction
    /// silently would be worse than keeping it.
    bool setLensChoice(const std::string& makerModel);

    /// The hand-chosen lens, or empty when the profile came from EXIF.
    [[nodiscard]] const std::string& lensChoice() const noexcept { return lensChoice_; }

    /// Every lens the database carries, for a picker. Static because the
    /// database is, and a caller listing lenses has no photograph open yet.
    [[nodiscard]] static std::vector<std::string> lensDatabaseNames();

    /// The lens profile for the open photo, looked up when it was opened.
    /// `found` is false when the lens is unknown, which includes every manual
    /// lens — those report no name in EXIF at all.
    [[nodiscard]] const pipe::LensProfile& lensProfile() const noexcept {
        return lensProfile_;
    }
    [[nodiscard]] gpu::Device& device() noexcept { return *device_; }

    // ── HDR merge ────────────────────────────────────────────────────────
    //
    // Independent of the open photograph: the merge runs its own minimal
    // graph and never touches develop_/preview_. Blocking — the facade's
    // caller runs it on a worker thread and polls progress from another,
    // which is why the two small calls are noexcept reads of atomics.
    float hdrMerge(const std::vector<std::string>& paths, int referenceIndex,
                   const std::string& outputPath);
    [[nodiscard]] float hdrMergeProgress() const noexcept;
    void hdrMergeCancel() noexcept;

    void setError(std::string message) { lastError_ = std::move(message); }
    [[nodiscard]] const char* lastError() const noexcept { return lastError_.c_str(); }

private:
    std::unique_ptr<gpu::Device>           device_;
    // Constructed eagerly so progress/cancel from another thread never race
    // a lazy creation inside run().
    std::unique_ptr<merge::HdrMerge>       hdrMerge_;
    std::unique_ptr<pipe::DevelopPipeline> develop_;
    std::unique_ptr<pipe::DevelopPipeline> preview_;
    /// The region graph and what it was last loaded with (#22).
    std::unique_ptr<pipe::DevelopPipeline> region_;
    // ponytail: the whole mosaic stays on the CPU for the region graph to cut
    // windows from (2 bytes a pixel, 48 MB at 24 MP); read windows back from
    // the full graph's source texture if that ever matters.
    std::shared_ptr<const raw::BayerImage> mosaic_;
    int                                    regionStep_ = 0;
    std::uint32_t                          regionX_ = 0, regionY_ = 0;
    bool                                   regionLoaded_ = false;
    /// Whether each graph has rendered the current edit. Decides which one
    /// the histogram and the eyedropper read (`measured`).
    bool                                   fullCurrent_ = false, previewCurrent_ = false, regionCurrent_ = false;
    /// The full graph's outputs were given back once a region was shown (#22).
    bool                                   fullReleased_ = false;
    [[nodiscard]] const pipe::DevelopPipeline& measured() const;
    Region                                 regionView_{};
    /// What `setAdjustments` last pushed, for the region graph's own apply.
    pipe::Adjustments                      adj_{};
    /// The creative LUT, for a region graph built after it was loaded.
    std::shared_ptr<const pipe::CubeLut>   lut_;
    std::string                            camera_;
    std::string                            lensChoice_;
    /// ⚠ Kept whole so a hand-chosen lens can be re-interpolated at this
    /// shot's own focal length and aperture, and so the interface can show the
    /// capture settings without reading the file a third time.
    raw::RawInfo                           info_;
    /// The RAW this was opened from. Export lifts its EXIF, so the file
    /// written carries the exposure, lens and date the picture was taken with.
    std::string                            sourcePath_;
    std::string                            lastError_;
    /// `setHeadless`. Read at every `openRaw`, so it outlives a reload.
    bool                                   headless_ = false;

    /// One copy for the process. Five megabytes of XML, parsed once — a
    /// per-photo parse would put a tenth of a second in front of every open.
    static const pipe::LensDatabase& lensDatabase();
    /// The Adobe `.lcp` profiles already installed on this machine, if any.
    static const pipe::AdobeLensProfiles& adobeProfiles();
    /// The profile the open file gets without a hand choice: DNG opcodes,
    /// then Adobe, then lensfun.
    [[nodiscard]] pipe::LensProfile automaticLensProfile() const;
    pipe::LensProfile                      lensProfile_;
};

}  // namespace orion
