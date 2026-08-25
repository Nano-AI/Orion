/*  From the tone controls to the screen.
 *
 *  `develop:linear` — every scene-linear adjustment plus the mask group's
 *  per-layer edits, in one dispatch — then the grade and creative vignette that
 *  share a kernel, the display transform with its curve and creative LUT, film
 *  grain, and the geometry node that crops, straightens, turns and keystones.
 *
 *  ⚠ Which of `develop:display` and `develop:grain` writes the eight bits, and
 *  therefore which one dithers, is decided in exactly one function here.
 *  Decision #113.
 */
#include "pipe/DevelopPipeline.h"

#include "pipe/ShaderParams.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "pipe/DevelopInternal.h"

namespace orion::pipe {

namespace {

bool sameChannel(const CurveChannel& a, const CurveChannel& b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (std::abs(a[i].x - b[i].x) > 1e-6f) return false;
        if (std::abs(a[i].y - b[i].y) > 1e-6f) return false;
    }
    return true;
}

bool sameCurve(const ToneCurveSpec& a, const ToneCurveSpec& b) {
    return sameChannel(a.master, b.master) && sameChannel(a.red, b.red) &&
           sameChannel(a.green, b.green) && sameChannel(a.blue, b.blue);
}

}  // namespace

void DevelopPipeline::buildOutputNodes() {
    using gpu::PixelFormat;

    nLinear_    = pipeline_.add({"develop:linear", "developLinear",
                                 {nFusion_, nGuideV2_, nGuidePrep_,
                                  nMaskRefine_[0], nMaskRefine_[1],
                                  nMaskRefine_[2], nMaskRefine_[3]},
                                 PixelFormat::RGBA16Float, {}});

    // Grading sits after the tone controls and before the display transform,
    // in scene-linear light. After the transform — where a color balance
    // control usually lives in a display-referred editor — the same offset
    // would do something different to a highlight than to a midtone for
    // reasons unrelated to the zone the user picked.
    //
    // The creative vignette is in the same kernel and the node is named for
    // both. It is pointwise and wants the same light, and a node of its own
    // would be a second full-resolution round trip for six lines of arithmetic
    // — the trade the creative LUT already lost inside `develop:display`.
    // research/vignette.md, decision #103.
    nGrade_     = pipeline_.add({"grade + vignette", "colorGrade", {nLinear_},
                                 PixelFormat::RGBA16Float, {}});

    auxCurveLut_ = pipeline_.addAuxTexture(kCurveResolution, kCurveRows,
                                           PixelFormat::R32Float);
    // Sixteen bits per channel, not eight. The display transform's output is
    // the last thing that happens to a pixel, so eight bits there caps every
    // export at eight bits whatever container it goes into — and a gradient
    // that survived the whole pipeline in float gets quantised on the way out.
    // Half float rather than 16-bit integer because it is guaranteed
    // read-write on Metal, and because it holds the shadows better.
    // The creative LUT's grid, allocated for the largest edge Orion accepts so
    // that loading a different LUT is an upload rather than a recompile. A 65
    // grid is 4.4 MB, which is nothing beside the frame buffers.
    auxCube_ = pipeline_.addAuxTexture(kMaxCubeSize, kMaxCubeSize * kMaxCubeSize,
                                       PixelFormat::RGBA32Float);
    // ⚠ **Float, not eight bits, and unconditionally.** Grain has to be added
    // to unquantised values or it is noise on top of banding, so the display
    // node no longer quantises — `develop:grain` below does, and it inherited
    // the Bayer dither that used to end `develop_display.slang`. That costs one
    // more full-resolution RGBA16Float intermediate, about 194 MB at 24 Mpx and
    // 3% of the graph. #81 weighed that against the alternative, which was
    // adding grain in scene-linear where the variance law does not hold.
    nDisplay_   = pipeline_.add({"develop:display", "developDisplay", {nGrade_},
                                 PixelFormat::RGBA16Float, {},
                                 {auxCurveLut_, auxCube_}});

    // The grain plate: a stacked mip chain in one texture, uploaded once and
    // never touched again. See GrainPlate.h for why the chain is stacked by
    // hand rather than mipmapped, and why the levels are not renormalized.
    auxGrainPlate_ = pipeline_.addAuxTexture(grain::kPlateSize, grain::kPlateHeight,
                                             PixelFormat::R32Float);
    nGrain_ = pipeline_.add({"develop:grain", "grain", {nDisplay_},
                             PixelFormat::RGBA8Unorm, {}, {auxGrainPlate_}});

    // Orientation is last, and is the only node whose output dimensions differ
    // from its input — a quarter turn swaps them.
    //
    // Allocate for the worst case so a user rotation never needs a recompile.
    // 1.5x covers the worst-case straighten bounding box (45 degrees
    // grows the frame by sqrt(2)).
    const std::uint32_t maxSide =
        static_cast<std::uint32_t>(std::max(width_, height_) * 1.45f);
    nGeometry_ = pipeline_.add({"geometry", "geometry", {nGrain_},
                                PixelFormat::RGBA8Unorm, {}, {},
                                true, maxSide, maxSide});
}

