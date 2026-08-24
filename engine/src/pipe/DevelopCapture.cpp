/*  The capture chain: everything between the sensor and the camera profile.
 *
 *  Both halves of it — the nodes `DevelopPipeline`'s constructor adds, and the
 *  parameter blocks `apply` pushes into them. That pairing is the point of
 *  decision #113: adding a node here is one file, not a constructor at the top
 *  of one file and a push twelve hundred lines below it.
 *
 *  In graph order: linearize, RCD demosaic, highlight recovery, the harmonic
 *  fill, the profiled wavelet denoise, the lens correction, spot removal,
 *  capture sharpening, the colour matrix, the profile's hue/sat table.
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

namespace orion::pipe {
namespace {

// CIE XYZ (D65) -> linear Rec.2020, the working space.
constexpr float kXyzToRec2020[9] = {
     1.7166512f, -0.3556708f, -0.2533663f,
    -0.6666844f,  1.6164812f,  0.0157685f,
     0.0176399f, -0.0427706f,  0.9421031f,
};

bool invert3x3(const float m[9], float out[9]) {
    const float det = m[0] * (m[4] * m[8] - m[5] * m[7])
                    - m[1] * (m[3] * m[8] - m[5] * m[6])
                    + m[2] * (m[3] * m[7] - m[4] * m[6]);
    if (std::abs(det) < 1e-12f) return false;

    const float k = 1.0f / det;
    out[0] = (m[4] * m[8] - m[5] * m[7]) * k;
    out[1] = (m[2] * m[7] - m[1] * m[8]) * k;
    out[2] = (m[1] * m[5] - m[2] * m[4]) * k;
    out[3] = (m[5] * m[6] - m[3] * m[8]) * k;
    out[4] = (m[0] * m[8] - m[2] * m[6]) * k;
    out[5] = (m[2] * m[3] - m[0] * m[5]) * k;
    out[6] = (m[3] * m[7] - m[4] * m[6]) * k;
    out[7] = (m[1] * m[6] - m[0] * m[7]) * k;
    out[8] = (m[0] * m[4] - m[1] * m[3]) * k;
    return true;
}

void multiply3x3(const float a[9], const float b[9], float out[9]) {
    for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 3; ++c)
            out[r * 3 + c] = a[r * 3 + 0] * b[0 * 3 + c]
                           + a[r * 3 + 1] * b[1 * 3 + c]
                           + a[r * 3 + 2] * b[2 * 3 + c];
}

/// Scales each row to sum to 1 so camera (1,1,1) maps to working (1,1,1).
/// Without this the white balance and the color matrix fight each other: the
/// data is already neutral after WB, and an unnormalized matrix then tints it.
/// dcraw normalizes rgb_cam for exactly this reason.
void normalizeRows(float m[9]) {
    for (int r = 0; r < 3; ++r) {
        const float sum = m[r * 3 + 0] + m[r * 3 + 1] + m[r * 3 + 2];
        if (std::abs(sum) < 1e-9f) continue;
        for (int c = 0; c < 3; ++c) m[r * 3 + c] /= sum;
    }
}
}  // namespace

void DevelopPipeline::buildCaptureNodes() {
    using gpu::PixelFormat;

    // ── The linear head ───────────────────────────────────────────────────
    //
    // A demosaiced source (a merged HDR DNG) needs exactly one of this
    // region's jobs: the white-balance gains and the clip to the brightest
    // describable neutral. One node stands in for linearize, the RCD chain,
    // the highlight machinery and the denoise pyramid — and the absent nodes
    // are not built at all, because built-but-disabled nodes still own their
    // full-resolution textures.
    //
    // ⚠ It is deliberately named into nLinearize_: the node takes the same
    // parameter block as linearize (see linear_source.slang), so
    // applyWhiteBalance pushes to "the graph's head" with no per-mode branch.
    // The ids of the skipped nodes stay -1, which is what the guards in
    // applyHighlights and applyDenoise key on.
    if (linearSource_) {
        nLinearize_ = pipeline_.add({"linear source", "linearSource", {kSource},
                                     PixelFormat::RGBA16Float, {}});

        nLens_ = pipeline_.add({"lens", "lensCorrect", {nLinearize_},
                                PixelFormat::RGBA16Float, {}});
        buildCaptureTail();
        return;
    }

    nLinearize_ = pipeline_.add({"linearize", "linearize", {kSource},
                                 PixelFormat::R32Float, {}});
    nDirs_      = pipeline_.add({"rcd:dirs", "rcdDirs", {nLinearize_},
                                 PixelFormat::R32Float, {}});
    nLpf_       = pipeline_.add({"rcd:lowpass", "rcdLpf", {nLinearize_},
                                 PixelFormat::R32Float, {}});
    nGreen_     = pipeline_.add({"rcd:green", "rcdGreen", {nLinearize_, nDirs_, nLpf_},
                                 PixelFormat::R32Float, {}});
    nRgb_       = pipeline_.add({"rcd:red/blue", "rcdRedBlue",
                                 {nLinearize_, nGreen_, nDirs_},
                                 PixelFormat::RGBA16Float, {}});
    // ── Highlight reconstruction (Masood, Zhu & Tappen) ─────────────────────
    //
    // Straight after the demosaic, so it has all three channels per pixel, and
    // before everything else, so no later stage ever sees the false color that
    // per-channel clipping produces. Still in linear camera RGB, which is where
    // the clipping levels are known.
    nHighlights_ = pipeline_.add({"highlights", "highlightRecover", {nRgb_},
                                  PixelFormat::RGBA16Float, {}});

    // ── The harmonic fill, over what the window fit cannot reach ──────────
    //
    // Rouf, Lau & Heidrich (PROCAMS 2012) §3.2 as Gortler et al.'s (SIGGRAPH
    // 1996 §3.5.1) pull-push. research/highlight-reconstruction.md.
    //
    // Immediately after the window fit and reading its output, which is the
    // ordering that makes the two one feature rather than two: `highlights`
    // recovers the partial-clip annulus round a blown light from real
    // cross-channel evidence, and this carries *that* — the recovered annulus,
    // not the raw one — across the fully blown core it declines to touch.
    //
    // The whole chain runs on a grid `kSolveScale` times coarser. See the note
    // on the members.
    {
        const int scale = hlfill::kSolveScale;
        hlW_[0] = std::max(1u, (width_  + scale - 1) / scale);
        hlH_[0] = std::max(1u, (height_ + scale - 1) / scale);
        hlLevels_ = std::min(kHlMaxLevels,
                             hlfill::levelsFor(int(hlW_[0]), int(hlH_[0])));
        for (int l = 1; l < hlLevels_; ++l) {
            hlW_[l] = std::max(1u, (hlW_[l - 1] + 1) / 2);
            hlH_[l] = std::max(1u, (hlH_[l - 1] + 1) / 2);
        }

        // Both sides of `highlights`, because "did the window fit recover this
        // channel" is `rec > raw` and cannot be read off either texture alone.
        // Decision #109; no node and no texture is added by asking — `nRgb_`
        // already exists and is already live at this point in the graph.
        nHlMask_ = pipeline_.add({"hl:mask", "hlMask", {nHighlights_, nRgb_},
                                  PixelFormat::RGBA16Float, {}, {},
                                  true, hlW_[0], hlH_[0]});
        nHlPull_[0] = nHlMask_;
        for (int l = 1; l < hlLevels_; ++l) {
            nHlPull_[l] = pipeline_.add({"hl:pull " + std::to_string(l), "hlPull",
                                         {nHlPull_[l - 1]},
                                         PixelFormat::RGBA16Float, {}, {},
                                         true, hlW_[l], hlH_[l]});
        }

        nHlPush_[hlLevels_ - 1] = nHlPull_[hlLevels_ - 1];
        for (int l = hlLevels_ - 2; l >= 0; --l) {
            nHlPush_[l] = pipeline_.add({"hl:push " + std::to_string(l), "hlPush",
                                         {nHlPull_[l], nHlPush_[l + 1]},
                                         PixelFormat::RGBA16Float, {}, {},
                                         true, hlW_[l], hlH_[l]});
        }

        // First input is the picture, so a disabled node resolves straight
        // through and the chain at strength zero costs its textures and none of
        // its time.
        nHlFill_ = pipeline_.add({"hl:fill", "hlApply",
                                  {nHighlights_, nHlPush_[0], nRgb_},
                                  PixelFormat::RGBA16Float, {}});
    }

    // ── Profiled wavelet denoise (Starck et al., starlet) ─────────────────
    //
    // Before the color matrix and before sharpening, because the noise model
    // var = a·x + b only holds in linear camera RGB — the matrix mixes the
    // channels and would mix their variances with them — and because sharpening
    // noise and then denoising it is a way to end up with neither.
    //
    // Four blurs give c_1..c_4 at taps 1, 2, 4, 8. The shrink chain then runs
    // coarse to fine, starting from c_4 as the base and adding back each scale's
    // shrunk detail. Reconstruction is exact when nothing is shrunk:
    //
    //     I = c_J + Σ_j w_j,   w_j = c_j − c_{j+1}
    for (int j = 0; j < kDenoiseScales; ++j) {
        const int input = (j == 0) ? nHlFill_ : nAtrousBlur_[j - 1];
        nAtrousBlur_[j] = pipeline_.add({"denoise:blur " + std::to_string(j),
                                         "atrousBlur", {input},
                                         PixelFormat::RGBA16Float, {}});
    }
    for (int j = kDenoiseScales - 1; j >= 0; --j) {
        const int fine   = (j == 0) ? nHlFill_ : nAtrousBlur_[j - 1];
        const int coarse = nAtrousBlur_[j];
        // The coarsest scale accumulates onto the residual itself; every finer
        // one accumulates onto the scale below it.
        const int accum  = (j == kDenoiseScales - 1) ? nAtrousBlur_[j]
                                                     : nAtrousShrink_[j + 1];
        nAtrousShrink_[j] = pipeline_.add({"denoise:shrink " + std::to_string(j),
                                           "atrousShrink", {fine, coarse, accum},
                                           PixelFormat::RGBA16Float, {}});
    }

    // ── Lens corrections ──────────────────────────────────────────────────
    //
    // After denoise so it is not resampling noise, before sharpening so the
    // sharpening answers for the softening a resample costs.
    nLens_ = pipeline_.add({"lens", "lensCorrect", {nAtrousShrink_[0]},
                            PixelFormat::RGBA16Float, {}});

    buildCaptureTail();
}

/// Spots, sharpening and the camera profile — identical for both kinds of
/// source, downstream of whichever head the branch above built into nLens_.
void DevelopPipeline::buildCaptureTail() {
    using gpu::PixelFormat;

    // ── Spot removal ──────────────────────────────────────────────────────
    //
    // After the lens correction, which is the one stage that *warps* rather
    // than acting pointwise: downstream of it a spot lives in the same space a
    // mask does, so `mask::toFrame` carries it from the displayed picture and
    // there is one transform rather than two. research/spot-removal.md §4.
    //
    // Before sharpening, so a healed patch is sharpened along with everything
    // around it instead of arriving already sharp.
    //
    // The measure pass is an N x 1 texture — one boundary mean per spot —
    // computed once rather than re-derived at every pixel of every disc.
    nSpotMeasure_ = pipeline_.add({"spots:measure", "spotMeasure", {nLens_},
                                   PixelFormat::RGBA32Float, {}, {},
                                   true, params::kMaxSpots, 1});
    nSpotApply_   = pipeline_.add({"spots:apply", "spotApply",
                                   {nLens_, nSpotMeasure_},
                                   PixelFormat::RGBA16Float, {}});

    // Capture sharpening belongs right after the denoise, and keeping it
    // upstream of the tone controls means an exposure drag never recomputes it.
    // Note the input: a disabled node passes its *first* input through, and
    // shrink 0's first input is the reconstruction — so switching the whole
    // chain off hands sharpen exactly what it would otherwise have got.
    nSharpen_   = pipeline_.add({"sharpen", "sharpen", {nSpotApply_},
                                 PixelFormat::RGBA16Float, {}});
    nMatrix_    = pipeline_.add({"camera->working", "cameraToWorking", {nSharpen_},
                                 PixelFormat::RGBA16Float, {}});

    // ── The rest of the camera profile ────────────────────────────────────
    //
    // The matrix is one of five parts of a DNG profile, and it is the only one
    // that cannot be right for a saturated narrow-band stimulus. HueSatMap is
    // the spec's correction stage and belongs immediately after it, still in
    // scene-linear light and before any user adjustment — a profile is what the
    // camera saw, not what the photographer asked for.
    auxHueSat_ = pipeline_.addAuxTexture(huesat::kSatDivisions, huesat::kHueDivisions,
                                         PixelFormat::RGBA32Float);
    nHueSat_   = pipeline_.add({"profile:hue/sat", "hueSatMap", {nMatrix_},
                                PixelFormat::RGBA16Float, {}, {auxHueSat_}});
}

void DevelopPipeline::pushStaticCaptureParams(const raw::BayerImage& image) {
    const std::uint32_t size[2] = {width_, height_};

    const float g = (image.camMul[1] != 0.0f) ? image.camMul[1] : 1.0f;
    params::Linearize lin{};
    for (int c = 0; c < 4; ++c) {
        lin.black[c] = static_cast<float>(image.black[c]);
        // Sony leaves camMul[3] at zero; fall back to the first green.
        const float mul = (c == 3 || image.camMul[c] == 0.0f) ? g : image.camMul[c];
        lin.whiteBalance[c] = mul / g;
    }
    lin.invRange = 1.0f / static_cast<float>(image.white - image.black[0]);
    whiteLevel_ = static_cast<float>(image.white);
    for (int c = 0; c < 3; ++c) blackLevel_[c] = static_cast<float>(image.black[c]);
    lin.filters  = image.filters;
    lin.size[0] = size[0]; lin.size[1] = size[1];
    linBase_ = lin;

    // Needs invRange and the black points, so it comes after linBase_ is whole.
    const float asShot[3] = {lin.whiteBalance[0], lin.whiteBalance[1],
                             lin.whiteBalance[2]};
    linBase_.whiteClip = whiteClipFor(asShot);

    params::Dirs dirs{{size[0], size[1]}};
    pipeline_.setParams(nDirs_, &dirs, sizeof dirs);
    pipeline_.setParams(nLpf_,  &dirs, sizeof dirs);

    params::Green green{{size[0], size[1]}, image.filters, 0};
    pipeline_.setParams(nGreen_, &green, sizeof green);
    pipeline_.setParams(nRgb_,   &green, sizeof green);

    // The fill's pyramid is a function of the frame's size and nothing else, so
    // it is pushed here — with the black levels and the CFA pattern — and never
    // on a slider tick. ⚠ Decision #92 is the precedent: a parameter block
    // re-pushed for a value nothing reads still dirties everything downstream of
    // it, and `apps/bench`'s highlight-fill invariant is what keeps this honest.
    for (int l = 1; l < hlLevels_; ++l) {
        params::HlPull pull{};
        pull.outSize[0] = hlW_[l];
        pull.outSize[1] = hlH_[l];
        pull.inSize[0]  = hlW_[l - 1];
        pull.inSize[1]  = hlH_[l - 1];
        pipeline_.setParams(nHlPull_[l], &pull, sizeof pull);
    }
    for (int l = hlLevels_ - 2; l >= 0; --l) {
        params::HlPush push{};
        push.size[0]       = hlW_[l];
        push.size[1]       = hlH_[l];
        push.coarseSize[0] = hlW_[l + 1];
        push.coarseSize[1] = hlH_[l + 1];
        pipeline_.setParams(nHlPush_[l], &push, sizeof push);
    }

    pushColorProfile(image.camToXyz, image.camMul);

    // Measured from this frame rather than looked up per camera and ISO. See
    // raw/NoiseProfile.h for why, and for the citation.
    noise_ = estimateNoise(image);
    if (const char* v = std::getenv("ORION_DEBUG_NOISE"); v != nullptr && *v == '1') {
        std::fprintf(stderr, "orion: noise a=%.3e b=%.3e measured=%d\n",
                     noise_.a, noise_.b, noise_.measured ? 1 : 0);
    }
}

void DevelopPipeline::pushColorProfile(const std::array<float, 9>& camToXyzStored,
                                       const std::array<float, 4>& camMul) {
    const std::uint32_t size[2] = {width_, height_};

    float xyzToCam[9], camToXyz[9], camToWorking[9];
    std::copy_n(camToXyzStored.begin(), 9, xyzToCam);
    if (!invert3x3(xyzToCam, camToXyz)) {
        throw std::runtime_error("camera color matrix is singular");
    }
    std::copy_n(xyzToCam, 9, xyzToCam_);
    multiply3x3(kXyzToRec2020, camToXyz, camToWorking);
    normalizeRows(camToWorking);

    params::ColorMatrix mat{};
    for (int c = 0; c < 3; ++c) {
        mat.row0[c] = camToWorking[0 * 3 + c];
        mat.row1[c] = camToWorking[1 * 3 + c];
        mat.row2[c] = camToWorking[2 * 3 + c];
    }
    mat.size[0] = size[0]; mat.size[1] = size[1];
    pipeline_.setParams(nMatrix_, &mat, sizeof mat);

    // The profile's hue/saturation table and the two matrices that put the
    // working space into the space the spec defines the table in.
    {
        const auto toPro   = huesat::rec2020ToProPhoto();
        const auto fromPro = huesat::proPhotoToRec2020();
        params::HueSat hs{};
        for (int r = 0; r < 3; ++r)
            for (int c = 0; c < 3; ++c) {
                hs.toProPhoto[r][c]   = toPro[r * 3 + c];
                hs.fromProPhoto[r][c] = fromPro[r * 3 + c];
            }
        hs.size[0] = size[0]; hs.size[1] = size[1];
        hs.hueDivisions = huesat::kHueDivisions;
        hs.satDivisions = huesat::kSatDivisions;
        pipeline_.setParams(nHueSat_, &hs, sizeof hs);

        const auto table = huesat::buildTable(huesat::blueSky());
        pipeline_.updateAux(auxHueSat_, table.data(),
                            huesat::kSatDivisions * 4 * sizeof(float));
    }

    // Anchor on the camera's actual multipliers, not on a temperature we
    // inferred from them. The temperature is only a handle for the user to
    // turn; routing "as shot" through it would bake every estimation error
    // into the image as a color cast.
    const float gRef = (camMul[1] != 0.0f) ? camMul[1] : 1.0f;
    asShotMul_ = {camMul[0] / gRef, 1.0f, camMul[2] / gRef};

    asShot_    = estimateFrom(asShotMul_, xyzToCam_);
    asShotRef_ = multipliersFor(asShot_, xyzToCam_);
}

void DevelopPipeline::pushStaticCaptureParams(const raw::LinearImage& image) {
    // The head node reuses linearize's parameter block with the mosaic's
    // fields at identity: the DNG's floating-point data is defined
    // black-subtracted and normalized to 1.0 at its ceiling (DNG 1.4 ch. 4),
    // so black is zero and the range is one. White balance and the clip work
    // exactly as they do for a mosaic — including whiteClipFor, whose inputs
    // (white 1, black 0, invRange 1) make the ceiling min(gains), the
    // brightest neutral this file can still describe.
    params::Linearize lin{};
    const float g = (image.camMul[1] != 0.0f) ? image.camMul[1] : 1.0f;
    for (int c = 0; c < 4; ++c) {
        lin.black[c] = 0.0f;
        const float mul = (c == 3 || image.camMul[c] == 0.0f) ? g : image.camMul[c];
        lin.whiteBalance[c] = mul / g;
    }
    lin.invRange = 1.0f;
    lin.filters  = 0;
    lin.size[0] = width_; lin.size[1] = height_;
    whiteLevel_ = 1.0f;
    for (int c = 0; c < 3; ++c) blackLevel_[c] = 0.0f;
    linBase_ = lin;

    const float asShot[3] = {lin.whiteBalance[0], lin.whiteBalance[1],
                             lin.whiteBalance[2]};
    linBase_.whiteClip = whiteClipFor(asShot);

    pushColorProfile(image.camToXyz, image.camMul);

    // ⚠ Unmeasured, on purpose. The noise estimator reads a mosaic, and a
    // merged frame's variance is not one profile anyway — every pixel carries
    // a different mix of exposures. `measured = false` keeps the denoise
    // chain off, which for this source is also not built.
    noise_ = {};
}

void DevelopPipeline::applyWhiteBalance(const Adjustments& adj,
                                        const ApplyContext& ctx) {
    const bool first = ctx.first;

    // White balance rewrites the linearize block, which sits at the head of the
    // graph — so moving temperature legitimately recomputes everything,
    // including the demosaic. That is inherent: the demosaic interpolates
    // white-balanced data.
    if (first || adj.wb.temperatureK != lastAdj_.wb.temperatureK ||
        adj.wb.tint != lastAdj_.wb.tint) {
        // Apply temperature as a *ratio* against the as-shot estimate, so
        // leaving the slider alone reproduces the camera's own multipliers
        // exactly, and moving it is a relative change from there.
        const auto want = multipliersFor(adj.wb, xyzToCam_);

        params::Linearize lin = linBase_;
        lin.whiteBalance[0] = asShotMul_[0] * want[0] / std::max(asShotRef_[0], 1e-6f);
        lin.whiteBalance[1] = 1.0f;
        lin.whiteBalance[2] = asShotMul_[2] * want[2] / std::max(asShotRef_[2], 1e-6f);
        lin.whiteBalance[3] = 1.0f;   // second green

        // The white point moves with the white balance, so the level a blown
        // pixel clips to has to move with it too. Pinning it would tint the
        // highlights the moment the temperature left as-shot.
        const float gains[3] = {lin.whiteBalance[0], lin.whiteBalance[1],
                                lin.whiteBalance[2]};
        lin.whiteClip = whiteClipFor(gains);
        lastWhiteClip_ = lin.whiteClip;
        pipeline_.setParams(nLinearize_, &lin, sizeof lin);
    }
}

void DevelopPipeline::applyHighlights(const Adjustments& adj,
                                      const ApplyContext& ctx) {
    // A linear source has no highlight machinery to drive — recovery already
    // happened, in the merge that made the file.
    if (nHighlights_ < 0) return;

    const bool first = ctx.first;

    // ── Highlight reconstruction ─────────────────────────────────────────
    //
    // The clipping level has to be recomputed whenever white balance moves.
    // Linearize scales each channel by its multiplier, so the level a channel
    // saturates at is scaled with it — and because white balance raises red and
    // blue relative to green, those levels routinely sit above 1.0. Testing
    // against a single level after white balance is itself a source of the
    // false color this node exists to remove.
    const bool highlightsMoved =
        first ||
        adj.highlightRecovery != lastAdj_.highlightRecovery ||
        adj.wb.temperatureK != lastAdj_.wb.temperatureK ||
        adj.wb.tint != lastAdj_.wb.tint;

    if (highlightsMoved) {
        pipeline_.setEnabled(nHighlights_, adj.highlightRecovery > 0.0f);

        if (adj.highlightRecovery > 0.0f) {
            const auto wanted = multipliersFor(adj.wb, xyzToCam_);
            const float m[3] = {
                asShotMul_[0] * wanted[0] / std::max(asShotRef_[0], 1e-6f),
                1.0f,
                asShotMul_[2] * wanted[2] / std::max(asShotRef_[2], 1e-6f),
            };

            params::Highlights hl{};
            hl.size[0] = width_;
            hl.size[1] = height_;

            // One level for all three, because linearize now clips all three to
            // the same ceiling. The per-channel thresholds this node was
            // written against belonged to the unclipped mosaic it used to see;
            // testing for them here would find nothing, since no channel can
            // still be above the common clip when it reaches this node.
            const float clip = whiteClipFor(m);
            hl.clipR = clip;
            hl.clipG = clip;
            hl.clipB = clip;
            // 0.97, in the middle of the 0.95-0.99 the research gives. Lower
            // treats sound highlights as clipped; higher misses the shoulder
            // where a channel is already non-linear before it reaches its stop.
            hl.gamma = hlfill::kClipGamma;
            hl.strength = adj.highlightRecovery;
            pipeline_.setParams(nHighlights_, &hl, sizeof hl);
        }

        // ── The harmonic fill, on the same control ────────────────────────
        //
        // ⚠ **It disables to nothing, and that is the whole of decisions #82 and
        // #92 restated.** Twenty-four nodes hang off this one float. At zero
        // every one of them is switched off rather than run at no strength, the
        // apply node's first input is the picture so the graph resolves straight
        // past it, and no parameter block is pushed for a value nothing reads.
        // `apps/bench`'s "highlight fill" invariant asserts both by node *name*,
        // because a count alone would be satisfied by any twenty-four nodes.
        //
        // One control for two nodes on purpose: the window fit and the fill are
        // the two halves of one coverage — Masood et al.'s over the partial
        // clip, Rouf et al.'s over the total one — and a photograph that wants
        // its highlights left alone wants both left alone.
        const bool filling = adj.highlightRecovery > 0.0f;
        pipeline_.setEnabled(nHlMask_, filling);
        pipeline_.setEnabled(nHlFill_, filling);
        for (int l = 1; l < hlLevels_; ++l) {
            pipeline_.setEnabled(nHlPull_[l], filling);
        }
        for (int l = 0; l < hlLevels_ - 1; ++l) {
            pipeline_.setEnabled(nHlPush_[l], filling);
        }

        if (filling) {
            const auto wanted = multipliersFor(adj.wb, xyzToCam_);
            const float m[3] = {
                asShotMul_[0] * wanted[0] / std::max(asShotRef_[0], 1e-6f),
                1.0f,
                asShotMul_[2] * wanted[2] / std::max(asShotRef_[2], 1e-6f),
            };
            // ⚠ The same `whiteClipFor` the linearize node clipped to. Two
            // derivations of one ceiling is how a mask ends up disagreeing with
            // the clip that made it, and the disagreement would be invisible:
            // the fill would simply decline on pixels it should have filled.
            const float clip = whiteClipFor(m);

            params::HlMask mask{};
            mask.outSize[0] = hlW_[0];
            mask.outSize[1] = hlH_[0];
            mask.inSize[0]  = width_;
            mask.inSize[1]  = height_;
            mask.scale      = std::uint32_t(hlfill::kSolveScale);
            mask.clip       = clip;
            mask.gamma      = hlfill::kClipGamma;
            mask.shoulder   = hlfill::kShoulder;
            pipeline_.setParams(nHlMask_, &mask, sizeof mask);

            params::HlApply fill{};
            fill.size[0]     = width_;
            fill.size[1]     = height_;
            fill.fillSize[0] = hlW_[0];
            fill.fillSize[1] = hlH_[0];
            fill.scale       = std::uint32_t(hlfill::kSolveScale);
            fill.clip        = clip;
            fill.gamma       = hlfill::kClipGamma;
            fill.strength    = adj.highlightRecovery;
            pipeline_.setParams(nHlFill_, &fill, sizeof fill);
        }
    }
}

void DevelopPipeline::applyLens(const Adjustments& adj,
                                const ApplyContext& ctx) {
    const bool first = ctx.first;

    // ── Lens corrections ─────────────────────────────────────────────────
    //
    // A whole resampling pass, so it is switched off rather than run as an
    // identity when nothing is set.
    const bool correctingLens =
        adj.lensProfile ||
        adj.lensDistortion != 0.0f || adj.lensVignette != 0.0f ||
        adj.lensCaRed != 0.0f || adj.lensCaBlue != 0.0f;

    // `correctingLens` is deliberately NOT a condition for re-pushing. It is
    // true whenever a slider is nonzero, not when one changed, and setParams
    // dirties everything downstream unconditionally — so a vignette left on
    // made every exposure tick recompute lens, sharpen, matrix, develop:linear,
    // display and geometry. Seven full-resolution passes where three were
    // needed, on a state the bench never measured because every latency number
    // was taken with the lens sliders at zero. Once the lens database lands,
    // corrections-on is the normal state. The changed-comparisons below already
    // cover the enable and disable transitions.
    if (first ||
        adj.lensProfile != lastAdj_.lensProfile ||
        adj.lensPoly[0] != lastAdj_.lensPoly[0] ||
        adj.lensPoly[1] != lastAdj_.lensPoly[1] ||
        adj.lensPoly[2] != lastAdj_.lensPoly[2] ||
        adj.lensVignettePa[0] != lastAdj_.lensVignettePa[0] ||
        adj.lensVignettePa[1] != lastAdj_.lensVignettePa[1] ||
        adj.lensVignettePa[2] != lastAdj_.lensVignettePa[2] ||
        adj.lensDistortion != lastAdj_.lensDistortion ||
        adj.lensVignette != lastAdj_.lensVignette ||
        adj.lensCaRed != lastAdj_.lensCaRed ||
        adj.lensCaBlue != lastAdj_.lensCaBlue) {
        pipeline_.setEnabled(nLens_, correctingLens);

        params::Lens lens{};
        lens.size[0] = width_;
        lens.size[1] = height_;
        lens.centerX = 0.5f;
        lens.centerY = 0.5f;
        // The sliders run -1..1; the coefficients they drive are much smaller.
        // These ranges cover what a real lens needs without letting the control
        // fold the picture through itself at the extremes.
        // A measured profile replaces the sliders rather than adding to them.
        // Two sources for one coefficient is a control that fights the data:
        // the profile knows what this lens does at this focal length, and a
        // slider on top of it would be undoing a correction by feel.
        if (adj.lensProfile) {
            lens.distA     = adj.lensPoly[0];
            lens.distB     = adj.lensPoly[1];
            lens.distC     = adj.lensPoly[2];
            lens.vignetteA = adj.lensVignettePa[0];
            lens.vignetteB = adj.lensVignettePa[1];
            lens.vignetteC = adj.lensVignettePa[2];
        } else {
            lens.distB     = adj.lensDistortion * 0.35f;
            lens.vignetteA = adj.lensVignette * 0.6f;
        }
        // At full slider this is about seven pixels of radial shift at the
        // corner of a 24 MP frame, which is well past any real lateral
        // aberration and still short of obviously wrong.
        lens.caRed    = adj.lensCaRed * 0.003f;
        lens.caBlue   = adj.lensCaBlue * 0.003f;

        // Keep the picture in the frame. Without this a barrel correction
        // reaches past the border and the clamp smears the edge column.
        lens.scale = lens::autoScale(width_, height_,
                                     lens.centerX, lens.centerY,
                                     lens.distA, lens.distB, lens.distC,
                                     lens.caRed, lens.caBlue);
        pipeline_.setParams(nLens_, &lens, sizeof lens);
    }
}

void DevelopPipeline::applyDenoise(const Adjustments& adj,
                                   const ApplyContext& ctx) {
    // Not built for a linear source; see buildCaptureNodes.
    if (nAtrousBlur_[0] < 0) return;

    const bool first = ctx.first;

    // ── Denoise ──────────────────────────────────────────────────────────
    //
    // Eight nodes, so like the guided filter it is switched off entirely when
    // unused rather than run at zero strength. Unlike the guided filter it also
    // has to be re-pushed when white balance moves: linearize scales each
    // channel by its multiplier, and a variance scales by the square of that.
    const bool denoising = noise_.measured &&
                           (adj.denoiseLuma > 0.0f || adj.denoiseColor > 0.0f);
    const bool denoiseMoved =
        first ||
        adj.denoiseLuma != lastAdj_.denoiseLuma ||
        adj.denoiseColor != lastAdj_.denoiseColor ||
        adj.wb.temperatureK != lastAdj_.wb.temperatureK ||
        adj.wb.tint != lastAdj_.wb.tint;

    if (denoiseMoved) {
        for (int j = 0; j < kDenoiseScales; ++j) {
            pipeline_.setEnabled(nAtrousBlur_[j], denoising);
            pipeline_.setEnabled(nAtrousShrink_[j], denoising);
        }
    }

    if (denoising && denoiseMoved) {
        // ‖W_j‖₂ for the 5x5 starlet. Noise falls away fast at coarse scales,
        // which is why one threshold across all of them over-smooths the fine
        // scales and under-smooths the coarse.
        constexpr float kScaleNorm[kDenoiseScales] = {0.8907f, 0.2007f, 0.0855f, 0.0412f};

        // The model was fitted on the mosaic, before white balance. Linearize
        // multiplies channel c by m_c, so its variance is multiplied by m_c².
        // Luminance is a weighted sum of independent channels, so its variance
        // is the weight-squared sum of theirs.
        const auto want = multipliersFor(adj.wb, xyzToCam_);
        const float mR = asShotMul_[0] * want[0] / std::max(asShotRef_[0], 1e-6f);
        const float mG = 1.0f;
        const float mB = asShotMul_[2] * want[2] / std::max(asShotRef_[2], 1e-6f);

        // Rec.2020 luminance weights, matching the shader.
        constexpr float wR = 0.2627f, wG = 0.6780f, wB = 0.0593f;
        const float aLum = noise_.a * (wR * wR * mR + wG * wG * mG + wB * wB * mB);
        const float bLum = noise_.b * (wR * wR * mR * mR + wG * wG * mG * mG +
                                       wB * wB * mB * mB);

        for (int j = 0; j < kDenoiseScales; ++j) {
            params::AtrousBlur blur{};
            blur.size[0] = width_;
            blur.size[1] = height_;
            blur.step = 1 << j;
            pipeline_.setParams(nAtrousBlur_[j], &blur, sizeof blur);

            params::AtrousShrink shrink{};
            shrink.size[0] = width_;
            shrink.size[1] = height_;
            shrink.noiseA = aLum;
            shrink.noiseB = bLum;
            shrink.scaleNorm = kScaleNorm[j];
            shrink.strength = adj.denoiseLuma;
            // Color is expressed relative to luma so that raising Color alone
            // still does something when Luminance is zero.
            shrink.chromaBoost = adj.denoiseLuma > 1e-6f
                ? std::max(adj.denoiseColor / adj.denoiseLuma, 0.0f)
                : 0.0f;
            if (adj.denoiseLuma <= 1e-6f) {
                // Color only: shrink chroma against the measured sigma and
                // leave luma untouched.
                shrink.strength = adj.denoiseColor;
                shrink.chromaBoost = 1.0f;
            }
            pipeline_.setParams(nAtrousShrink_[j], &shrink, sizeof shrink);
        }
    }
}

void DevelopPipeline::applySpots(const Adjustments& adj,
                                 const ApplyContext& ctx) {
    const bool first = ctx.first;
    const std::uint32_t size[2] = {width_, height_};

    // ── Spot removal (research/spot-removal.md) ──────────────────────────
    //
    // Both nodes off when there are no spots, so a photograph with none pays
    // their textures and nothing else — and the apply node's first input is the
    // lens output, so disabled it resolves straight past to exactly what
    // sharpening would otherwise have received.
    {
        const bool spotting = adj.spotCount > 0;
        const bool wasSpotting = lastAdj_.spotCount > 0;
        if (first || spotting != wasSpotting) {
            pipeline_.setEnabled(nSpotMeasure_, spotting);
            pipeline_.setEnabled(nSpotApply_, spotting);
        }

        const bool spotsMoved =
            first || adj.spotCount != lastAdj_.spotCount ||
            !std::equal(adj.spots.begin(), adj.spots.begin() + adj.spotCount,
                        lastAdj_.spots.begin());
        if (spotting && spotsMoved) {
            params::SpotMeasure sm{};
            sm.size[0] = size[0]; sm.size[1] = size[1];
            sm.count = std::min(adj.spotCount, params::kMaxSpots);
            // 32 rim samples. Orion's own number, with the reasoning in
            // UNSOURCED.md §21: enough that the smallest usable spot still
            // averages several distinct pixels, few enough that the largest
            // does not spend longer on its rim than on its interior.
            sm.samples = 32;

            params::SpotApply sa{};
            sa.size[0] = size[0]; sa.size[1] = size[1];
            sa.count = sm.count;

            // ⚠ No transform here, and that is the decision rather than an
            // omission. A spot is stored in frame coordinates already —
            // converted once by `displayedToFrame` when it was placed — because
            // dust is on the sensor and has to follow the subject through a
            // later crop or turn. Converting on every render would give a mask's
            // behavior instead, which is to stay put on screen.
            //
            // It also keeps the geometry out of the staleness comparison below,
            // which is a trap this file has fallen into twice already.
            for (int i = 0; i < sm.count; ++i) {
                const SpotEdit& e = adj.spots[std::size_t(i)];
                const float sp[4] = {e.destX, e.destY, e.srcX, e.srcY};
                const float sh[4] = {e.radius, e.feather, e.heal ? 1.0f : 0.0f, 0.0f};
                for (int k = 0; k < 4; ++k) {
                    sm.spots[i][k] = sp[k]; sm.shape[i][k] = sh[k];
                    sa.spots[i][k] = sp[k]; sa.shape[i][k] = sh[k];
                }
            }
            pipeline_.setParams(nSpotMeasure_, &sm, sizeof sm);
            pipeline_.setParams(nSpotApply_, &sa, sizeof sa);
        }
    }
}

void DevelopPipeline::applySharpen(const Adjustments& adj,
                                   const ApplyContext& ctx) {
    const bool first = ctx.first;
    const std::uint32_t size[2] = {width_, height_};

    if (first || adj.sharpenAmount != lastAdj_.sharpenAmount ||
        adj.sharpenRadius != lastAdj_.sharpenRadius ||
        adj.sharpenMasking != lastAdj_.sharpenMasking) {
        params::Sharpen sh{adj.sharpenAmount, adj.sharpenRadius,
                           adj.sharpenMasking, 0.0f, {size[0], size[1]}, {0, 0}};
        pipeline_.setParams(nSharpen_, &sh, sizeof sh);
    }
}

}  // namespace orion::pipe
