// Region-of-interest rendering, issue #22: what a drag frame and a settled
// render cost at fit and at 100%, through the Engine the app drives.
//
// "Before ROI" is what the canvas did: the quarter-scale preview graph for
// every drag frame whatever the zoom, and the whole 24 MP graph when the hand
// stops. "ROI" is `Engine::renderRegion` for the 100% view.
#include "bench.h"
#include "Engine.h"

#include <algorithm>
#include <cstdio>
#include <vector>

namespace bench {
namespace {
// A typical first edit: exposure dragged, with sharpening, luma denoise and
// the lens profile on — every one of them a node the region path supports.
orion::pipe::Adjustments typical(const orion::Engine& e) {
    orion::pipe::Adjustments a;
    a.wb = e.develop().asShotWhiteBalance();
    a.contrast = 1.3f;
    a.sharpenAmount = 0.6f;
    a.denoiseLuma = 1.0f;
    a.lensProfile = e.lensProfile().found;
    a.lensModel = e.lensProfile().model;
    return a;
}

// `cold` moves white balance instead of exposure, which dirties every node
// from linearize down: the cost of a fresh window, which is what a pan pays.
template <class F>
Stats drag(orion::Engine& e, orion::pipe::Adjustments a, int frames, F render, bool cold = false) {
    std::vector<double> wall;
    const float k0 = a.wb.temperatureK;
    for (int i = 0; i < frames; ++i) {
        if (cold) a.wb.temperatureK = k0 + 10.0f * float(i % 20);
        else a.exposureEv = 0.02f * float(i % 20);   // never the same value twice in a row
        e.setAdjustments(a);
        const auto t = Clock::now();
        render();
        wall.push_back(msSince(t));
    }
    return summarise(wall);
}

double mib(std::size_t bytes) { return double(bytes) / (1024.0 * 1024.0); }

void row(const char* label, const Stats& s) {
    std::printf("  %-34s median %7.2f ms   p95 %7.2f ms\n", label, s.median, s.p95);
}
}  // namespace

bool roiBench(const std::string& path) {
    orion::Engine e;
    e.openRaw(path);
    const auto a = typical(e);
    e.setAdjustments(a);
    e.render();
    e.renderPreview();
    const auto& d = e.develop();
    std::printf("ROI bench: %ux%u displayed; preview %ux%u\n", d.outputWidth(), d.outputHeight(),
                e.previewDevelop().outputWidth(), e.previewDevelop().outputHeight());

    std::printf("Before ROI (what the canvas does today)\n");
    row("drag frame, preview graph (any zoom)", drag(e, a, 40, [&] { e.renderPreview(); }));
    row("settled, full graph (any zoom)", drag(e, a, 8, [&] { e.render(); }));
    row("whole graph from the mosaic, preview", drag(e, a, 20, [&] { e.renderPreview(); }, true));
    row("whole graph from the mosaic, full", drag(e, a, 8, [&] { e.render(); }, true));
    std::printf("  textures: full %.0f MiB, preview %.0f MiB\n",
                mib(e.develop().graph().allocatedBytes()),
                mib(e.previewDevelop().graph().allocatedBytes()));

    // A 2560 x 1440 canvas (a 27-inch display's worth of device pixels).
    // At 100% one screen pixel is one frame pixel, so the view covers that many
    // of the frame's; at 50% twice as many. Centred.
    constexpr std::uint32_t kSW = 2560, kSH = 1440;
    const float W = float(d.outputWidth()), H = float(d.outputHeight());
    const auto view = [&](double zoom) {
        const float w = std::min(1.0f, float(kSW / zoom) / W), h = std::min(1.0f, float(kSH / zoom) / H);
        return orion::Engine::Region{0.5f - w / 2, 0.5f - h / 2, w, h,
                                     std::uint32_t(w * W * zoom), std::uint32_t(h * H * zoom)};
    };
    for (double zoom : {1.0, 0.5}) {
        const auto v = view(zoom);
        e.renderRegion(v);
        std::printf("ROI at %.0f%%: %ux%u on screen, step %d, window %ux%u\n", zoom * 100, v.outW, v.outH,
                    e.regionStep(), e.regionDevelop().width(), e.regionDevelop().height());
        row("drag frame, region", drag(e, a, 40, [&] { e.renderRegion(v); }));
        row("settled, region (the same render)", drag(e, a, 8, [&] { e.renderRegion(v); }));
        row("whole region graph (a pan)", drag(e, a, 20, [&] { e.renderRegion(v); }, true));
        std::printf("  textures: region %.0f MiB\n", mib(e.regionDevelop().graph().allocatedBytes()));
    }
    // A pan: the window moves by a quarter screen each frame, so every frame
    // reloads the mosaic window and renders the whole region graph.
    std::vector<double> pan;
    auto v = view(1.0);
    for (int i = 0; i < 12; ++i) {
        v.x = 0.1f + 0.05f * float(i % 6);
        const auto t = Clock::now();
        e.renderRegion(v);
        pan.push_back(msSince(t));
    }
    row("pan frame at 100% (reload + render)", summarise(pan));
    return true;
}

}  // namespace bench
