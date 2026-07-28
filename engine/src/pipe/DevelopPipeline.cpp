#include "pipe/DevelopPipeline.h"

#include "pipe/ShaderParams.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace orion::pipe {
namespace {

// CIE XYZ (D65) -> linear Rec.2020, the working space.
constexpr float kXyzToRec2020[9] = {
     1.7166512f, -0.3556708f, -0.2533663f,
    -0.6666844f,  1.6164812f,  0.0157685f,
     0.0176399f, -0.0427706f,  0.9421031f,
};

bool invert3x3(const float m[9], float out[9]) {
    const float det = m[0] * (m[4] * m[8] - m[5] * m[7])
                    - m[1] * (m[3] * m[8] - m[5] * m[6])
                    + m[2] * (m[3] * m[7] - m[4] * m[6]);
    if (std::abs(det) < 1e-12f) return false;

    const float k = 1.0f / det;
    out[0] = (m[4] * m[8] - m[5] * m[7]) * k;
    out[1] = (m[2] * m[7] - m[1] * m[8]) * k;
    out[2] = (m[1] * m[5] - m[2] * m[4]) * k;
    out[3] = (m[5] * m[6] - m[3] * m[8]) * k;
    out[4] = (m[0] * m[8] - m[2] * m[6]) * k;
    out[5] = (m[2] * m[3] - m[0] * m[5]) * k;
    out[6] = (m[3] * m[7] - m[4] * m[6]) * k;
    out[7] = (m[1] * m[6] - m[0] * m[7]) * k;
    out[8] = (m[0] * m[4] - m[1] * m[3]) * k;
    return true;
}

void multiply3x3(const float a[9], const float b[9], float out[9]) {
    for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 3; ++c)
            out[r * 3 + c] = a[r * 3 + 0] * b[0 * 3 + c]
                           + a[r * 3 + 1] * b[1 * 3 + c]
                           + a[r * 3 + 2] * b[2 * 3 + c];
}

/// Scales each row to sum to 1 so camera (1,1,1) maps to working (1,1,1).
/// Without this the white balance and the colour matrix fight each other: the
/// data is already neutral after WB, and an unnormalised matrix then tints it.
/// dcraw normalises rgb_cam for exactly this reason.
void normaliseRows(float m[9]) {
    for (int r = 0; r < 3; ++r) {
        const float sum = m[r * 3 + 0] + m[r * 3 + 1] + m[r * 3 + 2];
        if (std::abs(sum) < 1e-9f) continue;
        for (int c = 0; c < 3; ++c) m[r * 3 + c] /= sum;
    }
}

bool sameChannel(const CurveChannel& a, const CurveChannel& b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (std::abs(a[i].x - b[i].x) > 1e-6f) return false;
        if (std::abs(a[i].y - b[i].y) > 1e-6f) return false;
    }
    return true;
}

bool sameCurve(const ToneCurveSpec& a, const ToneCurveSpec& b) {
    return sameChannel(a.master, b.master) && sameChannel(a.red, b.red) &&
           sameChannel(a.green, b.green) && sameChannel(a.blue, b.blue);
}

/// LibRaw's flip flag as clockwise quarter turns.
int quarterTurnsFor(int flip) {
    switch (flip) {
        case 3: return 2;   // 180
        case 5: return 3;   // 90 anticlockwise
        case 6: return 1;   // 90 clockwise
        default: return 0;
    }
}

}  // namespace

