/*  The standard develop graph.
 *
 *  Builds and owns the seven-node pipeline that turns a decoded mosaic into
 *  display-ready pixels, and exposes the adjustments on top of it. Both the
 *  app and the bench tool drive this, so what you measure is what you see.
 */

#pragma once

#include "gpu/Resources.h"
#include "pipe/Adjustments.h"
#include "pipe/HueSatMap.h"
#include "pipe/CubeLut.h"
#include "pipe/Dehaze.h"
#include "pipe/ExposureFusion.h"
#include "pipe/HighlightFill.h"
#include "pipe/LocalLaplacian.h"
#include "pipe/MaskGeometry.h"
#include "pipe/ShaderParams.h"
#include "pipe/Pipeline.h"
#include "pipe/ToneCurve.h"
#include "pipe/WhiteBalance.h"
#include "raw/NoiseProfile.h"
#include "raw/RawImage.h"

#include <array>
#include <utility>
#include <memory>
#include <string>

namespace orion::pipe {

/// What the brush prefix predicate said the last time it ran for a component.
///
/// ⚠ **`evaluations` is here so a check on `prefix` cannot be vacuous.** A test
/// that asserts "the prefix was the whole previous stroke" passes trivially if
/// the predicate never ran at all — `apply` skips a component whose edit did not
/// change, so a forgotten `brushRevision` bump would leave a stale `prefix`
/// sitting there reading exactly like a fast path that was taken. Assert that
/// this counter moved as well, and the check has something to fail on.
struct BrushPrefixStat {
    /// How many times `params::unchangedPrefix` has run for this component.
    int evaluations = 0;
    /// Its last answer — the stable prefix length. `-1` before the first run.
    int prefix = -1;
    /// The stroke lengths that answer was about: what was uploaded before, and
    /// what is being uploaded now.
    int previousCount = 0;
    int count = 0;
    /// **What the kernel was actually told**, which is not the same number as
    /// `prefix` and must not be assumed to be. `prefix` is a statement about
    /// the previous *upload*; this is a statement about the *texture*, and it
    /// is zero unless the accumulator is known to hold exactly that many dabs
    /// of this stroke. Decision #108.
    ///
    /// ⚠ Here so a check can assert the fast path was *taken*. An accumulator
    /// that is never continued from is correct and buys nothing, and every
    /// bit-identity test in this file passes on it.
    int firstDab = 0;
    /// How many times `reconcileBrushAccum` has refused the fast path because
    /// the node was about to run on parameters `apply` did not push. Counted
    /// rather than inferred: the refusal produces a *correct* frame, so nothing
    /// about the picture can tell whether it happened.
    int refusals = 0;
};

class DevelopPipeline {
public:
    DevelopPipeline(gpu::Device& device, const std::string& shaderDir,
                    const raw::BayerImage& image);

    /// The linear-source graph: a demosaiced DNG (a merged HDR frame) instead
    /// of a mosaic. One node — white balance and the clip — stands where
    /// linearize, the demosaic and the highlight machinery stand for a mosaic;
    /// everything from the denoise input onward is the same graph. The absent
    /// nodes are not built at all rather than disabled: disabled nodes still
    /// own their textures, and this path exists partly to *not* pay for them.
    DevelopPipeline(gpu::Device& device, const std::string& shaderDir,
                    const raw::LinearImage& image);

    /// True when this pipeline can be reused for `image` — same dimensions and
    /// same CFA layout. Rebuilding costs sixteen shader compiles and about
    /// 2.5 GiB of texture allocation, so reusing is the difference between
    /// switching photos in milliseconds and in seconds.
    [[nodiscard]] bool canReload(const raw::BayerImage& image) const noexcept;

    /// The linear twin. A linear image never reloads a mosaic graph and vice
    /// versa — the two build different node sets.
    [[nodiscard]] bool canReload(const raw::LinearImage& image) const noexcept;

    /// Swaps in a new image without touching the compiled graph.
    void reload(const raw::BayerImage& image);
    void reload(const raw::LinearImage& image);

    /// Pushes the current adjustments into the graph, dirtying only what they
    /// affect. Cheap — call it freely on every slider tick.
    void apply(const Adjustments&);

    /// Replaces one component's brush stroke: dab centers in normalized
    /// coordinates of the displayed picture, the same space the gradient masks
    /// are placed in.
    ///
    /// Kept out of `Adjustments` because it is variable-length and that struct
    /// is compared field by field on every tick. Bump a center and
    /// `MaskComponentEdit::brushRevision` is what tells `apply` the mask is
    /// stale — so a caller that changes the stroke must change the revision too,
    /// or the picture will not follow the hand.
    ///
    /// Out-of-range `component` is ignored rather than clamped: writing a stroke
    /// into the wrong component would put paint somewhere the photographer did
    /// not, which is worse than nothing happening.
    /// `erase` is one value per dab: nonzero takes coverage away instead of
    /// laying it down. Null means the whole stroke paints, which is what every
    /// caller written before erasing existed sends.
    void setBrushStroke(int component, const float* xy, const float* erase,
                        int count);

