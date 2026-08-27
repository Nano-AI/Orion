/*  Host-side mirrors of the shader parameter blocks.
 *
 *  These MUST match the corresponding structs in the engine/shaders Slang sources byte
 *  for byte. Metal's rules: float4 aligns to 16, uint2 aligns to 8, and the
 *  struct is padded to its largest member's alignment. The static_asserts below
 *  catch a drift at compile time rather than as a mystery in the output image.
 */

#pragma once

#include "pipe/GrainPlate.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

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
    /// ⚠ Four local sets, one per layer. A layer is a run of mask components
    /// with its own coverage, so the subject can be graded one way and the sky
    /// another. All pointwise — research/masking.md §2b.
    float         layerExposureEv[4];
    float         layerContrast[4];
    float         layerSaturation[4];
    /// A color cast, not a white balance: temperature and tint are applied
    /// before the demosaic and cannot be local.
    float         layerWarmth[4];
    float         layerTint[4];
    /// The four tone bands, per layer, in the globals' units. The coverage
    /// scales the parameter, like every other local control.
    float         layerHighlights[4];
    float         layerShadows[4];
    float         layerWhites[4];
    float         layerBlacks[4];
    /// Which coverage texture each layer reads. ⚠ The graph is static, so a
    /// layer's coverage cannot be a node picked per render — the kernel binds
    /// all four component slots and this says which one ends each layer.
    std::int32_t  layerMask[4];
    std::int32_t  layerCount;
    float         maskActive;
    /// Draw the coverage on screen. A viewing aid; never set for an export.
    float         maskOverlay;
    /// Which layer the overlay paints — the one being edited.
    std::int32_t  maskOverlayLayer;
};
static_assert(sizeof(LinearAdjust) == 320);

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

/// The guided filter's edge threshold, in **squared log2-exposure units**.
///
/// He, Sun & Tang's coefficient is `a = var(I) / (var(I) + eps)`, so `eps` is
/// the local variance at which exactly half the detail passes through. In the
/// units this pipeline works in that makes `sqrt(eps)` a **local standard
/// deviation in stops**: 0.2 of one.
///
/// ⚠ **A standard deviation, not a step height, and the two differ by a factor
/// of two.** A window straddling a step of `h` stops half and half has variance
/// `h²/4`, so the step that half-passes is `h = 2·sqrt(eps)` = **0.4 stops**.
/// The comment at the call site said "about a fifth of a stop" and meant the
/// standard deviation; a reader checking it against an edge in a photograph
/// would have measured twice that and thought the filter wrong.
///
/// ⚠ Named here rather than written at the call site so a check can assert the
/// threshold the product actually ships. `research/UNSOURCED.md` §7 carried this
/// as *reasoned but untested* until #187 measured it — the reasoning was right
/// and the arithmetic relating it to an edge was never written down.
inline constexpr float kGuideEpsilon = 0.04f;

// ── Guided feathering of the mask group (research/masking.md §4) ───────────

struct MaskGuidePrep {
    std::uint32_t outSize[2];
    std::uint32_t inSize[2];
    std::int32_t  scale;
    std::int32_t  _pad[3];
};
static_assert(sizeof(MaskGuidePrep) == 32);

struct MaskGuideAb {
    std::uint32_t size[2];
    float         epsilon;
    float         _pad;
};
static_assert(sizeof(MaskGuideAb) == 16);

struct MaskGuideApply {
    std::uint32_t size[2];
    std::uint32_t coeffSize[2];
    float         strength;
    float         _pad[3];
};
static_assert(sizeof(MaskGuideApply) == 32);

/// Spot removal — research/spot-removal.md. Shared by both kernels; the
/// measure pass ignores the fields the apply pass needs and vice versa, which
/// is cheaper than two nearly identical blocks that can drift apart.
inline constexpr int kMaxSpots = 64;

struct alignas(16) SpotMeasure {
    std::uint32_t size[2];
    std::int32_t  count;
    std::int32_t  samples;
    float         spots[kMaxSpots][4];   // xy destination, zw source
    float         shape[kMaxSpots][4];   // x radius, y feather, z heal
};
static_assert(sizeof(SpotMeasure) == 16 + kMaxSpots * 32);