/// The grain plate. After `compile` rather than beside the node, because it
/// is an upload and not a declaration — see GrainPlate.h for why the mip
/// chain is stacked by hand rather than mipmapped.

void DevelopPipeline::uploadGrainPlate() {
    // per-`Pipeline` because the aux texture is — building it here rather than
    // caching it statically keeps the ownership obvious and costs one upload
    // per graph, of which there are two.
    {
        const auto plate = grain::buildPlate();
        pipeline_.updateAux(auxGrainPlate_, plate.data(),
                            static_cast<std::size_t>(grain::kPlateSize) * sizeof(float));
    }
}

void DevelopPipeline::applyGrade(const Adjustments& adj,
                                 const ApplyContext& ctx) {
    const bool first = ctx.first;

    // ── Color grading, and the creative vignette in the same pass ────────
    const auto zoneMoved = [](const float a[3], const float b[3]) {
        return a[0] != b[0] || a[1] != b[1] || a[2] != b[2];
    };
    // ⚠ The vignette's circle is the *crop's*, so the geometry has to be in
    // this comparison as well as the two sliders. Straightening a photograph
    // with a vignette on and not re-pushing here would leave the darkening
    // centred on where the composition used to be — and it would look like a
    // slightly off-centre vignette rather than like a bug.
    const bool cropMoved =
        adj.rotateQuarters != lastAdj_.rotateQuarters ||
        adj.straightenDeg  != lastAdj_.straightenDeg ||
        adj.cropX != lastAdj_.cropX || adj.cropY != lastAdj_.cropY ||
        adj.cropW != lastAdj_.cropW || adj.cropH != lastAdj_.cropH;
    const bool vignetteMoved =
        adj.vignetteAmount     != lastAdj_.vignetteAmount ||
        adj.vignetteFieldAngle != lastAdj_.vignetteFieldAngle;

    // Decision #92: only re-push when something this block reads has moved. A
    // geometry change matters here **only** while the vignette is on, so a
    // straighten drag on a photograph without one does not dirty the grade.
    const bool vignetting = adj.vignetteAmount != 0.0f;

    // ⚠ Hoisted out of the push below because **Balance is read by the kernel
    // only while a wheel is off centre**. It slides the zone centres, and with
    // every wheel centred those weights multiply an all-zero offset and a slope
    // of one — so a Balance drag on an ungraded photograph must dirty nothing
    // and switch nothing on. That is decision #92's rule (a block re-pushed for
    // a value nothing reads) and #82's (a node run at zero strength), and both
    // of them shipped once already.
    const auto zoneIsFlat = [](const float z[3]) {
        return z[0] == 0.0f && z[1] == 0.0f && z[2] == 0.0f;
    };
    const bool grading = !zoneIsFlat(adj.gradeShadow)
                      || !zoneIsFlat(adj.gradeMidtone)
                      || !zoneIsFlat(adj.gradeHighlight);
    const bool balanceMoved = adj.gradeBalance != lastAdj_.gradeBalance;

    if (first || zoneMoved(adj.gradeShadow, lastAdj_.gradeShadow)
              || zoneMoved(adj.gradeMidtone, lastAdj_.gradeMidtone)
              || zoneMoved(adj.gradeHighlight, lastAdj_.gradeHighlight)
              || vignetteMoved
              || (grading && balanceMoved)
              || (vignetting && cropMoved)) {

        // A whole pass over the frame, so it is switched off rather than run
        // as an identity. A disabled node passes its first input through.
        //
        // ⚠ `grading` deliberately excludes Balance: see above.
        pipeline_.setEnabled(nGrade_, grading || vignetting);

        if (grading || vignetting) {
            params::Grade g{};
            g.size[0] = width_;
            g.size[1] = height_;

            const auto fill = [](const float in[3], float out[4]) {
                gradeOffsets(in[0], in[1], out);
                out[3] = in[2];
            };
            fill(adj.gradeShadow,    g.shadow);
            fill(adj.gradeMidtone,   g.midtone);
            fill(adj.gradeHighlight, g.highlight);
            g.balance = std::clamp(adj.gradeBalance, -1.0f, 1.0f);

            // ⚠ Read from `adj`, never from `lastAdj_`. That mistake shipped
            // once already — the grain retarget switched its node on and handed
            // it the previous frame's Amount, so the kernel ran and took its
            // early exit, and every test stayed green (#82).
            const Circle comp = compositionCircle(adj, exifQuarters_,
                                                  width_, height_);
            g.vignetteCenter[0] = comp.centerX;
            g.vignetteCenter[1] = comp.centerY;
            g.vignetteRadius    = comp.radius;
            g.vignetteAmount    = std::clamp(adj.vignetteAmount, -3.0f, 3.0f);
            g.vignetteTanTheta  = std::tan(
                std::clamp(adj.vignetteFieldAngle, 1.0f, 85.0f)
                * 3.14159265358979f / 180.0f);

            pipeline_.setParams(nGrade_, &g, sizeof g);
        }
    }
}