    /// Uploads a raster matte for one component — a segmentation result, or
    /// anything else that is an image rather than a formula.
    ///
    /// ⚠ **`alpha` is in frame coordinates**: row-major, top-left origin, the
    /// whole uncropped and unturned frame, values 0..1. A producer working from
    /// the displayed picture has to undo the geometry before calling this. The
    /// kernel does no correction, deliberately — see `auxMatte_`.
    ///
    /// Larger than `kMaxMatteEdge` on either side is rejected rather than
    /// scaled: silently resampling someone's matte is how a boundary loses the
    /// precision they went to the trouble of producing.
    ///
    /// Passing `nullptr` clears the matte, which is what "no longer a matte
    /// component" means.
    bool setMaskMatte(int component, const float* alpha, int width, int height);

    /// A point on the displayed picture, in the frame the mask and spot
    /// kernels work in. Uses the geometry last applied.
    [[nodiscard]] std::pair<float, float> displayedToFrame(float x, float y) const;

    /// The inverse. A spot is stored in frame coordinates, so drawing one takes
    /// this — nothing else in the program needs it, because a mask is stored in
    /// displayed coordinates already.
    [[nodiscard]] std::pair<float, float> frameToDisplayed(float x, float y) const;

    [[nodiscard]] std::uint32_t maxMatteWidth()  const noexcept { return matteW_; }
    [[nodiscard]] std::uint32_t maxMatteHeight() const noexcept { return matteH_; }

    /// How many dabs one component's stroke holds.
    [[nodiscard]] int brushDabCount(int component) const noexcept {
        if (component < 0 || component >= kMaxMaskComponents) return 0;
        return int(brushDabs_[std::size_t(component)].size() / 2);
    }

    /// What the incremental-brush predicate answered for one component the last
    /// time `apply` pushed its stroke. Read-only, and nothing in the renderer
    /// reads it yet — session one of `ROADMAP.md`'s incremental accumulator
    /// ships the predicate alone, deliberately, because a stale accumulator's
    /// failure mode is a picture that looks right.
    [[nodiscard]] BrushPrefixStat brushPrefixStat(int component) const noexcept {
        if (component < 0 || component >= kMaxMaskComponents) return {};
        return brushPrefix_[std::size_t(component)];
    }

    /// Which component the one brush accumulator currently belongs to, or -1.
    [[nodiscard]] int brushAccumOwner() const noexcept { return accumOwner_; }

    /// Bytes the accumulator is holding. Zero until a dab is laid — it is
    /// registered at 1x1 and grown on first use, because it is ~97 MB at
    /// 24 Mpx and most photographs are never painted on.
    [[nodiscard]] std::size_t brushAccumBytes() const;

    /// Renders every dirty node. Returns GPU-side milliseconds.
    double render();

    /// Output dimensions after orientation, which is what the UI should show.
    /// Total clockwise quarter turns currently applied.
    [[nodiscard]] int quarterTurns() const noexcept { return turns_; }

    [[nodiscard]] std::uint32_t outputWidth()  const noexcept;
    [[nodiscard]] std::uint32_t outputHeight() const noexcept;

    /// The whole frame after rotation, before any crop — what the crop
    /// rectangle is normalized against.
    [[nodiscard]] std::uint32_t frameWidth()  const noexcept;
    [[nodiscard]] std::uint32_t frameHeight() const noexcept;

    /// The camera's own white balance, used as the starting point.
    [[nodiscard]] WhiteBalance asShotWhiteBalance() const noexcept { return asShot_; }

    [[nodiscard]] const gpu::Texture& output() const { return pipeline_.output(); }

    /// Loads a creative LUT, uploading its grid into the display node's second
    /// auxiliary texture. Replaces whatever was there.
    ///
    /// Not an adjustment: adjustments are plain data that a sidecar round-trips
    /// and the bench sweeps, and a lookup table is neither. What *is* an
    /// adjustment is how much of it to apply.
    void setCreativeLut(const CubeLut&);
    void clearCreativeLut();

    [[nodiscard]] bool hasCreativeLut() const noexcept { return lutSize_ >= 2; }
    [[nodiscard]] const std::string& creativeLutTitle() const noexcept { return lutTitle_; }

