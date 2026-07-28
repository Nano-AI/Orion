#include "Engine.h"

#include <stdexcept>
#include <algorithm>
#include <vector>

namespace orion {

Engine::Engine() : device_(gpu::Device::create()) {}

Engine::~Engine() = default;

void Engine::openRaw(const std::string& path) {
    const auto image = raw::decodeBayer(path);
    // Replace only once decode and pipeline construction have both succeeded,
    // so a failed open leaves the previously open image intact.
    auto next = std::make_unique<pipe::DevelopPipeline>(*device_, ORION_SHADER_DIR, image);
    develop_ = std::move(next);
    camera_  = image.camera;
}

void Engine::setAdjustments(const pipe::Adjustments& adj) {
    if (!develop_) throw std::runtime_error("no image open");
    develop_->apply(adj);
}

double Engine::render() {
    if (!develop_) throw std::runtime_error("no image open");
    return develop_->render();
}

void Engine::sampleAt(float u, float v, float outRgb[3]) const {
    outRgb[0] = outRgb[1] = outRgb[2] = 0.0f;
    if (!develop_) return;

    const std::uint32_t w = develop_->outputWidth();
    const std::uint32_t h = develop_->outputHeight();
    if (w == 0 || h == 0) return;

    const auto clamp01 = [](float x) { return x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x); };
    const auto x = static_cast<std::uint32_t>(clamp01(u) * float(w - 1));
    const auto y = static_cast<std::uint32_t>(clamp01(v) * float(h - 1));

    // Reference image is unoriented, so undo the rotation to find the pixel.
    // The caller works in oriented coordinates because that is what it sees.
    const std::uint32_t rw = develop_->width();
    const std::uint32_t rh = develop_->height();
    const std::uint32_t rx = (rw == w) ? x : std::min(y, rw - 1);
    const std::uint32_t ry = (rw == w) ? y : std::min(x, rh - 1);

    __fp16 px[4]{};
    develop_->referenceImage().readPixel(std::min(rx, rw - 1),
                                         std::min(ry, rh - 1), px);

    // Scene-linear and unbounded. Normalise by the peak so hue survives; the
    // caller only needs the ratio between channels.
    const float peak = std::max({float(px[0]), float(px[1]), float(px[2]), 1e-6f});
    for (int i = 0; i < 3; ++i) outRgb[i] = std::max(float(px[i]), 0.0f) / peak;
}

void Engine::exportImage(const std::string& path, const util::ExportOptions& options) {
    if (!develop_) throw std::runtime_error("no image open");

    // Make sure what we write matches what is on screen.
    develop_->render();

    // The orientation node's texture is square; only the oriented rectangle
    // inside it holds pixels.
    const std::uint32_t w = develop_->outputWidth();
    const std::uint32_t h = develop_->outputHeight();
    const std::size_t rowBytes = static_cast<std::size_t>(w) * 4;

    std::vector<std::uint8_t> pixels(rowBytes * h);
    develop_->output().download(pixels.data(), rowBytes, w, h);

    util::writeImage(path, pixels.data(), w, h, rowBytes, options);
}

const pipe::DevelopPipeline& Engine::develop() const {
    if (!develop_) throw std::runtime_error("no image open");
    return *develop_;
}

}  // namespace orion