void DevelopPipeline::applyTone(const Adjustments& adj,
                                const ApplyContext& ctx) {
    const bool first = ctx.first;
    const bool visibilityMoved = ctx.visibilityMoved;
    const bool needsGuide = ctx.needsGuide;
    const std::uint32_t size[2] = {width_, height_};

    const auto liveCount = [](const Adjustments& a) {
        int n = 0;
        for (int i = 0; i < a.maskCount; ++i)
            if (!a.maskComponents[std::size_t(i)].hidden) ++n;
        return n;
    };

    // ⚠ The layer table below is a function of the `startsLayer` flags, so a
    // break that moves must re-push it. This was the one field the guard did
    // not name: splitting a row re-rendered its coverage (the fold restarts
    // from zero) while the table kept naming last frame's runs, and layer 0's
    // grade landed through the new row's coverage — in the app, unlinking a
    // mask made the previous mask's edits vanish until the eye button forced a
    // push. A reorder that moves a flag across indices is the same defect.
    const auto runsMoved = [&] {
        for (int i = 0; i < adj.maskCount; ++i)
            if (adj.maskComponents[std::size_t(i)].startsLayer !=
                lastAdj_.maskComponents[std::size_t(i)].startsLayer)
                return true;
        return false;
    };

    const bool linearMoved =
        first || visibilityMoved || runsMoved() ||
        adj.layers != lastAdj_.layers ||
        adj.maskOverlay != lastAdj_.maskOverlay ||
        adj.maskOverlayLayer != lastAdj_.maskOverlayLayer ||
        adj.maskCount != lastAdj_.maskCount ||
        adj.hueShift != lastAdj_.hueShift ||
        adj.satShift != lastAdj_.satShift ||
        adj.lumShift != lastAdj_.lumShift ||
        adj.exposureEv != lastAdj_.exposureEv ||
        adj.highlights != lastAdj_.highlights ||
        adj.shadows    != lastAdj_.shadows    ||
        adj.whites     != lastAdj_.whites     ||
        adj.blacks     != lastAdj_.blacks     ||
        adj.vibrance   != lastAdj_.vibrance   ||
        adj.saturation != lastAdj_.saturation;

    if (linearMoved) {
        params::LinearAdjust la{adj.exposureEv + kBaselineExposureEv,
                                adj.highlights, adj.shadows,
                                adj.whites, adj.blacks, adj.vibrance,
                                adj.saturation,
                                // Tell the shader whether the guide textures
                                // hold what it thinks they hold. `linearMoved`
                                // already covers every way this can flip:
                                // `needsGuide` turns on exactly when highlights
                                // or shadows crosses zero, and both are in it.
                                needsGuide ? 1.0f : 0.0f,
                                {size[0], size[1]},
                                {guideW_, guideH_},
                                {}, {}, {}};
        // ⚠ Layers are runs of components, resolved here rather than stored:
        // a layer's coverage is the **last** component of its run, and which
        // component that is moves whenever a row is added, removed or
        // reordered. Storing the index would be a second copy of the grouping,
        // and this file has been bitten by a second copy of a claim before.
        int layer = -1;
        for (int i = 0; i < adj.maskCount; ++i) {
            const auto& c = adj.maskComponents[std::size_t(i)];
            if (i == 0 || c.startsLayer) {
                if (layer + 1 >= kMaxMaskComponents) break;
                ++layer;
            }
            if (layer < 0) continue;
            // Hidden components still end a run — hiding one must not silently
            // merge its layer into the next.
            la.layerMask[layer] = i;
        }
        la.layerCount = layer + 1;
        for (int L = 0; L < kMaxMaskComponents; ++L) {
            const auto& e = adj.layers[std::size_t(L)];
            la.layerExposureEv[L] = e.exposureEv;
            la.layerContrast[L]   = e.contrast;
            la.layerSaturation[L] = e.saturation;
            la.layerWarmth[L]     = e.warmth;
            la.layerTint[L]       = e.tint;
        }
        la.maskActive  = (liveCount(adj) > 0) ? 1.0f : 0.0f;
        la.maskOverlay = adj.maskOverlay ? 1.0f : 0.0f;
        // Second belt after CApi's clamp: the shader clamps to layerCount - 1
        // as well, but an index the host knows is out of range should never
        // travel at all.
        la.maskOverlayLayer =
            std::clamp(adj.maskOverlayLayer, 0, std::max(la.layerCount - 1, 0));
        std::copy(adj.hueShift.begin(), adj.hueShift.end(), la.hueShift);
        std::copy(adj.satShift.begin(), adj.satShift.end(), la.satShift);
        std::copy(adj.lumShift.begin(), adj.lumShift.end(), la.lumShift);
        pipeline_.setParams(nLinear_, &la, sizeof la);
    }
}