DevelopPipeline::DevelopPipeline(gpu::Device& device, const std::string& shaderDir,
                                 const raw::BayerImage& image)
    : pipeline_(device, shaderDir), width_(image.width), height_(image.height) {

    using gpu::PixelFormat;

    nLinearize_ = pipeline_.add({"linearize", "linearize", {kSource},
                                 PixelFormat::R32Float, {}});
    nDirs_      = pipeline_.add({"rcd:dirs", "rcdDirs", {nLinearize_},
                                 PixelFormat::R32Float, {}});
    nLpf_       = pipeline_.add({"rcd:lowpass", "rcdLpf", {nLinearize_},
                                 PixelFormat::R32Float, {}});
    nGreen_     = pipeline_.add({"rcd:green", "rcdGreen", {nLinearize_, nDirs_, nLpf_},
                                 PixelFormat::R32Float, {}});
    nRgb_       = pipeline_.add({"rcd:red/blue", "rcdRedBlue",
                                 {nLinearize_, nGreen_, nDirs_},
                                 PixelFormat::RGBA16Float, {}});
    // Capture sharpening belongs right after the demosaic, and keeping it
    // upstream of the tone controls means an exposure drag never recomputes it.
    nSharpen_   = pipeline_.add({"sharpen", "sharpen", {nRgb_},
                                 PixelFormat::RGBA16Float, {}});
    nMatrix_    = pipeline_.add({"camera->working", "cameraToWorking", {nSharpen_},
                                 PixelFormat::RGBA16Float, {}});
    // Every scene-linear adjustment fuses into one dispatch, and the display
    // transform plus curve into another. They are all pointwise; separate
    // passes only bought a 194 MB round trip each at 24 MP.
    // ── Guided filter (He, Sun & Tang) ────────────────────────────────────
    // Sits before exposure on purpose: exposure is a multiply, so in log2 it is
    // an add the tone node applies for free — which keeps this whole six-pass
    // chain cached while the exposure slider moves.
    nGuidePrep_ = pipeline_.add({"guide:prep", "guidePrep", {nMatrix_},
                                 PixelFormat::RG32Float, {}});
    nGuideH1_   = pipeline_.add({"guide:blur h", "boxBlur", {nGuidePrep_},
                                 PixelFormat::RG32Float, {}});
    nGuideV1_   = pipeline_.add({"guide:blur v", "boxBlur", {nGuideH1_},
                                 PixelFormat::RG32Float, {}});
    nGuideAb_   = pipeline_.add({"guide:coeffs", "guideAb", {nGuideV1_},
                                 PixelFormat::RG32Float, {}});
    nGuideH2_   = pipeline_.add({"guide:blur h2", "boxBlur", {nGuideAb_},
                                 PixelFormat::RG32Float, {}});
    nGuideV2_   = pipeline_.add({"guide:blur v2", "boxBlur", {nGuideH2_},
                                 PixelFormat::RG32Float, {}});

    nLinear_    = pipeline_.add({"develop:linear", "developLinear",
                                 {nMatrix_, nGuideV2_, nGuidePrep_},
                                 PixelFormat::RGBA16Float, {}});

    auxCurveLut_ = pipeline_.addAuxTexture(kCurveResolution, kCurveRows,
                                           PixelFormat::R32Float);
    nDisplay_   = pipeline_.add({"develop:display", "developDisplay", {nLinear_},
                                 PixelFormat::RGBA8Unorm, {}, {auxCurveLut_}});

    // Orientation is last, and is the only node whose output dimensions differ
    // from its input — a quarter turn swaps them.
    exifQuarters_ = quarterTurnsFor(image.flip);
    const bool swaps = (exifQuarters_ % 2) != 0;

    // Allocate for the worst case so a user rotation never needs a recompile.
    // 1.5x covers the worst-case straighten bounding box (45 degrees
    // grows the frame by sqrt(2)).
    const std::uint32_t maxSide =
        static_cast<std::uint32_t>(std::max(width_, height_) * 1.45f);
    nGeometry_ = pipeline_.add({"geometry", "geometry", {nDisplay_},
                                PixelFormat::RGBA8Unorm, {}, {},
                                maxSide, maxSide});
    (void)swaps;

    pipeline_.compile(width_, height_);

    // ── Static parameters: everything that depends only on the file ────────
    const std::uint32_t size[2] = {width_, height_};

    const float g = (image.camMul[1] != 0.0f) ? image.camMul[1] : 1.0f;
    params::Linearize lin{};
    for (int c = 0; c < 4; ++c) {
        lin.black[c] = static_cast<float>(image.black[c]);
        // Sony leaves camMul[3] at zero; fall back to the first green.
        const float mul = (c == 3 || image.camMul[c] == 0.0f) ? g : image.camMul[c];
        lin.whiteBalance[c] = mul / g;
    }
    lin.invRange = 1.0f / static_cast<float>(image.white - image.black[0]);
    lin.filters  = image.filters;
    lin.size[0] = size[0]; lin.size[1] = size[1];
    linBase_ = lin;

    params::Dirs dirs{{size[0], size[1]}};
    pipeline_.setParams(nDirs_, &dirs, sizeof dirs);
    pipeline_.setParams(nLpf_,  &dirs, sizeof dirs);

    params::Green green{{size[0], size[1]}, image.filters, 0};
    pipeline_.setParams(nGreen_, &green, sizeof green);
    pipeline_.setParams(nRgb_,   &green, sizeof green);

    float xyzToCam[9], camToXyz[9], camToWorking[9];
    std::copy_n(image.camToXyz.begin(), 9, xyzToCam);
    if (!invert3x3(xyzToCam, camToXyz)) {
        throw std::runtime_error("camera colour matrix is singular");
    }
    std::copy_n(xyzToCam, 9, xyzToCam_);
    multiply3x3(kXyzToRec2020, camToXyz, camToWorking);
    normaliseRows(camToWorking);

    params::ColorMatrix mat{};
    for (int c = 0; c < 3; ++c) {
        mat.row0[c] = camToWorking[0 * 3 + c];
        mat.row1[c] = camToWorking[1 * 3 + c];
        mat.row2[c] = camToWorking[2 * 3 + c];
    }
    mat.size[0] = size[0]; mat.size[1] = size[1];
    pipeline_.setParams(nMatrix_, &mat, sizeof mat);

    // Guided filter parameters. Radius scales with the frame so the effect
    // covers the same fraction of the picture regardless of megapixels;
    // epsilon is in squared log2-exposure units, and 0.04 is about a fifth of
    // a stop — below that is texture and noise, above it is an edge.
    const int guideRadius =
        std::max(4, static_cast<int>(std::max(width_, height_) / 200));

    params::GuidePrep gp{{size[0], size[1]}, {0, 0}};
    pipeline_.setParams(nGuidePrep_, &gp, sizeof gp);

    params::BoxBlur bh{{size[0], size[1]}, guideRadius, 1};
    params::BoxBlur bv{{size[0], size[1]}, guideRadius, 0};
    pipeline_.setParams(nGuideH1_, &bh, sizeof bh);
    pipeline_.setParams(nGuideV1_, &bv, sizeof bv);
    pipeline_.setParams(nGuideH2_, &bh, sizeof bh);
    pipeline_.setParams(nGuideV2_, &bv, sizeof bv);

    params::GuideAb ga{{size[0], size[1]}, 0.04f, 0.0f};
    pipeline_.setParams(nGuideAb_, &ga, sizeof ga);

    // Anchor on the camera's actual multipliers, not on a temperature we
    // inferred from them. The temperature is only a handle for the user to
    // turn; routing "as shot" through it would bake every estimation error
    // into the image as a colour cast.
    const float gRef = (image.camMul[1] != 0.0f) ? image.camMul[1] : 1.0f;
    asShotMul_ = {image.camMul[0] / gRef, 1.0f, image.camMul[2] / gRef};

    asShot_    = estimateFrom(asShotMul_, xyzToCam_);
    asShotRef_ = multipliersFor(asShot_, xyzToCam_);

    Adjustments initial;
    initial.wb = asShot_;
    apply(initial);

    pipeline_.setSource(image.samples.data(), static_cast<std::size_t>(width_) * 2);
}