struct alignas(16) SpotApply {
    std::uint32_t size[2];
    std::int32_t  count;
    std::int32_t  _pad;
    float         spots[kMaxSpots][4];
    float         shape[kMaxSpots][4];
};
static_assert(sizeof(SpotApply) == 16 + kMaxSpots * 32);

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
    /// Nonzero when this node writes eight bits. ⚠ It is no longer always the
    /// node that does: film grain must be added to unquantised values, so with
    /// the Amount slider up this node writes `RGBA16Float` and `develop:grain`
    /// carries the flag instead. Exactly one of the two holds it at a time and
    /// `DevelopPipeline::retargetOutputChain` is the only place that decides.
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

/// Three-way color grading, and the creative vignette fused into the same pass.
/// Mirrors GradeParams in color_grade.slang.
/// Each row is an already zero-sum RGB offset with that zone's slope in w.
struct alignas(16) Grade {
    std::uint32_t size[2];
    float         _pad0, _pad1;
    float         shadow[4];
    float         midtone[4];
    float         highlight[4];
    /// The creative vignette — research/vignette.md, decision #103. The circle
    /// is the *composition's*, not the frame's: center normalized in this
    /// frame, radius its half-diagonal in units of the frame's height, both
    /// derived on the host from the crop rectangle by
    /// `DevelopPipeline::compositionCircle`.
    float         vignetteCenter[2];
    float         vignetteRadius;
    /// Stops at the corner. Zero is the identity and skips the falloff.
    float         vignetteAmount;
    /// tan of the half-diagonal field angle of the lens being imitated, so the
    /// kernel evaluates cos^4 with no trigonometry.
    float         vignetteTanTheta;
    /// Balance, -1..+1 — decision #101, research/UNSOURCED.md §26. Slides all
    /// three zone centres along log2(Y/0.18); positive moves them down, so more
    /// of the picture counts as highlight. Zero is bit-identical to no Balance,
    /// and it takes one of the three pad words rather than growing the block.
    float         balance;
    float         _pad2[2];
};
static_assert(sizeof(Grade) == 96);

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
/// How many dabs sit on one row of a component's dab texture, and how many
/// rows it has. 256 x 64 is 16,384 dabs — at the nib's spacing that is roughly
/// eighty frame-widths of stroke, against the 256 a constant block could hold.
inline constexpr int kDabStride = 256;
inline constexpr int kDabRows   = 64;
inline constexpr int kMaxDabs   = kDabStride * kDabRows;

/// How many consecutive dabs share one bounding box.
/// `research/brush-acceleration.md` — Clark (1976), hierarchical bounding
/// volumes, one level.
///
/// The kernel's cost is one texture fetch per dab **per pixel**, so at 2400 dabs
/// it was 2.1 seconds a render. A box per run of 64 lets a pixel skip 64 fetches
/// with one test.
///
/// ⚠ 64 is the equilibrium between the two ways this loses: the `count/64`
/// bounds fetches every pixel pays whatever happens (1024 of them at a block of
/// 16, which is worse than the disease) and the wasted inner work when a block
/// is entered (256 dabs dragged in at a block of 256, on a box that a curving
/// run leaves slack).
///
/// ⚠ **Runs of consecutive dabs, never a spatial partition.** Paint is
/// source-over and erase is destination-out and the two do not commute, so the
/// dabs over a pixel must be applied in the order they were laid. An index range
/// keeps that for free; a per-tile bin has to rebuild it.
inline constexpr int kDabBlock  = 64;
inline constexpr int kMaxDabBlocks = (kMaxDabs + kDabBlock - 1) / kDabBlock;

/// ⚠ `DAB_BLOCK` in `mask_component.slang` must be this number. The two cannot
/// be shared — one is C++ and the other is Slang — so this assert is the tripwire:
/// changing the block size here fails the build and sends whoever did it to the
/// shader. Disagreeing silently is not a crash, it is a box read against the
/// wrong run of dabs, which drops paint out of the middle of a stroke and looks
/// like a brush that skips.
static_assert(kDabBlock == 64, "mask_component.slang DAB_BLOCK must match");

