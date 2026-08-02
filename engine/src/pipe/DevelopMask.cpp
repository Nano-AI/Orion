/*  The mask group, the brush, and the guided feathering of the fold.
 *
 *  research/masking.md and research/brush-acceleration.md. A mask is its
 *  parameters rather than an image — normalized coordinates in, alpha out — so
 *  it survives a resize, and an export matches the preview it was made on.
 *
 *  All of it in one place: the base the fold starts from and the four component
 *  nodes chained onto it, the raster mattes, the dab and dab-bounds textures,
 *  the single incremental accumulator, seven refine nodes per slot, and the two
 *  calls that wrap a render so the accumulator cannot composite the same dabs
 *  twice. Decision #113.
 */
#include "pipe/DevelopPipeline.h"

#include "pipe/ShaderParams.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "pipe/DevelopInternal.h"

namespace orion::pipe {

void DevelopPipeline::buildMaskNodes() {
    using gpu::PixelFormat;

    // The mask group. Components are a pure function of position, so the chain
    // has no image input and hangs off the side of the graph, bound as
    // develop:linear's fourth input.
    //
    // The base writes the fold's identity and stays enabled — a disabled node
    // copies its first input through, and this one has no input to copy. It is a
    // single full-resolution R16F write whose params never change, so the
    // per-node cache serves it for the life of the image.
    nMaskBase_ = pipeline_.add({"mask:base", "maskBase", {},
                                PixelFormat::R16Float, {}});

    // One node per component, each folding its own coverage into the one before
    // it. All the same shape, so a group of one runs the same code as a group of
    // four; unused components are disabled in `apply`, which costs their texture
    // and none of their time. research/masking.md §6.
    // One matte per component slot, so a group can hold a subject on one row
    // and a person on another. Allocated at `kMaxMatteEdge` on the long side
    // rather than at the frame's: a segmentation network runs at a fixed
    // internal resolution far below 24 MP, and the guided refinement is what
    // recovers the boundary afterwards. Four of these cost about 4 MB together,
    // against 48 MB for one at full resolution.
    const bool tall = height_ > width_;
    matteW_ = tall ? std::max(1u, kMaxMatteEdge * width_ / std::max(height_, 1u))
                   : kMaxMatteEdge;
    matteH_ = tall ? kMaxMatteEdge
                   : std::max(1u, kMaxMatteEdge * height_ / std::max(width_, 1u));

    int prevMask = nMaskBase_;
    for (int i = 0; i < kMaxMaskComponents; ++i) {
        auxMatte_[i] = pipeline_.addAuxTexture(matteW_, matteH_,
                                               PixelFormat::R16Float);
        // One texel per dab. 256 x 64 is 16,384 of them for 128 KB, against the
        // 256 a four-kilobyte constant block could hold.
        // ⚠ RGBA rather than RG: the third channel carries whether the dab
        // adds coverage or takes it away. 256 KB a component against 128, for
        // the ability to erase — which is the difference between a brush and a
        // one-way stamp.
        auxDabs_[i] = pipeline_.addAuxTexture(params::kDabStride, params::kDabRows,
                                              PixelFormat::RGBA32Float);
        // One box per run of 64 dabs: (minX, minY, maxX, maxY) of the centers.
        // 256 texels, 4 KB. research/brush-acceleration.md.
        //
        // ⚠ A texture rather than the parameter block, and not the dab's spare
        // `w` channel either. 256 boxes is 4 KB on its own — the whole `setBytes`
        // limit, of which this struct already spends 152 bytes a component — and
        // scattering a four-float box across four dab texels would cost four
        // fetches to read the thing that exists to save fetches.
        auxDabBounds_[i] = pipeline_.addAuxTexture(params::kMaxDabBlocks, 1,
                                                   PixelFormat::RGBA32Float);
    }
    // The incremental brush accumulator — one texture, for whichever component
    // is being painted on. research/brush-acceleration.md, decision #108.
    //
    // ⚠ **R32Float, and one rather than one per component.** R32 because the
    // claim is bit-identity with a full evaluation and only float32 round-trips
    // float32 exactly. One because at 24 Mpx it is ~97 MB: four would be
    // ~388 MB of intermediates for a feature that is only ever used by the
    // component under the cursor.
    //
    // ⚠ Registered at 1x1 and grown by `ensureBrushAccum` on the first dab. A
    // photograph that is never painted on pays two bytes, which is the whole
    // reason `Pipeline::resizeAux` exists.
    auxBrushAccum_ = pipeline_.addAuxTexture(1, 1, PixelFormat::R32Float);

    for (int i = 0; i < kMaxMaskComponents; ++i) {
        nMaskComponent_[i] =
            pipeline_.add({"mask:" + std::to_string(i), "maskComponent",
                           {prevMask, nHueSat_}, PixelFormat::R16Float, {},
                           {auxMatte_[i], auxDabs_[i], auxDabBounds_[i],
                            auxBrushAccum_}});
        prevMask = nMaskComponent_[i];
    }

    // ── Guided feathering of the folded group (research/masking.md §4) ────
    //
    // The second input binding §4 asks for, and it hangs off the *group* rather
    // than off each component: what a photographer wants snapped to an edge is
    // the coverage they can see, which is the fold. Six nodes once, not six per
    // component.
    //
    // Every one of them is disabled when the strength is zero, so a photograph
    // with no refinement pays for their textures and none of their time — and
    // the consumer reads straight past them to the fold, because `resolve`
    // follows a disabled node back to a live producer.
    for (int i = 0; i < kMaxMaskComponents; ++i) {
        const std::string tag = "mask" + std::to_string(i) + ":";
        nMaskGuidePrep_[i] = pipeline_.add({tag + "guide prep", "maskGuidePrep",
                                            {nGuidePrep_, nMaskComponent_[i]},
                                            PixelFormat::RGBA32Float, {}, {},
                                            true, guideW_, guideH_});
        nMaskGuideH1_[i]   = pipeline_.add({tag + "guide blur h", "boxBlur4",
                                            {nMaskGuidePrep_[i]},
                                            PixelFormat::RGBA32Float, {}, {},
                                            true, guideW_, guideH_});
        nMaskGuideV1_[i]   = pipeline_.add({tag + "guide blur v", "boxBlur4",
                                            {nMaskGuideH1_[i]},
                                            PixelFormat::RGBA32Float, {}, {},
                                            true, guideW_, guideH_});
        nMaskGuideAb_[i]   = pipeline_.add({tag + "guide coeffs", "maskGuideAb",
                                            {nMaskGuideV1_[i]},
                                            PixelFormat::RG32Float, {}, {},
                                            true, guideW_, guideH_});
        nMaskGuideH2_[i]   = pipeline_.add({tag + "guide blur h2", "boxBlur",
                                            {nMaskGuideAb_[i]},
                                            PixelFormat::RG32Float, {}, {},
                                            true, guideW_, guideH_});
        nMaskGuideV2_[i]   = pipeline_.add({tag + "guide blur v2", "boxBlur",
                                            {nMaskGuideH2_[i]},
                                            PixelFormat::RG32Float, {}, {},
                                            true, guideW_, guideH_});

        // ⚠ The mask is this node's *first* input, so a disabled refine
        // resolves to its component rather than to a coefficient texture.
        // Getting that order wrong hands develop:linear a two-channel
        // coefficient grid as its coverage.
        nMaskRefine_[i]    = pipeline_.add({tag + "refine", "maskGuideApply",
                                            {nMaskComponent_[i], nMaskGuideV2_[i],
                                             nGuidePrep_},
                                            PixelFormat::R16Float, {}});
    }
}

void DevelopPipeline::pushStaticMaskParams() {
    const std::uint32_t size[2] = {width_, height_};

    // The mask group's fold starts from zero, the additive identity — see
    // mask_base.slang for why it is a node at all. Set once per image; the
    // per-node cache serves it thereafter.
    params::MaskBase mb{{size[0], size[1]}, 0.0f, 0.0f};
    pipeline_.setParams(nMaskBase_, &mb, sizeof mb);

    // ── Guided feathering of the mask group (research/masking.md §4) ──────
    //
    // The static half: everything but the strength, which is the only thing a
    // slider moves. Set once per image and served by the per-node cache after.
    {
        params::MaskGuidePrep mp{};
        mp.outSize[0] = guideW_; mp.outSize[1] = guideH_;
        mp.inSize[0]  = size[0]; mp.inSize[1]  = size[1];
        mp.scale = kGuideScale;
        for (int i = 0; i < kMaxMaskComponents; ++i)
            pipeline_.setParams(nMaskGuidePrep_[i], &mp, sizeof mp);

        // ⚠ A *feathering* radius, not the recovery chain's, and the paper's
        // r = 60 is not transferable — its figures are sub-megapixel, so 60
        // there is 6-10% of the frame and would be ~500 px here.
        //
        // The mechanism does transfer: the local linear model can only pull the
        // boundary onto an edge that lies *inside* the window, so r is a search
        // radius and wants to be a small multiple of how far the placed mask
        // misses by. That error belongs to the mask's source — a brush stroke
        // laid at fit zoom, or a segmenter run at a fixed internal size — and
        // those scale with the frame, which is what makes a frame fraction the
        // right law rather than a constant. maxdim/100 is 60 px at 6024, 15 on
        // the subsampled grid. Orion's own number: UNSOURCED.md §19.
        const int refineFull   = std::max(8, static_cast<int>(
                                     std::max(width_, height_) / 100));
        const int refineRadius = std::max(2, refineFull / kGuideScale);

        params::BoxBlur rh{{guideW_, guideH_}, refineRadius, 1};
        params::BoxBlur rv{{guideW_, guideH_}, refineRadius, 0};
        for (int i = 0; i < kMaxMaskComponents; ++i) {
            pipeline_.setParams(nMaskGuideH1_[i], &rh, sizeof rh);
            pipeline_.setParams(nMaskGuideV1_[i], &rv, sizeof rv);
            pipeline_.setParams(nMaskGuideH2_[i], &rh, sizeof rh);
            pipeline_.setParams(nMaskGuideV2_[i], &rv, sizeof rv);
        }

        // ⚠ Epsilon is in squared log2-exposure units, because the guide is the
        // same log2 luminance the recovery chain uses. The paper's 1e-6 assumes
        // I in [0,1] display-encoded intensity and does **not** transfer: near
        // midtones d(encoded)/d(stop) is about 0.15, so their sigma of 1e-3
        // encoded units is roughly 0.0065 of a stop, i.e. 4e-5 stops squared.
        //
        // That faithful conversion is unusable here, and the reason is a
        // departure this chain inherits: `mask_guide_prep` area-averages both
        // moments over the s x s block, exactly as `guide_down.slang` does, so
        // `var` is the true *full-resolution* window variance and carries the
        // photograph's noise at full strength. (Subsampling the signal first
        // would divide that noise variance by about s^2 — He & Sun's own
        // arrangement — but it aliases the variance term, which is why this
        // codebase does not do it.) Deep shadows on a 14-stop raw run to a
        // window variance around 0.02 stops squared, so an epsilon below that
        // snaps the matte to shadow noise.
        //
        // 0.01 is the compromise, and it is a tenth of a stop of spread. A step
        // of height h across half a window has variance h^2/4, so the filter
        // follows a half-stop edge at a = 0.86 and ignores a tenth-stop one at
        // a = 0.2 — which is the behavior wanted, since a mask boundary is
        // placed against a subject and not against texture. A quarter of the
        // recovery chain's 0.04, because feathering should follow weaker edges
        // than tone recovery should. Orion's own number: UNSOURCED.md §19.
        params::MaskGuideAb mab{{guideW_, guideH_}, 0.01f, 0.0f};
        for (int i = 0; i < kMaxMaskComponents; ++i)
            pipeline_.setParams(nMaskGuideAb_[i], &mab, sizeof mab);
    }
}

void DevelopPipeline::applyMaskComponents(const Adjustments& adj,
                                          const ApplyContext& ctx) {
    const bool first = ctx.first;
    const persp::Matrix3* perspective = ctx.perspective;
    const mask::Crop& crop = ctx.crop;
    const int turns = ctx.turns;
    const float rotW = ctx.rotW, rotH = ctx.rotH;
    const bool frameMoved = ctx.frameMoved;
    const bool visibilityMoved = ctx.visibilityMoved;

    // The whole frame → display map as one matrix — crop, straighten, quarter
    // turns and the correction — so a mask defined on the displayed picture can
    // be pulled back into this one exactly rather than having its parameters
    // pushed forward approximately. `ctx.perspective` is the display → frame
    // direction the placements take, so this wants its inverse, which is the
    // same argument `mask::fromFrame` takes.
    //
    // Computed once for the whole list: it depends on the geometry and not on
    // any component, and `persp::inverse` is an adjugate nobody should pay for
    // per row.
    persp::Matrix3 perspInverse{};
    const persp::Matrix3* perspInversePtr = nullptr;
    if (perspective != nullptr && !persp::isIdentity(*perspective)) {
        perspInverse = persp::inverse(*perspective);
        perspInversePtr = &perspInverse;
    }
    const persp::Matrix3 display = mask::displayMatrix(
        crop, turns, adj.straightenDeg * 3.14159265358979324f / 180.0f,
        adj.cropX + adj.cropW * 0.5f, adj.cropY + adj.cropH * 0.5f,
        rotW, rotH, perspInversePtr);

    // ⚠ Hidden components are *disabled*, not zeroed. A disabled node resolves
    // to its first input, and this node's first input is the fold so far — so a
    // hidden component is skipped exactly, for free, and keeps every setting it
    // had. Zeroing its coverage instead would still cost a full-resolution pass
    // to produce nothing.

    if (visibilityMoved) {
        for (int i = 0; i < kMaxMaskComponents; ++i) {
            pipeline_.setEnabled(nMaskComponent_[i],
                                 i < adj.maskCount &&
                                 !adj.maskComponents[std::size_t(i)].hidden);
        }
    }

    for (int i = 0; i < kMaxMaskComponents; ++i) {
        const MaskComponentEdit& c = adj.maskComponents[std::size_t(i)];
        // A component past the count is disabled; its params cannot reach the
        // picture, so uploading them would only dirty a node that will not run.
        if (i >= adj.maskCount) continue;
        // ⚠ `adj.exposureEv` is in this comparison because a range component's
        // bias is derived from it, and a bias is not part of
        // `MaskComponentEdit`. Without it the band would keep the exposure it
        // was created under and drift off the picture as the slider moved —
        // the same staleness trap `matteDirty_` exists for, arriving by a
        // different route.
        if (!first && frameMoved == false && !matteDirty_[std::size_t(i)] &&
            adj.exposureEv == lastAdj_.exposureEv &&
            c == lastAdj_.maskComponents[std::size_t(i)] &&
            i < lastAdj_.maskCount) {
            continue;
        }
        matteDirty_[std::size_t(i)] = false;

        params::MaskComponent m{};
        m.size[0] = width_; m.size[1] = height_;
        m.kind    = c.kind;
        m.invert  = c.invert ? 1 : 0;
        m.compose = c.compose;
        // ⚠ Row 0 always begins a layer whatever it says, because the fold has
        // to start somewhere. Without this the first component folds into
        // `mask:base`'s zero by luck rather than by rule.
        m.startsLayer = (i == 0 || c.startsLayer) ? 1 : 0;
        // ⚠ A row that begins a layer folds from **zero**, so `subtract` gives
        // 0·(1−α) = 0 and `intersect` gives 0·α = 0 — the layer comes out empty
        // whatever is painted into it. Forced to add rather than left to the
        // caller, because the failure is a mask that draws nothing and looks
        // exactly like a mask that was placed wrong. Same reasoning as the
        // first row of a group, which has always had this property.
        if (m.startsLayer != 0) m.compose = int(params::MaskCompose::Add);

        m.rangeLo = c.rangeLo;
        m.rangeHi = c.rangeHi;
        m.rangeSoft = c.rangeSoft;

        // ⚠ No exposure bias on the color band, unlike the luminance one. The
        // metric is Oklab chromaticity, which is exactly invariant under a
        // multiply — so the exposure slider cannot move it, and the number set
        // against the picture is the number the kernel wants. That is also why
        // `adj.exposureEv` being in the staleness comparison above is harmless
        // here: it re-pushes a block whose color fields did not change.
        m.colorR = c.color[0];
        m.colorG = c.color[1];
        m.colorB = c.color[2];
        m.colorTol  = c.colorTol;
        m.colorSoft = c.colorSoft;
        // Stops as displayed rather than as captured — see the kind-5 branch.
        m.rangeBias = adj.exposureEv + kBaselineExposureEv;

        m.matteSize[0] = matteLive_[std::size_t(i)][0];
        m.matteSize[1] = matteLive_[std::size_t(i)][1];

        // ⚠ **A radial mask now travels as the numbers the photographer set**,
        // and the kernel carries each pixel back to meet them. Nothing here is
        // transformed at all.
        //
        // What stood here pushed the mask *forward* into frame coordinates:
        // `toFrame` for the centre and the angle, `radiusToFrame` for the
        // semi-axes through the map's derivative at that centre. Every step of
        // that was exact to first order and none of it could be exact, because
        // one 2×2 cannot describe a map whose derivative differs at every point.
        // The rim of a large mask under a strong keystone was off by a full unit
        // of coverage. Decision #138.
        //
        // It also deletes a whole class of bookkeeping: the angle no longer has
        // to be a *delta* against an already-turned placement (decision #83), the
        // straighten no longer has to be added to the ellipse's angle while the
        // quarter turns are kept out of it (#83 again), and the crop no longer
        // scales the semi-axes by hand. All four live in the matrix.
        m.center[0] = c.center[0]; m.center[1] = c.center[1];
        m.semi[0]   = c.radius[0]; m.semi[1]   = c.radius[1];
        m.angle     = c.angle;

        // The one map, for both geometric kinds — the ramp's denominator is its
        // bottom row. `display` is computed once per apply, above the loop.
        for (int r = 0; r < 9; ++r) m.display[r] = display.m[r];

        // A linear gradient's ramp, pulled back from the picture the
        // photographer is looking at instead of pushed forward into this one.
        //
        // ⚠ **The endpoints that stood here were wrong in a way no amount of
        // care about their position could fix.** They were built from
        // `placed.angle` and `lengthAlong` — the ramp's image direction and its
        // image length, both exact — and the kernel then projected onto the
        // segment between them, which puts the level sets perpendicular to that
        // segment *in frame coordinates*. The drawn mask's level sets are
        // perpendicular in *display* coordinates, and a homography preserves
        // neither perpendicularity nor the spacing along the ramp. t is a linear
        // **functional**: it goes through J⁻ᵀ where a pair of endpoints goes
        // through J, and the two agree only where J is conformal — which is
        // every case anybody checks by hand. Decision #137.
        //
        // In display coordinates the ramp runs from z along u, exactly as
        // `CanvasLayout.MaskPlacement` derives its two handles, so these three
        // numbers are the photographer's own and go through no transform at all.
        // The transform is applied to the *point*, by the matrix.
        // t(q) = ⟨n, (q,1)⟩ / ⟨M₃, (q,1)⟩, where M is the whole frame → display
        // map. Exact for the homography, the crop, the straighten and the
        // quarter turns at once, because all four are in M.
        //
        // ⚠ Only the numerator is copied. `ramp.den` *is* `display`'s bottom
        // row — the kernel reads it there, and `testRampDenominatorIsTheMatrix`
        // is what keeps that true rather than merely currently so.
        const auto ramp = mask::ramp(c.center[0], c.center[1], c.angle,
                                     c.length, display);
        for (int r = 0; r < 3; ++r) {
            m.rampNum[r] = ramp.num[r];
        }
        m.feather   = c.feather;
        m.roundness = c.roundness;

        // `nibPx` is set in the brush branch below, where the crop is to hand.
        m.flow     = c.brushFlow;
        m.hardness = c.brushHardness;

        if (c.kind == 3) {
            const int have = brushDabCount(i);
            m.count = std::min(have, params::kMaxDabs);
            m.dabStride = params::kDabStride;

            // ⚠ The stroke goes into an auxiliary *texture*, not into the
            // parameter block, and only when it has actually changed. Uploading
            // it on every tick would dirty the mask node on every tick, which
            // is the cost this graph exists to avoid.
            //
            // The geometry counts as a change: every center goes through
            // `mask::toFrame`, so a crop or a quarter turn moves all of them.
            const bool dabsStale =
                first || frameMoved ||
                i >= lastAdj_.maskCount ||
                c.brushRevision != lastAdj_.maskComponents[std::size_t(i)].brushRevision ||
                lastAdj_.maskComponents[std::size_t(i)].kind != 3;

            if (dabsStale) {
                const auto& dabs = brushDabs_[std::size_t(i)];
                std::vector<float> texels(
                    std::size_t(params::kDabStride) * params::kDabRows * 4, 0.0f);
                for (int d = 0; d < m.count; ++d) {
                    // Every dab goes through the *same* transform the gradient's
                    // center does, and it did not before: the centers were
                    // copied straight from displayed coordinates into the
                    // shader, so a stroke ignored the crop and the rotation.
                    //
                    // Not only a rotated-frame problem. A portrait file carries
                    // an EXIF quarter turn, so `turns` is nonzero with the
                    // rotate control untouched — which is why a stroke on a
                    // portrait frame landed mirrored and ninety degrees off.
                    const auto p = mask::toFrame(
                        {dabs[std::size_t(d) * 2 + 0],
                         dabs[std::size_t(d) * 2 + 1], 0.0f},
                        crop, turns,
                        adj.straightenDeg * 3.14159265358979324f / 180.0f,
                        adj.cropX + adj.cropW * 0.5f, adj.cropY + adj.cropH * 0.5f,
                        rotW, rotH, perspective);
                    const auto& signs = brushErase_[std::size_t(i)];
                    const float erasing =
                        (std::size_t(d) < signs.size() && signs[std::size_t(d)] != 0.0f)
                            ? 1.0f : 0.0f;
                    texels[std::size_t(d) * 4 + 0] = p.centerX;
                    texels[std::size_t(d) * 4 + 1] = p.centerY;
                    texels[std::size_t(d) * 4 + 2] = erasing;
                }
                // ⚠ **Before the upload, and before the boxes** — how much of
                // this stroke is the stroke that is already on the GPU.
                //
                // Nothing reads the answer yet. `ROADMAP.md` splits the
                // incremental accumulator in two on purpose: the predicate is
                // the whole risk, because the way it fails is a stale coverage
                // rendering a completely plausible brushstroke that no
                // screenshot and no perceptual check can see is wrong. So it is
                // built, measured and attacked here, a session before anything
                // depends on being able to trust it.
                //
                // ⚠ The nib is computed further down, from the crop, so it is
                // recomputed here rather than read from `m` — see the comment
                // on `m.nibPx` below. The two expressions must stay identical;
                // a predicate comparing a nib the kernel never sees would
                // accept a stroke whose every dab changed size.
                {
                    const float shownW = float(width_)  * std::max(adj.cropW, 1e-6f);
                    const float shownH = float(height_) * std::max(adj.cropH, 1e-6f);
                    const params::BrushShape shape{
                        c.kind, c.brushRadius * std::min(shownW, shownH),
                        c.brushFlow, c.brushHardness};

                    // ⚠ **One accumulator, one owner.** Taking it away from
                    // another component patches that component's parameters
                    // back to a full evaluation *before* anything else, or two
                    // kernels write one texture and what renders is a stroke
                    // nobody drew.
                    if (accumOwner_ != i && m.count > 0) {
                        if (accumOwner_ >= 0) {
                            auto& old = maskParams_[std::size_t(accumOwner_)];
                            if (old.accumUse != 0 || old.firstDab != 0) {
                                old.accumUse = 0;
                                old.firstDab = 0;
                                pipeline_.setParams(nMaskComponent_[accumOwner_],
                                                    &old, sizeof old);
                            }
                            brushPrev_[std::size_t(accumOwner_)]    = {};
                            brushPending_[std::size_t(accumOwner_)] = {};
                        }
                        accumOwner_ = i;
                        brushPrev_[std::size_t(i)]    = {};
                        brushPending_[std::size_t(i)] = {};
                        ensureBrushAccum();
                    }

                    auto& prev = brushPrev_[std::size_t(i)];
                    auto& stat = brushPrefix_[std::size_t(i)];
                    stat.prefix = params::unchangedPrefix(prev, shape,
                                                          texels.data(), m.count);
                    stat.previousCount = prev.live ? prev.count : 0;
                    stat.count = m.count;
                    ++stat.evaluations;

                    // ⚠ **The accumulator holds a whole stroke or it holds
                    // nothing usable.** `prefix` is the length of the stable
                    // head; the texture holds the coverage of `prev.count`
                    // dabs. Continuing is only sound when those are the same
                    // number — *undo three dabs and paint three different ones*
                    // gives a prefix shorter than what is on the GPU, and the
                    // three dabs the photographer took back are still in it.
                    const bool appended = accumOwner_ == i && prev.live &&
                                          stat.prefix == prev.count &&
                                          prev.count > 0 &&
                                          m.count >= prev.count &&
                                          m.accumulate == 0;
                    m.accumUse = (accumOwner_ == i) ? 1 : 0;
                    m.firstDab = appended ? prev.count : 0;
                    stat.firstDab = m.firstDab;

                    // The claim, not the fact. It becomes `brushPrev_` only
                    // once the pipeline reports having run this node — see
                    // `commitBrushAccum`. Only the live prefix is kept, not the
                    // padded buffer: a short stroke costs a few kilobytes and
                    // the 16,384-dab cap costs 256 KB.
                    auto& pending = brushPending_[std::size_t(i)];
                    pending.state.texels.assign(
                        texels.data(), texels.data() + std::size_t(m.count) * 4);
                    pending.state.count = m.count;
                    pending.state.shape = shape;
                    pending.state.live  = m.count > 0;
                    pending.valid = m.accumUse != 0;
                }

                pipeline_.updateAux(auxDabs_[std::size_t(i)], texels.data(),
                                    std::size_t(params::kDabStride) * 4 * sizeof(float));

                // One box per run of `kDabBlock` dabs, so the kernel can skip 64
                // fetches with one test. research/brush-acceleration.md.
                //
                // ⚠ Built from `texels` — the values actually uploaded — and by
                // the same function the GPU tests call. See `buildDabBounds`.
                std::vector<float> bounds(
                    std::size_t(params::kMaxDabBlocks) * 4, 0.0f);
                params::buildDabBounds(texels.data(), m.count, bounds.data());
                pipeline_.updateAux(auxDabBounds_[std::size_t(i)], bounds.data(),
                                    std::size_t(params::kMaxDabBlocks) * 4
                                        * sizeof(float));
            }

            // The nib, as a radius in *frame pixels*.
            //
            // The kernel used to measure the dab in normalized coordinates,
            // where one unit of x and one unit of y are different numbers of
            // pixels on any frame that is not square — so the nib was an
            // ellipse on screen and the Size slider stretched it rather than
            // growing it. A brush has to be round under the cursor.
            //
            // Measured against the *displayed* picture's shorter side, so the
            // nib keeps its size on screen as the picture is cropped tighter.
            const float shownW = float(width_)  * std::max(adj.cropW, 1e-6f);
            const float shownH = float(height_) * std::max(adj.cropH, 1e-6f);
            m.nibPx = c.brushRadius * std::min(shownW, shownH);

            // ⚠ Still a cap, at sixty-four times the old one: about eighty
            // frame-widths of stroke. Said out loud rather than left to be
            // discovered as "the end of my stroke did nothing".
            if (have > m.count) {
                std::fprintf(stderr,
                             "orion: brush stroke truncated, %d of %d dabs "
                             "(one component holds %d)\n",
                             m.count, have, params::kMaxDabs);
            }
        } else {
            // ⚠ A component that is not a brush has no stroke on the GPU, so
            // the stored one stops being evidence of anything. Kind 3 → 2 → 3
            // with the stroke untouched would otherwise report the whole thing
            // as an unchanged prefix, and session two's accumulator will have
            // been released and reallocated underneath that claim.
            brushPrev_[std::size_t(i)]    = {};
            brushPending_[std::size_t(i)] = {};
            if (accumOwner_ == i) accumOwner_ = -1;
        }
        maskParams_[std::size_t(i)] = m;
        pipeline_.setParams(nMaskComponent_[i], &m, sizeof m);
    }

    // ⚠ **Given back when the last brush goes away.** ~97 MB at 24 Mpx is not
    // something to keep for a row that was deleted or switched to a gradient;
    // a group of four gradients would otherwise be paying for an accumulator
    // nothing reads, which is the objection `ROADMAP.md` raised against having
    // one of these per component in the first place.
    {
        bool anyBrush = false;
        for (int i = 0; i < adj.maskCount && !anyBrush; ++i)
            anyBrush = adj.maskComponents[std::size_t(i)].kind == 3;
        if (!anyBrush) releaseBrushAccum();
    }
}

void DevelopPipeline::applyMaskRefine(const Adjustments& adj,
                                      const ApplyContext& ctx) {
    const bool first = ctx.first;
    const bool needsGuide = ctx.needsGuide;
    const bool refining = ctx.refining;
    const std::uint32_t size[2] = {width_, height_};

    // ── Guided feathering of the mask group (research/masking.md §4) ──────

    const bool wasRefining =
        lastAdj_.maskCount > 0 && lastAdj_.maskRefine > 0.0f;

    // ⚠ `guide:prep` is shared, and it is the one node of the self-guided chain
    // that mask refinement also reads. It must therefore be enabled if *either*
    // wants it — and it is deliberately not in the loop above any more.
    //
    // The failure this avoids is silent and ugly: a disabled node resolves to
    // its producer, so with highlights and shadows at zero the refine chain
    // would have been handed `huesat`'s RGBA16F output through a
    // `Texture2D<float2>` binding and read color components as a luminance and
    // its square. Not a crash — a plausible-looking wrong mask.
    if (first || (needsGuide || refining) !=
                 ((lastAdj_.highlights != 0.0f || lastAdj_.shadows != 0.0f) ||
                  wasRefining)) {
        pipeline_.setEnabled(nGuidePrep_, needsGuide || refining);
    }

    if (first || refining != wasRefining) {
        for (int i = 0; i < kMaxMaskComponents; ++i) {
            for (int n : {nMaskGuidePrep_[i], nMaskGuideH1_[i], nMaskGuideV1_[i],
                          nMaskGuideAb_[i], nMaskGuideH2_[i], nMaskGuideV2_[i],
                          nMaskRefine_[i]}) {
                pipeline_.setEnabled(n, refining);
            }
        }
    }

    if (first || adj.maskRefine != lastAdj_.maskRefine) {
        params::MaskGuideApply mga{};
        mga.size[0] = size[0];       mga.size[1] = size[1];
        mga.coeffSize[0] = guideW_;  mga.coeffSize[1] = guideH_;
        mga.strength = adj.maskRefine;
        for (int i = 0; i < kMaxMaskComponents; ++i)
            pipeline_.setParams(nMaskRefine_[i], &mga, sizeof mga);
    }
}

void DevelopPipeline::setBrushStroke(int component, const float* xy,
                                    const float* erase, int count) {
    // Ignored rather than clamped: a stroke written into the wrong component
    // would put paint somewhere the photographer did not.
    if (component < 0 || component >= kMaxMaskComponents) return;
    auto& dabs = brushDabs_[std::size_t(component)];
    auto& signs = brushErase_[std::size_t(component)];
    dabs.clear();
    signs.clear();
    if (xy == nullptr || count <= 0) return;
    dabs.assign(xy, xy + std::size_t(count) * 2);
    // A null `erase` is a stroke that paints throughout.
    signs.assign(std::size_t(count), 0.0f);
    if (erase != nullptr) signs.assign(erase, erase + std::size_t(count));
}

bool DevelopPipeline::setMaskMatte(int component, const float* alpha,
                                   int width, int height) {
    // Ignored rather than clamped, for the same reason a brush stroke is: a
    // matte written into the wrong component covers something nobody selected.
    if (component < 0 || component >= kMaxMaskComponents) return false;
    const auto slot = std::size_t(component);

    if (alpha == nullptr || width <= 0 || height <= 0) {
        matteLive_[slot][0] = 0;
        matteLive_[slot][1] = 0;
        matteDirty_[slot] = true;
        return true;
    }

    // ⚠ Rejected, not resampled. A producer that hands over more detail than
    // the aux texture holds has gone to some trouble for that boundary, and
    // quietly throwing half of it away — then refining the result and calling
    // it edge-aware — is worse than refusing.
    if (std::uint32_t(width) > matteW_ || std::uint32_t(height) > matteH_) {
        return false;
    }

    // The aux texture is allocated for the largest matte; a smaller one lands
    // in its top-left corner and `matteSize` tells the kernel how much is real.
    // Uploading only the live rows would leave whatever the last matte wrote
    // outside them, and the bilinear tap at the right and bottom edges reaches
    // one texel past — so the whole texture is written every time.
    std::vector<__fp16> full(std::size_t(matteW_) * matteH_, __fp16(0.0f));
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const float v = alpha[std::size_t(y) * std::size_t(width) + std::size_t(x)];
            full[std::size_t(y) * matteW_ + std::size_t(x)] =
                __fp16(std::clamp(v, 0.0f, 1.0f));
        }
    }
    pipeline_.updateAux(auxMatte_[slot], full.data(),
                        std::size_t(matteW_) * sizeof(__fp16));

    matteLive_[slot][0] = std::uint32_t(width);
    matteLive_[slot][1] = std::uint32_t(height);
    matteDirty_[slot] = true;
    return true;
}

