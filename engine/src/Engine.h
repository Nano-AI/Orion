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
    std::unique_ptr<gpu::Device>           device_;
    std::unique_ptr<pipe::DevelopPipeline> develop_;
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