/// Builds the per-block boxes from the dab texels that were uploaded.
///
/// `texels` is the RGBA32F dab texture's contents, four floats a dab, and
/// `bounds` receives `kMaxDabBlocks` × 4 floats: `(minX, minY, maxX, maxY)` of
/// the centers in each run of `kDabBlock`.
///
/// ⚠ **It takes the texels, deliberately, rather than the positions they came
/// from.** The kernel's rejection is bit-identical only because every center in
/// a block is ≥ that block's stored minimum; a box computed from
/// higher-precision values before the write can round tighter than the float32
/// that actually lands in the texture, and the kernel would then skip a dab the
/// full loop would have composited.
///
/// ⚠ And it lives here rather than in `DevelopPipeline.cpp` because the GPU
/// tests dispatch the kernel directly and need the same boxes. A second
/// implementation in the test would be a stand-in with its own bugs, checking
/// the product against a copy of the product's own mistake.
inline void buildDabBounds(const float* texels, int count, float* bounds)
{
    for (int i = 0; i < kMaxDabBlocks * 4; ++i) bounds[i] = 0.0f;
    const int blocks = (count + kDabBlock - 1) / kDabBlock;
    for (int b = 0; b < blocks; ++b) {
        const int lo = b * kDabBlock;
        const int hi = (lo + kDabBlock < count) ? lo + kDabBlock : count;
        float minX = texels[std::size_t(lo) * 4 + 0];
        float minY = texels[std::size_t(lo) * 4 + 1];
        float maxX = minX, maxY = minY;
        for (int d = lo + 1; d < hi; ++d) {
            const float x = texels[std::size_t(d) * 4 + 0];
            const float y = texels[std::size_t(d) * 4 + 1];
            if (x < minX) minX = x;
            if (x > maxX) maxX = x;
            if (y < minY) minY = y;
            if (y > maxY) maxY = y;
        }
        bounds[std::size_t(b) * 4 + 0] = minX;
        bounds[std::size_t(b) * 4 + 1] = minY;
        bounds[std::size_t(b) * 4 + 2] = maxX;
        bounds[std::size_t(b) * 4 + 3] = maxY;
    }
}

/// Everything besides the dab centers that changes what a laid dab covers.
///
/// The predicate below has to reject on these as well as on the texels: two
/// uploads can carry identical centers and still paint differently, because the
/// nib is one radius for the whole stroke rather than one per dab. Widening the
/// brush and painting on re-lays every dab that came before it.
///
/// ⚠ `nibPx` is in **frame pixels** and is derived from the crop, so a tighter
/// crop moves it with the geometry. That is deliberate — it is the number the
/// kernel reads.
struct BrushShape {
    std::int32_t kind = 0;      // 3 is a brush; anything else has no stroke
    float        nibPx = 0.0f;
    float        flow = 0.0f;
    float        hardness = 0.0f;
    bool operator==(const BrushShape&) const = default;
};

/// One component's last uploaded stroke, kept so the next upload can be
/// compared against it. `research/brush-acceleration.md` — this is the host
/// half of the incremental accumulator, and it is deliberately built and
/// attacked a session before anything reads it.
///
/// `texels` holds only the live prefix — four floats a dab, exactly the values
/// handed to `updateAux` — so a short stroke costs a few kilobytes and the cap
/// costs 256 KB, the same size as the buffer `apply` builds and throws away on
/// every pointer event anyway.
struct BrushPrefixState {
    std::vector<float> texels;   ///< 4 floats a dab, post-transform, as uploaded
    int                count = 0;
    BrushShape         shape{};
    /// False until a stroke has actually been uploaded for this component, and
    /// reset whenever the accumulator behind it would be invalid — a reload, or
    /// the component ceasing to be a brush.
    bool               live = false;
};

