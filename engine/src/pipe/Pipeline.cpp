#include "pipe/Pipeline.h"

#include <memory>

#include <algorithm>
#include <cstring>
#include <queue>
#include <stdexcept>
#include <unordered_map>

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
    // ⚠ One `MTLLibrary` per distinct metallib, not one per node. The develop
    // graph is 149 nodes over **48** distinct kernels, so loading per node
    // created ~101 redundant libraries and held them for the graph's lifetime —
    // doubled, because a photograph builds a full graph and a preview graph.
    // Not a leak (they were released with the graph) but resident waste, and
    // each load also costs a file read and ~1.6 KB of autoreleased temporaries.
    //
    // ⚠ Keyed by kernel name because that *is* the file's basename, one line
    // below. Keying by anything else would let two nodes naming one file get
    // two libraries again, quietly.
    std::unordered_map<std::string, gpu::Library*> byKernel;

    for (const auto& node : nodes_) {
        gpu::Library* lib = nullptr;
        if (auto it = byKernel.find(node.kernel); it != byKernel.end()) {
            lib = it->second;
        } else {
            auto owned = gpu::Library::createFromFile(
                device_, shaderDir_ + "/" + node.kernel + ".metallib");
            lib = owned.get();
            byKernel.emplace(node.kernel, lib);
            libraries_.push_back(std::move(owned));
        }
        auto kernel = gpu::Kernel::create(device_, *lib, node.kernel);

        // ⚠ **The shader and the code that feeds it must agree, and Metal will
        // not say so if they do not.** An unbound texture slot is nil, not an
        // error: reads give zero and writes go nowhere, so a kernel one slot
        // short of its shader runs to completion and produces nothing. Because
        // metallibs are loaded from disk by path, right here, a long-running
        // process picks up shaders rebuilt underneath it — which is exactly how
        // every mask silently covered zero for an hour while the suite stayed
        // green. Cheap to check once per node at compile; impossible to see
        // afterwards.
        const std::size_t binding = node.inputs.size() + node.aux.size() + 1;
        if (binding < kernel->textureSlotsUsed()) {
            throw std::runtime_error(
                "kernel '" + node.kernel + "' (node '" + node.name + "') uses " +
                std::to_string(kernel->textureSlotsUsed()) +
                " texture slots but the graph binds " + std::to_string(binding) +
                " — the shader and the binary disagree; rebuild both");
        }

        kernels_.push_back(std::move(kernel));

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

void Pipeline::resizeAux(int auxId, std::uint32_t width, std::uint32_t height) {
    if (auxId < 0 || auxId >= static_cast<int>(aux_.size())) {
        throw std::runtime_error("resizeAux: bad aux id");
    }
    auto& spec = auxSpecs_[static_cast<std::size_t>(auxId)];
    if (spec.width == width && spec.height == height) return;
    spec.width  = width;
    spec.height = height;
    aux_[static_cast<std::size_t>(auxId)] =
        gpu::Texture::create(device_, width, height, spec.format);
}

std::uint32_t Pipeline::auxWidth(int auxId) const {
    if (auxId < 0 || auxId >= static_cast<int>(auxSpecs_.size())) {
        throw std::runtime_error("auxWidth: bad aux id");
    }
    return auxSpecs_[static_cast<std::size_t>(auxId)].width;
}

bool Pipeline::nodeDirty(int nodeId) const {
    if (nodeId < 0 || nodeId >= static_cast<int>(dirty_.size())) {
        throw std::runtime_error("nodeDirty: bad node id");
    }
    return dirty_[static_cast<std::size_t>(nodeId)] &&
           nodes_[static_cast<std::size_t>(nodeId)].enabled;
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

std::size_t Pipeline::peakLiveBytes() const noexcept {
    // Last reader of each node's output, in execution order. A node nobody
    // reads is still written, so it is live for exactly its own step.
    std::vector<std::size_t> lastUse(nodes_.size(), 0);
    for (std::size_t step = 0; step < order_.size(); ++step) {
        const int id = order_[step];
        if (id < 0 || static_cast<std::size_t>(id) >= nodes_.size()) continue;
        lastUse[static_cast<std::size_t>(id)] =
            std::max(lastUse[static_cast<std::size_t>(id)], step);
        for (int in : nodes_[static_cast<std::size_t>(id)].inputs) {
            if (in >= 0 && static_cast<std::size_t>(in) < nodes_.size()) {
                lastUse[static_cast<std::size_t>(in)] = step;
            }
        }
    }

    std::size_t live = source_ ? source_->sizeBytes() : 0;
    std::size_t peak = live;
    // ⚠ **Only what has actually been written can be freed**, which is not the
    // pedantry it looks like: a node absent from `order_` — every disabled one
    // — has `lastUse` 0, so an unguarded subtraction runs at step 0 against a
    // texture that was never added and wraps `size_t` to something enormous.
    // The first version of this did exactly that.
    std::vector<bool> written(nodes_.size(), false);
    for (std::size_t step = 0; step < order_.size(); ++step) {
        const int id = order_[step];
        if (id < 0 || static_cast<std::size_t>(id) >= outputs_.size()) continue;
        if (outputs_[static_cast<std::size_t>(id)]) {
            live += outputs_[static_cast<std::size_t>(id)]->sizeBytes();
            written[static_cast<std::size_t>(id)] = true;
        }
        peak = std::max(peak, live);
        // Everything whose last reader was this step can go back to the pool.
        for (std::size_t n = 0; n < nodes_.size(); ++n) {
            if (written[n] && lastUse[n] == step && n < outputs_.size() &&
                outputs_[n] && static_cast<int>(n) != id) {
                live -= outputs_[n]->sizeBytes();
                written[n] = false;
            }
        }
    }
    return peak;
}

}  // namespace orion::pipe
