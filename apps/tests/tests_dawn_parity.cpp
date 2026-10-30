// Dawn against Metal, node by node (#375).
//
// The develop graph renders on Metal with every node pinned; then each node's
// kernel runs ALONE on Dawn, fed Metal's own input textures and the node's own
// parameter block, and its output is compared with Metal's. Isolated, so a
// difference is that kernel's and not something carried down the chain. The
// whole graph on Dawn is the rest of the suite, run with ORION_GPU=dawn.
//
// Tolerance, per kernel, in the output's own units: both backends end in the
// Metal compiler, but by different roads — slangc's MSL compiled with
// -ffast-math, against slangc's WGSL translated by Tint and compiled without
// it — so a transcendental or a fused multiply-add may round differently.
// What counts as passing is stated per kernel in `kAllowed` and printed.

#include "harness.h"

#include "util/Half.h"

#include <filesystem>
#include <map>

namespace {

using namespace orion;

struct Seen {
    int         nodes = 0;
    double      worst = 0.0;      // max |metal - dawn| over every sample
    std::size_t differ = 0, samples = 0;
};

std::vector<float> floats(const gpu::Texture& t) {
    const std::size_t n = std::size_t(t.width()) * t.height();
    const std::size_t bpp = gpu::bytesPerPixel(t.format());
    std::vector<std::uint8_t> bytes(n * bpp);
    t.download(bytes.data(), t.width() * bpp);
    std::vector<float> out;
    switch (t.format()) {
        case gpu::PixelFormat::R16Float: case gpu::PixelFormat::RG16Float:
        case gpu::PixelFormat::RGBA16Float: {
            out.resize(bytes.size() / 2);
            for (std::size_t i = 0; i < out.size(); ++i) {
                std::uint16_t h;
                std::memcpy(&h, bytes.data() + 2 * i, 2);
                out[i] = util::halfToFloat(h);
            }
            break;
        }
        case gpu::PixelFormat::RGBA8Unorm:
            for (auto b : bytes) out.push_back(float(b) / 255.0f);
            break;
        case gpu::PixelFormat::R16Uint:
            out.resize(bytes.size() / 2);
            for (std::size_t i = 0; i < out.size(); ++i) {
                std::uint16_t v;
                std::memcpy(&v, bytes.data() + 2 * i, 2);
                out[i] = float(v);
            }
            break;
        default:   // the 32-bit float formats
            out.resize(bytes.size() / 4);
            std::memcpy(out.data(), bytes.data(), bytes.size());
    }
    return out;
}

std::unique_ptr<gpu::Texture> copyTo(gpu::Device& dawn, const gpu::Texture& t) {
    const std::size_t bpp = gpu::bytesPerPixel(t.format());
    std::vector<std::uint8_t> bytes(std::size_t(t.width()) * t.height() * bpp);
    t.download(bytes.data(), t.width() * bpp);
    auto copy = gpu::Texture::create(dawn, t.width(), t.height(), t.format());
    copy->upload(bytes.data(), t.width() * bpp);
    return copy;
}

raw::BayerImage parityInput() {
    raw::BayerImage in;
    in.width = 384; in.height = 256;
    in.filters = 0x94949494u;
    in.white = 4095;
    in.camMul = {2.0f, 1.0f, 1.5f, 1.0f};
    in.camToXyz = {0.4124f, 0.3576f, 0.1805f, 0.2126f, 0.7152f, 0.0722f,
                   0.0193f, 0.1192f, 0.9505f};
    in.samples.resize(in.pixelCount());
    std::mt19937 rng(375);
    std::normal_distribution<double> g(0.0, 1.0);
    for (std::uint32_t y = 0; y < in.height; ++y) {
        for (std::uint32_t x = 0; x < in.width; ++x) {
            // A ramp, a ring, noise, and a blown disc for highlight recovery.
            double v = 60.0 + 3600.0 * x / in.width + 400.0 * std::sin(0.05 * (x + 2.0 * y));
            const double dx = x - 280.0, dy = y - 90.0;
            if (dx * dx + dy * dy < 900.0) v = 4095.0;
            v += std::sqrt(0.5 * std::max(v, 0.0) + 20.0) * g(rng);
            in.samples[std::size_t(y) * in.width + x] =
                std::uint16_t(std::clamp(v, 0.0, 4095.0));
        }
    }
    return in;
}

std::vector<pipe::Adjustments> parityEdits(const pipe::DevelopPipeline& d) {
    pipe::Adjustments base;
    base.wb = d.asShotWhiteBalance();
    base.contrast = 1.45f;

    auto global = base;   // every whole-frame filter at once
    global.exposureEv = 0.3f; global.highlights = -0.6f; global.shadows = 0.5f;
    global.whites = 0.2f; global.blacks = -0.1f; global.vibrance = 0.4f;
    global.saturation = 0.2f; global.hueShift[4] = 0.3f; global.satShift[5] = -0.4f;
    global.lumShift[2] = 0.2f; global.highlightRecovery = 0.8f;
    global.gradeShadow[0] = 0.1f; global.gradeShadow[1] = 0.05f;
    global.gradeHighlight[2] = 0.1f; global.vignetteAmount = -1.0f;
    global.denoiseLuma = 1.5f; global.denoiseColor = 1.0f; global.denoiseDetail = 0.5f;
    global.denoiseNlm = 0.4f; global.texture = 0.5f; global.denoiseSmoothness = 0.6f;
    global.grainAmount = 0.03f; global.fusion = 0.6f; global.dehaze = 0.4f;
    global.clarity = 0.5f; global.sharpenAmount = 1.0f; global.sharpenMasking = 0.3f;
    global.lensDistortion = 0.2f; global.lensVignette = 0.3f;
    global.lensCaRed = 0.2f; global.lensCaBlue = -0.2f;
    global.straightenDeg = 3.0f; global.perspectiveVertical = 0.3f;
    global.cropX = 0.05f; global.cropY = 0.05f; global.cropW = 0.85f; global.cropH = 0.9f;

    // Masks, layers, spots. ⚠ **Every component slot and the last layers**:
    // a repack error hides in what no edit sets — the tail of each per-layer
    // array is where a wrong stride lands — so all eight components are used,
    // seven layers, and the last layers set fields the first ones do not.
    auto local = base;
    local.maskCount = 8;
    local.maskComponents[0].kind = 1;   // linear
    local.maskComponents[1].kind = 2;   // radial
    local.maskComponents[1].startsLayer = true;
    local.maskComponents[2].kind = 5;   // range
    local.maskComponents[2].startsLayer = true;
    local.maskComponents[3].kind = 6;   // color
    local.maskComponents[3].compose = 1;
    local.maskComponents[4].kind = 3;   // brush: the accumulator's path
    local.maskComponents[4].startsLayer = true;
    local.maskComponents[4].brushRadius = 0.06f;
    local.maskComponents[4].brushFlow = 0.6f;
    local.maskComponents[4].brushRevision = 1;
    local.maskComponents[5].kind = 2;
    local.maskComponents[5].startsLayer = true;
    local.maskComponents[5].center[0] = 0.7f;
    local.maskComponents[5].invert = true;
    local.maskComponents[6].kind = 1;
    local.maskComponents[6].startsLayer = true;
    local.maskComponents[6].angle = 1.2f;
    local.maskComponents[7].kind = 6;
    local.maskComponents[7].startsLayer = true;
    local.maskComponents[7].color[0] = 0.4f;
    local.maskRefine = 0.5f;
    local.layers[0].exposureEv = 0.7f; local.layers[0].denoiseLuma = 1.5f;
    local.layers[1].contrast = 0.4f; local.layers[1].warmth = 0.3f;
    local.layers[1].denoiseColor = 1.0f;
    local.layers[2].saturation = -0.5f; local.layers[2].shadows = 0.4f;
    local.layers[3].highlights = -0.6f; local.layers[3].exposureEv = -0.4f;
    local.layers[4].whites = 0.5f; local.layers[4].tint = -0.3f;
    local.layers[5].blacks = -0.4f; local.layers[5].denoiseLuma = 2.0f;
    local.layers[6].tint = 0.4f; local.layers[6].saturation = 0.6f;
    local.layers[6].denoiseColor = 2.0f; local.layers[6].highlights = 0.3f;
    local.maskOverlayLayer = 6;
    local.spotCount = 2;
    local.spots[0] = {0.3f, 0.4f, 0.6f, 0.5f};
    local.spots[1] = {0.7f, 0.6f, 0.2f, 0.3f};
    local.spots[1].heal = false;
    local.maskOverlay = true;
    return {base, global, local};
}

// Each enabled node of `d`'s graph, rendered on Metal, replayed alone on Dawn.
void compareGraph(pipe::DevelopPipeline& d, gpu::Device& dawn, std::map<std::string, Seen>& seen,
                  std::map<std::string, std::unique_ptr<gpu::Library>>& libs,
                  std::map<std::string, std::unique_ptr<gpu::Kernel>>& kernels) {
    auto& g = d.graph();
    std::vector<int> all(g.nodeCount());
    for (std::size_t i = 0; i < all.size(); ++i) all[i] = int(i);
    g.setPinned(all);
    d.render();

    // ⚠ Not `lastRun().executed`: a render can run the graph twice (the
    // highlight fill measures, then fills), and the last pass re-runs only
    // what moved. A node that did not re-run was clean, so what it holds is
    // still its kernel applied to what it reads.
    for (int id : all) {
        const auto& node = g.node(id);
        const auto binds = g.bindings(id);
        if (!node.enabled || std::find(binds.begin(), binds.end(), nullptr) != binds.end()) continue;
        std::vector<std::unique_ptr<gpu::Texture>> owned;
        std::vector<const gpu::Texture*> onDawn;
        for (const auto* t : binds) {
            owned.push_back(copyTo(dawn, *t));
            onDawn.push_back(owned.back().get());
        }
        auto& lib = libs[node.kernel];
        if (!lib) {
            lib = gpu::Library::createFromFile(
                dawn, std::string(ORION_SHADER_DIR) + "/" + node.kernel + ".metallib");
            kernels[node.kernel] = gpu::Kernel::create(dawn, *lib, node.kernel);
        }
        // Before the dispatch: a read-write input (the brush accumulator) is
        // compared against what it held going in, not what Metal left.
        std::vector<std::vector<float>> before;
        for (const auto* t : binds) before.push_back(floats(*t));
        const gpu::Texture& out = *binds.back();
        gpu::CommandBuffer cb(dawn);
        cb.dispatch(*kernels[node.kernel], onDawn,
                    node.params.empty() ? nullptr : node.params.data(),
                    node.params.size(), out.width(), out.height());
        cb.commitAndWait();

        // ⚠ **Every binding, not only the output.** A kernel may write more
        // than its last slot (the brush accumulator is an aux texture written
        // in place). Inputs must come back unchanged; the output must match
        // Metal's; a read-write aux must match Metal's state after the same
        // dispatch, which for an accumulator that already holds its dabs is
        // the state it went in with.
        Seen& s = seen[node.kernel];
        s.nodes++;
        for (std::size_t k = 0; k < binds.size(); ++k) {
            const auto a = k + 1 == binds.size() ? floats(out) : before[k];
            const auto b = floats(*onDawn[k]);
            if (a.size() != b.size()) { s.worst = 1e9; continue; }
            for (std::size_t i = 0; i < a.size(); ++i) {
                // NaN in both is agreement; NaN in one is the worst.
                const bool na = std::isnan(a[i]), nb = std::isnan(b[i]);
                const double diff = (na || nb) ? (na == nb ? 0.0 : 1e9) : std::fabs(double(a[i]) - b[i]);
                s.worst = std::max(s.worst, diff);
                s.differ += diff > 0.0;
            }
            s.samples += a.size();
        }
    }
}

// The brush component's stroke (component 4 of the local edit).
void paintBrush(pipe::DevelopPipeline& d) {
    std::vector<float> xy;
    for (int i = 0; i < 60; ++i) {
        xy.push_back(0.2f + 0.01f * float(i));
        xy.push_back(0.7f - 0.006f * float(i));
    }
    d.setBrushStroke(4, xy.data(), nullptr, 60);
}

raw::LinearImage parityLinear(const raw::BayerImage& like) {
    raw::LinearImage linear;
    linear.width = 192; linear.height = 128;
    linear.camMul = like.camMul;
    linear.camToXyz = like.camToXyz;
    linear.rgba.resize(linear.pixelCount() * 4);
    for (std::size_t i = 0; i < linear.pixelCount(); ++i) {
        const float v = 0.02f + 1.2f * float(i % linear.width) / float(linear.width);
        for (int c = 0; c < 3; ++c) linear.rgba[4 * i + c] = util::floatToHalf(v * (0.6f + 0.2f * c));
        linear.rgba[4 * i + 3] = util::floatToHalf(1.0f);
    }
    return linear;
}

}  // namespace

