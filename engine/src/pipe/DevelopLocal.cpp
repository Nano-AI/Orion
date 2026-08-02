/*  The multi-node local operators, and the guided filter beside them.
 *
 *  Dehaze (16 nodes), local Laplacian clarity (32), exposure fusion (32) and
 *  He, Sun & Tang's guided filter (6) — most of the graph, and all four obeying
 *  one rule: **switched off entirely at zero rather than run at no strength**,
 *  because thirty-two full-resolution passes computing an identity are
 *  thirty-two passes of nothing.
 *
 *  Two of them need a whole-frame reduction that cannot be a node — the
 *  atmospheric light and the fusion plan — so those live here as well, beside
 *  the chains that read them. Decision #113.
 */
#include "pipe/DevelopPipeline.h"

#include "pipe/LensGeometry.h"
#include "pipe/ShaderParams.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <string>
#include <vector>


namespace orion::pipe {

void DevelopPipeline::buildLocalNodes() {
    using gpu::PixelFormat;

    // ── Dehaze, the dark channel prior (He, Sun & Tang) ───────────────────
    //
    // Before clarity, because the two claim different things: dehaze says it is
    // recovering the scene's true radiance and clarity is a look applied on
    // top. Physically the restoration comes first.
    //
    // In scene-linear light rather than on display-encoded pixels, which is a
    // departure from how the paper's own results were produced and a closer
    // reading of its Eq. (1): J*t + A(1-t) is a physical mixture, and a mixture
    // is only linear in linear light. research/dehaze.md.
    peakW_ = std::max(1u, (width_  + dehaze::kPeakScale - 1) / dehaze::kPeakScale);
    peakH_ = std::max(1u, (height_ + dehaze::kPeakScale - 1) / dehaze::kPeakScale);
    hazeW_ = std::max(1u, (width_  + dehaze::kGuideScale - 1) / dehaze::kGuideScale);
    hazeH_ = std::max(1u, (height_ + dehaze::kGuideScale - 1) / dehaze::kGuideScale);

    // The dark channel of the image itself — A = (1,1,1) — which is what the
    // candidates for the atmospheric light are ranked by.
    nDehazeChan_ = pipeline_.add({"dehaze:channel min", "dehazeChannelMin", {nHueSat_},
                                  PixelFormat::R16Float, {}});
    nDarkH_ = pipeline_.add({"dehaze:dark h", "dehazeRank", {nDehazeChan_},
                             PixelFormat::R16Float, {}});
    nDarkV_ = pipeline_.add({"dehaze:dark v", "dehazeRank", {nDarkH_},
                             PixelFormat::R16Float, {}});
    nPeak_  = pipeline_.add({"dehaze:candidates", "dehazePeak", {nDarkV_, nHueSat_},
                             PixelFormat::RGBA16Float, {}, {},
                             true, peakW_, peakH_});

    // The same minimum again, now normalized by A — Eq. (11)'s inner term.
    nDehazeChanA_ = pipeline_.add({"dehaze:channel min/A", "dehazeChannelMin", {nHueSat_},
                                   PixelFormat::R16Float, {}});
    nMinH_ = pipeline_.add({"dehaze:min h", "dehazeRank", {nDehazeChanA_},
                            PixelFormat::R16Float, {}});
    nMinV_ = pipeline_.add({"dehaze:min v", "dehazeRank", {nMinH_},
                            PixelFormat::R16Float, {}});
    // TPAMI 35 (2013) section 5: "we first apply a max filter to counteract the
    // morphological effects of the min filter". A minimum over a patch dilates
    // every dark object by its radius, so without this the transmission map is
    // systematically wide around each one.
    nMaxH_ = pipeline_.add({"dehaze:max h", "dehazeRank", {nMinV_},
                            PixelFormat::R16Float, {}});
    nMaxV_ = pipeline_.add({"dehaze:max v", "dehazeRank", {nMaxH_},
                            PixelFormat::R16Float, {}});

    // Guided-filter refinement, cross-guided: the guide is the hazy image and
    // the input is the transmission. The authors' own replacement for the
    // matting Laplacian they used in 2009 — "visually similar", and about 40 ms
    // against 10 seconds.
    nHazePrep_ = pipeline_.add({"dehaze:moments", "dehazePrep", {nHueSat_, nMaxV_},
                                PixelFormat::RGBA32Float, {}, {},
                                true, hazeW_, hazeH_});
    nHazeBlurH_ = pipeline_.add({"dehaze:blur h", "boxBlur4", {nHazePrep_},
                                 PixelFormat::RGBA32Float, {}, {}, true, hazeW_, hazeH_});
    nHazeBlurV_ = pipeline_.add({"dehaze:blur v", "boxBlur4", {nHazeBlurH_},
                                 PixelFormat::RGBA32Float, {}, {}, true, hazeW_, hazeH_});
    nHazeAb_    = pipeline_.add({"dehaze:coeffs", "dehazeAb", {nHazeBlurV_},
                                 PixelFormat::RG32Float, {}, {}, true, hazeW_, hazeH_});
    nHazeBlurH2_ = pipeline_.add({"dehaze:blur h2", "boxBlur", {nHazeAb_},
                                  PixelFormat::RG32Float, {}, {}, true, hazeW_, hazeH_});
    nHazeBlurV2_ = pipeline_.add({"dehaze:blur v2", "boxBlur", {nHazeBlurH2_},
                                  PixelFormat::RG32Float, {}, {}, true, hazeW_, hazeH_});

    // First input is the profile output, so a disabled chain resolves straight
    // through and dehaze at zero costs nothing.
    nDehaze_ = pipeline_.add({"dehaze", "dehazeRecover", {nHueSat_, nHazeBlurV2_},
                              PixelFormat::RGBA16Float, {}});

    // ── Local Laplacian clarity (Paris et al. 2011 / Aubry et al. 2014) ───
    //
    // Placed here, before the tone controls, for the same reason the guided
    // filter is: exposure is a multiply, so in log2 it is an additive constant,
    // and the Laplacian of a constant offset is zero. Computing clarity before
    // exposure therefore gives bit for bit what computing it after would, while
    // leaving all thirty-two of these nodes cached for the slider people
    // actually drag. research/local-laplacian.md.
    //
    // Thirty-two nodes and seven kernels, none over a hundred lines. The count
    // is the pyramid's, not the code's: six levels, and sixteen remapped copies
    // of the image packed four textures wide.
    for (int l = 0; l < kLlfLevels; ++l) {
        llfW_[l] = (l == 0) ? width_  : std::max(1u, (llfW_[l - 1] + 1) / 2);
        llfH_[l] = (l == 0) ? height_ : std::max(1u, (llfH_[l - 1] + 1) / 2);
    }

    // The channel the filter runs on: normalized log2 luminance. Reads the
    // profile output, not the tone output, so it describes the scene.
    nLlfLuma_ = pipeline_.add({"clarity:luma", "llfLuma", {nDehaze_},
                               PixelFormat::R16Float, {}});
    nLlfGauss_[0] = nLlfLuma_;

    // The input's own Gaussian pyramid. This is where g comes from — the value
    // that picks which two remappings get interpolated at each coefficient.
    for (int l = 1; l < kLlfLevels; ++l) {
        nLlfGauss_[l] = pipeline_.add({"clarity:gauss " + std::to_string(l),
                                       "llfDown", {nLlfGauss_[l - 1]},
                                       PixelFormat::R16Float, {}, {},
                                       true, llfW_[l], llfH_[l]});
    }

    // The eight remapped pyramids, four gammas to a texture. Level one fuses
    // the remapping into the first halving, so the full-resolution remapped
    // images are never stored — they are recomputed point-wise in
    // llf_collapse0, which is cheaper than a 194 MB round trip each.
    for (int l = 1; l < kLlfLevels; ++l) {
        for (int s = 0; s < kLlfStacks; ++s) {
            const std::string tag = "clarity:remap " + std::to_string(l) +
                                    "." + std::to_string(s);
            if (l == 1) {
                // Separable: remap and halve horizontally, then halve
                // vertically. The remapping is evaluated at five taps rather
                // than twenty-five, which is what the profile said to fix.
                nLlfRemapH_[s] = pipeline_.add({tag + " h", "llfRemapH", {nLlfLuma_},
                                                PixelFormat::RGBA16Float, {}, {},
                                                true, llfW_[1], height_});
                nLlfPack_[l][s] = pipeline_.add({tag + " v", "llfDownV", {nLlfRemapH_[s]},
                                                 PixelFormat::RGBA16Float, {}, {},
                                                 true, llfW_[1], llfH_[1]});
            } else {
                nLlfPack_[l][s] = pipeline_.add({tag, "llfDownPacked", {nLlfPack_[l - 1][s]},
                                                 PixelFormat::RGBA16Float, {}, {},
                                                 true, llfW_[l], llfH_[l]});
            }
        }
    }

    // Interpolate, difference and collapse in one pass per level, coarse to
    // fine. The output Laplacian pyramid is never written out: each level adds
    // its coefficient to the expanded level below it and is done.
    //
    // The coarsest level is the residual, taken from the input's own pyramid
    // unchanged — that is what makes this a detail filter and not a tone one.
    nLlfOut_[kLlfLevels - 1] = nLlfGauss_[kLlfLevels - 1];
    for (int l = kLlfLevels - 2; l >= 1; --l) {
        nLlfOut_[l] = pipeline_.add({"clarity:collapse " + std::to_string(l),
                                     "llfCollapse",
                                     {nLlfGauss_[l],
                                      nLlfPack_[l][0], nLlfPack_[l][1],
                                      nLlfPack_[l][2], nLlfPack_[l][3],
                                      nLlfPack_[l + 1][0], nLlfPack_[l + 1][1],
                                      nLlfPack_[l + 1][2], nLlfPack_[l + 1][3],
                                      nLlfOut_[l + 1]},
                                     PixelFormat::R16Float, {}, {},
                                     true, llfW_[l], llfH_[l]});
    }
    nLlfOut_[0] = pipeline_.add({"clarity:collapse 0", "llfCollapse0",
                                 {nLlfLuma_,
                                  nLlfPack_[1][0], nLlfPack_[1][1],
                                  nLlfPack_[1][2], nLlfPack_[1][3],
                                  nLlfOut_[1]},
                                 PixelFormat::R16Float, {}});

    // Back onto the picture as one scale factor per pixel, which is Paris et
    // al.'s color ratios and is what keeps hue and saturation still.
    //
    // First input is the profile output on purpose: a disabled node resolves
    // to its first input, so clarity at zero costs exactly nothing.
    nClarity_ = pipeline_.add({"clarity", "llfApply",
                               {nDehaze_, nLlfOut_[0], nLlfLuma_},
                               PixelFormat::RGBA16Float, {}});

    // ── Simulated exposure fusion (Hessel & Morel) ────────────────────────
    //
    // After clarity and before the tone controls, for the reason every local
    // operator in this pipeline sits there: exposure is a multiply, so the
    // whole chain stays cached while the slider people drag actually moves.
    // research/exposure-fusion.md carries the placement argument in full.
    for (int l = 0; l < kFuseLevels; ++l) {
        fuseW_[l] = (l == 0) ? std::max(1u, (width_  + kFuseScale - 1) / kFuseScale)
                             : std::max(1u, (fuseW_[l - 1] + 1) / 2);
        fuseH_[l] = (l == 0) ? std::max(1u, (height_ + kFuseScale - 1) / kFuseScale)
                             : std::max(1u, (fuseH_[l - 1] + 1) / 2);
    }

    nFuseProxy_ = pipeline_.add({"fusion:proxy", "fuseProxy", {nClarity_},
                                 PixelFormat::R16Float, {}, {},
                                 true, fuseW_[0], fuseH_[0]});

    // Level zero of both stacks is point-wise in the proxy; every coarser level
    // is the same halving the local Laplacian filter already uses.
    for (int l = 0; l < kFuseLevels; ++l) {
        for (int st = 0; st < kFuseStacks; ++st) {
            const std::string tag = "." + std::to_string(l) + "." + std::to_string(st);
            nFuseImage_[l][st] = (l == 0)
                ? pipeline_.add({"fusion:images" + tag, "fuseSplit", {nFuseProxy_},
                                 PixelFormat::RGBA16Float, {}, {},
                                 true, fuseW_[0], fuseH_[0]})
                : pipeline_.add({"fusion:images" + tag, "llfDownPacked",
                                 {nFuseImage_[l - 1][st]},
                                 PixelFormat::RGBA16Float, {}, {},
                                 true, fuseW_[l], fuseH_[l]});

            nFuseWeight_[l][st] = (l == 0)
                ? pipeline_.add({"fusion:weights" + tag, "fuseSplit", {nFuseProxy_},
                                 PixelFormat::RGBA16Float, {}, {},
                                 true, fuseW_[0], fuseH_[0]})
                : pipeline_.add({"fusion:weights" + tag, "llfDownPacked",
                                 {nFuseWeight_[l - 1][st]},
                                 PixelFormat::RGBA16Float, {}, {},
                                 true, fuseW_[l], fuseH_[l]});
        }
    }

    // Blend and collapse in one pass per level, coarse to fine. The coarsest is
    // the residual — carried whole, which is what makes this a reconstruction
    // rather than a sum of high-pass bands.
    for (int l = kFuseLevels - 1; l >= 0; --l) {
        const bool residual = (l == kFuseLevels - 1);
        const int  coarse   = residual ? l : l + 1;
        nFuseOut_[l] = pipeline_.add({"fusion:blend " + std::to_string(l), "fuseBlend",
                                      {nFuseImage_[l][0], nFuseImage_[l][1],
                                       nFuseWeight_[l][0], nFuseWeight_[l][1],
                                       nFuseImage_[coarse][0], nFuseImage_[coarse][1],
                                       residual ? nFuseWeight_[l][0] : nFuseOut_[l + 1]},
                                      PixelFormat::R16Float, {}, {},
                                      true, fuseW_[l], fuseH_[l]});
    }

    // First input is the clarity output, so a disabled chain resolves straight
    // through and fusion at zero costs nothing.
    nFusion_ = pipeline_.add({"fusion", "fuseApply",
                              {nClarity_, nFuseOut_[0], nFuseProxy_},
                              PixelFormat::RGBA16Float, {}});

    // Every scene-linear adjustment fuses into one dispatch, and the display
    // transform plus curve into another. They are all pointwise; separate
    // passes only bought a 194 MB round trip each at 24 MP.
    // ── Guided filter (He, Sun & Tang) ────────────────────────────────────
    // Sits before exposure on purpose: exposure is a multiply, so in log2 it is
    // an add the tone node applies for free — which keeps this whole six-pass
    // chain cached while the exposure slider moves.
    nGuidePrep_ = pipeline_.add({"guide:prep", "guidePrep", {nHueSat_},
                                 PixelFormat::RG32Float, {}});
    // Subsample before filtering (He & Sun, 2015). Everything from here to the
    // coefficients runs on a grid sixteen times smaller.
    guideW_ = std::max(1u, (width_ + kGuideScale - 1) / kGuideScale);
    guideH_ = std::max(1u, (height_ + kGuideScale - 1) / kGuideScale);
    nGuideDown_ = pipeline_.add({"guide:subsample", "guideDown", {nGuidePrep_},
                                 PixelFormat::RG32Float, {}, {},
                                 true, guideW_, guideH_});

    nGuideH1_   = pipeline_.add({"guide:blur h", "boxBlur", {nGuideDown_},
                                 PixelFormat::RG32Float, {}, {},
                                 true, guideW_, guideH_});
    nGuideV1_   = pipeline_.add({"guide:blur v", "boxBlur", {nGuideH1_},
                                 PixelFormat::RG32Float, {}, {},
                                 true, guideW_, guideH_});
    nGuideAb_   = pipeline_.add({"guide:coeffs", "guideAb", {nGuideV1_},
                                 PixelFormat::RG32Float, {}, {},
                                 true, guideW_, guideH_});
    nGuideH2_   = pipeline_.add({"guide:blur h2", "boxBlur", {nGuideAb_},
                                 PixelFormat::RG32Float, {}, {},
                                 true, guideW_, guideH_});
    nGuideV2_   = pipeline_.add({"guide:blur v2", "boxBlur", {nGuideH2_},
                                 PixelFormat::RG32Float, {}, {},
                                 true, guideW_, guideH_});
}

void DevelopPipeline::pushStaticLocalParams() {
    const std::uint32_t size[2] = {width_, height_};

    // Guided filter parameters. Radius scales with the frame so the effect
    // covers the same fraction of the picture regardless of megapixels;
    // epsilon is in squared log2-exposure units, and 0.04 is about a fifth of
    // a stop — below that is texture and noise, above it is an edge.
    // The radius is subsampled along with the image, so the filter still covers
    // the same fraction of the picture.
    const int fullRadius =
        std::max(4, static_cast<int>(std::max(width_, height_) / 200));
    const int guideRadius = std::max(2, fullRadius / kGuideScale);

    params::GuidePrep gp{{size[0], size[1]}, {0, 0}};
    pipeline_.setParams(nGuidePrep_, &gp, sizeof gp);

    params::GuideDown gd{};
    gd.outSize[0] = guideW_; gd.outSize[1] = guideH_;
    gd.inSize[0] = size[0];  gd.inSize[1] = size[1];
    gd.scale = kGuideScale;
    pipeline_.setParams(nGuideDown_, &gd, sizeof gd);

    params::BoxBlur bh{{guideW_, guideH_}, guideRadius, 1};
    params::BoxBlur bv{{guideW_, guideH_}, guideRadius, 0};
    pipeline_.setParams(nGuideH1_, &bh, sizeof bh);
    pipeline_.setParams(nGuideV1_, &bv, sizeof bv);
    pipeline_.setParams(nGuideH2_, &bh, sizeof bh);
    pipeline_.setParams(nGuideV2_, &bv, sizeof bv);

    params::GuideAb ga{{guideW_, guideH_}, 0.04f, 0.0f};
    pipeline_.setParams(nGuideAb_, &ga, sizeof ga);
}

void DevelopPipeline::applyDehaze(const Adjustments& adj,
                                  const ApplyContext& ctx) {
    const bool first = ctx.first;

    // ── Dehaze ───────────────────────────────────────────────────────────
    //
    // Sixteen nodes, so the same rule as the guided filter, the denoiser and
    // clarity: switched off entirely at zero rather than run at no strength.
    dehazing_ = adj.dehaze > 1e-4f;
    const bool hazeMoved = first || adj.dehaze != lastAdj_.dehaze;

    // A is estimated from everything upstream of this chain, so white balance
    // invalidates it and nothing downstream does.
    if (first || adj.wb.temperatureK != lastAdj_.wb.temperatureK ||
        adj.wb.tint != lastAdj_.wb.tint) {
        airlightValid_ = false;
    }

    // A different photograph through the same graph. The size-derived blocks
    // below happen to still be right, but `first` is the one signal that says
    // "nothing that was pushed can be assumed", so it says it here too.
    if (first) hazeShapeValid_ = false;

    if (hazeMoved) {
        for (int n : {nDehazeChan_, nDarkH_, nDarkV_, nPeak_, nDehazeChanA_,
                      nMinH_, nMinV_, nMaxH_, nMaxV_, nHazePrep_, nHazeBlurH_,
                      nHazeBlurV_, nHazeAb_, nHazeBlurH2_, nHazeBlurV2_, nDehaze_}) {
            pipeline_.setEnabled(n, dehazing_);
        }
    }

    // ⚠ **Only omega moves with the slider.** Everything else in this chain is
    // a function of the frame's size, the paper's constants and A — so it is
    // pushed once and then left alone.
    //
    // It used to be pushed on every tick, and `setParams` dirties the whole
    // downstream subgraph whether or not the bytes changed. That put the
    // *entire* dark-channel and rank chain — nine nodes, six of them
    // full-resolution rank passes over 24 MP — back on the queue for a value
    // none of them read. Measured on _PIC8220: a dehaze tick dispatched 55
    // nodes where 46 was the honest number, and the nine were the expensive
    // ones. Same shape as the lens-vignette bug the `exposure drag, lens on`
    // invariant exists for; `dehaze drag` is now the invariant for this one.
    if (dehazing_ && !hazeShapeValid_) {
        // The dark channel of the image itself: A = (1,1,1).
        params::DehazeChan plain{};
        plain.size[0] = width_; plain.size[1] = height_;
        plain.airlight[0] = plain.airlight[1] = plain.airlight[2] = 1.0f;
        pipeline_.setParams(nDehazeChan_, &plain, sizeof plain);

        const auto rank = [&](int node, bool horizontal, bool maximum) {
            params::DehazeRank r{};
            r.size[0] = width_; r.size[1] = height_;
            r.radius = dehaze::kPatchRadius;
            r.horizontal = horizontal ? 1 : 0;
            r.maximum = maximum ? 1 : 0;
            pipeline_.setParams(node, &r, sizeof r);
        };
        rank(nDarkH_, true,  false);
        rank(nDarkV_, false, false);
        rank(nMinH_,  true,  false);
        rank(nMinV_,  false, false);
        rank(nMaxH_,  true,  true);
        rank(nMaxV_,  false, true);

        params::DehazePeak peak{};
        peak.outSize[0] = peakW_; peak.outSize[1] = peakH_;
        peak.inSize[0]  = width_; peak.inSize[1]  = height_;
        peak.scale = dehaze::kPeakScale;
        pipeline_.setParams(nPeak_, &peak, sizeof peak);

        // The paper's radius is a fraction of the frame, not a pixel count.
        const int fullRadius =
            std::max(1, int(std::max(width_, height_)) / dehaze::kGuideRadiusDivisor);
        const int radius = std::max(1, fullRadius / dehaze::kGuideScale);

        params::BoxBlur4 b4h{{hazeW_, hazeH_}, radius, 1};
        params::BoxBlur4 b4v{{hazeW_, hazeH_}, radius, 0};
        pipeline_.setParams(nHazeBlurH_, &b4h, sizeof b4h);
        pipeline_.setParams(nHazeBlurV_, &b4v, sizeof b4v);

        params::DehazeAb ab{{hazeW_, hazeH_}, dehaze::kEpsilon, 0.0f};
        pipeline_.setParams(nHazeAb_, &ab, sizeof ab);

        params::BoxBlur b2h{{hazeW_, hazeH_}, radius, 1};
        params::BoxBlur b2v{{hazeW_, hazeH_}, radius, 0};
        pipeline_.setParams(nHazeBlurH2_, &b2h, sizeof b2h);
        pipeline_.setParams(nHazeBlurV2_, &b2v, sizeof b2v);

        pushAirlight();
        hazeShapeValid_ = true;
    }

    // The slider itself. One node, at a quarter of the frame's resolution, and
    // everything below it in the chain follows from there.
    if (dehazing_ && hazeMoved) {
        params::DehazePrep prep{};
        prep.outSize[0] = hazeW_; prep.outSize[1] = hazeH_;
        prep.inSize[0]  = width_; prep.inSize[1]  = height_;
        prep.scale = dehaze::kGuideScale;
        // The slider *is* omega. Zero gives t = 1 and Eq. (16) is the identity.
        prep.omega = dehaze::kOmega * std::clamp(adj.dehaze, 0.0f, 1.0f);
        prep.lo = llf::kWindowLoEv;
        prep.invRange = 1.0f / llf::kWindowEv;
        pipeline_.setParams(nHazePrep_, &prep, sizeof prep);
    }
}

void DevelopPipeline::applyFusion(const Adjustments& adj,
                                  const ApplyContext& ctx) {
    const bool first = ctx.first;

    // ── Exposure fusion ──────────────────────────────────────────────────
    //
    // Thirty-two nodes, all at quarter resolution. Same rule as every other
    // multi-node feature here: switched off entirely at zero.
    fusing_ = adj.fusion > 1e-4f;
    const bool fusionMoved = first || adj.fusion != lastAdj_.fusion;

    // The plan comes from the frame's median, which everything upstream of this
    // chain can move and no slider below it can.
    if (first || adj.wb.temperatureK != lastAdj_.wb.temperatureK ||
        adj.wb.tint != lastAdj_.wb.tint) {
        fusePlanValid_ = false;
    }

    if (fusionMoved) {
        pipeline_.setEnabled(nFuseProxy_, fusing_);
        pipeline_.setEnabled(nFusion_, fusing_);
        for (int l = 0; l < kFuseLevels; ++l) {
            pipeline_.setEnabled(nFuseOut_[l], fusing_);
            for (int st = 0; st < kFuseStacks; ++st) {
                pipeline_.setEnabled(nFuseImage_[l][st], fusing_);
                pipeline_.setEnabled(nFuseWeight_[l][st], fusing_);
            }
        }
    }

    if (fusing_ && (fusionMoved || !fusePlanValid_)) {
        params::FuseProxy proxy{};
        proxy.outSize[0] = fuseW_[0]; proxy.outSize[1] = fuseH_[0];
        proxy.inSize[0]  = width_;    proxy.inSize[1]  = height_;
        proxy.scale = kFuseScale;
        proxy.slope = sef::kProxySlopeEv;
        pipeline_.setParams(nFuseProxy_, &proxy, sizeof proxy);

        for (int l = 1; l < kFuseLevels; ++l) {
            params::LlfDown d{{fuseW_[l], fuseH_[l]}, {fuseW_[l - 1], fuseH_[l - 1]}};
            for (int st = 0; st < kFuseStacks; ++st) {
                pipeline_.setParams(nFuseImage_[l][st], &d, sizeof d);
                pipeline_.setParams(nFuseWeight_[l][st], &d, sizeof d);
            }
        }

        params::FuseApply ap{};
        ap.size[0] = width_;       ap.size[1] = height_;
        ap.proxySize[0] = fuseW_[0]; ap.proxySize[1] = fuseH_[0];
        ap.slope    = sef::kProxySlopeEv;
        ap.strength = std::clamp(adj.fusion, 0.0f, 1.0f);
        ap.maxGain  = sef::kMaxGain;
        pipeline_.setParams(nFusion_, &ap, sizeof ap);

        pushFusionPlan();
    }
}

void DevelopPipeline::applyClarity(const Adjustments& adj,
                                   const ApplyContext& ctx) {
    const bool first = ctx.first;

    // ── Local Laplacian clarity ──────────────────────────────────────────
    //
    // Thirty-two nodes, so it follows the guided filter's and the denoiser's
    // rule: switched off entirely at zero rather than run at no strength.
    const bool clarifying   = std::fabs(adj.clarity) > 1e-4f;
    const bool clarityMoved = first || adj.clarity != lastAdj_.clarity;

    if (clarityMoved) {
        pipeline_.setEnabled(nLlfLuma_, clarifying);
        pipeline_.setEnabled(nClarity_, clarifying);
        for (int l = 1; l < kLlfLevels; ++l) {
            pipeline_.setEnabled(nLlfGauss_[l], clarifying);
            for (int st = 0; st < kLlfStacks; ++st) {
                pipeline_.setEnabled(nLlfPack_[l][st], clarifying);
                if (l == 1) pipeline_.setEnabled(nLlfRemapH_[st], clarifying);
            }
        }
        for (int l = 0; l <= kLlfLevels - 2; ++l) {
            pipeline_.setEnabled(nLlfOut_[l], clarifying);
        }
    }

    if (clarifying && clarityMoved) {
        const float alpha = llf::alphaForClarity(adj.clarity);

        params::LlfLuma lum{{width_, height_}, llf::kWindowLoEv,
                            1.0f / llf::kWindowEv};
        pipeline_.setParams(nLlfLuma_, &lum, sizeof lum);

        for (int l = 1; l < kLlfLevels; ++l) {
            params::LlfDown d{{llfW_[l], llfH_[l]}, {llfW_[l - 1], llfH_[l - 1]}};
            pipeline_.setParams(nLlfGauss_[l], &d, sizeof d);

            for (int st = 0; st < kLlfStacks; ++st) {
                if (l == 1) {
                    params::LlfRemapH r{};
                    r.outSize[0] = llfW_[1]; r.outSize[1] = height_;
                    r.inSize[0]  = width_;   r.inSize[1]  = height_;
                    // Each texture carries four consecutive gammas, so the
                    // stack index is where its first one sits on the range.
                    r.gamma0    = float(st * 4) * llf::kGammaStep;
                    r.gammaStep = llf::kGammaStep;
                    r.sigmaR    = llf::kSigmaR;
                    r.alpha     = alpha;
                    r.noiseLo   = llf::kNoiseLo;
                    r.noiseHi   = llf::kNoiseHi;
                    pipeline_.setParams(nLlfRemapH_[st], &r, sizeof r);

                    params::LlfDown v{{llfW_[1], llfH_[1]}, {llfW_[1], height_}};
                    pipeline_.setParams(nLlfPack_[l][st], &v, sizeof v);
                } else {
                    pipeline_.setParams(nLlfPack_[l][st], &d, sizeof d);
                }
            }
        }

        for (int l = kLlfLevels - 2; l >= 1; --l) {
            params::LlfCollapse c{};
            c.size[0] = llfW_[l];           c.size[1] = llfH_[l];
            c.coarseSize[0] = llfW_[l + 1]; c.coarseSize[1] = llfH_[l + 1];
            c.gammaStep  = llf::kGammaStep;
            c.gammaCount = llf::kGammaLevels;
            pipeline_.setParams(nLlfOut_[l], &c, sizeof c);
        }

        params::LlfCollapse0 c0{};
        c0.size[0] = width_;         c0.size[1] = height_;
        c0.coarseSize[0] = llfW_[1]; c0.coarseSize[1] = llfH_[1];
        c0.gammaStep  = llf::kGammaStep;
        c0.gammaCount = llf::kGammaLevels;
        c0.sigmaR     = llf::kSigmaR;
        c0.alpha      = alpha;
        c0.noiseLo    = llf::kNoiseLo;
        c0.noiseHi    = llf::kNoiseHi;
        pipeline_.setParams(nLlfOut_[0], &c0, sizeof c0);

        params::LlfApply ap{{width_, height_}, llf::kWindowEv,
                            llf::kMaxCorrectionEv};
        pipeline_.setParams(nClarity_, &ap, sizeof ap);
    }
}

/// Whether the guided filter runs at all.
///
/// ⚠ `ctx.needsGuide` is derived in `contextFor` rather than here, because
/// two other stages read it: `develop:linear` is told whether the guide
/// textures hold what it thinks they hold, and the mask group's refine chain
/// shares `guide:prep` with this one.

void DevelopPipeline::applyGuide(const Adjustments& adj,
                                 const ApplyContext& ctx) {
    const bool first = ctx.first;
    const bool needsGuide = ctx.needsGuide;

    if (first || needsGuide != (lastAdj_.highlights != 0.0f ||
                                lastAdj_.shadows != 0.0f)) {
        for (int n : {nGuideDown_, nGuideH1_, nGuideV1_,
                      nGuideAb_, nGuideH2_, nGuideV2_}) {
            pipeline_.setEnabled(n, needsGuide);
        }
    }
}

void DevelopPipeline::pushFusionPlan() {
    params::FusePlanBlock plan{};
    plan.images = fusePlan_.images;
    plan.bright = fusePlan_.bright;
    plan.dark   = fusePlan_.dark;
    plan.span   = fusePlan_.span();
    plan.alpha  = sef::kAlphaDefault;
    plan.beta   = sef::kBeta;
    plan.lambda = sef::kLambda;
    plan.sigma  = sef::kSigma;

    for (int st = 0; st < kFuseStacks; ++st) {
        params::FuseSplit sp{};
        sp.size[0] = fuseW_[0]; sp.size[1] = fuseH_[0];
        sp.base    = st * 4;
        sp.plan    = plan;
        sp.epsilon = sef::kWeightEpsilon;

        sp.weights = 0;
        pipeline_.setParams(nFuseImage_[0][st], &sp, sizeof sp);
        sp.weights = 1;
        pipeline_.setParams(nFuseWeight_[0][st], &sp, sizeof sp);
    }

    for (int l = kFuseLevels - 1; l >= 0; --l) {
        const bool residual = (l == kFuseLevels - 1);
        params::FuseBlend b{};
        b.size[0] = fuseW_[l]; b.size[1] = fuseH_[l];
        const int coarse = residual ? l : l + 1;
        b.coarseSize[0] = fuseW_[coarse]; b.coarseSize[1] = fuseH_[coarse];
        b.images   = fusePlan_.images;
        b.residual = residual ? 1 : 0;
        pipeline_.setParams(nFuseOut_[l], &b, sizeof b);
    }
}

void DevelopPipeline::estimateFusionPlan() {
    const gpu::Texture& tex = pipeline_.nodeOutput(nFuseProxy_);

    std::vector<__fp16> buf(static_cast<std::size_t>(fuseW_[0]) * fuseH_[0]);
    tex.download(buf.data(), static_cast<std::size_t>(fuseW_[0]) * sizeof(__fp16),
                 fuseW_[0], fuseH_[0]);

    std::vector<float> values(buf.size());
    for (std::size_t i = 0; i < buf.size(); ++i) values[i] = float(buf[i]);

    float median = 0.5f;
    if (!values.empty()) {
        const auto mid = values.begin() + static_cast<std::ptrdiff_t>(values.size() / 2);
        std::nth_element(values.begin(), mid, values.end());
        median = *mid;
    }

    fusePlan_ = sef::planFor(median, sef::kAlphaDefault, sef::kBeta);
    fusePlanValid_ = true;
    pushFusionPlan();
}

void DevelopPipeline::pushAirlight() {
    params::DehazeChan chan{};
    chan.size[0] = width_; chan.size[1] = height_;
    for (int c = 0; c < 3; ++c) chan.airlight[c] = airlight_[c];
    pipeline_.setParams(nDehazeChanA_, &chan, sizeof chan);

    params::DehazeRecover rec{};
    rec.size[0] = width_; rec.size[1] = height_;
    rec.coeffSize[0] = hazeW_; rec.coeffSize[1] = hazeH_;
    rec.t0 = dehaze::kT0;
    rec.lo = llf::kWindowLoEv;
    rec.invRange = 1.0f / llf::kWindowEv;
    for (int c = 0; c < 3; ++c) rec.airlight[c] = airlight_[c];
    pipeline_.setParams(nDehaze_, &rec, sizeof rec);
}

void DevelopPipeline::estimateAirlight() {
    const gpu::Texture& tex = pipeline_.nodeOutput(nPeak_);

    std::vector<__fp16> buf(static_cast<std::size_t>(peakW_) * peakH_ * 4);
    tex.download(buf.data(), static_cast<std::size_t>(peakW_) * 4 * sizeof(__fp16),
                 peakW_, peakH_);

    std::vector<dehaze::Candidate> cand(static_cast<std::size_t>(peakW_) * peakH_);
    for (std::size_t i = 0; i < cand.size(); ++i) {
        cand[i] = {float(buf[i * 4 + 0]), float(buf[i * 4 + 1]),
                   float(buf[i * 4 + 2]), float(buf[i * 4 + 3])};
    }

    airlight_ = dehaze::airlightFrom(std::move(cand));
    airlightValid_ = true;
    pushAirlight();
}

}  // namespace orion::pipe