void DevelopPipeline::applyOutput(const Adjustments& adj,
                                  const ApplyContext& ctx) {
    const bool first = ctx.first;

    const bool curveMoved = first || !sameCurve(adj.curve, lastAdj_.curve);

    // The creative LUT's strength lives in this node's parameters too, so it
    // has to be in the condition that re-pushes them. Leaving it out is not a
    // visible bug — the slider simply does nothing, and the bench reports the
    // control as dead, which is how this one was found.
    if (first || adj.contrast != lastAdj_.contrast || curveMoved ||
        adj.lutStrength != lastAdj_.lutStrength) {
        pushDisplayParams(adj);
    }

    // ⚠ Guarded like every other push, and the guard is the whole slider. The
    // display node's own guard omitted `lutStrength` once and the LUT slider
    // was simply dead; the same shape of mistake here would be a grain slider
    // that does nothing until some unrelated control happens to move.
    if (first || adj.grainAmount != lastAdj_.grainAmount ||
        adj.grainSize != lastAdj_.grainSize) {
        // ⚠ Crossing zero is not a parameter push, it is a change of which node
        // writes the eight bits — so it retargets the chain rather than just
        // re-uploading the block. Everything else here disables to nothing when
        // it is off and this has to as well: a pointwise pass at full
        // resolution is ~6 ms of every frame of every drag, whatever it is
        // multiplying the noise by.
        const bool graining = adj.grainAmount > 0.0f;
        if (first || graining != graining_) {
            graining_ = graining;
            retargetOutputChain(adj);
        } else {
            pushGrainParams(adj);
        }
    }

    // Rebuilding the LUT walks four splines. Skip it when the curve has not
    // moved, which is every frame of an exposure drag.
    if (curveMoved) {
        const auto lut = buildCurveLut(adj.curve);
        pipeline_.updateAux(auxCurveLut_, lut.data(), kCurveResolution * sizeof(float));
    }
}

