// The white balance eyedropper and Auto. research/white-balance.md.

#include "Engine.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace orion {

bool Engine::whiteBalanceAt(float u, float v, float radius, pipe::Adjustments& adj) {
    if (!develop_) throw std::runtime_error("no image open");
    // The preview graph, a sixteenth of the pixels, because the solve renders
    // once a pass; the patch is a fraction of the frame, so it covers the same
    // part of the picture in either graph.
    pipe::DevelopPipeline& graph = preview_ ? *preview_ : *develop_;
    const auto run = [&](const pipe::Adjustments& a) {
        graph.apply(a);
        if (preview_) renderPreview();
        else render();
        previewCurrent_ = fullCurrent_ = regionCurrent_ = false;   // none has had the caller's edit
    };

    // The solve reads the camera RGB back through the profile's hue/saturation
    // table as if it were the identity, which it is only on the neutral axis,
    // so a patch that starts off neutral lands near it, and solving again from
    // there converges.
    constexpr int kPasses = 4;
    pipe::Adjustments trial = adj;
    const auto clamp01 = [](float x) { return std::clamp(x, 0.0f, 1.0f); };
    try {
        for (int pass = 0; pass < kPasses; ++pass) {
            run(trial);
            const auto f = graph.displayedToFrame(clamp01(u), clamp01(v));
            const int r = radius < 0.0f ? -1 : std::max(1, int(std::lround(radius * float(graph.width()))));
            const auto wb = graph.neutralWhiteBalance(f.first, f.second, r);
            if (!wb) {
                run(adj_);
                return false;
            }
            const bool settled = std::abs(wb->temperatureK - trial.wb.temperatureK) < 1.0f &&
                                 std::abs(wb->tint - trial.wb.tint) < 0.001f;
            trial.wb = *wb;
            if (settled) break;
        }
    } catch (...) {
        graph.apply(adj_);   // a trial's values are nobody's edit
        throw;
    }
    adj.wb = trial.wb;
    return true;
}

}  // namespace orion