    /// Sixteen bits out of the display and geometry nodes, or eight.
    ///
    /// The screen's drawable is `bgra8Unorm`, so the wide path moves twice the
    /// bytes through the two largest nodes in the graph for precision the
    /// display cannot show. Export is the only consumer that can use it.
    /// Switching reallocates two textures and re-renders them, so it belongs
    /// around an export and nowhere near a slider.
    void setWideOutput(bool wide);
    [[nodiscard]] bool wideOutput() const noexcept { return wideOutput_; }

    /// Frame pixels per pixel of this graph: 1 for the full render, and the
    /// preview's decimation factor for the preview graph.
    ///
    /// ⚠ **This is what makes the two graphs sample one grain field rather than
    /// two realisations of it.** The plate is addressed in frame coordinates,
    /// so without it a preview pixel would show the per-pixel variance of the
    /// field where the settled render averages `step^2` of them — and the
    /// preview would read an order of magnitude grainier than the picture it
    /// previews. See research/film-grain.md and decision #81.
    void setGridStep(float step);

    /// The image after white balance and the camera matrix, but before any user
    /// adjustment. This is what the color picker must sample: reading the
    /// edited result would mean adjusting a band changes which band you would
    /// pick next time, which is a feedback loop, not a tool.
    [[nodiscard]] const gpu::Texture& referenceImage() const {
        return pipeline_.nodeOutput(nHueSat_);
    }
    [[nodiscard]] Pipeline&           graph()        { return pipeline_; }
    [[nodiscard]] const Pipeline&     graph() const  { return pipeline_; }

    /// What `highlights` was handed, what it returned, and the ceiling both
    /// were judged against.
    ///
    /// Exists for the clip-set census in `apps/bench` (block 3e) and
    /// `research/highlight-reconstruction.md` §7: the question "how much of the
    /// partial-clip region does the shipping window fit decline" is answerable
    /// only by comparing this node's two sides against the *same* clip level
    /// the node itself used. A second derivation of that ceiling in the bench
    /// would be measuring a different set from the one the shader classified.
    struct HighlightStages {
        const gpu::Texture* input  = nullptr;   ///< linear camera RGB, pre-recovery
        const gpu::Texture* output = nullptr;   ///< after `highlightRecover`
        /// After `hlApply`. ⚠ Stale unless the fill ran on the last render —
        /// the node is disabled at `highlightRecovery` 0 and holds whatever it
        /// last wrote. Only meaningful with the control up.
        const gpu::Texture* filled = nullptr;
        float clip  = 0.0f;                     ///< the common ceiling linearize clipped to
        float gamma = 0.0f;                     ///< fraction of it that counts as clipped
    };
    [[nodiscard]] HighlightStages highlightStages() const;

    [[nodiscard]] std::uint32_t width()  const noexcept { return width_; }
    [[nodiscard]] std::uint32_t height() const noexcept { return height_; }

private:
    // ── How this class is laid out across five files (decision #113) ───────
    //
    // The graph has four regions, and each one owns a `.cpp` that holds *both*
    // halves of it: the nodes it adds at construction and the parameters it
    // pushes per frame. That pairing is the whole point. Adding a node used to
    // mean editing the constructor and then `apply` twelve hundred lines
    // further down; now the two edits are adjacent.
    //
    // | file | region | builds | pushes |
    // |---|---|---|---|
    // | `DevelopCapture.cpp` | sensor to profile | linearize, demosaic, highlights, fill, denoise, lens, spots, sharpen, matrix, hue/sat | white balance, highlights, lens, denoise, spots, sharpen |
    // | `DevelopLocal.cpp`   | the multi-node operators | dehaze, clarity, fusion, guided filter | the same four, and their two frame reductions |
    // | `DevelopMask.cpp`    | the mask group | base, components, refine chains, the brush accumulator | components, strokes, feathering |
    // | `DevelopOutput.cpp`  | tone to the screen | develop:linear, grade, display, grain, geometry | tone, grade, display, grain, geometry |
    // | `DevelopPipeline.cpp`| the spine | the calls below, in order | `ApplyContext`, then the calls below, in order |
    //
    // ⚠ **Both call lists are order-critical and neither may be sorted.**
    // Nodes are added in graph order and their indices are held in the members
    // below, so a reordered `build` compiles and changes which node feeds
    // which. `apply`'s order is preserved for the weaker reason that it was
    // never proved not to matter — the stages push to distinct nodes, but that
    // is a claim about 1,300 lines, and #113 was a refactor rather than a
    // place to start making claims.

