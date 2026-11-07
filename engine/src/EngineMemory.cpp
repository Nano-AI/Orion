// What the engine gives back between renders: a kept photo's whole device
// footprint (#318) and an idle canvas's spare (#406).

#include "Engine.h"

namespace orion {

void Engine::releaseRenderMemory() noexcept {
    // ⚠ The full graph re-uploads from `mosaic_`, which the region graph cuts
    // its windows from too; a linear frame has no CPU copy and keeps its source.
    if (develop_) develop_->releaseDevice(mosaic_ ? std::shared_ptr<const void>(mosaic_, mosaic_->samples.data()) : nullptr);
    if (preview_) preview_->releaseDevice(previewSource_);
    if (region_) {
        region_->releaseDevice(nullptr);
        region_->graph().releaseSource();
        regionLoaded_ = false;   // the next region reloads its window
    }
    if (display_) display_->release();   // the kernel stays compiled
    samples_.reset();
    device_->reclaim();
}

bool Engine::memoryShort() const noexcept {
    const std::uint64_t free = device_->freeBytes();
    const std::uint64_t total = device_->info().recommendedWorkingSet;
    if (free != gpu::Device::kUnknown && total > 0 && free < total / 4) return true;
    const std::uint64_t held = develop_->graph().allocatedBytes();
    const std::uint64_t all = device_->allocatedBytes() ? device_->allocatedBytes() : device_->textureBytes();
    return all > held && all - held > develop_->graph().memoryBudget();
}

void Engine::trim(bool full) noexcept {
    if (!develop_) return;
    for (auto* g : {develop_.get(), preview_.get(), region_.get()})
        if (g) g->graph().trimPool();
    if (full) {
        develop_->graph().releaseOutputs();
        fullReleased_ = true;
        fullCurrent_ = false;
    }
    device_->reclaim();   // before the pressure is read: what was dropped is still counted till then
    if (!full && memoryShort()) {
        develop_->graph().releaseCache();
        device_->reclaim();
    }
}

}  // namespace orion
