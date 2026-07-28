/*  The standard develop graph.
 *
 *  Builds and owns the seven-node pipeline that turns a decoded mosaic into
 *  display-ready pixels, and exposes the adjustments on top of it. Both the
 *  app and the bench tool drive this, so what you measure is what you see.
 */

#pragma once

#include "gpu/Resources.h"
#include "pipe/Pipeline.h"
#include "raw/RawImage.h"

#include <memory>
#include <string>

namespace orion::pipe {

struct Adjustments {
    float exposureEv = 0.0f;
    float black      = 0.0f;
    float contrast   = 1.0f;
    float saturation = 1.0f;
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

    [[nodiscard]] const gpu::Texture& output() const { return pipeline_.output(); }
    [[nodiscard]] Pipeline&           graph()        { return pipeline_; }
    [[nodiscard]] const Pipeline&     graph() const  { return pipeline_; }

    [[nodiscard]] std::uint32_t width()  const noexcept { return width_; }
    [[nodiscard]] std::uint32_t height() const noexcept { return height_; }

private:
    Pipeline      pipeline_;
    std::uint32_t width_ = 0, height_ = 0;
    int nLinearize_ = -1, nDirs_ = -1, nGreen_ = -1, nRgb_ = -1;
    int nMatrix_ = -1, nExposure_ = -1, nAgx_ = -1;
};

}  // namespace orion::pipe