    /// What one `apply` derives once and several stages then read.
    ///
    /// ⚠ **Derived per call, never stored.** Every field is a pure function of
    /// the adjustments handed in, `lastAdj_` and the frame's shape. It exists
    /// because the stages are *not* independent: the mask's placement and the
    /// geometry node's matrix are the same perspective, and a second
    /// derivation of it is exactly how a mask ends up a few percent off its
    /// subject on a corrected photograph.
    struct ApplyContext {
        /// Nothing that was pushed can be assumed — the first apply, or a
        /// different photograph through the same graph.
        bool first = false;

        /// The composed keystone, or **null** when the control is neutral.
        /// Null rather than an identity matrix, so the neutral case takes the
        /// branch a build without perspective took and stays bit-identical.
        const persp::Matrix3* perspective = nullptr;

        /// The crop, the total quarter turns and the rotated frame's shape, as
        /// `geometry.slang` sees them. Every mask centre and every brush dab
        /// goes through these — pipe/MaskGeometry.h.
        mask::Crop crop{};
        int   turns = 0;
        float rotW = 0.0f, rotH = 0.0f;

        /// Anything a mask's placement depends on moved: the crop, the turns,
        /// the straighten or the perspective.
        bool frameMoved = false;
        /// A component was added, removed, hidden or shown.
        bool visibilityMoved = false;

        /// The guided filter feeds the local highlight and shadow masks only.
        /// Read by the mask stage as well, because `guide:prep` is shared and
        /// must be enabled if *either* consumer wants it.
        bool needsGuide = false;
        /// Guided feathering of the fold is on.
        bool refining = false;
    };

    /// Composes the above. The one place any of it is derived.
    [[nodiscard]] ApplyContext contextFor(const Adjustments&);

    // The four regions' constructors, called in this order and no other.
    void buildCaptureNodes();
    /// Spots, sharpening and the camera profile — the part of the capture
    /// region both kinds of source share, downstream of nLens_.
    void buildCaptureTail();
    void buildLocalNodes();
    void buildMaskNodes();
    void buildOutputNodes();

    // The size- and image-derived blocks, pushed once per photograph rather
    // than per tick. ⚠ Decision #92: a block re-pushed for a value nothing
    // reads still dirties everything downstream of it.
    void pushStaticCaptureParams(const raw::BayerImage&);
    void pushStaticCaptureParams(const raw::LinearImage&);
    /// The tail both overloads share: the camera matrix, the profile's
    /// hue/sat table, and the as-shot anchors — everything derived from the
    /// color metadata rather than from the mosaic.
    void pushColorProfile(const std::array<float, 9>& camToXyzStored,
                          const std::array<float, 4>& camMul);
    void pushStaticLocalParams();
    void pushStaticMaskParams();
    /// The grain plate. After `compile`, because it is an upload rather than a
    /// declaration.
    void uploadGrainPlate();

    // One per stage of `apply`, in the order `apply` calls them.
    void applyWhiteBalance(const Adjustments&, const ApplyContext&);
    void applyHighlights(const Adjustments&, const ApplyContext&);
    void applyGrade(const Adjustments&, const ApplyContext&);
    void applyLens(const Adjustments&, const ApplyContext&);
    void applyDenoise(const Adjustments&, const ApplyContext&);
    void applyDehaze(const Adjustments&, const ApplyContext&);
    void applyFusion(const Adjustments&, const ApplyContext&);
    void applyClarity(const Adjustments&, const ApplyContext&);
    void applyMaskComponents(const Adjustments&, const ApplyContext&);
    void applyGuide(const Adjustments&, const ApplyContext&);
    void applySpots(const Adjustments&, const ApplyContext&);
    void applyMaskRefine(const Adjustments&, const ApplyContext&);
    void applyTone(const Adjustments&, const ApplyContext&);
    void applySharpen(const Adjustments&, const ApplyContext&);
    void applyOutput(const Adjustments&, const ApplyContext&);
    void applyGeometry(const Adjustments&, const ApplyContext&);

    /// Everything both public constructors share; `linearSource_` decides
    /// what buildCaptureNodes makes of the graph's head.
    DevelopPipeline(gpu::Device& device, const std::string& shaderDir,
                    std::uint32_t width, std::uint32_t height, bool linearSource);

    /// The paint and matte state a reload must clear — a different photograph
    /// through the same graph shares nothing with the strokes laid on it.
    void clearPaintState();

    Pipeline      pipeline_;
    std::uint32_t width_ = 0, height_ = 0;
    /// The source is demosaiced linear RGB rather than a mosaic. Set once at
    /// construction; in this mode nLinearize_ names the linearSource node and
    /// the rcd/highlight/denoise ids stay -1.
    bool linearSource_ = false;
    int nLinearize_ = -1, nDirs_ = -1, nLpf_ = -1, nGreen_ = -1, nRgb_ = -1;
    int nSharpen_ = -1, nMatrix_ = -1, nLinear_ = -1, nDisplay_ = -1, nOrient_ = -1;
    int nGeometry_ = -1, nGrain_ = -1;
    int nHighlights_ = -1;
    /// The ceiling the last `apply` clipped to, cached where linearize computed
    /// it so `highlightStages()` cannot derive a second one.
    float lastWhiteClip_ = 0.0f;