void DevelopPipeline::ensureBrushAccum() {
    if (auxBrushAccum_ < 0) return;
    if (pipeline_.auxWidth(auxBrushAccum_) == width_) return;
    pipeline_.resizeAux(auxBrushAccum_, width_, height_);
    // Whatever any component believed was in there went with the old texture.
    for (int i = 0; i < kMaxMaskComponents; ++i) {
        brushPrev_[std::size_t(i)]    = {};
        brushPending_[std::size_t(i)] = {};
    }
}

void DevelopPipeline::releaseBrushAccum() {
    if (auxBrushAccum_ < 0) return;
    if (pipeline_.auxWidth(auxBrushAccum_) <= 1) return;
    pipeline_.resizeAux(auxBrushAccum_, 1, 1);
    accumOwner_ = -1;
    for (int i = 0; i < kMaxMaskComponents; ++i) {
        brushPrev_[std::size_t(i)]    = {};
        brushPending_[std::size_t(i)] = {};
        // ⚠ And the parameters, not only the record. The texture is 1x1 now:
        // a kernel still carrying `accumUse` would read zero everywhere past
        // the first texel, which is a stroke that quietly vanished rather than
        // anything that looks like a fault.
        auto& m = maskParams_[std::size_t(i)];
        if (m.accumUse == 0 && m.firstDab == 0) continue;
        m.accumUse = 0;
        m.firstDab = 0;
        pipeline_.setParams(nMaskComponent_[i], &m, sizeof m);
    }
}

