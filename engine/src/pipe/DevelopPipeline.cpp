#include "pipe/DevelopPipeline.h"

#include "raw/RawImage.h"

#include <cmath>
#include <string>

namespace orion::pipe {

// The flip-to-turns mapping lives in raw/RawImage.h: the thumbnail path needs
// the same answer, and two copies of an EXIF table is how a filmstrip and a
// canvas come to disagree about which way is up.
using orion::raw::quarterTurnsFor;

/*  The spine.
 *
 *  Two lists, and almost nothing else: the nodes the graph is built from, in
 *  graph order, and the parameter blocks a frame pushes, in push order. The
 *  bodies live in `DevelopCapture.cpp`, `DevelopLocal.cpp`, `DevelopMask.cpp`
 *  and `DevelopOutput.cpp`, each of which holds *both* halves of its region.
 *  Decision #113 and the table in the header.
 */

DevelopPipeline::DevelopPipeline(gpu::Device& device, const std::string& shaderDir,
                                 std::uint32_t width, std::uint32_t height,
                                 bool linearSource)
    : pipeline_(device, shaderDir), width_(width), height_(height),
      linearSource_(linearSource) {

    // A linear source arrives demosaiced: RGBA half data instead of a
    // 16-bit mosaic sample per pixel. Declared before compile, which is
    // where the texture is allocated.
    if (linearSource_) {
        pipeline_.setSourceFormat(gpu::PixelFormat::RGBA16Float);
    }

    buildCaptureNodes();
    buildLocalNodes();
    buildMaskNodes();
    buildOutputNodes();

    pipeline_.compile(width_, height_);

    // ⚠ **What `nodeOutput` hands out after the render, and a liveness walk
    // cannot see.** `DevelopLocal` reads these two once the graph has finished
    // — the fusion proxy for its histogram and the dehaze peak for its
    // estimate — so neither may ever be recycled into a later node. A pool that
    // missed this would give those readers another node's pixels: right size,
    // right format, wrong picture. Decision #156.
    //
    // The final output and the source are pinned structurally by `Pipeline`
    // itself and are deliberately not repeated here.
    pipeline_.setPinned({nFuseProxy_, nPeak_});

    // Once per graph. The plate is 33 MB and identical for every photograph.
    uploadGrainPlate();
}

DevelopPipeline::DevelopPipeline(gpu::Device& device, const std::string& shaderDir,
                                 const raw::BayerImage& image)
    : DevelopPipeline(device, shaderDir, image.width, image.height, false) {
    applyImageParams(image);
    pipeline_.setSource(image.samples.data(), static_cast<std::size_t>(width_) * 2);
}

DevelopPipeline::DevelopPipeline(gpu::Device& device, const std::string& shaderDir,
                                 const raw::LinearImage& image)
    : DevelopPipeline(device, shaderDir, image.width, image.height, true) {
    applyImageParams(image);
    // fp16 RGBA: eight bytes per pixel.
    pipeline_.setSource(image.rgba.data(), static_cast<std::size_t>(width_) * 8);
}

bool DevelopPipeline::canReload(const raw::BayerImage& image) const noexcept {
    return !linearSource_ && image.width == width_ && image.height == height_ &&
           image.filters == filters_;
}

bool DevelopPipeline::canReload(const raw::LinearImage& image) const noexcept {
    return linearSource_ && image.width == width_ && image.height == height_;
}

void DevelopPipeline::clearPaintState() {
    // ⚠ A reload is a *different photograph* through the same compiled graph,
    // and paint and mattes are the two pieces of state `Adjustments` does not
    // carry — so nothing above would replace them. A matte in particular is not
    // written to a sidecar, so the second frame of a folder would open with a
    // Subject row that had never been run on it, quietly covered by the
    // previous photo's subject. Cleared here rather than left to the caller:
    // the app happens to re-send every stroke on open, and "happens to" is not
    // an invariant.
    for (int i = 0; i < kMaxMaskComponents; ++i) {
        brushDabs_[std::size_t(i)].clear();
        brushErase_[std::size_t(i)].clear();
        // ⚠ And the record of what was uploaded, for the same reason: a
        // different photograph through the same graph shares nothing with the
        // stroke that was on it, and the second file of a folder opening with
        // "the prefix is unchanged" would keep the first one's coverage.
        brushPrev_[std::size_t(i)] = {};
        brushPending_[std::size_t(i)] = {};
        brushPrefix_[std::size_t(i)] = {};
        // ⚠ And the accumulator's ownership. The texture keeps the previous
        // photograph's coverage until something overwrites it, so a component
        // that stayed a brush across the reload would otherwise continue a
        // stroke laid on a different picture.
        maskParams_[std::size_t(i)].firstDab = 0;
        maskParams_[std::size_t(i)].accumUse = 0;
        matteLive_[i][0] = 0;
        matteLive_[i][1] = 0;
        matteDirty_[i] = true;
    }
    accumOwner_ = -1;
}

void DevelopPipeline::reload(const raw::BayerImage& image) {
    clearPaintState();

    applyImageParams(image);

    // Force every parameter block to be re-pushed: the new file has different
    // black levels, white balance and color matrix, and `primed_` would
    // otherwise suppress writes whose values happen to match.
    primed_ = false;
    Adjustments initial;
    initial.wb = asShot_;
    apply(initial);

    pipeline_.setSource(image.samples.data(), static_cast<std::size_t>(width_) * 2);
}

void DevelopPipeline::reload(const raw::LinearImage& image) {
    clearPaintState();

    applyImageParams(image);

    // Same reasoning as the mosaic reload, one line up.
    primed_ = false;
    Adjustments initial;
    initial.wb = asShot_;
    apply(initial);

    pipeline_.setSource(image.rgba.data(), static_cast<std::size_t>(width_) * 8);
}

void DevelopPipeline::gradeOffsets(float x, float y, float out[3]) noexcept {
    // The puck's angle picks a hue; its distance from the center picks how far.
    // Each primary contributes by the cosine of its angular distance, which is
    // the standard three-phase decomposition a color wheel implies.
    const float radius = std::min(std::sqrt(x * x + y * y), 1.0f);
    if (radius < 1e-6f) {
        out[0] = out[1] = out[2] = 0.0f;
        return;
    }

    const float theta = std::atan2(y, x);
    constexpr float kTwoPiOverThree = 2.0943951023931953f;   // 120 degrees
    constexpr float kStrength = 0.25f;

    float v[3];
    for (int c = 0; c < 3; ++c) {
        v[c] = std::cos(theta - float(c) * kTwoPiOverThree);
    }

    // Subtracting the mean is what makes this a color control. Without it,
    // pushing toward yellow also lifts the zone, so every wheel fights the
    // exposure slider and a neutral gray no longer keeps its luminance.
    //
    // It also means some component of every offset is negative, which is why
    // kStrength is 0.03 and not the 0.15 it started at. This is scene-linear
    // light: a dark patch sits around 0.005, so a 0.15 offset drove two of its
    // channels straight through zero and the shader's clamp held them there.
    // Measured on a night frame, the shadow patch came back at luma 0.12 with
    // 0.15 and 0.22 with 0.03 — the larger number was *darker*, because it was
    // crushing channels to black rather than tinting them.
    const float mean = (v[0] + v[1] + v[2]) / 3.0f;
    for (int c = 0; c < 3; ++c) {
        out[c] = kStrength * radius * (v[c] - mean);
    }
}

DevelopPipeline::Circle DevelopPipeline::compositionCircle(
        const Adjustments& adj, int exifQuarters,
        std::uint32_t width, std::uint32_t height) noexcept {
    // The same clamps `geometry.slang`'s parameters get, because a rectangle
    // this disagreed with would put the vignette somewhere the crop is not.
    const float cw = std::clamp(adj.cropW, 0.01f, 1.0f);
    const float ch = std::clamp(adj.cropH, 0.01f, 1.0f);
    const float cx = std::clamp(adj.cropX, 0.0f, 1.0f - cw);
    const float cy = std::clamp(adj.cropY, 0.0f, 1.0f - ch);

    const int turns = ((exifQuarters + adj.rotateQuarters) % 4 + 4) % 4;
    const bool swap = (turns % 2) != 0;

    const float w = static_cast<float>(std::max(width, 1u));
    const float h = static_cast<float>(std::max(height, 1u));
    const float rotW = swap ? h : w;
    const float rotH = swap ? w : h;

    // The crop's center, in rotated-frame pixels. `geometry` walks an output
    // pixel to `(cropOrigin + t*cropSize) * rotated - 0.5`; this is that at the
    // middle of the output, which is the middle of the rectangle.
    float px = (cx + cw * 0.5f) * rotW - 0.5f;
    float py = (cy + ch * 0.5f) * rotH - 0.5f;

    // Straighten, about the frame's center — the same pivot `apply` passes.
    // ⚠ Forward, not inverted: `geometry` maps output to source, so the
    // rectangle's center in *source* space is the output center taken through
    // that same map, not through its inverse.
    if (std::abs(adj.straightenDeg) > 1e-6f) {
        const float rad = adj.straightenDeg * 3.14159265358979f / 180.0f;
        const float s = std::sin(rad), c = std::cos(rad);
        const float dx = px - rotW * 0.5f;
        const float dy = py - rotH * 0.5f;
        px = rotW * 0.5f + (dx * c - dy * s);
        py = rotH * 0.5f + (dx * s + dy * c);
    }

    // Undo the quarter turns, exactly as the kernel does, to land on the grid
    // every upstream node runs on.
    const float inMaxX = w - 1.0f, inMaxY = h - 1.0f;
    float sx = px, sy = py;
    switch (turns) {
        case 1:  sx = py;          sy = inMaxY - px; break;
        case 2:  sx = inMaxX - px; sy = inMaxY - py; break;
        case 3:  sx = inMaxX - py; sy = px;          break;
        default: break;
    }

    Circle out{};
    // Pixel index back to a continuous coordinate, then normalized: index i is
    // the center of the interval [i, i+1).
    out.centerX = (sx + 0.5f) / w;
    out.centerY = (sy + 0.5f) / h;

    // Half the rectangle's diagonal, in units of the frame's height. Rotation
    // cannot change a length, so this needs none of the arithmetic above.
    const float dw = cw * rotW, dh = ch * rotH;
    out.radius = 0.5f * std::sqrt(dw * dw + dh * dh) / h;
    return out;
}

float DevelopPipeline::whiteClipFor(const float multipliers[3]) const noexcept {
    // Each channel saturates where its own sensor reading runs out, scaled by
    // the gain white balance gives it:
    //
    //     T_k = (W − B_k) / (W − B_ref) · m_k
    //
    // The lowest of the three is the brightest neutral the frame can still
    // describe. Above it a channel is claiming more of one primary than a white
    // at full brightness, which the white point does not admit — and that claim
    // is exactly what a blown highlight makes, in the shape of the white
    // balance gains, which is why an unclipped one comes out magenta.
    float clip = 0.0f;
    for (int c = 0; c < 3; ++c) {
        const float level =
            (whiteLevel_ - blackLevel_[c]) * linBase_.invRange * multipliers[c];
        clip = (c == 0) ? level : std::min(clip, level);
    }
    // A frame whose black point sits at the white point is not a frame; clipping
    // to zero would render it black rather than admit that.
    return std::max(clip, 1e-3f);
}

DevelopPipeline::HighlightStages DevelopPipeline::highlightStages() const {
    HighlightStages s;
    s.input  = &pipeline_.nodeOutput(nRgb_);
    s.output = &pipeline_.nodeOutput(nHighlights_);
    s.filled = &pipeline_.nodeOutput(nHlFill_);
    s.clip   = lastWhiteClip_;
    s.gamma  = hlfill::kClipGamma;
    return s;
}

std::pair<float, float> DevelopPipeline::displayedToFrame(float x, float y) const {
    const mask::Crop crop{lastAdj_.cropX, lastAdj_.cropY,
                          lastAdj_.cropW, lastAdj_.cropH};
    const bool swaps = (turns_ % 2) != 0;
    const float rotW = float(swaps ? height_ : width_);
    const float rotH = float(swaps ? width_  : height_);
    // ⚠ Through the same homography a mask goes through. A spot is stored in
    // frame coordinates and converted once when it is placed, so a perspective
    // correction that reached the picture and not this call would put every
    // spot placed afterwards on the wrong piece of dust — silently, because a
    // misplaced heal still looks like a heal.
    const auto p = mask::toFrame(
        {x, y, 0.0f}, crop, turns_,
        lastAdj_.straightenDeg * 3.14159265358979324f / 180.0f,
        lastAdj_.cropX + lastAdj_.cropW * 0.5f,
        lastAdj_.cropY + lastAdj_.cropH * 0.5f, rotW, rotH,
        persp::isIdentity(perspective_) ? nullptr : &perspective_);
    return {p.centerX, p.centerY};
}

std::pair<float, float> DevelopPipeline::frameToDisplayed(float x, float y) const {
    const mask::Crop crop{lastAdj_.cropX, lastAdj_.cropY,
                          lastAdj_.cropW, lastAdj_.cropH};
    const bool swaps = (turns_ % 2) != 0;
    const float rotW = float(swaps ? height_ : width_);
    const float rotH = float(swaps ? width_  : height_);
    const auto p = mask::fromFrame(
        {x, y, 0.0f}, crop, turns_,
        lastAdj_.straightenDeg * 3.14159265358979324f / 180.0f,
        lastAdj_.cropX + lastAdj_.cropW * 0.5f,
        lastAdj_.cropY + lastAdj_.cropH * 0.5f, rotW, rotH,
        persp::isIdentity(perspectiveInverse_) ? nullptr : &perspectiveInverse_);
    return {p.centerX, p.centerY};
}

void DevelopPipeline::applyImageParams(const raw::BayerImage& image) {
    exifQuarters_ = quarterTurnsFor(image.flip);
    filters_      = image.filters;

    pushStaticCaptureParams(image);
    pushStaticLocalParams();
    pushStaticMaskParams();

    Adjustments initial;
    initial.wb = asShot_;
    apply(initial);
}

void DevelopPipeline::applyImageParams(const raw::LinearImage& image) {
    exifQuarters_ = quarterTurnsFor(image.flip);
    filters_      = 0;   // no mosaic; nothing downstream may index a pattern

    pushStaticCaptureParams(image);
    pushStaticLocalParams();
    pushStaticMaskParams();

    Adjustments initial;
    initial.wb = asShot_;
    apply(initial);
}

/// Everything one `apply` derives once and more than one stage then reads.
///
/// ⚠ **This is the only place any of it is derived.** A second "the same map
/// but in the other space" is how a mask ends up a few percent off its subject
/// on a corrected photograph, and a second "did the frame move" is how a stroke
/// stops following the hand.
bool DevelopPipeline::guideNeeded(const Adjustments& adj) {
    if (adj.highlights != 0.0f || adj.shadows != 0.0f) return true;
    for (const auto& l : adj.layers)
        if (l.highlights != 0.0f || l.shadows != 0.0f) return true;
    return false;
}

DevelopPipeline::ApplyContext
DevelopPipeline::contextFor(const Adjustments& adj) {
    ApplyContext ctx;

    // Only push what actually moved. setParams dirties the whole downstream
    // subgraph, so pushing every block on every tick would make dragging the
    // curve also recompute exposure and AgX — three nodes of work for a
    // one-node change, and the difference between 4 ms and 12 ms.
    ctx.first = !primed_;
    const bool first = ctx.first;

    // ── Perspective ─────────────────────────────────────────────────────
    //
    // Composed here, before anything reads it, because three things do: the
    // geometry node's parameter block, every mask and brush dab on the way into
    // frame coordinates, and `displayedToFrame` for a spot. One matrix, one
    // derivation — a second "the same map but in the other space" is how a mask
    // ends up a few percent off its subject on a corrected photograph.
    //
    // The keystone is a function of the three controls and the *rotated* frame's
    // shape, so it is recomputed when either moves and at no other time. Solving
    // an 8x8 system per slider tick would be nothing next to the graph, but the
    // point of the latch is decision #92: an unchanged block still dirties the
    // node it is pushed to.
    if (first ||
        adj.perspectiveVertical   != lastAdj_.perspectiveVertical ||
        adj.perspectiveHorizontal != lastAdj_.perspectiveHorizontal ||
        adj.perspectiveAspect     != lastAdj_.perspectiveAspect ||
        adj.rotateQuarters        != lastAdj_.rotateQuarters) {
        const int t = ((exifQuarters_ + adj.rotateQuarters) % 4 + 4) % 4;
        const bool swapsAxes = (t % 2) != 0;
        perspective_ = persp::compose(
            {adj.perspectiveVertical, adj.perspectiveHorizontal,
             adj.perspectiveAspect},
            float(swapsAxes ? height_ : width_),
            float(swapsAxes ? width_  : height_));
        perspectiveInverse_ = persp::inverse(perspective_);
    }
    // Null rather than an identity matrix, so the neutral case does no work at
    // all rather than work that happens to come out neutral.
    const persp::Matrix3* perspective =
        persp::isIdentity(perspective_) ? nullptr : &perspective_;
    ctx.perspective = perspective;

    // The mask group. Each component gets its own staleness, so painting on one
    // does not re-stamp the others and a gradient slider does not re-walk a
    // stroke. The geometry that every component shares — the crop, the turns and
    // the straighten — is computed once out here rather than four times.
    const bool frameMoved =
        first ||
        adj.cropX != lastAdj_.cropX || adj.cropY != lastAdj_.cropY ||
        adj.cropW != lastAdj_.cropW || adj.cropH != lastAdj_.cropH ||
        adj.rotateQuarters != lastAdj_.rotateQuarters ||
        adj.straightenDeg != lastAdj_.straightenDeg ||
        // ⚠ Perspective is in here for the same reason the crop is: every mask
        // center and every brush dab goes through it, so moving it moves all of
        // them. Leaving it out is a stroke that stops following the hand — the
        // exact shape of the bug the crop entry was added for.
        adj.perspectiveVertical   != lastAdj_.perspectiveVertical ||
        adj.perspectiveHorizontal != lastAdj_.perspectiveHorizontal ||
        adj.perspectiveAspect     != lastAdj_.perspectiveAspect;

    // The mask is placed on the picture the photographer is looking at, which is
    // cropped and rotated; it is applied before the geometry node, which sees
    // neither. Without this a mask slides off its subject the moment the frame is
    // turned. pipe/MaskGeometry.h.
    const mask::Crop crop{adj.cropX, adj.cropY, adj.cropW, adj.cropH};
    const int turns = ((exifQuarters_ + adj.rotateQuarters) % 4 + 4) % 4;
    // The straighten pivot and the rotated frame's shape, both as the geometry
    // shader sees them — the rotation happens in that frame's pixels, so its
    // aspect is part of the transform.
    const bool swaps = (turns % 2) != 0;
    const float rotW = float(swaps ? height_ : width_);
    const float rotH = float(swaps ? width_  : height_);
    ctx.crop  = crop;
    ctx.turns = turns;
    ctx.rotW  = rotW;
    ctx.rotH  = rotH;
    ctx.frameMoved = frameMoved;

    bool visibilityMoved = first || adj.maskCount != lastAdj_.maskCount;
    for (int i = 0; !visibilityMoved && i < kMaxMaskComponents; ++i) {
        visibilityMoved = adj.maskComponents[std::size_t(i)].hidden
                       != lastAdj_.maskComponents[std::size_t(i)].hidden;
    }
    ctx.visibilityMoved = visibilityMoved;

    // The guided filter is six nodes and only feeds the highlight and shadow
    // bands — the global pair and every layer's, which is why the predicate
    // walks the layers too. With all of them at zero it is pure cost, and
    // white balance — which rewrites the head of the graph and reruns
    // everything — pays it on every tick. Skipping it takes a temperature
    // drag from sixteen nodes to ten.
    const bool needsGuide = guideNeeded(adj);
    ctx.needsGuide = needsGuide;

    const bool refining = adj.maskCount > 0 && adj.maskRefine > 0.0f;
    ctx.refining = refining;

    return ctx;
}

/// Pushes the current adjustments into the graph, dirtying only what they
/// affect.
///
/// ⚠ **The order below is the order the blocks were pushed in before the split,
/// it must not be sorted, and that is measured rather than assumed.** The split
/// preserved it on the argument that "the stages push to distinct nodes so it
/// probably does not matter" is a claim about thirteen hundred lines. Then
/// mutation M8 moved `applyTone` two places later, past `applyOutput`, and
/// **the rendered bytes changed**, on the geometry and crop-preview frames.
/// `orion-tests` and the whole bench stayed green; only the byte comparison saw
/// it. ⚠ **Why those two frames and not the other eight is not diagnosed** —
/// `applyOutput` is the one stage that can reallocate a texture, through
/// `retargetOutputChain`, which makes it the place to start looking, but that
/// is a guess and is written here as one. The measurement stands on its own:
/// reordering this list is a picture change, not a tidy-up.
///
/// `lastAdj_` stays last, because every stage reads it.
void DevelopPipeline::apply(const Adjustments& adj) {
    const ApplyContext ctx = contextFor(adj);

    applyWhiteBalance(adj, ctx);
    applyHighlights(adj, ctx);
    applyGrade(adj, ctx);
    applyLens(adj, ctx);
    applyDenoise(adj, ctx);
    applyDehaze(adj, ctx);
    applyFusion(adj, ctx);
    applyClarity(adj, ctx);
    applyMaskComponents(adj, ctx);
    applyGuide(adj, ctx);
    applySpots(adj, ctx);
    applyMaskRefine(adj, ctx);
    applyTone(adj, ctx);
    applySharpen(adj, ctx);
    applyOutput(adj, ctx);
    applyGeometry(adj, ctx);

    lastAdj_ = adj;
    primed_  = true;
}

double DevelopPipeline::renderOnce() {
    reconcileBrushAccum();
    const double ms = pipeline_.render();
    commitBrushAccum();
    return ms;
}

double DevelopPipeline::render() {
    // A is a reduction over the whole frame, so it cannot be a node, and every
    // node downstream of it needs it before it runs. When it is stale the graph
    // is rendered once to produce the pooled candidates, A is read back, and
    // the parameters that depend on it are pushed — the per-node cache then
    // makes the second pass redo only what those parameters touched.
    //
    // Stale means the image changed or white balance moved. It is never stale
    // because a slider moved, so this does not run on the interaction path.
    const bool needsAirlight = dehazing_ && !airlightValid_;
    const bool needsPlan     = fusing_ && !fusePlanValid_;

    if (needsAirlight || needsPlan) {
        const double first = renderOnce();
        if (needsAirlight) estimateAirlight();
        if (needsPlan)     estimateFusionPlan();
        return first + renderOnce();
    }
    return renderOnce();
}

}  // namespace orion::pipe
