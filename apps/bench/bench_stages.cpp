/*  orion-bench --stages — one picture per stage of the develop graph.
 *
 *  Feeds docs/pipeline-graph, which shows what each node does to a real
 *  photograph. Two passes:
 *
 *    1. Pictures. The mosaic is binned 2x2 first (Bayer phase kept), because
 *       every node is pinned so the pool recycles nothing, and ~200 resident
 *       textures at 24 MP is several GiB. The pictures are previews anyway.
 *    2. Timings. Full resolution, nothing pinned, `setProfiling` on — one
 *       command buffer per node, so the numbers say which node is expensive,
 *       not how long a frame takes (see Pipeline::setProfiling).
 *
 *  Writes JPEGs plus manifest.js (a script, not JSON, so the page works when
 *  opened straight from disk, where fetch() is refused).
 */
#include "bench.h"

#include "util/ImageWriter.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace bench {
namespace {

using orion::gpu::PixelFormat;
using orion::gpu::Texture;

/// How a stage's texture is turned into something a screen can show.
enum class View {
    Linear,   // scene-linear RGB: sRGB transfer curve, no gamut conversion
    Display,  // already display-encoded
    Gray,     // one channel, 0..1, shown as is
    GrayLin,  // one linear channel: sRGB transfer curve
    MinMax,   // one channel of arbitrary range, stretched to fit
};

struct Stage { const char* node; const char* slug; View view; bool crop; };

// One representative node per stage, in graph order. Must name nodes the
// graph actually builds — an unknown name fails the run rather than skipping.
constexpr Stage kStages[] = {
    {"linearize",          "linearize",  View::GrayLin, true},
    {"rcd:red/blue",       "demosaic",   View::Linear,  true},
    {"hl:fill",            "highlights", View::Linear,  false},
    {"denoise:shrink 3",   "denoise",    View::Linear,  true},
    {"lens",               "lens",       View::Linear,  false},
    {"sharpen",            "sharpen",    View::Linear,  true},
    {"camera->working",    "matrix",     View::Linear,  false},
    {"profile:hue/sat",    "profile",    View::Linear,  false},
    {"dehaze",             "dehaze",     View::Linear,  false},
    {"clarity:luma",       "clarity-luma", View::Gray,  false},
    {"clarity",            "clarity",    View::Linear,  false},
    {"fusion",             "fusion",     View::Linear,  false},
    {"guide:blur v2",      "guide",      View::MinMax,  false},
    {"mask:0",             "mask",       View::Gray,    false},
    {"mask0:refine",       "mask-refine", View::Gray,   false},
    {"develop:linear",     "tone",       View::Linear,  false},
    {"grade + vignette",   "grade",      View::Linear,  false},
    {"develop:display",    "display",    View::Display, false},
    {"develop:grain",      "grain",      View::Display, true},
};

constexpr std::uint32_t kPreviewEdge = 2000;  // long edge of the stage pictures
constexpr std::uint32_t kCropEdge    = 360;   // 1:1 detail crops

float srgb(float v) {
    v = std::clamp(v, 0.0f, 1.0f);
    return v <= 0.0031308f ? 12.92f * v : 1.055f * std::pow(v, 1.0f / 2.4f) - 0.055f;
}

/// The texture as float RGB, 3 per pixel, already through its `View`.
std::vector<float> toRgb(const Texture& t, View view, float rawScale, float rawBlack) {
    const std::uint32_t w = t.width(), h = t.height();
    const std::size_t n = std::size_t(w) * h;
    const std::size_t ch = orion::gpu::bytesPerPixel(t.format()) /
        (t.format() == PixelFormat::RGBA8Unorm ? 1 :
         t.format() == PixelFormat::R16Uint || t.format() == PixelFormat::R16Float ||
         t.format() == PixelFormat::RGBA16Float ? 2 : 4);
    std::vector<float> src(n * ch);
    switch (t.format()) {
    case PixelFormat::R16Uint: {
        std::vector<std::uint16_t> v(n);
        t.download(v.data(), std::size_t(w) * 2);
        for (std::size_t i = 0; i < n; ++i) src[i] = (float(v[i]) - rawBlack) * rawScale;
        break;
    }
    case PixelFormat::R16Float: case PixelFormat::RGBA16Float: {
        std::vector<__fp16> v(n * ch);
        t.download(v.data(), std::size_t(w) * ch * 2);
        for (std::size_t i = 0; i < v.size(); ++i) src[i] = float(v[i]);
        break;
    }
    case PixelFormat::RGBA8Unorm: {
        std::vector<std::uint8_t> v(n * 4);
        t.download(v.data(), std::size_t(w) * 4);
        for (std::size_t i = 0; i < v.size(); ++i) src[i] = v[i] / 255.0f;
        break;
    }
    default:
        t.download(src.data(), std::size_t(w) * ch * 4);
    }

    float lo = 0.0f, hi = 1.0f;
    if (view == View::MinMax) {
        lo = 1e30f; hi = -1e30f;
        for (std::size_t i = 0; i < n; ++i) { lo = std::min(lo, src[i * ch]); hi = std::max(hi, src[i * ch]); }
        if (hi <= lo) hi = lo + 1.0f;
    }
    std::vector<float> rgb(n * 3);
    for (std::size_t i = 0; i < n; ++i) {
        for (int c = 0; c < 3; ++c) {
            const float v = src[i * ch + (ch >= 3 ? c : 0)];
            float o = v;
            switch (view) {
            case View::Linear: case View::GrayLin: o = srgb(v); break;
            case View::MinMax: o = (v - lo) / (hi - lo); break;
            default: o = std::clamp(v, 0.0f, 1.0f);
            }
            rgb[i * 3 + c] = o;
        }
    }
    return rgb;
}

void writeRgb(const std::string& path, const std::vector<float>& rgb,
              std::uint32_t w, std::uint32_t h) {
    std::vector<std::uint16_t> px(std::size_t(w) * h * 4);
    for (std::size_t i = 0; i < std::size_t(w) * h; ++i) {
        for (int c = 0; c < 3; ++c) px[i * 4 + c] = std::uint16_t(rgb[i * 3 + c] * 65535.0f + 0.5f);
        px[i * 4 + 3] = 65535;
    }
    const auto jpg = orion::util::encodeJpeg(px.data(), w, h, std::size_t(w) * 8, 0.85f);
    if (jpg.empty()) throw std::runtime_error("JPEG encode failed: " + path);
    std::ofstream(path, std::ios::binary).write(reinterpret_cast<const char*>(jpg.data()), std::streamsize(jpg.size()));
}

/// Box-averaged down to kPreviewEdge on the long side.
void writePreview(const std::string& path, const std::vector<float>& rgb,
                  std::uint32_t w, std::uint32_t h, std::uint32_t& ow, std::uint32_t& oh) {
    const std::uint32_t f = std::max(1u, (std::max(w, h) + kPreviewEdge - 1) / kPreviewEdge);
    ow = w / f; oh = h / f;
    std::vector<float> out(std::size_t(ow) * oh * 3, 0.0f);
    for (std::uint32_t y = 0; y < oh * f; ++y)
        for (std::uint32_t x = 0; x < ow * f; ++x)
            for (int c = 0; c < 3; ++c)
                out[(std::size_t(y / f) * ow + x / f) * 3 + c] += rgb[(std::size_t(y) * w + x) * 3 + c];
    for (float& v : out) v /= float(f * f);
    writeRgb(path, out, ow, oh);
}

/// A 1:1 crop around (cx, cy), given as fractions of the frame.
void writeCrop(const std::string& path, const std::vector<float>& rgb,
               std::uint32_t w, std::uint32_t h, float cx, float cy) {
    const std::uint32_t e = std::min({kCropEdge, w, h});
    const std::uint32_t x0 = std::min(w - e, std::uint32_t(std::max(0.0f, cx * w - e / 2.0f)));
    const std::uint32_t y0 = std::min(h - e, std::uint32_t(std::max(0.0f, cy * h - e / 2.0f)));
    std::vector<float> out(std::size_t(e) * e * 3);
    for (std::uint32_t y = 0; y < e; ++y)
        std::copy_n(&rgb[(std::size_t(y0 + y) * w + x0) * 3], e * 3, &out[std::size_t(y) * e * 3]);
    writeRgb(path, out, e, e);
}

/// Where the subject is, as fractions of the frame: the centroid of the
/// brightest tenth of the luma range. Crude, and right for the bright subject
/// on a dark ground that the samples are; detail crops and the page's zoom
/// both centre on it.
// ponytail: brightest-region heuristic; pass a position on the command line
// if a sample ever needs a subject that is not its highlight.
std::pair<float, float> subjectOf(const std::vector<float>& rgb, std::uint32_t w, std::uint32_t h) {
    float hi = 0.0f;
    for (std::size_t i = 0; i < std::size_t(w) * h; ++i) hi = std::max(hi, rgb[i * 3 + 1]);
    double sx = 0, sy = 0, n = 0;
    for (std::uint32_t y = 0; y < h; y += 2)
        for (std::uint32_t x = 0; x < w; x += 2)
            if (rgb[(std::size_t(y) * w + x) * 3 + 1] >= 0.6f * hi) { sx += x; sy += y; n += 1; }
    return n > 0 ? std::pair{float(sx / n / w), float(sy / n / h)} : std::pair{0.5f, 0.5f};
}

/// Half resolution, same CFA phase: each output sample averages the four
/// same-color samples of the 4x4 block it stands for.
orion::raw::BayerImage bin2(const orion::raw::BayerImage& s) {
    orion::raw::BayerImage d = s;
    d.width = (s.width / 4) * 2;
    d.height = (s.height / 4) * 2;
    d.samples.assign(std::size_t(d.width) * d.height, 0);
    for (std::uint32_t y = 0; y < d.height; ++y) {
        const std::uint32_t by = (y & ~1u) * 2 + (y & 1);
        for (std::uint32_t x = 0; x < d.width; ++x) {
            const std::uint32_t bx = (x & ~1u) * 2 + (x & 1);
            const auto at = [&](std::uint32_t xx, std::uint32_t yy) { return std::uint32_t(s.samples[std::size_t(yy) * s.width + xx]); };
            d.samples[std::size_t(y) * d.width + x] =
                std::uint16_t((at(bx, by) + at(bx + 2, by) + at(bx, by + 2) + at(bx + 2, by + 2) + 2) / 4);
        }
    }
    return d;
}

/// A look that makes every stage do something visible. Spots are left off:
/// they need coordinates on a particular photograph.
orion::pipe::Adjustments demoLook() {
    orion::pipe::Adjustments a;
    a.highlightRecovery = 0.5f;
    a.denoiseLuma = 1.5f;
    a.denoiseColor = 1.0f;
    a.lensDistortion = 0.15f;
    a.sharpenAmount = 1.0f;
    a.dehaze = 0.1f;
    a.clarity = 0.35f;
    a.fusion = 0.3f;
    a.exposureEv = -0.3f;
    a.highlights = -0.15f;
    a.shadows = 0.15f;
    a.vibrance = 0.2f;
    a.gradeShadow[0] = -0.15f; a.gradeShadow[1] = -0.1f;      // cool shadows
    a.gradeHighlight[0] = 0.12f; a.gradeHighlight[1] = 0.08f; // warm highlights
    a.vignetteAmount = -0.8f;
    a.contrast = 1.05f;
    a.grainAmount = 0.03f;
    a.straightenDeg = 2.0f;
    // One linear gradient from the top, darkening the sky by a stop.
    a.maskCount = 1;
    a.maskComponents[0].kind = 1;
    a.maskComponents[0].startsLayer = true;
    a.maskComponents[0].center[0] = 0.5f; a.maskComponents[0].center[1] = 0.3f;
    a.maskComponents[0].angle = -1.5708f;
    a.maskRefine = 0.6f;
    a.layers[0].exposureEv = -1.0f;
    return a;
}

std::string jsonEscape(const std::string& s) {
    std::string o;
    for (char c : s) { if (c == '"' || c == '\\') o += '\\'; o += c; }
    return o;
}

}  // namespace