void DevelopPipeline::apply(const Adjustments& adj) {
    const std::uint32_t size[2] = {width_, height_};

    // Only push what actually moved. setParams dirties the whole downstream
    // subgraph, so pushing all three blocks on every tick would make dragging
    // the curve also recompute exposure and AgX — three nodes of work for a
    // one-node change, and the difference between 4 ms and 12 ms.
    const bool first = !primed_;

    // White balance rewrites the linearize block, which sits at the head of the
    // graph — so moving temperature legitimately recomputes everything,
    // including the demosaic. That is inherent: the demosaic interpolates
    // white-balanced data.
    if (first || adj.wb.temperatureK != lastAdj_.wb.temperatureK ||
        adj.wb.tint != lastAdj_.wb.tint) {
        // Apply temperature as a *ratio* against the as-shot estimate, so
        // leaving the slider alone reproduces the camera's own multipliers
        // exactly, and moving it is a relative change from there.
        const auto want = multipliersFor(adj.wb, xyzToCam_);

        params::Linearize lin = linBase_;
        lin.whiteBalance[0] = asShotMul_[0] * want[0] / std::max(asShotRef_[0], 1e-6f);
        lin.whiteBalance[1] = 1.0f;
        lin.whiteBalance[2] = asShotMul_[2] * want[2] / std::max(asShotRef_[2], 1e-6f);
        lin.whiteBalance[3] = 1.0f;   // second green
        pipeline_.setParams(nLinearize_, &lin, sizeof lin);
    }

    const bool linearMoved =
        first ||
        adj.hueShift != lastAdj_.hueShift ||
        adj.satShift != lastAdj_.satShift ||
        adj.lumShift != lastAdj_.lumShift ||
        adj.exposureEv != lastAdj_.exposureEv ||
        adj.highlights != lastAdj_.highlights ||
        adj.shadows    != lastAdj_.shadows    ||
        adj.whites     != lastAdj_.whites     ||
        adj.blacks     != lastAdj_.blacks     ||
        adj.vibrance   != lastAdj_.vibrance   ||
        adj.saturation != lastAdj_.saturation;

    if (linearMoved) {
        params::LinearAdjust la{adj.exposureEv, adj.highlights, adj.shadows,
                                adj.whites, adj.blacks, adj.vibrance,
                                adj.saturation, 0.0f, {size[0], size[1]}, {0, 0},
                                {}, {}, {}};
        std::copy(adj.hueShift.begin(), adj.hueShift.end(), la.hueShift);
        std::copy(adj.satShift.begin(), adj.satShift.end(), la.satShift);
        std::copy(adj.lumShift.begin(), adj.lumShift.end(), la.lumShift);
        pipeline_.setParams(nLinear_, &la, sizeof la);
    }

    if (first || adj.sharpenAmount != lastAdj_.sharpenAmount ||
        adj.sharpenRadius != lastAdj_.sharpenRadius ||
        adj.sharpenMasking != lastAdj_.sharpenMasking) {
        params::Sharpen sh{adj.sharpenAmount, adj.sharpenRadius,
                           adj.sharpenMasking, 0.0f, {size[0], size[1]}, {0, 0}};
        pipeline_.setParams(nSharpen_, &sh, sizeof sh);
    }

    const bool curveMoved = first || !sameCurve(adj.curve, lastAdj_.curve);

    if (first || adj.contrast != lastAdj_.contrast || curveMoved) {
        params::Display d{adj.contrast, -2.5f,
                          adj.curve.isIdentity() ? 1u : 0u,
                          kCurveResolution, {size[0], size[1]}, {0, 0}};
        pipeline_.setParams(nDisplay_, &d, sizeof d);
    }

    // Rebuilding the LUT walks four splines. Skip it when the curve has not
    // moved, which is every frame of an exposure drag.
    if (curveMoved) {
        const auto lut = buildCurveLut(adj.curve);
        pipeline_.updateAux(auxCurveLut_, lut.data(), kCurveResolution * sizeof(float));
    }

    const bool geometryMoved =
        first ||
        adj.rotateQuarters != lastAdj_.rotateQuarters ||
        adj.straightenDeg  != lastAdj_.straightenDeg ||
        adj.cropX != lastAdj_.cropX || adj.cropY != lastAdj_.cropY ||
        adj.cropW != lastAdj_.cropW || adj.cropH != lastAdj_.cropH ||
        adj.cropPreview != lastAdj_.cropPreview;

    if (geometryMoved) {
        const int turns = ((exifQuarters_ + adj.rotateQuarters) % 4 + 4) % 4;
        const bool swap = (turns % 2) != 0;

        const float rotW = static_cast<float>(swap ? height_ : width_);
        const float rotH = static_cast<float>(swap ? width_  : height_);

        params::Geometry g{};
        g.inSize[0] = width_;
        g.inSize[1] = height_;
        g.quarterTurns  = static_cast<std::uint32_t>(turns);
        g.straightenRad = adj.straightenDeg * 3.14159265358979f / 180.0f;

        if (adj.cropPreview) {
            // Whole frame, expanded so a straightened picture is not clipped by
            // its own bounding box. The corners outside the original are
            // transparent, which is what lets the UI show them as empty.
            const float a = std::abs(g.straightenRad);
            const float grow = std::cos(a) + std::sin(a);

            g.cropSize[0] = grow;
            g.cropSize[1] = grow;
            g.cropOrigin[0] = -(grow - 1.0f) * 0.5f;
            g.cropOrigin[1] = -(grow - 1.0f) * 0.5f;

            g.outSize[0] = std::max(1u, static_cast<std::uint32_t>(rotW * grow));
            g.outSize[1] = std::max(1u, static_cast<std::uint32_t>(rotH * grow));
        } else {
            const float cw = std::clamp(adj.cropW, 0.01f, 1.0f);
            const float ch = std::clamp(adj.cropH, 0.01f, 1.0f);
            g.cropOrigin[0] = std::clamp(adj.cropX, 0.0f, 1.0f - cw);
            g.cropOrigin[1] = std::clamp(adj.cropY, 0.0f, 1.0f - ch);
            g.cropSize[0]   = cw;
            g.cropSize[1]   = ch;

            g.outSize[0] = std::max(1u, static_cast<std::uint32_t>(rotW * cw));
            g.outSize[1] = std::max(1u, static_cast<std::uint32_t>(rotH * ch));
        }

        pipeline_.setParams(nGeometry_, &g, sizeof g);
        turns_ = turns;
        outW_  = g.outSize[0];
        outH_  = g.outSize[1];
    }

    lastAdj_ = adj;
    primed_  = true;
}

std::uint32_t DevelopPipeline::outputWidth() const noexcept  { return outW_; }
std::uint32_t DevelopPipeline::outputHeight() const noexcept { return outH_; }

double DevelopPipeline::render() { return pipeline_.render(); }

}  // namespace orion::pipe