void DevelopPipeline::applyGeometry(const Adjustments& adj,
                                    const ApplyContext& ctx) {
    const bool first = ctx.first;
    const persp::Matrix3* perspective = ctx.perspective;

    const bool geometryMoved =
        first ||
        adj.rotateQuarters != lastAdj_.rotateQuarters ||
        adj.straightenDeg  != lastAdj_.straightenDeg ||
        adj.cropX != lastAdj_.cropX || adj.cropY != lastAdj_.cropY ||
        adj.cropW != lastAdj_.cropW || adj.cropH != lastAdj_.cropH ||
        adj.cropPreview != lastAdj_.cropPreview ||
        adj.perspectiveVertical   != lastAdj_.perspectiveVertical ||
        adj.perspectiveHorizontal != lastAdj_.perspectiveHorizontal ||
        adj.perspectiveAspect     != lastAdj_.perspectiveAspect;

    if (geometryMoved) {
        const int turns = ((exifQuarters_ + adj.rotateQuarters) % 4 + 4) % 4;
        const bool swap = (turns % 2) != 0;

        const float rotW = static_cast<float>(swap ? height_ : width_);
        const float rotH = static_cast<float>(swap ? width_  : height_);

        params::Geometry g{};
        g.inSize[0] = width_;
        g.inSize[1] = height_;
        g.quarterTurns  = static_cast<std::uint32_t>(turns);
        g.straightenRad = adj.straightenDeg * 3.14159265358979f / 180.0f;

        const float cw = std::clamp(adj.cropW, 0.01f, 1.0f);
        const float ch = std::clamp(adj.cropH, 0.01f, 1.0f);
        const float cx = std::clamp(adj.cropX, 0.0f, 1.0f - cw);
        const float cy = std::clamp(adj.cropY, 0.0f, 1.0f - ch);

        // The picture turns about the frame's center, and the pivot is passed
        // rather than derived so the preview and the committed render cannot
        // disagree. Pivoting on the crop instead — which this did briefly —
        // re-rotates the picture every time the rectangle is dragged, and the
        // image swims out from under the box.
        g.pivot[0] = 0.5f;
        g.pivot[1] = 0.5f;

        // The composed homography, or the branch that says there is not one.
        // ⚠ `perspectiveOn` is what makes a neutral control **bit-identical**
        // to a build without perspective: the kernel takes the branch it took
        // before, so no baseline in any suite rebases silently.
        g.perspectiveOn = perspective != nullptr ? 1u : 0u;
        for (int r = 0; r < 3; ++r) {
            for (int c = 0; c < 3; ++c) g.perspective[r][c] = perspective_.m[r * 3 + c];
            g.perspective[r][3] = 0.0f;   // padding, never read
        }

        if (adj.cropPreview) {
            // The canvas the UI asked for. It has to cover the frame's rotated
            // bounding box, which grows with both the angle and the frame's
            // aspect — at 45 degrees on a 3:2 frame that is 1.77x the short
            // side, so the old fixed 1.42 clipped the corners off anything past
            // about 17 degrees.
            const float m = std::max(adj.previewSize, 1.0f);

            g.cropSize[0] = m;
            g.cropSize[1] = m;
            g.cropOrigin[0] = adj.previewX;
            g.cropOrigin[1] = adj.previewY;

            // The texture does *not* grow with the canvas. A larger area is
            // sampled into a frame-sized target instead, so the preview costs
            // the same memory at 90 degrees as at zero — it just resolves a
            // little softer, which is what a crop preview can afford. Capped at
            // the 1.45 the graph allocates for.
            const float texScale = std::min(m, 1.45f);
            g.outSize[0] = std::max(1u, static_cast<std::uint32_t>(rotW * texScale));
            g.outSize[1] = std::max(1u, static_cast<std::uint32_t>(rotH * texScale));
        } else {
            g.cropOrigin[0] = cx;
            g.cropOrigin[1] = cy;
            g.cropSize[0]   = cw;
            g.cropSize[1]   = ch;

            g.outSize[0] = std::max(1u, static_cast<std::uint32_t>(rotW * cw));
            g.outSize[1] = std::max(1u, static_cast<std::uint32_t>(rotH * ch));
        }

        pipeline_.setParams(nGeometry_, &g, sizeof g);
        turns_ = turns;
        outW_  = g.outSize[0];
        outH_  = g.outSize[1];
        frameW_ = static_cast<std::uint32_t>(rotW);
        frameH_ = static_cast<std::uint32_t>(rotH);
    }
}