bool stages(const std::string& path, const std::string& outDir, bool demo) {
    namespace fs = std::filesystem;
    fs::create_directories(outDir);
    const auto full = orion::raw::decodeBayer(path);
    const auto img = bin2(full);
    auto device = orion::gpu::Device::create();
    // The app's own default (Engine.swift: contrast 1.45), unless asked for
    // the demo look that switches every stage on.
    orion::pipe::Adjustments look;
    look.contrast = 1.45f;
    if (demo) look = demoLook();

    std::string js = "window.ORION_STAGES = {\n  camera: \"" + jsonEscape(full.camera) + "\",\n";
    js += "  previewSize: [" + std::to_string(img.width) + ", " + std::to_string(img.height) + "],\n";
    js += "  fullSize: [" + std::to_string(full.width) + ", " + std::to_string(full.height) + "],\n";
    js += "  stages: [\n";

    {
        orion::pipe::DevelopPipeline d(*device, ORION_SHADER_DIR, img);
        auto& g = d.graph();
        std::vector<int> all(g.nodeCount());
        std::map<std::string, int> ids;
        for (int i = 0; i < int(all.size()); ++i) { all[i] = i; ids[g.nodeName(i)] = i; }
        g.setPinned(all);  // nothing recycled, so every node can be read back
        d.apply(look);
        d.render();
        d.render();  // dehaze picks its airlight from the first render's candidates

        const float rawScale = 1.0f / float(std::max(1, int(img.white) - int(img.black[0])));
        const auto [cx, cy] = subjectOf(toRgb(g.sourceTexture(), View::Gray, rawScale, float(img.black[0])),
                                        img.width, img.height);
        js.insert(js.find("  stages: ["), "  subject: [" + std::to_string(cx) + ", " + std::to_string(cy) + "],\n");
        const auto emit = [&](const std::string& node, const std::string& slug, const Texture& t,
                              View view, bool crop) {
            const auto rgb = toRgb(t, view, rawScale, float(img.black[0]));
            std::uint32_t ow, oh;
            writePreview(outDir + "/" + slug + ".jpg", rgb, t.width(), t.height(), ow, oh);
            if (crop) writeCrop(outDir + "/" + slug + "-crop.jpg", rgb, t.width(), t.height(), cx, cy);
            js += "    { node: \"" + jsonEscape(node) + "\", file: \"" + slug + ".jpg\", crop: " +
                  (crop ? "\"" + slug + "-crop.jpg\"" : std::string("null")) +
                  ", size: [" + std::to_string(t.width()) + ", " + std::to_string(t.height()) + "]" +
                  ", format: \"" + orion::gpu::formatName(t.format()) + "\" },\n";
            std::printf("  %-20s %ux%u -> %s.jpg\n", node.c_str(), ow, oh, slug.c_str());
        };

        emit("source mosaic", "source", g.sourceTexture(), View::GrayLin, true);
        for (const Stage& s : kStages) {
            const auto it = ids.find(s.node);
            if (it == ids.end()) { std::fprintf(stderr, "no node named '%s'\n", s.node); return false; }
            emit(s.node, s.slug, g.nodeOutput(it->second), s.view, s.crop);
        }
        // The final picture is the geometry node's visible region, not its
        // worst-case allocation.
        const std::uint32_t w = d.outputWidth(), h = d.outputHeight();
        const auto px = output16(d, w, h);
        std::vector<float> rgb(std::size_t(w) * h * 3);
        for (std::size_t i = 0; i < std::size_t(w) * h; ++i)
            for (int c = 0; c < 3; ++c) rgb[i * 3 + c] = px[i * 4 + c] / 65535.0f;
        std::uint32_t ow, oh;
        writePreview(outDir + "/geometry.jpg", rgb, w, h, ow, oh);
        js += "    { node: \"geometry\", file: \"geometry.jpg\", crop: null, size: [" +
              std::to_string(w) + ", " + std::to_string(h) + "], format: \"rgba8\" },\n";
    }
    js += "  ],\n  timings: {\n";

    {
        orion::pipe::DevelopPipeline d(*device, ORION_SHADER_DIR, full);
        d.apply(look);
        d.render();
        d.render();
        d.graph().setProfiling(true);
        d.reload(full);  // marks the whole graph dirty
        d.apply(look);
        // The graph's own render, not DevelopPipeline's: that one runs a
        // second pass once dehaze has picked its airlight, and lastRun()
        // keeps only the second. The airlight is already set from above.
        d.graph().render();
        double total = 0.0;
        for (const auto& t : d.graph().lastRun()) {
            if (!t.executed) continue;
            js += "    \"" + jsonEscape(t.name) + "\": " + std::to_string(t.ms) + ",\n";
            total += t.ms;
        }
        std::printf("  profiled %zu nodes at full resolution, %.1f ms serialized\n",
                    d.graph().lastRun().size(), total);
    }
    js += "  },\n};\n";
    std::ofstream(outDir + "/manifest.js") << js;
    std::printf("  wrote %s/manifest.js\n", outDir.c_str());
    return true;
}

}  // namespace bench
