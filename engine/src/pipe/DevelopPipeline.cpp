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

}  // namespace

DevelopPipeline::DevelopPipeline(gpu::Device& device, const std::string& shaderDir,
                                 const raw::BayerImage& image)
    : pipeline_(device, shaderDir), width_(image.width), height_(image.height) {

    using gpu::PixelFormat;

    nLinearize_ = pipeline_.add({"linearize", "linearize", {kSource},
                                 PixelFormat::R32Float, {}});
    nDirs_      = pipeline_.add({"rcd:dirs", "rcdDirs", {nLinearize_},
                                 PixelFormat::R32Float, {}});
    nGreen_     = pipeline_.add({"rcd:green", "rcdGreen", {nLinearize_, nDirs_},
                                 PixelFormat::R32Float, {}});
    nRgb_       = pipeline_.add({"rcd:red/blue", "rcdRedBlue", {nLinearize_, nGreen_},
                                 PixelFormat::RGBA16Float, {}});
    nMatrix_    = pipeline_.add({"camera->working", "cameraToWorking", {nRgb_},
                                 PixelFormat::RGBA16Float, {}});
    nExposure_  = pipeline_.add({"exposure", "exposure", {nMatrix_},
                                 PixelFormat::RGBA16Float, {}});
    // AgX now hands off in float rather than encoding to 8-bit, so the tone
    // curve has full precision to work with before the final quantise.
    nAgx_       = pipeline_.add({"agx", "agx", {nExposure_},
                                 PixelFormat::RGBA16Float, {}});

    auxCurveLut_ = pipeline_.addAuxTexture(kCurveResolution, kCurveRows,
                                           PixelFormat::R32Float);
    nCurve_     = pipeline_.add({"tone curve", "toneCurve", {nAgx_},
                                 PixelFormat::RGBA8Unorm, {}, {auxCurveLut_}});

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
    pipeline_.setParams(nLinearize_, &lin, sizeof lin);

    params::Dirs dirs{{size[0], size[1]}};
    pipeline_.setParams(nDirs_, &dirs, sizeof dirs);

    params::Green green{{size[0], size[1]}, image.filters, 0};
    pipeline_.setParams(nGreen_, &green, sizeof green);
    pipeline_.setParams(nRgb_,   &green, sizeof green);

    float xyzToCam[9], camToXyz[9], camToWorking[9];
    std::copy_n(image.camToXyz.begin(), 9, xyzToCam);
    if (!invert3x3(xyzToCam, camToXyz)) {
        throw std::runtime_error("camera colour matrix is singular");
    }
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

    apply({});
    pipeline_.setSource(image.samples.data(), static_cast<std::size_t>(width_) * 2);
}

void DevelopPipeline::apply(const Adjustments& adj) {
    const std::uint32_t size[2] = {width_, height_};

    // Only push what actually moved. setParams dirties the whole downstream
    // subgraph, so pushing all three blocks on every tick would make dragging
    // the curve also recompute exposure and AgX — three nodes of work for a
    // one-node change, and the difference between 4 ms and 12 ms.
    const bool first = !primed_;

    if (first || adj.exposureEv != lastAdj_.exposureEv || adj.black != lastAdj_.black) {
        params::Exposure expo{adj.exposureEv, adj.black, {size[0], size[1]}};
        pipeline_.setParams(nExposure_, &expo, sizeof expo);
    }

    if (first || adj.contrast != lastAdj_.contrast || adj.saturation != lastAdj_.saturation) {
        params::Agx agx{adj.contrast, -2.5f, adj.saturation, 0.0f, {size[0], size[1]}};
        pipeline_.setParams(nAgx_, &agx, sizeof agx);
    }

    if (first) {
        params::Curve curve{{size[0], size[1]}, kCurveResolution, 0};
        pipeline_.setParams(nCurve_, &curve, sizeof curve);
    }

    // Rebuilding the LUT walks four splines. Skip it when the curve has not
    // moved, which is every frame of an exposure drag.
    if (first || !sameCurve(adj.curve, lastAdj_.curve)) {
        const auto lut = buildCurveLut(adj.curve);
        pipeline_.updateAux(auxCurveLut_, lut.data(), kCurveResolution * sizeof(float));
    }

    lastAdj_ = adj;
    primed_  = true;
}

double DevelopPipeline::render() { return pipeline_.render(); }

}  // namespace orion::pipe