/// **How many leading dabs of this stroke are unchanged since the last upload.**
///
/// Returns the length of the stable prefix: the number of dabs at the head of
/// `texels` that are bit-identical to the ones stored in `prev`, capped at
/// whichever of the two strokes is shorter. `0` if nothing was stored, or if
/// the nib, the flow, the hardness or the kind moved.
///
/// ⚠ **It compares the post-transform texels — the floats actually uploaded —
/// never the displayed-coordinate dab list.** A crop, a straighten or a quarter
/// turn moves every center through `mask::toFrame` while leaving the stored
/// stroke untouched, and a predicate reading the pre-transform list would call
/// that prefix unchanged and keep coverage belonging to the old geometry. This
/// is the same reason `buildDabBounds` above takes the texels.
///
/// ⚠ **The comparison is `memcmp`, not `==`.** The claim being made is that the
/// kernel would compute the same coverage from the same inputs, and that
/// follows from identical bits with no argument about float semantics attached.
/// `==` would additionally have to defend `-0.0f == 0.0f` and `NaN != NaN`.
///
/// ⚠ The erase flag rides in the texel's `z`, so it is inside the comparison
/// for free — which it must be: paint is source-over, erase is destination-out,
/// and **the two do not commute**, so a dab that changed from one to the other
/// invalidates every dab after it.
///
/// ⚠ **A grown count is not evidence of an unchanged prefix.** The tempting
/// cheap version — "the stroke got longer, so what came before it is still
/// there" — fails on *undo three dabs and paint three different ones*: the same
/// count, a different prefix, and the coverage that survives renders a
/// completely plausible brushstroke that nothing perceptual can see is wrong.
/// The loop below is the whole reason this function exists.
inline int unchangedPrefix(const BrushPrefixState& prev, const BrushShape& shape,
                           const float* texels, int count)
{
    if (!prev.live || texels == nullptr) return 0;
    if (!(shape == prev.shape)) return 0;
    const int stored = std::min(prev.count,
                                int(prev.texels.size() / 4));
    const int n = std::min(count, stored);
    for (int d = 0; d < n; ++d) {
        if (std::memcmp(&texels[std::size_t(d) * 4],
                        &prev.texels[std::size_t(d) * 4],
                        4 * sizeof(float)) != 0) {
            return d;
        }
    }
    return n;
}

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
/// own shape, and it is what makes a stroke a few kilobytes of centers rather
/// than a raster. research/masking.md §1 and §3.
struct alignas(8) MaskComponent {
    std::uint32_t size[2];
    std::int32_t  kind;        // 0 off, 1 linear, 2 radial, 3 brush, 4 matte, 5 range
    std::int32_t  invert;      // inverts this component, before the fold
    std::int32_t  compose;     // MaskCompose
    std::int32_t  count;       // dabs in this component's stroke
    std::int32_t  accumulate;  // 1 continues the stroke in `src` and skips the fold
    float         nibPx;       // brush nib radius, in frame pixels
    float         flow;        // 0..1 per dab
    float         hardness;    // 0 soft, 1 hard-edged
    float         feather;     // radial, 0..1
    float         roundness;   // radial superellipse exponent
    float         angle;       // radial rotation, radians, in frame coordinates
    float         _pad;
    /// A linear gradient's ramp: t(q) = <rampNum, (q,1)>, **affine in frame
    /// coordinates** — the space the kernel runs in and the space every mask
    /// is stored in. The geometry the mask was drawn under entered these
    /// numbers once, at the gesture (or at migration for a display-space-era
    /// sidecar); nothing here changes when the crop, straighten, turns or
    /// correction move later, which is what anchors the ramp to the image.
    ///
    /// ⚠ The field's history is the bug's history. Two endpoints (pre-#137)
    /// put the level sets perpendicular in the wrong space under a keystone.
    /// The exact display-space pull-back that replaced them (#137/#138 — this
    /// numerator over a stored 3×3's bottom row) transformed exactly, and
    /// *therefore* pinned the mask to the crop rectangle: the matrix carried
    /// the live geometry, so cropping after placement re-aimed the stored
    /// numbers at different image pixels. Frame storage removed the matrix
    /// and the bug together.
    float         rampNum[3];  // linear
    float         _pad2;