void DevelopPipeline::reconcileBrushAccum() {
    // ⚠ **The last thing before the GPU runs, and that placement is the whole
    // argument.** Every other node here is a pure function of its inputs, so
    // running one twice is a waste and never a wrong answer. A node that
    // accumulates into a persistent texture is not: run it twice with one set
    // of parameters and the dabs in `[firstDab, count)` are composited twice —
    // a heavier stroke, in the right place, in the right shape, which is to say
    // a picture that looks entirely plausible and is not the photographer's.
    //
    // A node can be dirtied by things `apply` never sees: white balance moves
    // and the reference image behind every mask component changes, a matte is
    // uploaded, `setEnabled` brings a hidden component back. In each of those
    // the node runs again with whatever parameters it was last given. So the
    // rule is not "prove nothing else dirties it" — that is a claim about a
    // 2,500-line file that the next edit quietly breaks — but *ask*, here,
    // where nothing can intervene afterwards.
    //
    // `brushPending_[i].valid` says these parameters were computed by this
    // `apply` against the accumulator's current contents. Anything else that is
    // about to re-run with `firstDab > 0` is refused the fast path and lays the
    // whole stroke, which is exactly what it did before this feature existed.
    for (int i = 0; i < kMaxMaskComponents; ++i) {
        auto& m = maskParams_[std::size_t(i)];
        if (m.firstDab <= 0) continue;
        if (brushPending_[std::size_t(i)].valid) continue;
        if (!pipeline_.nodeDirty(nMaskComponent_[i])) continue;
        m.firstDab = 0;
        pipeline_.setParams(nMaskComponent_[i], &m, sizeof m);
        brushPrev_[std::size_t(i)] = {};
        brushPrefix_[std::size_t(i)].firstDab = 0;
        ++brushPrefix_[std::size_t(i)].refusals;
    }
}