    // ── Harmonic highlight fill (Rouf, Lau & Heidrich §3.2) ───────────────
    //
    // A pull-push pyramid over the fully clipped region, solved on a grid
    // `hlfill::kSolveScale` times coarser than the picture and lifted back by
    // the apply node. research/highlight-reconstruction.md.
    //
    // ⚠ **The subsampling is the whole cost story and it was measured first.**
    // ROADMAP costed this chain at full resolution: 13 levels, 194 MB at level
    // zero, ~516 MB for the two chains. rho is harmonic, so it has no detail to
    // lose, and `testHighlightFillGpu` sweeps the factor against a Gauss-Seidel
    // reference every run — 6.1% of rim span at full resolution against 6.9% at
    // a quarter, for a sixteenth of the memory. The node count barely moves
    // either way, because the level count is logarithmic in the frame.
    static constexpr int kHlMaxLevels = 20;
    int nHlMask_ = -1;
    int nHlFill_ = -1;
    /// `[0]` aliases `nHlMask_`: the mask node writes level zero of the pull
    /// chain directly, restriction and classification in one pass.
    int nHlPull_[kHlMaxLevels]{};
    /// `[hlLevels_ - 1]` aliases the pull chain's top — the coarsest level has
    /// nothing below it to blend in, so its push is its pull and the descent
    /// simply starts one level down.
    int nHlPush_[kHlMaxLevels]{};
    int hlLevels_ = 0;
    std::uint32_t hlW_[kHlMaxLevels]{}, hlH_[kHlMaxLevels]{};

    int nLens_ = -1;
    int nGrade_ = -1;
    int nHueSat_ = -1, auxHueSat_ = -1;
    float whiteLevel_ = 0.0f;
    float blackLevel_[3]{};
    static constexpr int kDenoiseScales = 4;
    int nAtrousBlur_[kDenoiseScales]{-1, -1, -1, -1};
    int nAtrousShrink_[kDenoiseScales]{-1, -1, -1, -1};
    raw::NoiseProfile noise_{};
    int nGuidePrep_ = -1, nGuideDown_ = -1, nGuideH1_ = -1, nGuideV1_ = -1;

    // ── Local Laplacian clarity ───────────────────────────────────────────
    //
    // The gamma stacks are packed four to an RGBA texture, so the number of
    // textures follows from the number of gamma levels. The collapse kernels
    // take exactly two of them by name, which is the one place that packing
    // is not generic — hence the assert rather than a silent miscompile.
    static constexpr int kLlfLevels = llf::kPyramidLevels;
    static constexpr int kLlfStacks = (llf::kGammaLevels + 3) / 4;
    static_assert(kLlfStacks == 4,
                  "llf_collapse.slang binds four packed stacks by name; "
                  "changing kGammaLevels needs that kernel widened to match.");

    // ── Dehaze (dark channel prior) ───────────────────────────────────────
    int nDehazeChan_ = -1, nDarkH_ = -1, nDarkV_ = -1, nPeak_ = -1;
    int nDehazeChanA_ = -1, nMinH_ = -1, nMinV_ = -1, nMaxH_ = -1, nMaxV_ = -1;
    int nHazePrep_ = -1, nHazeBlurH_ = -1, nHazeBlurV_ = -1, nHazeAb_ = -1;
    int nHazeBlurH2_ = -1, nHazeBlurV2_ = -1, nDehaze_ = -1;
    std::uint32_t peakW_ = 0, peakH_ = 0, hazeW_ = 0, hazeH_ = 0;

    /// A from Eq. (11). A reduction over the whole frame, so it is not a node;
    /// it depends only on what is upstream of dehaze, so it is cached and
    /// recomputed when white balance or the profile moves — never per tick.
    std::array<float, 3> airlight_{1.0f, 1.0f, 1.0f};
    bool airlightValid_ = false;
    bool dehazing_ = false;

    /// Whether the chain's size-derived parameter blocks have been pushed.
    ///
    /// ⚠ Everything in the dehaze chain except omega is a function of the
    /// frame's size, the paper's constants and A — so re-pushing it per tick
    /// dirties nine nodes, six of them full-resolution rank passes, for a value
    /// none of them read. `apps/bench`'s `dehaze drag` invariant is what keeps
    /// this true.
    bool hazeShapeValid_ = false;

