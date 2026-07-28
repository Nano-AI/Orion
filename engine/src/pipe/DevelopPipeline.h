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

    /// The camera's own white balance, used as the starting point.
    [[nodiscard]] WhiteBalance asShotWhiteBalance() const noexcept { return asShot_; }

    [[nodiscard]] const gpu::Texture& output() const { return pipeline_.output(); }
    [[nodiscard]] Pipeline&           graph()        { return pipeline_; }
    [[nodiscard]] const Pipeline&     graph() const  { return pipeline_; }

    [[nodiscard]] std::uint32_t width()  const noexcept { return width_; }
    [[nodiscard]] std::uint32_t height() const noexcept { return height_; }

private:
    Pipeline      pipeline_;
    std::uint32_t width_ = 0, height_ = 0;
    int nLinearize_ = -1, nDirs_ = -1, nGreen_ = -1, nRgb_ = -1;
    int nMatrix_ = -1, nLinear_ = -1, nDisplay_ = -1;
    int auxCurveLut_ = -1;
    Adjustments  lastAdj_{};
    bool         primed_ = false;
    WhiteBalance asShot_{};
    float        xyzToCam_[9]{};
    params::Linearize linBase_{};
};

}  // namespace orion::pipe
