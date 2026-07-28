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
    void sampleAt(float u, float v, float outRgb[3]) const;
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
