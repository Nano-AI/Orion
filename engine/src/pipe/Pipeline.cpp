#include "pipe/Pipeline.h"

#include <memory>

#include <algorithm>
#include <cstring>
#include <queue>
#include <stdexcept>

namespace orion::pipe {

Pipeline::Pipeline(gpu::Device& device, std::string shaderDir)
    : device_(device), shaderDir_(std::move(shaderDir)) {}

Pipeline::~Pipeline() = default;

int Pipeline::add(Node node) {
    if (compiled_) throw std::runtime_error("cannot add nodes after compile()");
    nodes_.push_back(std::move(node));
    return static_cast<int>(nodes_.size()) - 1;
}

void Pipeline::compile(std::uint32_t width, std::uint32_t height) {
    if (nodes_.empty()) throw std::runtime_error("pipeline has no nodes");

    width_  = width;
    height_ = height;

    // Kahn's algorithm. Also catches a cycle, which at this level would mean a
    // graph-construction bug rather than anything a user could cause.
    const int n = static_cast<int>(nodes_.size());
    std::vector<int> indegree(n, 0);
    std::vector<std::vector<int>> dependents(n);

    for (int i = 0; i < n; ++i) {
        for (int in : nodes_[i].inputs) {
            if (in == kSource) continue;
            if (in < 0 || in >= n) {
                throw std::runtime_error("node '" + nodes_[i].name + "' has a bad input id");
            }
            ++indegree[i];
            dependents[in].push_back(i);
        }
    }

    std::queue<int> ready;
    for (int i = 0; i < n; ++i) {
        if (indegree[i] == 0) ready.push(i);
    }

    order_.clear();
    order_.reserve(n);
    while (!ready.empty()) {
        const int i = ready.front();
        ready.pop();
        order_.push_back(i);
        for (int d : dependents[i]) {
            if (--indegree[d] == 0) ready.push(d);
        }
    }

    if (static_cast<int>(order_.size()) != n) {
        throw std::runtime_error("pipeline graph contains a cycle");
    }

    // Source mosaic: one 16-bit sample per pixel.
    source_ = gpu::Texture::create(device_, width_, height_, gpu::PixelFormat::R16Uint);

    aux_.clear();
    aux_.reserve(auxSpecs_.size());
    for (const auto& spec : auxSpecs_) {
        aux_.push_back(gpu::Texture::create(device_, spec.width, spec.height, spec.format));
    }

    libraries_.clear();
    kernels_.clear();
    outputs_.clear();
    libraries_.reserve(n);
    kernels_.reserve(n);
    outputs_.reserve(n);
    for (const auto& node : nodes_) {
        auto lib = gpu::Library::createFromFile(
            device_, shaderDir_ + "/" + node.kernel + ".metallib");
        kernels_.push_back(gpu::Kernel::create(device_, *lib, node.kernel));
        libraries_.push_back(std::move(lib));

        const std::uint32_t w = node.outWidth  ? node.outWidth  : width_;
        const std::uint32_t h = node.outHeight ? node.outHeight : height_;
        outputs_.push_back(gpu::Texture::create(device_, w, h, node.format));
    }

    dirty_.assign(n, true);
    compiled_ = true;
}

void Pipeline::setParams(int nodeId, const void* data, std::size_t bytes) {
    if (nodeId < 0 || nodeId >= static_cast<int>(nodes_.size())) {
        throw std::runtime_error("setParams: bad node id");
    }
    auto& params = nodes_[nodeId].params;
    params.resize(bytes);
    std::memcpy(params.data(), data, bytes);
    markDownstreamDirty(nodeId);
}

void Pipeline::setEnabled(int nodeId, bool enabled) {
    if (nodeId < 0 || nodeId >= static_cast<int>(nodes_.size())) return;
    if (nodes_[nodeId].enabled == enabled) return;
    nodes_[nodeId].enabled = enabled;
    markDownstreamDirty(nodeId);
}

void Pipeline::setNodeFormat(int nodeId, gpu::PixelFormat format) {
    if (nodeId < 0 || nodeId >= static_cast<int>(nodes_.size())) return;
    if (nodes_[nodeId].format == format) return;

    nodes_[nodeId].format = format;
    if (!compiled_) return;   // compile() will honour it

    const auto& node = nodes_[nodeId];
    const std::uint32_t w = node.outWidth  ? node.outWidth  : width_;
    const std::uint32_t h = node.outHeight ? node.outHeight : height_;
    outputs_[nodeId] = gpu::Texture::create(device_, w, h, format);

    // The old texture is gone, so whatever was cached in it is gone with it.
    dirty_[nodeId] = true;
    markDownstreamDirty(nodeId);
}

gpu::PixelFormat Pipeline::nodeFormat(int nodeId) const {
    if (nodeId < 0 || nodeId >= static_cast<int>(nodes_.size())) {
        throw std::runtime_error("nodeFormat: bad node id");
    }
    return nodes_[nodeId].format;
}

int Pipeline::resolve(int nodeId) const {
    // Walk back past disabled nodes to whoever last produced real pixels.
    int id = nodeId;
    int guard = 0;
    while (id >= 0 && id < static_cast<int>(nodes_.size()) &&
           !nodes_[id].enabled && ++guard < 64) {
        if (nodes_[id].inputs.empty()) break;
        id = nodes_[id].inputs.front();
    }
    return id;
}

void Pipeline::markDownstreamDirty(int nodeId) {
    // Walk forward in topological order: once a node is dirty, anything that
    // consumes it is dirty too. Single pass, because order_ guarantees
    // producers are visited before consumers.
    dirty_[nodeId] = true;
    for (int id : order_) {
        if (dirty_[id]) continue;
        for (int in : nodes_[id].inputs) {
            if (in != kSource && dirty_[in]) {
                dirty_[id] = true;
                break;
            }
        }
    }
}

int Pipeline::addAuxTexture(std::uint32_t width, std::uint32_t height,
                            gpu::PixelFormat format) {
    if (compiled_) throw std::runtime_error("cannot add aux textures after compile()");
    auxSpecs_.push_back({width, height, format});
    return static_cast<int>(auxSpecs_.size()) - 1;
}

void Pipeline::updateAux(int auxId, const void* data, std::size_t bytesPerRow) {
    if (auxId < 0 || auxId >= static_cast<int>(aux_.size())) {
        throw std::runtime_error("updateAux: bad aux id");
    }
    aux_[auxId]->upload(data, bytesPerRow);

    for (int id : order_) {
        const auto& a = nodes_[id].aux;
        if (std::find(a.begin(), a.end(), auxId) != a.end()) markDownstreamDirty(id);
    }
}

void Pipeline::setSource(const void* samples, std::size_t bytesPerRow) {
    if (!compiled_) throw std::runtime_error("setSource before compile()");
    source_->upload(samples, bytesPerRow);
    std::fill(dirty_.begin(), dirty_.end(), true);
}

double Pipeline::render() {
    if (!compiled_) throw std::runtime_error("render before compile()");

    lastRun_.clear();
    lastRun_.reserve(nodes_.size());

    double total = 0.0;
    std::unique_ptr<gpu::CommandBuffer> batch;
    if (!profiling_) batch = std::make_unique<gpu::CommandBuffer>(device_);

    for (int id : order_) {
        const Node& node = nodes_[id];
        const bool run = dirty_[id] && node.enabled;
        lastRun_.push_back({node.name, run, 0.0});
        if (!run) continue;

        std::vector<const gpu::Texture*> textures;
        textures.reserve(node.inputs.size() + node.aux.size() + 1);
        for (int in : node.inputs) {
            const int src = (in == kSource) ? kSource : resolve(in);
            textures.push_back(src == kSource ? source_.get() : outputs_[src].get());
        }
        for (int a : node.aux) {
            textures.push_back(aux_[a].get());
        }
        textures.push_back(outputs_[id].get());

        // Dispatch over the node's own output, which may differ from the
        // graph's working size once rotation or crop is in play.
        const std::uint32_t w = node.outWidth  ? node.outWidth  : width_;
        const std::uint32_t h = node.outHeight ? node.outHeight : height_;

        if (profiling_) {
            gpu::CommandBuffer one(device_);
            one.dispatch(*kernels_[id], textures,
                         node.params.empty() ? nullptr : node.params.data(),
                         node.params.size(), w, h);
            one.commitAndWait();
            lastRun_.back().ms = one.gpuMilliseconds();
            total += lastRun_.back().ms;
        } else {
            batch->dispatch(*kernels_[id], textures,
                            node.params.empty() ? nullptr : node.params.data(),
                            node.params.size(), w, h);
        }
    }

    if (batch) {
        batch->commitAndWait();
        total = batch->gpuMilliseconds();
    }
    std::fill(dirty_.begin(), dirty_.end(), false);
    return total;
}

const gpu::Texture& Pipeline::output() const {
    if (order_.empty()) throw std::runtime_error("pipeline not compiled");
    return *outputs_[order_.back()];
}

const gpu::Texture& Pipeline::nodeOutput(int nodeId) const {
    if (nodeId < 0 || nodeId >= static_cast<int>(outputs_.size())) {
        throw std::runtime_error("nodeOutput: bad node id");
    }
    return *outputs_[nodeId];
}

const gpu::Texture& Pipeline::sourceTexture() const {
    if (!source_) throw std::runtime_error("pipeline not compiled");
    return *source_;
}

std::size_t Pipeline::intermediateBytes() const noexcept {
    std::size_t total = source_ ? source_->sizeBytes() : 0;
    for (const auto& t : outputs_) total += t->sizeBytes();
    return total;
}

}  // namespace orion::pipe
