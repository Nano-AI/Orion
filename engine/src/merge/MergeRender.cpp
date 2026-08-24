/*  MergeRender — see the header. The kernels are the develop graph's own
 *  (linearize.slang, rcd_*.slang); only the parameters differ: unit white
 *  balance and a 1.0 clip, because the merge wants camera-native data in
 *  own-frame-normalized units.
 */

#include "merge/MergeRender.h"

#include "pipe/ShaderParams.h"
#include "util/Half.h"

#include <utility>

namespace orion::merge {

MergeRender::MergeRender(gpu::Device& device, std::string shaderDir)
    : device_(device), shaderDir_(std::move(shaderDir)) {}

std::vector<float> MergeRender::demosaic(const raw::BayerImage& image) {
    using gpu::PixelFormat;
    using pipe::kSource;

    if (!pipeline_ || image.width != width_ || image.height != height_) {
        auto next = std::make_unique<pipe::Pipeline>(device_, shaderDir_);
        const int lin = next->add({"linearize", "linearize", {kSource},
                                   PixelFormat::R32Float, {}});
        const int dirs = next->add({"rcd:dirs", "rcdDirs", {lin},
                                    PixelFormat::R32Float, {}});
        const int lpf = next->add({"rcd:lowpass", "rcdLpf", {lin},
                                   PixelFormat::R32Float, {}});
        const int green = next->add({"rcd:green", "rcdGreen", {lin, dirs, lpf},
                                     PixelFormat::R32Float, {}});
        const int rgb = next->add({"rcd:red/blue", "rcdRedBlue", {lin, green, dirs},
                                   PixelFormat::RGBA16Float, {}});
        next->compile(image.width, image.height);

        pipeline_  = std::move(next);
        width_     = image.width;
        height_    = image.height;
        nLinearize_ = lin; nDirs_ = dirs; nLpf_ = lpf;
        nGreen_ = green;   nRgb_ = rgb;
    }

    // Per frame, not per graph: black levels and the CFA phase are the
    // frame's own even when the shape is shared across a bracket.
    pipe::params::Linearize lin{};
    for (int c = 0; c < 4; ++c) {
        lin.black[c]        = float(image.black[std::size_t(c)]);
        lin.whiteBalance[c] = 1.0f;
    }
    lin.invRange  = 1.0f / float(image.white - image.black[0]);
    lin.filters   = image.filters;
    lin.size[0]   = width_;
    lin.size[1]   = height_;
    lin.whiteClip = 1.0f;
    pipeline_->setParams(nLinearize_, &lin, sizeof lin);

    pipe::params::Dirs dirs{{width_, height_}};
    pipeline_->setParams(nDirs_, &dirs, sizeof dirs);
    pipeline_->setParams(nLpf_,  &dirs, sizeof dirs);

    pipe::params::Green green{{width_, height_}, image.filters, 0};
    pipeline_->setParams(nGreen_, &green, sizeof green);
    pipeline_->setParams(nRgb_,   &green, sizeof green);

    pipeline_->setSource(image.samples.data(),
                         static_cast<std::size_t>(width_) * 2);
    pipeline_->render();

    // The final node is pinned structurally by Pipeline, so this readback is
    // safe. fp16 out of the texture, widened to the floats the merge takes.
    std::vector<std::uint16_t> half(std::size_t(width_) * height_ * 4);
    pipeline_->nodeOutput(nRgb_).download(
        half.data(), std::size_t(width_) * 4 * sizeof(std::uint16_t),
        width_, height_);

    std::vector<float> out(std::size_t(width_) * height_ * 3);
    for (std::size_t p = 0; p < std::size_t(width_) * height_; ++p) {
        out[p * 3 + 0] = util::halfToFloat(half[p * 4 + 0]);
        out[p * 3 + 1] = util::halfToFloat(half[p * 4 + 1]);
        out[p * 3 + 2] = util::halfToFloat(half[p * 4 + 2]);
    }
    return out;
}

}  // namespace orion::merge
