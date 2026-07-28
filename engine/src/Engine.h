#pragma once

#include "gpu/MetalDevice.h"
#include "pipe/DevelopPipeline.h"
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

    /// Rendered colour at normalised image coordinates, 0..1 per channel.
    /// Samples at normalised *oriented* coordinates.
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
    void exportImage(const std::string& path, const util::ExportOptions&);

    [[nodiscard]] bool hasImage() const noexcept { return develop_ != nullptr; }
    [[nodiscard]] const pipe::DevelopPipeline& develop() const;
    [[nodiscard]] const std::string& camera() const noexcept { return camera_; }
    [[nodiscard]] gpu::Device& device() noexcept { return *device_; }

    void setError(std::string message) { lastError_ = std::move(message); }
    [[nodiscard]] const char* lastError() const noexcept { return lastError_.c_str(); }

private:
    std::unique_ptr<gpu::Device>           device_;
    std::unique_ptr<pipe::DevelopPipeline> develop_;
    std::string                            camera_;
    std::string                            lastError_;
};

}  // namespace orion