std::uint32_t DevelopPipeline::outputWidth() const noexcept  { return outW_; }
std::uint32_t DevelopPipeline::outputHeight() const noexcept { return outH_; }
std::uint32_t DevelopPipeline::frameWidth()  const noexcept { return frameW_; }
std::uint32_t DevelopPipeline::frameHeight() const noexcept { return frameH_; }

void DevelopPipeline::pushDisplayParams(const Adjustments& adj) {
    params::Display d{};
    d.contrast      = adj.contrast;
    d.pivot         = -2.5f;
    d.curveIdentity = adj.curve.isIdentity() ? 1u : 0u;
    d.resolution    = kCurveResolution;
    d.size[0] = width_; d.size[1] = height_;
    // Dither only when this node is the one quantising. At sixteen bits there
    // is nothing to hide, and with grain on this node hands float to the node
    // that rounds. See `retargetOutputChain`.
    d.dither = (!wideOutput_ && !graining_) ? 1u : 0u;

    // A strength of zero is the same as no LUT at all, and saying so here means
    // the shader skips the lookup entirely rather than interpolating a table it
    // is about to discard.
    const bool applying = lutSize_ >= 2 && adj.lutStrength > 1e-4f;
    d.lutSize     = applying ? static_cast<std::uint32_t>(lutSize_) : 0u;
    d.lutStrength = std::clamp(adj.lutStrength, 0.0f, 1.0f);
    for (int c = 0; c < 3; ++c) {
        d.lutMin[c] = lutMin_[static_cast<std::size_t>(c)];
        d.lutMax[c] = lutMax_[static_cast<std::size_t>(c)];
    }

    pipeline_.setParams(nDisplay_, &d, sizeof d);
}

void DevelopPipeline::setCreativeLut(const CubeLut& lut) {
    if (!lut.valid()) { clearCreativeLut(); return; }

    // The texture is allocated at the largest accepted edge, so the upload has
    // to be a full-size buffer whatever the file's grid is: rows are
    // kMaxCubeSize entries wide and the shader only ever indexes the first
    // `lutSize_` of them.
    std::vector<float> grid(static_cast<std::size_t>(kMaxCubeSize) *
                            kMaxCubeSize * kMaxCubeSize * 4, 0.0f);

    const int n = lut.size;
    for (int b = 0; b < n; ++b) {
        for (int g = 0; g < n; ++g) {
            for (int r = 0; r < n; ++r) {
                // Red varies fastest in the file; y packs blue and green.
                //
                // The row index uses the LUT's own edge, not the texture's
                // width — `b * n + g` is what the shader recomputes, and the
                // texture is merely wide enough to hold the largest one. Using
                // the width here instead puts every blue slice in the wrong
                // place, which reads as a plausible color cast rather than as
                // an obvious break.
                const std::size_t src =
                    ((static_cast<std::size_t>(b) * n + g) * n + r) * 3;
                const std::size_t row = static_cast<std::size_t>(b) * n + g;
                const std::size_t dst = (row * kMaxCubeSize + r) * 4;
                grid[dst + 0] = lut.data[src + 0];
                grid[dst + 1] = lut.data[src + 1];
                grid[dst + 2] = lut.data[src + 2];
                grid[dst + 3] = 1.0f;
            }
        }
    }

    pipeline_.updateAux(auxCube_, grid.data(),
                        static_cast<std::size_t>(kMaxCubeSize) * 4 * sizeof(float));

    lutSize_  = lut.size;
    lutMin_   = lut.domainMin;
    lutMax_   = lut.domainMax;
    lutTitle_ = lut.title;
    pushDisplayParams(lastAdj_);
}

