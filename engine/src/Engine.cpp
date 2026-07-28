#include "Engine.h"

#include <stdexcept>
#include <algorithm>
#include <vector>

namespace orion {

Engine::Engine() : device_(gpu::Device::create()) {}

Engine::~Engine() = default;

void Engine::openRaw(const std::string& path) {
    const auto image = raw::decodeBayer(path);
    sourcePath_ = path;

    // Reuse the compiled graph whenever the new frame has the same shape.
    // Rebuilding recompiles sixteen shaders and reallocates roughly 2.5 GiB of
    // textures — for a folder of frames from one camera, all of that is
    // identical work repeated per photo.
    if (develop_ && develop_->canReload(image)) {
        develop_->reload(image);
        camera_ = image.camera;
        return;
    }

    // Replace only once decode and construction have both succeeded, so a
    // failed open leaves the previously open image intact.
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

void Engine::sampleAt(float u, float v, float* outDisplay, float* outScene) const {
    if (outDisplay) outDisplay[0] = outDisplay[1] = outDisplay[2] = 0.0f;
    if (outScene)   outScene[0]   = outScene[1]   = outScene[2]   = 0.0f;
    if (!develop_) return;

    const std::uint32_t w = develop_->outputWidth();
    const std::uint32_t h = develop_->outputHeight();
    if (w == 0 || h == 0) return;

    const auto clamp01 = [](float x) { return x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x); };
    const auto x = static_cast<std::uint32_t>(clamp01(u) * float(w - 1));
    const auto y = static_cast<std::uint32_t>(clamp01(v) * float(h - 1));

    // What is on screen.
    if (outDisplay) {
        __fp16 px[4]{};
        develop_->output().readPixel(x, y, px);
        for (int i = 0; i < 3; ++i) {
            outDisplay[i] = std::clamp(float(px[i]), 0.0f, 1.0f);
        }
    }

    // The unedited scene color. The reference image is unrotated, so invert
    // the orientation exactly as orient.slang applies it — a naive transpose
    // is wrong for every quarter turn and samples a mirrored pixel.
    if (outScene) {
        const std::uint32_t rw = develop_->width();
        const std::uint32_t rh = develop_->height();

        std::uint32_t sx = x, sy = y;
        switch (develop_->quarterTurns()) {
            case 1: sx = y;              sy = rh - 1 - x; break;
            case 2: sx = rw - 1 - x;     sy = rh - 1 - y; break;
            case 3: sx = rw - 1 - y;     sy = x;          break;
            default: break;
        }
        sx = std::min(sx, rw - 1);
        sy = std::min(sy, rh - 1);

        __fp16 px[4]{};
        develop_->referenceImage().readPixel(sx, sy, px);

        // Scene-linear and unbounded. Normalize by the peak: the caller wants
        // hue, and hue is scale-invariant.
        const float peak = std::max({float(px[0]), float(px[1]), float(px[2]), 1e-6f});
        for (int i = 0; i < 3; ++i) outScene[i] = std::max(float(px[i]), 0.0f) / peak;
    }
}

void Engine::histogram(std::uint32_t* out, std::uint32_t bins) const {
    if (out == nullptr || bins == 0) return;
    std::fill_n(out, std::size_t(bins) * 3, 0u);
    if (!develop_) return;

    const std::uint32_t w = develop_->outputWidth();
    const std::uint32_t h = develop_->outputHeight();
    if (w == 0 || h == 0) return;

    const std::size_t rowBytes = std::size_t(w) * 4 * sizeof(__fp16);
    std::vector<__fp16> pixels(std::size_t(w) * h * 4);
    develop_->output().download(pixels.data(), rowBytes, w, h);

    // A prime stride decorrelates from any repeating structure in the image,
    // so a picket fence cannot alias into a false peak.
    constexpr std::size_t kStride = 31;
    const std::size_t count = std::size_t(w) * h;

    for (std::size_t i = 0; i < count; i += kStride) {
        for (std::uint32_t c = 0; c < 3; ++c) {
            const float v = std::clamp(float(pixels[i * 4 + c]), 0.0f, 1.0f);
            const std::uint32_t bin = std::min<std::uint32_t>(
                bins - 1, static_cast<std::uint32_t>(v * float(bins)));
            ++out[c * bins + bin];
        }
    }
}

void Engine::exportImage(const std::string& path, const util::ExportOptions& options) {
    if (!develop_) throw std::runtime_error("no image open");

    // Make sure what we write matches what is on screen.
    develop_->render();

    // The orientation node's texture is square; only the oriented rectangle
    // inside it holds pixels.
    const std::uint32_t w = develop_->outputWidth();
    const std::uint32_t h = develop_->outputHeight();

    util::ExportOptions o = options;
    // Only the real write carries metadata. The size estimate does not, and
    // the difference is a few hundred bytes against a file measured in
    // megabytes — but it is a difference, so it is stated rather than hidden.
    o.metadataFrom = sourcePath_;

    const auto pixels = readOutput16(w, h);
    util::writeImage(path, pixels.data(), w, h,
                     static_cast<std::size_t>(w) * 4 * sizeof(std::uint16_t), o);
}

std::size_t Engine::exportedSize(const util::ExportOptions& options) {
    if (!develop_) throw std::runtime_error("no image open");

    develop_->render();

    const std::uint32_t w = develop_->outputWidth();
    const std::uint32_t h = develop_->outputHeight();

    const auto pixels = readOutput16(w, h);
    return util::encodedSize(pixels.data(), w, h,
                             static_cast<std::size_t>(w) * 4 * sizeof(std::uint16_t),
                             options);
}

/// The output as 16-bit unsigned, which is what the image formats want.
///
/// The graph ends in half float. Converting here rather than making the graph
/// write integers keeps the pipeline in one numeric world, and half float is
/// the format Metal guarantees is read-write.
std::vector<std::uint16_t> Engine::readOutput16(std::uint32_t w, std::uint32_t h) const {
    const std::size_t count = static_cast<std::size_t>(w) * h * 4;
    std::vector<__fp16> half(count);
    develop_->output().download(half.data(),
                                static_cast<std::size_t>(w) * 4 * sizeof(__fp16), w, h);

    std::vector<std::uint16_t> out(count);
    for (std::size_t i = 0; i < count; ++i) {
        const float v = std::clamp(float(half[i]), 0.0f, 1.0f);
        out[i] = static_cast<std::uint16_t>(v * 65535.0f + 0.5f);
    }
    return out;
}

const pipe::DevelopPipeline& Engine::develop() const {
    if (!develop_) throw std::runtime_error("no image open");
    return *develop_;
}

}  // namespace orion
