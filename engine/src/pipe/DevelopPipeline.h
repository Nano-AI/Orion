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
#include "raw/NoiseProfile.h"
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

    /// Fine rotation in degrees, applied after the quarter turns. Positive
    /// rotates the image clockwise, which is what a "straighten" control means.
    float straightenDeg = 0.0f;

    /// Crop rectangle in normalised post-rotation coordinates. The full frame
    /// is origin (0,0) size (1,1).
    float cropX = 0.0f, cropY = 0.0f;
    float cropW = 1.0f, cropH = 1.0f;

    /// While the crop tool is open, render the whole straightened frame and let
    /// the UI draw the crop rectangle over it. That is how Photoshop and
    /// Lightroom behave: you see what you are cutting away, and straightening
    /// rotates the picture under a stationary rectangle rather than zooming in.
    bool cropPreview = false;

    /// The preview canvas, in the same coordinates as the crop rectangle. It
    /// has to cover the frame's rotated bounding box, which depends on both
    /// the angle and the frame's aspect — a fixed factor could not, and at
    /// anything past about 17 degrees on a 3:2 frame it clipped the corners.
    ///
    /// Supplied by the UI rather than derived here: the crop overlay has to
    /// land on the same rectangle, and two derivations is exactly how the
    /// handles and the pixels drifted apart before.
    float previewX = 0.0f, previewY = 0.0f;
    float previewSize = 1.0f;

    /// Lens corrections. Manual for now: the lensfun database would fill these
    /// in from the lens the EXIF names, and the maths is the same either way.
    float lensDistortion = 0.0f;   // -1..1, poly3 k1
    float lensVignette   = 0.0f;   // -1..1, p_a
    float lensCaRed      = 0.0f;   // -1..1
    float lensCaBlue     = 0.0f;   // -1..1

    /// Highlight reconstruction, 0..1. A correction rather than an effect, so
    /// it defaults to on: a clipped red channel under a white cloud is a defect
    /// of the sensor, not a look.
    float highlightRecovery = 1.0f;

    /// Profiled wavelet denoise. Strengths are multiples of the measured
    /// noise level, so 1.0 means "shrink coefficients smaller than one sigma"
    /// rather than an arbitrary amount — which is what makes the same setting
    /// behave the same way on a clean frame and a very noisy one.
    float denoiseLuma   = 0.0f;   // 0..4, 0 disables the whole chain
    float denoiseColour = 0.0f;   // 0..4, applied on top of luma

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

    /// True when this pipeline can be reused for `image` — same dimensions and
    /// same CFA layout. Rebuilding costs sixteen shader compiles and about
    /// 2.5 GiB of texture allocation, so reusing is the difference between
    /// switching photos in milliseconds and in seconds.
    [[nodiscard]] bool canReload(const raw::BayerImage& image) const noexcept;

    /// Swaps in a new image without touching the compiled graph.
    void reload(const raw::BayerImage& image);

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

    /// The whole frame after rotation, before any crop — what the crop
    /// rectangle is normalised against.
    [[nodiscard]] std::uint32_t frameWidth()  const noexcept;
    [[nodiscard]] std::uint32_t frameHeight() const noexcept;

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
    int nLinearize_ = -1, nDirs_ = -1, nLpf_ = -1, nGreen_ = -1, nRgb_ = -1;
    int nSharpen_ = -1, nMatrix_ = -1, nLinear_ = -1, nDisplay_ = -1, nOrient_ = -1;
    int nGeometry_ = -1;
    int nHighlights_ = -1;
    int nLens_ = -1;
    float whiteLevel_ = 0.0f;
    float blackLevel_[3]{};
    static constexpr int kDenoiseScales = 4;
    int nAtrousBlur_[kDenoiseScales]{-1, -1, -1, -1};
    int nAtrousShrink_[kDenoiseScales]{-1, -1, -1, -1};
    raw::NoiseProfile noise_{};
    int nGuidePrep_ = -1, nGuideDown_ = -1, nGuideH1_ = -1, nGuideV1_ = -1;

    /// He & Sun's subsampling ratio for the guided filter. Four is what they
    /// report as visually indistinguishable, and it takes the filter from
    /// ninety milliseconds to something you can drag.
    static constexpr int kGuideScale = 4;
    std::uint32_t guideW_ = 0, guideH_ = 0;
    int nGuideAb_ = -1, nGuideH2_ = -1, nGuideV2_ = -1;
    int exifQuarters_ = 0;
    int turns_ = 0;
    std::uint32_t outW_ = 0, outH_ = 0;
    std::uint32_t frameW_ = 0, frameH_ = 0;
    int auxCurveLut_ = -1;
    Adjustments  lastAdj_{};
    bool         primed_ = false;
    WhiteBalance         asShot_{};
    std::array<float, 3> asShotMul_{1.0f, 1.0f, 1.0f};
    std::array<float, 3> asShotRef_{1.0f, 1.0f, 1.0f};
    float        xyzToCam_[9]{};
    params::Linearize linBase_{};
    std::uint32_t filters_ = 0;
    void applyImageParams(const raw::BayerImage&);
};

}  // namespace orion::pipe