void testDawnParityGpu() {
    section("Dawn matches Metal, kernel by kernel (GPU)");
    std::unique_ptr<gpu::Device> metal, dawn;
    try {
        metal = gpu::Device::create(gpu::Backend::Metal);
        dawn  = gpu::Device::create(gpu::Backend::Dawn);
    } catch (const std::exception& e) {
        std::printf("  skipped: %s\n", e.what());
        return;
    }
    report(dawn->backend() == gpu::Backend::Dawn, "the second device is Dawn", dawn->info().name);

    // ⚠ The bar, per kernel: exact unless measured otherwise, so a new
    // difference anywhere fails instead of hiding under a shared bar. Measured:
    // colorGrade and developLinear move one fp16 step (0.000977) on a handful
    // of samples in millions — the two compilers round a transcendental
    // differently — and every other kernel is bit-identical. (With Dawn's
    // robustness on, llfRemapH and nlmLuma also moved one step.)
    const std::map<std::string, double> kAllowed = {
        {"colorGrade", 1.0 / 1024.0}, {"developLinear", 1.0 / 1024.0}};

    std::map<std::string, Seen> seen;
    std::map<std::string, std::unique_ptr<gpu::Library>> libs;
    std::map<std::string, std::unique_ptr<gpu::Kernel>>  kernels;
    double chain = 0.0;   // the whole graph on each backend, output against output
    try {
        const auto input = parityInput();
        pipe::DevelopPipeline probe(*metal, ORION_SHADER_DIR, input);
        for (const auto& edit : parityEdits(probe)) {
            for (bool wide : {false, true}) {
                pipe::DevelopPipeline d(*metal, ORION_SHADER_DIR, input);
                d.setWideOutput(wide);
                paintBrush(d);
                d.apply(edit);
                compareGraph(d, *dawn, seen, libs, kernels);

                // And the whole chain on Dawn, end to end: what a user sees.
                pipe::DevelopPipeline w(*dawn, ORION_SHADER_DIR, input);
                w.setWideOutput(wide);
                paintBrush(w);
                w.apply(edit);
                w.render();
                const auto a = floats(d.output()), b = floats(w.output());
                double worst = 0.0;
                for (std::size_t i = 0; i < a.size() && a.size() == b.size(); ++i)
                    worst = std::max(worst, double(std::fabs(a[i] - b[i])));
                chain = std::max(chain, a.size() == b.size() ? worst : 1e9);
            }
        }
        // A linear (demosaiced) source has its own first node.
        pipe::DevelopPipeline d(*metal, ORION_SHADER_DIR, parityLinear(input));
        auto edit = parityEdits(probe)[0];
        edit.wb = d.asShotWhiteBalance();
        d.apply(edit);
        compareGraph(d, *dawn, seen, libs, kernels);
    } catch (const std::exception& e) {
        report(false, "parity fixture completes", e.what());
    }

    for (const auto& [kernel, s] : seen) {
        char line[160];
        std::snprintf(line, sizeof line, "%2d nodes, worst %.6f, %zu of %zu samples differ",
                      s.nodes, s.worst, s.differ, s.samples);
        std::printf("  %-20s %s\n", kernel.c_str(), line);
        const auto allowed = kAllowed.find(kernel);
        report(s.worst <= (allowed == kAllowed.end() ? 0.0 : allowed->second), kernel, line);
    }
    std::printf("  %zu kernels compared\n", seen.size());
    // ⚠ The chain's bar is zero: the single-step differences above could meet
    // downstream, but measured they do not reach the output at all, so any
    // difference there is new and fails.
    char line[96];
    std::snprintf(line, sizeof line, "worst %.6f over three edits, screen and wide", chain);
    std::printf("  whole graph, Dawn against Metal: %s\n", line);
    report(chain == 0.0, "the whole develop graph on Dawn matches Metal's output exactly", line);

    // Every kernel the build makes must at least compile on Dawn, including
    // any no graph above reaches (llfRemapDown has no node today).
    int compiled = 0;
    for (const auto& entry : std::filesystem::directory_iterator(ORION_SHADER_DIR)) {
        if (entry.path().extension() != ".wgsl") continue;
        const std::string name = entry.path().stem().string();
        try {
            auto lib = gpu::Library::createFromFile(*dawn, entry.path().string());
            auto k = gpu::Kernel::create(*dawn, *lib, name);
            ++compiled;
        } catch (const std::exception& e) {
            report(false, "kernel compiles on Dawn: " + name, e.what());
        }
    }
    report(compiled >= 57, "every kernel compiles on Dawn", std::to_string(compiled) + " compiled");
    std::printf("  %d kernels compile on Dawn\n", compiled);
}