std::size_t DevelopPipeline::brushAccumBytes() const {
    if (auxBrushAccum_ < 0) return 0;
    const std::uint32_t w = pipeline_.auxWidth(auxBrushAccum_);
    if (w <= 1) return 0;
    return std::size_t(w) * height_ * sizeof(float);
}

void DevelopPipeline::commitBrushAccum() {
    // ⚠ **Advanced from what ran, never from what was asked for.** The full
    // graph is given parameters on every pointer event and rendered once, when
    // the gesture ends; a hidden component is given parameters and never runs
    // at all. Recording the claim at push time would leave the host believing
    // the accumulator holds a stroke it was merely told about — the exact
    // out-of-step this whole design is guarding, arriving through the front
    // door.
    const auto& run = pipeline_.lastRun();
    for (int i = 0; i < kMaxMaskComponents; ++i) {
        auto& pending = brushPending_[std::size_t(i)];
        if (!pending.valid) continue;
        const std::string want = "mask:" + std::to_string(i);
        bool executed = false;
        for (const auto& n : run) {
            if (n.name == want) { executed = n.executed; break; }
        }
        if (executed) brushPrev_[std::size_t(i)] = std::move(pending.state);
        else          brushPrev_[std::size_t(i)] = {};
        pending = {};
    }
}

}  // namespace orion::pipe
