#include "Engine.h"

#include <stdexcept>

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

const pipe::DevelopPipeline& Engine::develop() const {
    if (!develop_) throw std::runtime_error("no image open");
    return *develop_;
}

}  // namespace orion
