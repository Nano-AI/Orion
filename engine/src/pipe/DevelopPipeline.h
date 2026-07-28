/*  The standard develop graph.
 *
 *  Builds and owns the seven-node pipeline that turns a decoded mosaic into
 *  display-ready pixels, and exposes the adjustments on top of it. Both the
 *  app and the bench tool drive this, so what you measure is what you see.
 */

#pragma once

#include "gpu/Resources.h"
#include "pipe/ShaderParams.h"
#include "pipe/Pipeline.h"
#include "pipe/ToneCurve.h"
#include "pipe/WhiteBalance.h"
#include "raw/RawImage.h"

#include <array>
#include <memory>
#include <string>

namespace orion::pipe {

struct Adjustments {
    // White balance. Defaults are replaced with the camera's own estimate when
    // a file is opened, so "as shot" is where every image starts.
    WhiteBalance wb{};

    // Scene-linear tone, in order of the pipeline.
    float exposureEv = 0.0f;
    float highlights = 0.0f;   // -1..1, negative recovers
    float shadows    = 0.0f;
    float whites     = 0.0f;
    float blacks     = 0.0f;

    float vibrance   = 0.0f;   // -1..1
    float saturation = 0.0f;   // -1..1, 0 is untouched

    // Colour mixer, eight bands: red, orange, yellow, green, aqua, blue,
    // purple, magenta. Each -1..1.
    std::array<float, 8> hueShift{};
    std::array<float, 8> satShift{};
    std::array<float, 8> lumShift{};

    /// Extra quarter turns clockwise on top of the camera's own orientation.
    int rotateQuarters = 0;

    // Capture sharpening. Sits just after the demosaic.
    float sharpenAmount  = 0.0f;   // 0..2
    float sharpenRadius  = 1.0f;   // pixels
    float sharpenMasking = 0.0f;   // 0..1, higher protects flat areas

    // Display transform and look.
    float contrast   = 1.0f;
    ToneCurveSpec curve{};
};

class DevelopPipeline {
public:
    DevelopPipeline(gpu::Device& device, const std::string& shaderDir,
                    const raw::BayerImage& image);

    /// Pushes the current adjustments into the graph, dirtying only what they
    /// affect. Cheap — call it freely on every slider tick.
    void apply(const Adjustments&);

    /// Renders every dirty node. Returns GPU-side milliseconds.
    double render();

    /// Output dimensions after orientation, which is what the UI should show.
    /// Total clockwise quarter turns currently applied.
    [[nodiscard]] int quarterTurns() const noexcept { return turns_; }

    [[nodiscard]] std::uint32_t outputWidth()  const noexcept;
    [[nodiscard]] std::uint32_t outputHeight() const noexcept;

    /// The camera's own white balance, used as the starting point.
    [[nodiscard]] WhiteBalance asShotWhiteBalance() const noexcept { return asShot_; }

    [[nodiscard]] const gpu::Texture& output() const { return pipeline_.output(); }

    /// The image after white balance and the camera matrix, but before any user
    /// adjustment. This is what the colour picker must sample: reading the
    /// edited result would mean adjusting a band changes which band you would
    /// pick next time, which is a feedback loop, not a tool.
    [[nodiscard]] const gpu::Texture& referenceImage() const {
        return pipeline_.nodeOutput(nMatrix_);
    }
    [[nodiscard]] Pipeline&           graph()        { return pipeline_; }
    [[nodiscard]] const Pipeline&     graph() const  { return pipeline_; }

    [[nodiscard]] std::uint32_t width()  const noexcept { return width_; }
    [[nodiscard]] std::uint32_t height() const noexcept { return height_; }

private:
    Pipeline      pipeline_;
    std::uint32_t width_ = 0, height_ = 0;
    int nLinearize_ = -1, nDirs_ = -1, nGreen_ = -1, nRgb_ = -1;
    int nSharpen_ = -1, nMatrix_ = -1, nLinear_ = -1, nDisplay_ = -1, nOrient_ = -1;
    int nGuidePrep_ = -1, nGuideH1_ = -1, nGuideV1_ = -1;
    int nGuideAb_ = -1, nGuideH2_ = -1, nGuideV2_ = -1;
    int exifQuarters_ = 0;
    int turns_ = 0;
    int auxCurveLut_ = -1;
    Adjustments  lastAdj_{};
    bool         primed_ = false;
    WhiteBalance         asShot_{};
    std::array<float, 3> asShotMul_{1.0f, 1.0f, 1.0f};
    std::array<float, 3> asShotRef_{1.0f, 1.0f, 1.0f};
    float        xyzToCam_[9]{};
    params::Linearize linBase_{};
};

}  // namespace orion::pipe