    float         center[2];   // radial centre, frame coordinates, as stored
    float         semi[2];     // radial semi-axes, frame coordinates, as stored
    /// Luminance range (kind 5), in stops — log2 Rec.2020 luminance on the
    /// reference image. Two independent smootherstep edges `rangeSoft` stops
    /// wide. research/masking.md §4b.
    float         rangeLo;
    float         rangeHi;
    float         rangeSoft;
    /// Added to the measured stops so the band matches what is on screen.
    float         rangeBias;
    /// Dabs per row of the dab texture. See mask_component.slang.
    ///
    /// ⚠ Placed here, after the range block, because that is where the shader
    /// puts it. The first version of this change inserted it *before* the range
    /// block on this side and after it on the other — the two structs would
    /// have disagreed from offset 88 onward, and every field past it would have
    /// been read from the wrong place. That is precisely what the asserts below
    /// exist to catch, and they did.
    std::int32_t  dabStride;
    /// **Where the dab loop starts.** Zero lays the whole stroke; anything
    /// larger says the persistent accumulator already holds the coverage of
    /// dabs `[0, firstDab)` and the kernel should continue from it.
    ///
    /// ⚠ Only ever set for the component that owns the accumulator — see
    /// `accumUse` — and only when the host has *observed* that the accumulator
    /// holds exactly that prefix. `params::unchangedPrefix` answering N is a
    /// statement about the previous upload, not about the texture.
    /// research/brush-acceleration.md; decision #108.
    std::int32_t  firstDab;
    /// Non-zero when this component owns the accumulator: it reads it when
    /// `firstDab > 0` and writes its coverage back either way.
    ///
    /// ⚠ **At most one component may have this set.** Two writers would
    /// interleave two strokes into one texture, and what renders is a
    /// brushstroke — just not one anybody drew.
    std::int32_t  accumUse;
    std::int32_t  _pad3;
    /// Matte (kind 4): the live rectangle of the aux texture, which is
    /// allocated for the largest matte a producer might hand over. Zero
    /// disables the branch. research/masking.md §5.
    std::uint32_t matteSize[2];
    /// Color range (kind 6): the picked shade, as scene-linear Rec.2020 RGB,
    /// converted to Oklab chromaticity by the kernel rather than by the host —
    /// one implementation of the transform, so the target and the pixel cannot
    /// disagree. `colorTol` is a Euclidean radius in (a/L, b/L) and
    /// `colorSoft` is how far the edge ramps. research/masking.md §4c.
    float         colorR;
    float         colorG;
    float         colorB;
    float         colorTol;
    float         colorSoft;
    /// Non-zero when this component begins a layer — the fold restarts here.
    std::int32_t  startsLayer;
};
static_assert(sizeof(MaskComponent) == 152);
// Every float2 in the shader's struct must land on an eight-byte boundary, or
// Metal pads and every field after the first pair shifts. `_pad2` after the
// ramp exists for exactly that: without it `center` would sit at 68, which
// Metal rounds up to 72 while C++ does not, and every field below would be
// read four bytes off — a plausible mask, not an obvious break.
static_assert(offsetof(MaskComponent, rampNum) == 56);
static_assert(offsetof(MaskComponent, center)    == 72);
static_assert(offsetof(MaskComponent, semi)      == 80);
static_assert(offsetof(MaskComponent, rangeLo)   == 88);
static_assert(offsetof(MaskComponent, dabStride) == 104);
static_assert(offsetof(MaskComponent, firstDab)  == 108);
static_assert(offsetof(MaskComponent, accumUse)  == 112);
static_assert(offsetof(MaskComponent, matteSize) == 120);
static_assert(offsetof(MaskComponent, colorR)    == 128);
static_assert(offsetof(MaskComponent, colorTol)  == 140);
static_assert(offsetof(MaskComponent, colorSoft) == 144);

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

/// The clipping mask. Mirrors HlMaskParams in hl_mask.slang.
struct HlMask {
    std::uint32_t outSize[2];
    std::uint32_t inSize[2];
    std::uint32_t scale;
    float         clip;
    float         gamma;
    float         shoulder;
};
static_assert(sizeof(HlMask) == 32);

/// The fill, lifted back into the picture. Mirrors HlApplyParams in
/// hl_apply.slang.
struct HlApply {
    std::uint32_t size[2];
    std::uint32_t fillSize[2];
    std::uint32_t scale;
    float         clip;
    float         gamma;
    float         strength;
};
static_assert(sizeof(HlApply) == 32);

