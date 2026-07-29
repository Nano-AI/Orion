/*  The standard develop graph.
 *
 *  Builds and owns the seven-node pipeline that turns a decoded mosaic into
 *  display-ready pixels, and exposes the adjustments on top of it. Both the
 *  app and the bench tool drive this, so what you measure is what you see.
 */

#pragma once

#include "gpu/Resources.h"
#include "pipe/HueSatMap.h"
#include "pipe/CubeLut.h"
#include "pipe/Dehaze.h"
#include "pipe/ExposureFusion.h"
#include "pipe/LocalLaplacian.h"
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

    // Color mixer, eight bands: red, orange, yellow, green, aqua, blue,
    // purple, magenta. Each -1..1.
    std::array<float, 8> hueShift{};
    std::array<float, 8> satShift{};
    std::array<float, 8> lumShift{};

    /// Extra quarter turns clockwise on top of the camera's own orientation.
    int rotateQuarters = 0;

    /// Fine rotation in degrees, applied after the quarter turns. Positive
    /// rotates the image clockwise, which is what a "straighten" control means.
    float straightenDeg = 0.0f;

    /// Crop rectangle in normalized post-rotation coordinates. The full frame
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

    /// A measured profile from the lens database, in place of the sliders.
    /// `lensPoly` is ptlens a, b, c; `lensVignettePa` is p_a, p_b, p_c. Both
    /// are physical coefficients at this frame's focal length and aperture,
    /// not normalized control positions. See pipe/LensDatabase.h.
    bool  lensProfile = false;
    float lensPoly[3]{};
    float lensVignettePa[3]{};

    /// Highlight reconstruction, 0..1. Off by default — see the note on
    /// Engine.highlightRecovery in the app.
    float highlightRecovery = 0.0f;

    /// Three-way colour grading, as ASC CDL per tonal zone. Each entry is a
    /// wheel's puck position (x, y) in the unit disc plus that zone's slope.
    /// research/color-grading.md.
    float gradeShadow[3]{};      // x, y, luminance
    float gradeMidtone[3]{};
    float gradeHighlight[3]{};

    /// Profiled wavelet denoise. Strengths are multiples of the measured
    /// noise level, so 1.0 means "shrink coefficients smaller than one sigma"
    /// rather than an arbitrary amount — which is what makes the same setting
    /// behave the same way on a clean frame and a very noisy one.
    float denoiseLuma   = 0.0f;   // 0..4, 0 disables the whole chain
    float denoiseColor = 0.0f;   // 0..4, applied on top of luma

    /// How much of the creative LUT to apply, 0..1. The LUT itself is not an
    /// adjustment — it is a file, set through `setCreativeLut`.
    float lutStrength = 1.0f;

    /// Single-image exposure fusion, 0..1 — shadow lift that keeps local
    /// contrast. The value is a power applied to the emitted gain, so zero is
    /// bit-exactly the identity. research/exposure-fusion.md.
    float fusion = 0.0f;

    /// Dehaze, 0..1. Maps onto the paper's own omega, so zero is exactly the
    /// identity and one is the value He, Sun & Tang fixed for every result in
    /// their paper. research/dehaze.md.
    float dehaze = 0.0f;

    /// Local Laplacian clarity, -1..1. Negative smooths detail, positive
    /// increases its contrast; the slider is the published alpha exponent, and
    /// its endpoints land on the paper's own illustrated values. Thirty-two
    /// nodes hang off this one float, so zero switches the whole chain off
    /// rather than running it at no strength. research/local-laplacian.md.
    float clarity = 0.0f;

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
    /// rectangle is normalized against.
    [[nodiscard]] std::uint32_t frameWidth()  const noexcept;
    [[nodiscard]] std::uint32_t frameHeight() const noexcept;

    /// The camera's own white balance, used as the starting point.
    [[nodiscard]] WhiteBalance asShotWhiteBalance() const noexcept { return asShot_; }

    [[nodiscard]] const gpu::Texture& output() const { return pipeline_.output(); }

    /// Loads a creative LUT, uploading its grid into the display node's second
    /// auxiliary texture. Replaces whatever was there.
    ///
    /// Not an adjustment: adjustments are plain data that a sidecar round-trips
    /// and the bench sweeps, and a lookup table is neither. What *is* an
    /// adjustment is how much of it to apply.
    void setCreativeLut(const CubeLut&);
    void clearCreativeLut();

    [[nodiscard]] bool hasCreativeLut() const noexcept { return lutSize_ >= 2; }
    [[nodiscard]] const std::string& creativeLutTitle() const noexcept { return lutTitle_; }

    /// Sixteen bits out of the display and geometry nodes, or eight.
    ///
    /// The screen's drawable is `bgra8Unorm`, so the wide path moves twice the
    /// bytes through the two largest nodes in the graph for precision the
    /// display cannot show. Export is the only consumer that can use it.
    /// Switching reallocates two textures and re-renders them, so it belongs
    /// around an export and nowhere near a slider.
    void setWideOutput(bool wide);
    [[nodiscard]] bool wideOutput() const noexcept { return wideOutput_; }

    /// The image after white balance and the camera matrix, but before any user
    /// adjustment. This is what the color picker must sample: reading the
    /// edited result would mean adjusting a band changes which band you would
    /// pick next time, which is a feedback loop, not a tool.
    [[nodiscard]] const gpu::Texture& referenceImage() const {
        return pipeline_.nodeOutput(nHueSat_);
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
    int nGrade_ = -1;
    int nHueSat_ = -1, auxHueSat_ = -1;
    float whiteLevel_ = 0.0f;
    float blackLevel_[3]{};
    static constexpr int kDenoiseScales = 4;
    int nAtrousBlur_[kDenoiseScales]{-1, -1, -1, -1};
    int nAtrousShrink_[kDenoiseScales]{-1, -1, -1, -1};
    raw::NoiseProfile noise_{};
    int nGuidePrep_ = -1, nGuideDown_ = -1, nGuideH1_ = -1, nGuideV1_ = -1;

    // ── Local Laplacian clarity ───────────────────────────────────────────
    //
    // The gamma stacks are packed four to an RGBA texture, so the number of
    // textures follows from the number of gamma levels. The collapse kernels
    // take exactly two of them by name, which is the one place that packing
    // is not generic — hence the assert rather than a silent miscompile.
    static constexpr int kLlfLevels = llf::kPyramidLevels;
    static constexpr int kLlfStacks = (llf::kGammaLevels + 3) / 4;
    static_assert(kLlfStacks == 4,
                  "llf_collapse.slang binds four packed stacks by name; "
                  "changing kGammaLevels needs that kernel widened to match.");

    // ── Dehaze (dark channel prior) ───────────────────────────────────────
    int nDehazeChan_ = -1, nDarkH_ = -1, nDarkV_ = -1, nPeak_ = -1;
    int nDehazeChanA_ = -1, nMinH_ = -1, nMinV_ = -1, nMaxH_ = -1, nMaxV_ = -1;
    int nHazePrep_ = -1, nHazeBlurH_ = -1, nHazeBlurV_ = -1, nHazeAb_ = -1;
    int nHazeBlurH2_ = -1, nHazeBlurV2_ = -1, nDehaze_ = -1;
    std::uint32_t peakW_ = 0, peakH_ = 0, hazeW_ = 0, hazeH_ = 0;

    /// A from Eq. (11). A reduction over the whole frame, so it is not a node;
    /// it depends only on what is upstream of dehaze, so it is cached and
    /// recomputed when white balance or the profile moves — never per tick.
    std::array<float, 3> airlight_{1.0f, 1.0f, 1.0f};
    bool airlightValid_ = false;
    bool dehazing_ = false;

    /// Reads the pooled candidates back and picks A. Costs one small download.
    void estimateAirlight();
    void pushAirlight();

    // ── Exposure fusion ───────────────────────────────────────────────────
    //
    // Runs on a quarter-resolution proxy: the method is a large-scale tonal
    // move and only a gain reaches the full-resolution picture, so nothing this
    // filter could have affected is lost to the subsampling.
    static constexpr int kFuseLevels = 6;
    static constexpr int kFuseScale  = 4;
    static constexpr int kFuseStacks = (sef::kMaxImages + 3) / 4;
    static_assert(kFuseStacks == 2,
                  "fuse_blend.slang binds two packed stacks by name.");

    int nFuseProxy_ = -1;
    int nFuseImage_[kFuseLevels][kFuseStacks]{};
    int nFuseWeight_[kFuseLevels][kFuseStacks]{};
    int nFuseOut_[kFuseLevels]{};
    int nFusion_ = -1;
    std::uint32_t fuseW_[kFuseLevels]{}, fuseH_[kFuseLevels]{};

    /// The simulated-image plan. Derived from the frame's median, so it is a
    /// whole-frame reduction and not a node — same treatment as dehaze's
    /// atmospheric light: computed when stale, never on a slider tick.
    sef::Plan fusePlan_{};
    bool fusePlanValid_ = false;
    bool fusing_ = false;
    void estimateFusionPlan();
    void pushFusionPlan();

    int nLlfLuma_ = -1;
    /// The separable halving's intermediate: half width, full height.
    int nLlfRemapH_[kLlfStacks]{};
    int nLlfGauss_[kLlfLevels]{};                 // [0] aliases nLlfLuma_
    int nLlfPack_[kLlfLevels][kLlfStacks]{};      // levels 1..kLlfLevels-1
    int nLlfOut_[kLlfLevels]{};                   // [kLlfLevels-1] is the residual
    int nClarity_ = -1;
    std::uint32_t llfW_[kLlfLevels]{}, llfH_[kLlfLevels]{};

    /// He & Sun's subsampling ratio for the guided filter. Four is what they
    /// report as visually indistinguishable, and it takes the filter from
    /// ninety milliseconds to something you can drag.
    static constexpr int kGuideScale = 4;
    std::uint32_t guideW_ = 0, guideH_ = 0;
    int nGuideAb_ = -1, nGuideH2_ = -1, nGuideV2_ = -1;
    int exifQuarters_ = 0;
    int turns_ = 0;

    /// Sixteen-bit tail. Matches the node declarations at construction.
    /// Narrow by default: the screen is the common case, and anything that
    /// wants sixteen bits has to ask.
    bool wideOutput_ = false;

    void pushDisplayParams(const Adjustments&);
    int  auxCube_ = -1;
    int  lutSize_ = 0;                       // 0 when none is loaded
    std::array<float, 3> lutMin_{0.0f, 0.0f, 0.0f};
    std::array<float, 3> lutMax_{1.0f, 1.0f, 1.0f};
    std::string lutTitle_;
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

    /// The brightest neutral this frame can describe, once white balance has
    /// been applied — the lowest of the three per-channel saturation levels.
    /// Linearize clips to it so a blown highlight comes out white instead of
    /// carrying the white-balance gains as a color cast.
    [[nodiscard]] float whiteClipFor(const float multipliers[3]) const noexcept;

public:
    /// A grading wheel's puck position, as a zero-sum RGB offset.
    ///
    /// Public and static because it is pure arithmetic with one property that
    /// has to hold — the three components sum to zero — and that property is
    /// the whole reason the wheel is a colour control rather than a brightness
    /// one. Testable without a device.
    static void gradeOffsets(float x, float y, float out[3]) noexcept;

private:
};

}  // namespace orion::pipe
