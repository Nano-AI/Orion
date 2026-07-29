/*  Host-side mirrors of the shader parameter blocks.
 *
 *  These MUST match the corresponding structs in the engine/shaders Slang sources byte
 *  for byte. Metal's rules: float4 aligns to 16, uint2 aligns to 8, and the
 *  struct is padded to its largest member's alignment. The static_asserts below
 *  catch a drift at compile time rather than as a mystery in the output image.
 */

#pragma once

#include <cstddef>
#include <cstdint>

namespace orion::pipe::params {

struct alignas(16) Linearize {
    float         black[4];        // per CFA channel, sensor counts
    float         whiteBalance[4]; // per CFA channel, normalized to green
    float         invRange;        // 1 / (white - black)
    std::uint32_t filters;         // CFA bitmask
    std::uint32_t size[2];
    float         whiteClip;       // common saturation level, post white balance
    float         _pad[3];
};
static_assert(sizeof(Linearize) == 64);

struct Dirs {
    std::uint32_t size[2];
};
static_assert(sizeof(Dirs) == 8);

struct Green {
    std::uint32_t size[2];
    std::uint32_t filters;
    std::uint32_t _pad;
};
static_assert(sizeof(Green) == 16);

using RedBlue = Green;

struct alignas(16) ColorMatrix {
    float         row0[4];   // camera -> working, w unused
    float         row1[4];
    float         row2[4];
    std::uint32_t size[2];
    std::uint32_t _pad[2];
};
static_assert(sizeof(ColorMatrix) == 64);

/// Every scene-linear adjustment, fused into one dispatch.
struct LinearAdjust {
    float         exposureEv;
    float         highlights;
    float         shadows;
    float         whites;
    float         blacks;
    float         vibrance;
    float         saturation;
    /// Nonzero when the guided-filter chain is live. When it is not, the two
    /// guide textures resolve back to the color matrix output — real pixels,
    /// but not the log2 luminance and coefficients this kernel would be
    /// reading them as. The shader falls back to the pixel's own EV instead.
    float         guideEnabled;
    std::uint32_t size[2];
    /// Dimensions of the subsampled guide coefficients, which this kernel lifts
    /// back to full resolution. See guide_down.slang.
    std::uint32_t guideSize[2];
    float         hueShift[8];
    float         satShift[8];
    float         lumShift[8];
    /// A local adjustment and its coverage. See mask_gradient.slang and
    /// research/masking.md — the alpha scales the parameter, not the result.
    float         localExposureEv;
    float         maskActive;
    /// Draw the coverage on screen. A viewing aid; never set for an export.
    float         maskOverlay;
};
static_assert(sizeof(LinearAdjust) == 156);

struct GuidePrep {
    std::uint32_t size[2];
    std::uint32_t _pad[2];
};
static_assert(sizeof(GuidePrep) == 16);

struct BoxBlur {
    std::uint32_t size[2];
    std::int32_t  radius;
    std::int32_t  horizontal;
};
static_assert(sizeof(BoxBlur) == 16);

struct GuideAb {
    std::uint32_t size[2];
    float         epsilon;
    float         _pad;
};
static_assert(sizeof(GuideAb) == 16);

struct Atrous {
    std::uint32_t size[2];
    std::int32_t  step;
    std::int32_t  _pad;
};
static_assert(sizeof(Atrous) == 16);

struct Shrink {
    std::uint32_t size[2];
    float         noiseA;
    float         noiseB;
    float         scaleNorm;
    float         strength;
    float         chromaBoost;
    float         _pad;
};
static_assert(sizeof(Shrink) == 32);

struct Sharpen {
    float         amount;
    float         radius;
    float         masking;
    float         _pad;
    std::uint32_t size[2];
    std::uint32_t _pad2[2];
};
static_assert(sizeof(Sharpen) == 32);

/// AgX plus the tone curve plus the creative LUT plus the display encode.
struct alignas(16) Display {
    float         contrast;
    float         pivot;
    std::uint32_t curveIdentity;
    std::uint32_t resolution;
    std::uint32_t size[2];
    /// Nonzero when this node writes eight bits. See develop_display.slang.
    std::uint32_t dither;
    /// Edge length of the creative LUT's grid; zero when none is loaded, which
    /// is what makes the lookup free rather than an identity table's worth of
    /// fetches on every pixel of every frame.
    std::uint32_t lutSize;
    float         lutStrength;
    float         _pad[3];
    float         lutMin[4];    // the .cube file's DOMAIN_MIN, w unused
    float         lutMax[4];
};
static_assert(sizeof(Display) == 80);

/// Three-way colour grading. Mirrors GradeParams in color_grade.slang.
/// Each row is an already zero-sum RGB offset with that zone's slope in w.
struct alignas(16) Grade {
    std::uint32_t size[2];
    float         _pad0, _pad1;
    float         shadow[4];
    float         midtone[4];
    float         highlight[4];
};
static_assert(sizeof(Grade) == 64);

/// The camera profile's hue/saturation table. Mirrors HueSatParams in
/// huesat.slang. The two matrices carry the working space in and out of linear
/// ProPhoto, which is the space the DNG spec's table is defined in.
struct alignas(16) HueSat {
    float         toProPhoto[3][4];     // rows, w unused
    float         fromProPhoto[3][4];
    std::uint32_t size[2];
    std::uint32_t hueDivisions;
    std::uint32_t satDivisions;
};
static_assert(sizeof(HueSat) == 112);

/// Lens corrections. Mirrors LensParams in lens.slang.
struct Lens {
    std::uint32_t size[2];
    float centerX, centerY;
    /// ptlens a, b, c. A manual distortion slider sets b alone, which is poly3.
    float distA, distB, distC;
    float caRed, caBlue;
    float vignetteA, vignetteB, vignetteC;
    /// Autoscale — the zoom that keeps every fetch inside the frame.
    /// Computed in LensGeometry.h; 0 is read by the shader as 1.
    float scale;
    float _pad[3];
};
static_assert(sizeof(Lens) == 64);

/// Local Laplacian filters — Paris et al. 2011, Aubry et al. 2014.
/// research/local-laplacian.md. Six kernels share these; every one of them
/// mirrors a struct in the matching shaders/llf_*.slang.
struct LlfLuma {
    std::uint32_t size[2];
    float         lo;         // window floor, log2 units
    float         invRange;   // 1 / (hi - lo)
};
static_assert(sizeof(LlfLuma) == 16);

/// Shared by llfDown and llfDownPacked — the same halving, one channel or four.
struct LlfDown {
    std::uint32_t outSize[2];
    std::uint32_t inSize[2];
};
static_assert(sizeof(LlfDown) == 16);

struct LlfRemap {
    std::uint32_t outSize[2];
    std::uint32_t inSize[2];
    float         gamma0;      // first of this texture's four
    float         gammaStep;
    float         sigmaR;
    float         alpha;
    float         noiseLo;     // Paris et al. section 5.2, tau's bounds
    float         noiseHi;
    float         _pad[2];
};
static_assert(sizeof(LlfRemap) == 48);

struct LlfRemapH {
    std::uint32_t outSize[2];
    std::uint32_t inSize[2];
    float         gamma0;
    float         gammaStep;
    float         sigmaR;
    float         alpha;
    float         noiseLo;
    float         noiseHi;
    float         _pad[2];
};
static_assert(sizeof(LlfRemapH) == 48);

struct LlfCollapse {
    std::uint32_t size[2];
    std::uint32_t coarseSize[2];
    float         gammaStep;
    std::int32_t  gammaCount;
    float         _pad[2];
};
static_assert(sizeof(LlfCollapse) == 32);

/// The finest level recomputes the remapping instead of reading it, so it
/// carries the remapping's own parameters as well.
struct LlfCollapse0 {
    std::uint32_t size[2];
    std::uint32_t coarseSize[2];
    float         gammaStep;
    std::int32_t  gammaCount;
    float         sigmaR;
    float         alpha;
    float         noiseLo;
    float         noiseHi;
    float         _pad[2];
};
static_assert(sizeof(LlfCollapse0) == 48);

struct LlfApply {
    std::uint32_t size[2];
    float         evPerUnit;
    float         maxEv;
};
static_assert(sizeof(LlfApply) == 16);

/// Dehaze — He, Sun & Tang's dark channel prior. research/dehaze.md.
struct alignas(16) DehazeChan {
    std::uint32_t size[2];
    float         _pad[2];
    float         airlight[4];   // A_c, w unused
};
static_assert(sizeof(DehazeChan) == 32);

struct DehazeRank {
    std::uint32_t size[2];
    std::int32_t  radius;
    std::int32_t  horizontal;
    std::int32_t  maximum;
    std::int32_t  _pad[3];
};
static_assert(sizeof(DehazeRank) == 32);

struct DehazePeak {
    std::uint32_t outSize[2];
    std::uint32_t inSize[2];
    std::int32_t  scale;
    std::int32_t  _pad[3];
};
static_assert(sizeof(DehazePeak) == 32);

struct DehazePrep {
    std::uint32_t outSize[2];
    std::uint32_t inSize[2];
    std::int32_t  scale;
    float         omega;
    float         lo;
    float         invRange;
};
static_assert(sizeof(DehazePrep) == 32);

struct BoxBlur4 {
    std::uint32_t size[2];
    std::int32_t  radius;
    std::int32_t  horizontal;
};
static_assert(sizeof(BoxBlur4) == 16);

struct DehazeAb {
    std::uint32_t size[2];
    float         epsilon;
    float         _pad;
};
static_assert(sizeof(DehazeAb) == 16);

struct alignas(16) DehazeRecover {
    std::uint32_t size[2];
    std::uint32_t coeffSize[2];
    float         t0;
    float         lo;
    float         invRange;
    float         _pad;
    float         airlight[4];
};
static_assert(sizeof(DehazeRecover) == 48);

/// Simulated exposure fusion — Hessel & Morel. research/exposure-fusion.md.
/// Mirrors FusePlan in shaders/ops/fuse_ops.slang.
struct FusePlanBlock {
    std::int32_t images;
    std::int32_t bright;
    std::int32_t dark;
    std::int32_t span;
    float        alpha;
    float        beta;
    float        lambda;
    float        sigma;
};
static_assert(sizeof(FusePlanBlock) == 32);

struct FuseProxy {
    std::uint32_t outSize[2];
    std::uint32_t inSize[2];
    std::int32_t  scale;
    float         slope;
    float         _pad[2];
};
static_assert(sizeof(FuseProxy) == 32);

struct FuseSplit {
    std::uint32_t size[2];
    std::int32_t  base;
    std::int32_t  weights;
    FusePlanBlock plan;
    float         epsilon;
    float         _pad[3];
};
static_assert(sizeof(FuseSplit) == 64);

struct FuseBlend {
    std::uint32_t size[2];
    std::uint32_t coarseSize[2];
    std::int32_t  images;
    std::int32_t  residual;
    float         _pad[2];
};
static_assert(sizeof(FuseBlend) == 32);

struct FuseApply {
    std::uint32_t size[2];
    std::uint32_t proxySize[2];
    float         slope;
    float         strength;
    float         maxGain;
    float         _pad;
};
static_assert(sizeof(FuseApply) == 32);

/// The value a mask group's fold starts from. Mirrors MaskBaseParams in
/// mask_base.slang.
struct MaskBase {
    std::uint32_t size[2];
    float         value;
    float         _pad;
};
static_assert(sizeof(MaskBase) == 16);

/// How many dabs one dispatch lays down.
///
/// A stroke is not capped at this by the *method* — a component with
/// `accumulate` set continues the coverage it is handed, so a long stroke is
/// several components chained nose to tail. Capping instead would either leave
/// gaps in a long stroke or silently resample the photographer's stroke into
/// something they did not draw. research/masking.md §1.
inline constexpr int kMaskDabsPerPass = 256;

/// How a component folds into the coverage before it. research/masking.md §6,
/// and the same values `ops/mask_ops.slang` names.
enum class MaskCompose : std::int32_t { Add = 0, Subtract = 1, Intersect = 2 };

/// One mask component — a primitive plus how it combines with the ones listed
/// before it. Mirrors MaskComponentParams in mask_component.slang, field for
/// field and offset for offset; the asserts below are what keep the two honest,
/// because a silently mismatched offset reads every later field from the wrong
/// place and renders as a plausible-looking mask rather than as anything
/// obviously broken.
///
/// **One radius for the whole brush, not one per dab** — that is the research's
/// own shape, and it is what makes a stroke a few kilobytes of centres rather
/// than a raster. research/masking.md §1 and §3.
struct alignas(8) MaskComponent {
    std::uint32_t size[2];
    std::int32_t  kind;        // 0 off, 1 linear, 2 radial, 3 brush
    std::int32_t  invert;      // inverts this component, before the fold
    std::int32_t  compose;     // MaskCompose
    std::int32_t  count;       // dabs this pass, <= kMaskDabsPerPass
    std::int32_t  accumulate;  // 1 continues the stroke in `src` and skips the fold
    float         radius;      // brush nib, normalized
    float         flow;        // 0..1 per dab
    float         hardness;    // 0 soft, 1 hard-edged
    float         feather;     // radial, 0..1
    float         roundness;   // radial superellipse exponent
    float         angle;       // radial rotation, radians
    float         _pad;
    float         zero[2];     // linear
    float         full[2];     // linear
    float         centre[2];   // radial
    float         semi[2];     // radial semi-axes, normalized
    float         dabs[kMaskDabsPerPass][2];   // brush centres, normalized
};
static_assert(sizeof(MaskComponent) == 88 + kMaskDabsPerPass * 8);
// Every float2 in the shader's struct must land on an eight-byte boundary, or
// Metal pads and every field after the first pair shifts.
static_assert(offsetof(MaskComponent, zero)   == 56);
static_assert(offsetof(MaskComponent, full)   == 64);
static_assert(offsetof(MaskComponent, centre) == 72);
static_assert(offsetof(MaskComponent, semi)   == 80);
static_assert(offsetof(MaskComponent, dabs)   == 88);

/// Guide subsampling. Mirrors GuideDownParams in guide_down.slang.
struct GuideDown {
    std::uint32_t outSize[2];
    std::uint32_t inSize[2];
    std::int32_t  scale;
    std::int32_t  _pad[3];
};
static_assert(sizeof(GuideDown) == 32);

/// Highlight reconstruction. Mirrors HighlightParams in highlights.slang.
struct Highlights {
    std::uint32_t size[2];
    float clipR, clipG;
    float clipB, gamma;
    float strength;
    float _pad;
};
static_assert(sizeof(Highlights) == 32);

/// À-trous blur, one scale. Mirrors AtrousParams in denoise_blur.slang.
struct AtrousBlur {
    std::uint32_t size[2];
    std::int32_t  step;     // 2^j
    std::int32_t  _pad;
};
static_assert(sizeof(AtrousBlur) == 16);

/// Shrinkage at one scale. Mirrors ShrinkParams in denoise_accum.slang.
struct AtrousShrink {
    std::uint32_t size[2];
    float noiseA;
    float noiseB;
    float scaleNorm;
    float strength;
    float chromaBoost;
    float _pad;
};
static_assert(sizeof(AtrousShrink) == 32);

struct Geometry {
    std::uint32_t outSize[2];
    std::uint32_t inSize[2];
    std::uint32_t quarterTurns;
    float         straightenRad;
    float         cropOrigin[2];
    float         cropSize[2];
    /// Straighten center, normalized in post-rotation frame space. Always the
    /// center of the user's crop rectangle, whether or not the crop tool's
    /// enlarged preview canvas is in play.
    float         pivot[2];
};
static_assert(sizeof(Geometry) == 48);

}  // namespace orion::pipe::params