/// Harmonic fill, pull half. Mirrors HlPullParams in hl_pull.slang.
struct HlPull {
    std::uint32_t outSize[2];
    std::uint32_t inSize[2];
};
static_assert(sizeof(HlPull) == 16);

/// Harmonic fill, push half. Mirrors HlPushParams in hl_push.slang.
struct HlPush {
    std::uint32_t size[2];
    std::uint32_t coarseSize[2];
};
static_assert(sizeof(HlPush) == 16);

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

/// What one unit of `Grain::amount` actually delivers as peak standard
/// deviation, **measured** — `testGrainGpu` pins it.
///
/// ⚠ Not 1.0, and the gap is not a defect. The plate is resampled bilinearly at
/// a rate the Size slider sets, and interpolating a correlated random field
/// reduces its variance; this is what is left. It is flat to ~1.5% across the
/// whole reachable Size range, which is the property that matters — Size
/// changes how big the grain is, not how strong.
///
/// ⚠ The one rate where it does *not* hold is exactly 1.0 frame pixels per
/// plate texel, where every sample lands on a texel center, no interpolation
/// happens, and the field comes back at its full 1.0. That is a knife edge —
/// 1.02 already measures 0.888 — so `kGrainSizeMin` puts it out of reach rather
/// than leaving a 14% step in the middle of a slider.
inline constexpr float kGrainSigmaFactor = 0.875f;

/// Grain radius in frame pixels, at the ends of the Size slider. The floor is
/// above 1.0 deliberately; see `kGrainSigmaFactor`. It is also where the
/// physics agrees: grain finer than the pixel it lands in is aliasing.
inline constexpr float kGrainSizeMin = 1.2f;
inline constexpr float kGrainSizeMax = 8.0f;

/// Film grain. Mirrors GrainParams in grain.slang; research/film-grain.md, #81.
struct alignas(16) Grain {
    std::uint32_t size[2];
    /// Nonzero when this node writes eight bits. ⚠ Inherited from `Display`:
    /// grain has to be added to unquantised values, so this node is the one
    /// that quantises and the Bayer dither moved here with the flag.
    std::uint32_t dither;
    /// Peak standard deviation in display units. Zero disables the branch.
    float         amount;
    /// Grain radius in frame pixels.
    float         grainSize;
    /// Frame pixels per node pixel: 1 in the full graph, `kPreviewScale` in the
    /// preview. ⚠ What makes the two graphs sample **one field** at two
    /// resolutions rather than two realisations of it.
    float         gridStep;
    float         _pad[2];
};
static_assert(sizeof(Grain) == 32);

/// ⚠ The shader hard-codes these; it cannot include a C++ header. Two
/// derivations of one plate geometry is how a level gets read from the wrong
/// rows — plausible noise over the wrong grain size, which looks like a taste
/// problem rather than a bug.
static_assert(grain::kPlateSize == 2048, "PLATE_SIZE in grain.slang");
static_assert(grain::kPlateLevels == 12, "PLATE_LEVELS in grain.slang");

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

    /// Nonzero when `perspective` below is anything but the identity.
    ///
    /// ⚠ A flag rather than a comparison in the shader, and it is what makes a
    /// zeroed control **bit-identical** to a build with no perspective in it:
    /// the kernel takes exactly the branch it took before, so every baseline in
    /// every suite stays where it was rather than silently rebasing.
    std::uint32_t perspectiveOn;
    float         _pad[3];

    /// The destination-to-source homography in texel coordinates of the rotated
    /// frame, one row per float4. research/perspective.md.
    ///
    /// ⚠ Rows are padded to sixteen bytes because a constant buffer pads a
    /// three-vector to sixteen anyway, and a struct that disagrees with its
    /// Slang mirror about that is silent: the kernel reads the wrong words and
    /// resamples a plausible wrong picture. `.w` is unused and is zero.
    /// The matrix begins at offset 64, which is where the alignment wants it.
    float         perspective[3][4];
};
static_assert(sizeof(Geometry) == 112);
static_assert(offsetof(Geometry, perspective) == 64,
              "the homography must start on a sixteen-byte boundary; "
              "geometry.slang's mirror assumes it");

}  // namespace orion::pipe::params