    /// Reads the pooled candidates back and picks A. Costs one small download.
    void estimateAirlight();
    void pushAirlight();

    // ── Exposure fusion ───────────────────────────────────────────────────
    //
    // Runs on a quarter-resolution proxy: the method is a large-scale tonal
    // move and only a gain reaches the full-resolution picture, so nothing this
    // filter could have affected is lost to the subsampling.
    static constexpr int kFuseLevels = 6;
    static constexpr int kFuseScale  = 4;
    static constexpr int kFuseStacks = (sef::kMaxImages + 3) / 4;
    static_assert(kFuseStacks == 2,
                  "fuse_blend.slang binds two packed stacks by name.");

    int nFuseProxy_ = -1;
    int nFuseImage_[kFuseLevels][kFuseStacks]{};
    int nFuseWeight_[kFuseLevels][kFuseStacks]{};
    int nFuseOut_[kFuseLevels]{};
    int nFusion_ = -1;
    int nMaskBase_ = -1;

    /// The mask group: one node per component, chained, each folding its own
    /// coverage into what the ones before it produced. research/masking.md §6.
    ///
    /// The chain starts at `nMaskBase_`, which writes zero — the additive
    /// identity — so every component node has the same shape and a group of one
    /// exercises the same code path as a group of three. Unused components are
    /// disabled, which costs their texture and none of their time.
    int nSpotMeasure_ = -1, nSpotApply_ = -1;

    int nMaskComponent_[kMaxMaskComponents]{};

    /// A raster component's matte, one per slot — research/masking.md §5.
    ///
    /// ⚠ In **frame** coordinates: the whole uncropped, unturned sensor frame,
    /// which is the space `develop:linear` works in. That is a contract on the
    /// producer rather than something the kernel corrects for, and it is what
    /// lets a matte survive a crop and a quarter turn the same way a parametric
    /// component does.
    static constexpr std::uint32_t kMaxMatteEdge = 1024;
    int auxMatte_[kMaxMaskComponents]{-1, -1, -1, -1};
    /// One texel per brush dab. See the note in `apply`: the stroke outgrew
    /// what a constant block can carry, and a texture is a binding the pipeline
    /// already had.
    int auxDabs_[kMaxMaskComponents]{-1, -1, -1, -1};
    /// One axis-aligned box per run of `kDabBlock` dabs, so a pixel can skip a
    /// whole run with one test. research/brush-acceleration.md.
    int auxDabBounds_[kMaxMaskComponents]{-1, -1, -1, -1};
    std::uint32_t matteW_ = 0, matteH_ = 0;
    /// The live rectangle of each matte, or zero where none has been uploaded.
    std::uint32_t matteLive_[kMaxMaskComponents][2]{};

    /// ⚠ Set when a matte is uploaded, cleared when its component's params are
    /// pushed.
    ///
    /// `apply` skips a component whose `MaskComponentEdit` has not changed, and
    /// a matte is not in that struct — it is a raster uploaded out of band. So
    /// without this the upload lands in the aux texture and `matteSize` never
    /// reaches the shader, which reads it as zero and draws nothing: a matte
    /// that is present, correct, reported as covering 15% of the frame, and
    /// invisible. Found by looking at a render, not by reading the code.
    ///
    /// The brush has the same problem and answers it with `brushRevision`, a
    /// field the *caller* has to remember to bump. This is the engine
    /// remembering instead, which is the version a caller cannot get wrong.
    bool matteDirty_[kMaxMaskComponents]{};

    /// Guided feathering of the folded group — research/masking.md §4.
    /// One chain for the whole group, not one per component: what gets snapped
    /// to an edge is the coverage the photographer can see.
    /// ⚠ **One chain per component slot, not one per group.** A layer's
    /// coverage is the last component of its run, and which component that is
    /// changes at runtime — so every slot gets a chain and `layerMask` picks
    /// the one that matters. Seven nodes each, all disabled at strength zero,
    /// so an unrefined stack pays for their textures and none of their time.
    int nMaskGuidePrep_[kMaxMaskComponents]{-1, -1, -1, -1};
    int nMaskGuideH1_[kMaxMaskComponents]{-1, -1, -1, -1};
    int nMaskGuideV1_[kMaxMaskComponents]{-1, -1, -1, -1};
    int nMaskGuideAb_[kMaxMaskComponents]{-1, -1, -1, -1};
    int nMaskGuideH2_[kMaxMaskComponents]{-1, -1, -1, -1};
    int nMaskGuideV2_[kMaxMaskComponents]{-1, -1, -1, -1};
    int nMaskRefine_[kMaxMaskComponents]{-1, -1, -1, -1};
    std::uint32_t fuseW_[kFuseLevels]{}, fuseH_[kFuseLevels]{};

