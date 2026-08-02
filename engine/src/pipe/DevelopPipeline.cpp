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

/// How far Orion's zero point sits below every other converter's.
///
/// **Measured, not chosen.** Orion opened a daylight frame about 1.3x darker in
/// the midtones than the camera's own JPEG, which read as flat and washed. A
/// two-dimensional fit of exposure against base contrast, scored as mean
/// absolute luma error over six patches spanning each frame's tonal range,
/// against two independent references (the camera's JPEG and Apple's RAW
/// rendering):
///
///     _PIC8095 daylight   best +1.20 EV, contrast 1.45, error 0.0171
///     _PIC8220 forecourt  best +1.20 EV, contrast 1.45, error 0.0103
///     _PIC8148 night sky  best +1.60 EV, contrast 2.05, error 0.0068
///
/// Two of three agree exactly. The night frame's error surface is nearly flat —
/// 0.0083 at the old defaults against 0.0068 at its own minimum — because a
/// near-black frame barely moves a mean luma, so its preference is noise. At
/// (+1.2, 1.45) its error is 0.0150. Consistent wherever there is signal.
///
/// The mechanism is the DNG specification's `BaselineExposure` (tag 50730),
/// "by how much (in EV units) to move the zero point", which Adobe applies
/// silently on open — which is why the user's Exposure slider still reads 0.00
/// here rather than +1.20. See research/camera-profiles.md.
///
/// ⚠️ **What this value is not yet known to be.** It fits one camera body. A
/// per-camera `BaselineExposure` and a property of Orion's own AgX zero point
/// are indistinguishable from a single body's data, and LibRaw does not carry
/// the tag for native ARW. The moment a second body is supported, measure it
/// again: if the number moves, it is per-camera and belongs in a table; if it
/// does not, it belongs in the display transform.
constexpr float kBaselineExposureEv = 1.2f;

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

        nHlMask_ = pipeline_.add({"hl:mask", "hlMask", {nHighlights_},
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
                                  {nHighlights_, nHlPush_[0]},
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
    for (int i = 0; i < kMaxMaskComponents; ++i) {
        nMaskComponent_[i] =
            pipeline_.add({"mask:" + std::to_string(i), "maskComponent",
                           {prevMask, nHueSat_}, PixelFormat::R16Float, {},
                           {auxMatte_[i], auxDabs_[i], auxDabBounds_[i]}});
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
    // research/vignette.md, decision #96.
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

    pipeline_.compile(width_, height_);

    // Once. The plate is 33 MB and identical for every photograph, but it is
    // per-`Pipeline` because the aux texture is — building it here rather than
    // caching it statically keeps the ownership obvious and costs one upload
    // per graph, of which there are two.
    {
        const auto plate = grain::buildPlate();
        pipeline_.updateAux(auxGrainPlate_, plate.data(),
                            static_cast<std::size_t>(grain::kPlateSize) * sizeof(float));
    }

    applyImageParams(image);

    pipeline_.setSource(image.samples.data(), static_cast<std::size_t>(width_) * 2);
}

bool DevelopPipeline::canReload(const raw::BayerImage& image) const noexcept {
    return image.width == width_ && image.height == height_ &&
           image.filters == filters_;
}

void DevelopPipeline::reload(const raw::BayerImage& image) {
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
        matteLive_[i][0] = 0;
        matteLive_[i][1] = 0;
        matteDirty_[i] = true;
    }

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

    // The mask group's fold starts from zero, the additive identity — see
    // mask_base.slang for why it is a node at all. Set once per image; the
    // per-node cache serves it thereafter.
    params::MaskBase mb{{size[0], size[1]}, 0.0f, 0.0f};
    pipeline_.setParams(nMaskBase_, &mb, sizeof mb);

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
    if (first || zoneMoved(adj.gradeShadow, lastAdj_.gradeShadow)
              || zoneMoved(adj.gradeMidtone, lastAdj_.gradeMidtone)
              || zoneMoved(adj.gradeHighlight, lastAdj_.gradeHighlight)
              || vignetteMoved
              || (vignetting && cropMoved)) {

        const auto zoneIsFlat = [](const float z[3]) {
            return z[0] == 0.0f && z[1] == 0.0f && z[2] == 0.0f;
        };
        const bool grading = !zoneIsFlat(adj.gradeShadow)
                          || !zoneIsFlat(adj.gradeMidtone)
                          || !zoneIsFlat(adj.gradeHighlight);

        // A whole pass over the frame, so it is switched off rather than run
        // as an identity. A disabled node passes its first input through.
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

    // ⚠ Hidden components are *disabled*, not zeroed. A disabled node resolves
    // to its first input, and this node's first input is the fold so far — so a
    // hidden component is skipped exactly, for free, and keeps every setting it
    // had. Zeroing its coverage instead would still cost a full-resolution pass
    // to produce nothing.
    const auto liveCount = [](const Adjustments& a) {
        int n = 0;
        for (int i = 0; i < a.maskCount; ++i)
            if (!a.maskComponents[std::size_t(i)].hidden) ++n;
        return n;
    };
    bool visibilityMoved = first || adj.maskCount != lastAdj_.maskCount;
    for (int i = 0; !visibilityMoved && i < kMaxMaskComponents; ++i) {
        visibilityMoved = adj.maskComponents[std::size_t(i)].hidden
                       != lastAdj_.maskComponents[std::size_t(i)].hidden;
    }
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

        const auto placed = mask::toFrame(
            {c.center[0], c.center[1], c.angle}, crop, turns,
            adj.straightenDeg * 3.14159265358979324f / 180.0f,
            adj.cropX + adj.cropW * 0.5f, adj.cropY + adj.cropH * 0.5f,
            rotW, rotH, perspective);

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

        m.center[0] = placed.centerX; m.center[1] = placed.centerY;
        mask::radiusToFrame(c.radius[0], c.radius[1], crop, m.semi[0], m.semi[1]);
        // ⚠ And the perspective's own scale at *this* mask's center, which the
        // crop's cannot carry: a homography's magnification is different at
        // every point, so it travels with the placement rather than living in
        // `radiusToFrame`. Exactly 1 when the control is neutral.
        m.semi[0] *= placed.scale;
        m.semi[1] *= placed.scale;
        m.angle     = placed.angle;

        // A linear gradient's endpoints, from the *placed* center and angle.
        // Half the length either side, so rotating about the center does not
        // also move the ramp.
        const float len = mask::lengthToFrame(c.length, crop) * placed.scale;
        const float dx = std::cos(m.angle) * len * 0.5f;
        const float dy = std::sin(m.angle) * len * 0.5f;
        m.zero[0] = m.center[0] - dx; m.zero[1] = m.center[1] - dy;
        m.full[0] = m.center[0] + dx; m.full[1] = m.center[1] + dy;
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
        }
        pipeline_.setParams(nMaskComponent_[i], &m, sizeof m);
    }

    const bool linearMoved =
        first || visibilityMoved ||
        adj.layers != lastAdj_.layers ||
        adj.maskOverlay != lastAdj_.maskOverlay ||
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

    // The guided filter is six nodes and only feeds the local highlight and
    // shadow masks. With both at zero it is pure cost, and white balance —
    // which rewrites the head of the graph and reruns everything — pays it on
    // every tick. Skipping it takes a temperature drag from sixteen nodes to
    // ten.
    const bool needsGuide = adj.highlights != 0.0f || adj.shadows != 0.0f;
    if (first || needsGuide != (lastAdj_.highlights != 0.0f ||
                                lastAdj_.shadows != 0.0f)) {
        for (int n : {nGuideDown_, nGuideH1_, nGuideV1_,
                      nGuideAb_, nGuideH2_, nGuideV2_}) {
            pipeline_.setEnabled(n, needsGuide);
        }
    }

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

    // ── Guided feathering of the mask group (research/masking.md §4) ──────
    const bool refining = adj.maskCount > 0 && adj.maskRefine > 0.0f;
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

    lastAdj_ = adj;
    primed_  = true;
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
        const double first = pipeline_.render();
        if (needsAirlight) estimateAirlight();
        if (needsPlan)     estimateFusionPlan();
        return first + pipeline_.render();
    }
    return pipeline_.render();
}

}  // namespace orion::pipe