void DevelopPipeline::clearCreativeLut() {
    lutSize_ = 0;
    lutTitle_.clear();
    lutMin_ = {0.0f, 0.0f, 0.0f};
    lutMax_ = {1.0f, 1.0f, 1.0f};
    pushDisplayParams(lastAdj_);
}

void DevelopPipeline::setWideOutput(bool wide) {
    if (wide == wideOutput_) return;
    wideOutput_ = wide;
    retargetOutputChain(lastAdj_);
}

/// Which node writes the eight bits, and therefore which one dithers.
///
/// ⚠ **Exactly one node quantises, and there are two candidates.** Grain has to
/// be added to unquantised values, so with the Amount slider up `develop:grain`
/// is the last writer and `develop:display` hands it float. With grain off the
/// node is disabled entirely and `develop:display` is the last writer again.
/// Deciding that in two places is how the narrow path ends up rounding twice —
/// or not at all, which is the banding the dither exists to prevent — so it is
/// decided here and nowhere else.
///
/// | wide | grain | display | grain node | dither |
/// |---|---|---|---|---|
/// | no  | off | `RGBA8Unorm`  | disabled      | display |
/// | no  | on  | `RGBA16Float` | `RGBA8Unorm`  | grain   |
/// | yes | off | `RGBA16Float` | disabled      | neither |
/// | yes | on  | `RGBA16Float` | `RGBA16Float` | neither |
///
/// ⚠ The `graining_` half of this is not a tidiness: a grain node left enabled
/// at Amount 0 is a full-resolution pointwise pass on every frame of every
/// drag, and `develop:display` writing float is a second one. Measured on
/// `_PIC8220`, the two together took the exposure slider from 3 nodes and
/// 10.63 ms p95 to 4 nodes and **17.03 ms** — past the 16 ms M0 gate, and the
/// slowdown that was reported from the app before the bench was next run.
/// ⚠ Takes the adjustments rather than reading `lastAdj_`. Inside `apply` the
/// member still holds the *previous* frame's values, so a version that read it
/// pushed Amount 0 to the node it had just switched on — the node ran, took the
/// shader's early exit, and the bench reported the control as having no effect
/// while the gate and every test stayed green.
void DevelopPipeline::retargetOutputChain(const Adjustments& adj) {
    const auto wideFmt   = gpu::PixelFormat::RGBA16Float;
    const auto narrowFmt = gpu::PixelFormat::RGBA8Unorm;
    const auto outFmt    = wideOutput_ ? wideFmt : narrowFmt;

    pipeline_.setEnabled(nGrain_, graining_);
    pipeline_.setNodeFormat(nDisplay_, graining_ ? wideFmt : outFmt);
    pipeline_.setNodeFormat(nGrain_, outFmt);
    pipeline_.setNodeFormat(nGeometry_, outFmt);

    // Both, because the dither flag moved between them and the normal pushes
    // are guarded on values that did not change.
    pushDisplayParams(adj);
    pushGrainParams(adj);
}

void DevelopPipeline::setGridStep(float step) {
    gridStep_ = step > 0.0f ? step : 1.0f;
    pushGrainParams(lastAdj_);
}

void DevelopPipeline::pushGrainParams(const Adjustments& adj) {
    params::Grain g{};
    g.size[0] = width_; g.size[1] = height_;
    // ⚠ Only when this node is the one that quantises, which needs grain to be
    // on as well as the output to be narrow. See `retargetOutputChain`.
    g.dither    = (!wideOutput_ && graining_) ? 1u : 0u;
    g.amount    = std::max(adj.grainAmount, 0.0f);
    // ⚠ Clamped, not just guarded against zero. `kGrainSizeMin` is above 1.0
    // because a rate of exactly one plate texel per frame pixel interpolates
    // nothing and comes back 14% louder than its neighbors on the slider.
    g.grainSize = std::clamp(adj.grainSize, params::kGrainSizeMin, params::kGrainSizeMax);
    g.gridStep  = gridStep_;
    pipeline_.setParams(nGrain_, &g, sizeof(g));
}

}  // namespace orion::pipe
