#include "pipe/DevelopPipeline.h"

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

/// LibRaw's flip flag as clockwise quarter turns.
int quarterTurnsFor(int flip) {
    switch (flip) {
        case 3: return 2;   // 180
        case 5: return 3;   // 90 anticlockwise
        case 6: return 1;   // 90 clockwise
        default: return 0;
    }
}

}  // namespace

DevelopPipeline::DevelopPipeline(gpu::Device& device, const std::string& shaderDir,
                                 const raw::BayerImage& image)
    : pipeline_(device, shaderDir), width_(image.width), height_(image.height) {

    using gpu::PixelFormat;

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
    // ── Highlight reconstruction (Masood, Zhu & Tang) ─────────────────────
    //
    // Straight after the demosaic, so it has all three channels per pixel, and
    // before everything else, so no later stage ever sees the false color that
    // per-channel clipping produces. Still in linear camera RGB, which is where
    // the clipping levels are known.
    nHighlights_ = pipeline_.add({"highlights", "highlightRecover", {nRgb_},
                                  PixelFormat::RGBA16Float, {}});

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
        const int input = (j == 0) ? nHighlights_ : nAtrousBlur_[j - 1];
        nAtrousBlur_[j] = pipeline_.add({"denoise:blur " + std::to_string(j),
                                         "atrousBlur", {input},
                                         PixelFormat::RGBA16Float, {}});
    }
    for (int j = kDenoiseScales - 1; j >= 0; --j) {
        const int fine   = (j == 0) ? nHighlights_ : nAtrousBlur_[j - 1];
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

    // Capture sharpening belongs right after the denoise, and keeping it
    // upstream of the tone controls means an exposure drag never recomputes it.
    // Note the input: a disabled node passes its *first* input through, and
    // shrink 0's first input is the reconstruction — so switching the whole
    // chain off hands sharpen exactly what it would otherwise have got.
    nSharpen_   = pipeline_.add({"sharpen", "sharpen", {nLens_},
                                 PixelFormat::RGBA16Float, {}});
    nMatrix_    = pipeline_.add({"camera->working", "cameraToWorking", {nSharpen_},
                                 PixelFormat::RGBA16Float, {}});
    // Every scene-linear adjustment fuses into one dispatch, and the display
    // transform plus curve into another. They are all pointwise; separate
    // passes only bought a 194 MB round trip each at 24 MP.
    // ── Guided filter (He, Sun & Tang) ────────────────────────────────────
    // Sits before exposure on purpose: exposure is a multiply, so in log2 it is
    // an add the tone node applies for free — which keeps this whole six-pass
    // chain cached while the exposure slider moves.
    nGuidePrep_ = pipeline_.add({"guide:prep", "guidePrep", {nMatrix_},
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

    nLinear_    = pipeline_.add({"develop:linear", "developLinear",
                                 {nMatrix_, nGuideV2_, nGuidePrep_},
                                 PixelFormat::RGBA16Float, {}});

    auxCurveLut_ = pipeline_.addAuxTexture(kCurveResolution, kCurveRows,
                                           PixelFormat::R32Float);
    // Sixteen bits per channel, not eight. The display transform's output is
    // the last thing that happens to a pixel, so eight bits there caps every
    // export at eight bits whatever container it goes into — and a gradient
    // that survived the whole pipeline in float gets quantised on the way out.
    // Half float rather than 16-bit integer because it is guaranteed
    // read-write on Metal, and because it holds the shadows better.
    nDisplay_   = pipeline_.add({"develop:display", "developDisplay", {nLinear_},
                                 PixelFormat::RGBA16Float, {}, {auxCurveLut_}});

    // Orientation is last, and is the only node whose output dimensions differ
    // from its input — a quarter turn swaps them.
    const bool swaps = (exifQuarters_ % 2) != 0;

    // Allocate for the worst case so a user rotation never needs a recompile.
    // 1.5x covers the worst-case straighten bounding box (45 degrees
    // grows the frame by sqrt(2)).
    const std::uint32_t maxSide =
        static_cast<std::uint32_t>(std::max(width_, height_) * 1.45f);
    nGeometry_ = pipeline_.add({"geometry", "geometry", {nDisplay_},
                                PixelFormat::RGBA16Float, {}, {},
                                true, maxSide, maxSide});
    (void)swaps;

    pipeline_.compile(width_, height_);

    applyImageParams(image);

    pipeline_.setSource(image.samples.data(), static_cast<std::size_t>(width_) * 2);
}

bool DevelopPipeline::canReload(const raw::BayerImage& image) const noexcept {
    return image.width == width_ && image.height == height_ &&
           image.filters == filters_;
}

void DevelopPipeline::reload(const raw::BayerImage& image) {
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

void DevelopPipeline::applyImageParams(const raw::BayerImage& image) {
    exifQuarters_ = quarterTurnsFor(image.flip);
    filters_      = image.filters;

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

    float xyzToCam[9], camToXyz[9], camToWorking[9];
    std::copy_n(image.camToXyz.begin(), 9, xyzToCam);
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

    // Anchor on the camera's actual multipliers, not on a temperature we
    // inferred from them. The temperature is only a handle for the user to
    // turn; routing "as shot" through it would bake every estimation error
    // into the image as a color cast.
    const float gRef = (image.camMul[1] != 0.0f) ? image.camMul[1] : 1.0f;
    asShotMul_ = {image.camMul[0] / gRef, 1.0f, image.camMul[2] / gRef};

    // Measured from this frame rather than looked up per camera and ISO. See
    // raw/NoiseProfile.h for why, and for the citation.
    noise_ = estimateNoise(image);
    if (const char* v = std::getenv("ORION_DEBUG_NOISE"); v != nullptr && *v == '1') {
        std::fprintf(stderr, "orion: noise a=%.3e b=%.3e measured=%d\n",
                     noise_.a, noise_.b, noise_.measured ? 1 : 0);
    }

    asShot_    = estimateFrom(asShotMul_, xyzToCam_);
    asShotRef_ = multipliersFor(asShot_, xyzToCam_);

    Adjustments initial;
    initial.wb = asShot_;
    apply(initial);

}

void DevelopPipeline::apply(const Adjustments& adj) {
    const std::uint32_t size[2] = {width_, height_};

    // Only push what actually moved. setParams dirties the whole downstream
    // subgraph, so pushing all three blocks on every tick would make dragging
    // the curve also recompute exposure and AgX — three nodes of work for a
    // one-node change, and the difference between 4 ms and 12 ms.
    const bool first = !primed_;

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
        pipeline_.setParams(nLinearize_, &lin, sizeof lin);
    }

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
            hl.gamma = 0.97f;
            hl.strength = adj.highlightRecovery;
            pipeline_.setParams(nHighlights_, &hl, sizeof hl);
        }
    }

    // ── Lens corrections ─────────────────────────────────────────────────
    //
    // A whole resampling pass, so it is switched off rather than run as an
    // identity when nothing is set.
    const bool correctingLens =
        adj.lensDistortion != 0.0f || adj.lensVignette != 0.0f ||
        adj.lensCaRed != 0.0f || adj.lensCaBlue != 0.0f;

    if (first || correctingLens ||
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
        lens.k1       = adj.lensDistortion * 0.35f;
        // At full slider this is about seven pixels of radial shift at the
        // corner of a 24 MP frame, which is well past any real lateral
        // aberration and still short of obviously wrong.
        lens.caRed    = adj.lensCaRed * 0.003f;
        lens.caBlue   = adj.lensCaBlue * 0.003f;
        lens.vignetteA = adj.lensVignette * 0.6f;
        pipeline_.setParams(nLens_, &lens, sizeof lens);
    }

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

    const bool linearMoved =
        first ||
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

    // The guided filter is six nodes and only feeds the local highlight and
    // shadow masks. With both at zero it is pure cost, and white balance —
    // which rewrites the head of the graph and reruns everything — pays it on
    // every tick. Skipping it takes a temperature drag from sixteen nodes to
    // ten.
    const bool needsGuide = adj.highlights != 0.0f || adj.shadows != 0.0f;
    if (first || needsGuide != (lastAdj_.highlights != 0.0f ||
                                lastAdj_.shadows != 0.0f)) {
        for (int n : {nGuidePrep_, nGuideDown_, nGuideH1_, nGuideV1_,
                      nGuideAb_, nGuideH2_, nGuideV2_}) {
            pipeline_.setEnabled(n, needsGuide);
        }
    }

    if (linearMoved) {
        params::LinearAdjust la{adj.exposureEv, adj.highlights, adj.shadows,
                                adj.whites, adj.blacks, adj.vibrance,
                                adj.saturation, 0.0f, {size[0], size[1]},
                                {guideW_, guideH_},
                                {}, {}, {}};
        std::copy(adj.hueShift.begin(), adj.hueShift.end(), la.hueShift);
        std::copy(adj.satShift.begin(), adj.satShift.end(), la.satShift);
        std::copy(adj.lumShift.begin(), adj.lumShift.end(), la.lumShift);
        pipeline_.setParams(nLinear_, &la, sizeof la);
    }

    if (first || adj.sharpenAmount != lastAdj_.sharpenAmount ||
        adj.sharpenRadius != lastAdj_.sharpenRadius ||
        adj.sharpenMasking != lastAdj_.sharpenMasking) {
        params::Sharpen sh{adj.sharpenAmount, adj.sharpenRadius,
                           adj.sharpenMasking, 0.0f, {size[0], size[1]}, {0, 0}};
        pipeline_.setParams(nSharpen_, &sh, sizeof sh);
    }

    const bool curveMoved = first || !sameCurve(adj.curve, lastAdj_.curve);

    if (first || adj.contrast != lastAdj_.contrast || curveMoved) {
        params::Display d{adj.contrast, -2.5f,
                          adj.curve.isIdentity() ? 1u : 0u,
                          kCurveResolution, {size[0], size[1]}, {0, 0}};
        pipeline_.setParams(nDisplay_, &d, sizeof d);
    }

    // Rebuilding the LUT walks four splines. Skip it when the curve has not
    // moved, which is every frame of an exposure drag.
    if (curveMoved) {
        const auto lut = buildCurveLut(adj.curve);
        pipeline_.updateAux(auxCurveLut_, lut.data(), kCurveResolution * sizeof(float));
    }

    const bool geometryMoved =
        first ||
        adj.rotateQuarters != lastAdj_.rotateQuarters ||
        adj.straightenDeg  != lastAdj_.straightenDeg ||
        adj.cropX != lastAdj_.cropX || adj.cropY != lastAdj_.cropY ||
        adj.cropW != lastAdj_.cropW || adj.cropH != lastAdj_.cropH ||
        adj.cropPreview != lastAdj_.cropPreview;

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

    lastAdj_ = adj;
    primed_  = true;
}

std::uint32_t DevelopPipeline::outputWidth() const noexcept  { return outW_; }
std::uint32_t DevelopPipeline::outputHeight() const noexcept { return outH_; }
std::uint32_t DevelopPipeline::frameWidth()  const noexcept { return frameW_; }
std::uint32_t DevelopPipeline::frameHeight() const noexcept { return frameH_; }

double DevelopPipeline::render() { return pipeline_.render(); }

}  // namespace orion::pipe
