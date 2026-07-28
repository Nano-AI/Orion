#include "Engine.h"

#include <stdexcept>
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