    /// The simulated-image plan. Derived from the frame's median, so it is a
    /// whole-frame reduction and not a node — same treatment as dehaze's
    /// atmospheric light: computed when stale, never on a slider tick.
    sef::Plan fusePlan_{};
    bool fusePlanValid_ = false;
    bool fusing_ = false;
    void estimateFusionPlan();
    void pushFusionPlan();

    int nLlfLuma_ = -1;
    /// The separable halving's intermediate: half width, full height.
    int nLlfRemapH_[kLlfStacks]{};
    int nLlfGauss_[kLlfLevels]{};                 // [0] aliases nLlfLuma_
    int nLlfPack_[kLlfLevels][kLlfStacks]{};      // levels 1..kLlfLevels-1
    int nLlfOut_[kLlfLevels]{};                   // [kLlfLevels-1] is the residual
    int nClarity_ = -1;
    std::uint32_t llfW_[kLlfLevels]{}, llfH_[kLlfLevels]{};

    /// He & Sun's subsampling ratio for the guided filter. Four is what they
    /// report as visually indistinguishable, and it takes the filter from
    /// ninety milliseconds to something you can drag.
    static constexpr int kGuideScale = 4;
    std::uint32_t guideW_ = 0, guideH_ = 0;
    int nGuideAb_ = -1, nGuideH2_ = -1, nGuideV2_ = -1;
    int exifQuarters_ = 0;
    int turns_ = 0;

    /// The perspective correction as one matrix, in texel coordinates of the
    /// rotated frame, and its inverse. Composed once per change in `apply` and
    /// read by three places — the geometry parameter block, `mask::toFrame`,
    /// and `displayedToFrame`. research/perspective.md.
    persp::Matrix3 perspective_{};
    persp::Matrix3 perspectiveInverse_{};

    /// Sixteen-bit tail. Matches the node declarations at construction.
    /// Narrow by default: the screen is the common case, and anything that
    /// wants sixteen bits has to ask.
    bool wideOutput_ = false;

    void pushDisplayParams(const Adjustments&);
    void pushGrainParams(const Adjustments&);
    /// Decides which of `develop:display` and `develop:grain` writes the eight
    /// bits, in one place. See the table on the definition.
    void retargetOutputChain(const Adjustments&);
    /// Whether the grain node runs at all. ⚠ Not a mirror of `grainAmount`: it
    /// is what `retargetOutputChain` keys on, and it moves the dither with it.
    bool graining_ = false;
    int  auxCube_ = -1;
    int  auxGrainPlate_ = -1;
    float gridStep_ = 1.0f;
    int  lutSize_ = 0;                       // 0 when none is loaded
    std::array<float, 3> lutMin_{0.0f, 0.0f, 0.0f};
    std::array<float, 3> lutMax_{1.0f, 1.0f, 1.0f};
    std::string lutTitle_;
    std::uint32_t outW_ = 0, outH_ = 0;
    std::uint32_t frameW_ = 0, frameH_ = 0;
    int auxCurveLut_ = -1;
    Adjustments  lastAdj_{};

    /// Each component's brush stroke, interleaved x, y. Lives here rather than
    /// in `Adjustments` because it is variable-length; see `setBrushStroke`.
    std::array<std::vector<float>, kMaxMaskComponents> brushDabs_;
    /// One per dab, parallel to `brushDabs_`. ⚠ Parallel rather than
    /// interleaved into the coordinate list on purpose: the stroke is written
    /// to the sidecar as a flat array of numbers, and re-interleaving it would
    /// read every stroke saved before erasing existed as garbage — silently,
    /// since a scrambled stroke is still a valid stroke.
    std::array<std::vector<float>, kMaxMaskComponents> brushErase_;
    /// The texels last uploaded for each component, so `apply` can ask how much
    /// of the stroke is unchanged. `research/brush-acceleration.md`; the
    /// predicate itself is `params::unchangedPrefix`.
    ///
    /// ⚠ Beside `brushDabs_` and **not** a view of it: this is the
    /// post-transform list, and the whole point is that the two disagree the
    /// moment the geometry moves.
    std::array<params::BrushPrefixState, kMaxMaskComponents> brushPrev_;
    std::array<BrushPrefixStat, kMaxMaskComponents> brushPrefix_{};

    // ── The incremental brush accumulator (decision #108) ──────────────────
    //
    // ⚠ **`brushPrev_` above answers a different question than this half
    // needs, and session one could not have known it.** It is updated when a
    // stroke is *uploaded*, which is right for a predicate nobody reads; the
    // accumulator is a texture, and a texture only changes when the graph is
    // *rendered*. Those are not the same moment: the full graph is given
    // parameters on every pointer event and rendered once when the gesture
    // ends. So `brushPrev_` is now advanced from what the pipeline reports
    // having executed, and `brushPending_` holds the claim in between.

    /// Which component owns the accumulator, or -1. **One at a time**, because
    /// there is one texture: four of them would be ~388 MB at 24 Mpx and a
    /// photograph with four brush components would pay it whether or not any of
    /// them was being painted. The cost of the choice is one full re-lay when
    /// the photographer moves to a different component — one event out of a
    /// gesture's hundreds. research/brush-acceleration.md.
    int accumOwner_ = -1;
    /// The accumulator itself. Registered at 1x1 and grown on the first dab,
    /// so a photograph that is never painted on pays two bytes.
    int auxBrushAccum_ = -1;

    /// A stroke the host has *pushed* but not yet seen rendered.
    struct BrushClaim {
        params::BrushPrefixState state{};
        bool valid = false;
    };
    std::array<BrushClaim, kMaxMaskComponents> brushPending_{};
    /// A faithful copy of the parameters currently sitting in each mask node,
    /// so `render` can patch `firstDab` back to zero without rebuilding the
    /// whole block. Kept in step by `apply`, which is the only writer.
    std::array<params::MaskComponent, kMaxMaskComponents> maskParams_{};

    /// Grows the accumulator to the frame's size the first time it is wanted.
    void ensureBrushAccum();
    /// Gives it back when no component is a brush any more. ~97 MB at 24 Mpx is
    /// not something to hold for a row the photographer deleted.
    void releaseBrushAccum();
    /// ⚠ Immediately before a render: refuse the fast path on any node that is
    /// about to run with parameters this `apply` did not push. Compositing the
    /// same dabs twice is the failure this exists to make impossible.
    void reconcileBrushAccum();
    /// Immediately after a render: advance the claim only for nodes the
    /// pipeline says actually ran.
    void commitBrushAccum();
    /// One render, with the two above wrapped around it.
    double renderOnce();
    bool         primed_ = false;
    WhiteBalance         asShot_{};
    std::array<float, 3> asShotMul_{1.0f, 1.0f, 1.0f};
    std::array<float, 3> asShotRef_{1.0f, 1.0f, 1.0f};
    float        xyzToCam_[9]{};
    params::Linearize linBase_{};
    std::uint32_t filters_ = 0;
    void applyImageParams(const raw::BayerImage&);
    void applyImageParams(const raw::LinearImage&);

    /// The brightest neutral this frame can describe, once white balance has
    /// been applied — the lowest of the three per-channel saturation levels.
    /// Linearize clips to it so a blown highlight comes out white instead of
    /// carrying the white-balance gains as a color cast.
    [[nodiscard]] float whiteClipFor(const float multipliers[3]) const noexcept;

public:
    /// A grading wheel's puck position, as a zero-sum RGB offset.
    ///
    /// Public and static because it is pure arithmetic with one property that
    /// has to hold — the three components sum to zero — and that property is
    /// the whole reason the wheel is a color control rather than a brightness
    /// one. Testable without a device.
    static void gradeOffsets(float x, float y, float out[3]) noexcept;

    /// Where the **composition** sits inside the frame, as a circle.
    ///
    /// The creative vignette runs upstream of `geometry`, which is the node
    /// that crops, straightens and turns — so the kernel sees the whole frame
    /// and has to be told where the picture the photographer is actually
    /// composing lives inside it. This is that map, evaluated for exactly two
    /// quantities:
    ///
    ///   * `centerX/centerY` — the crop rectangle's center, normalized in the
    ///     *unrotated* frame, which is the grid every upstream node runs on.
    ///   * `radius` — the crop rectangle's half-diagonal, in units of the
    ///     frame's height, so a kernel can compare it against an isotropic
    ///     distance without knowing the frame's pixel count.
    ///
    /// ⚠ Only a circle, and that is what makes this cheap and safe. Rotation —
    /// the straighten and the quarter turns — moves the center and cannot
    /// touch the radius, because a rotation preserves length. A second copy of
    /// `geometry.slang`'s full inverse would be the mistake decision #70 is
    /// about; two numbers that a rotation acts on trivially are not.
    ///
    /// Public and static so it can be checked against hand arithmetic with no
    /// GPU, the same reason `gradeOffsets` is.
    struct Circle { float centerX, centerY, radius; };
    [[nodiscard]] static Circle compositionCircle(const Adjustments& adj,
                                                  int exifQuarters,
                                                  std::uint32_t width,
                                                  std::uint32_t height) noexcept;

private:
};

}  // namespace orion::pipe
