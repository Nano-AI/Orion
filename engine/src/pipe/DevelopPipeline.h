/*  The standard develop graph.
 *
 *  Builds and owns the seven-node pipeline that turns a decoded mosaic into
 *  display-ready pixels, and exposes the adjustments on top of it. Both the
 *  app and the bench tool drive this, so what you measure is what you see.
 */

#pragma once

#include "gpu/Resources.h"
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

/// How many components one mask group holds.
///
/// Each is one node and one full-resolution R16Float intermediate — 48 MB at
/// 24 MP — so this is a memory number, not a conceptual limit. Four covers the
/// shapes photographers actually build (a sky gradient minus the trees, a
/// radial on the face plus a stroke on the hands) and costs 192 MB of
/// intermediates. Raising it is this constant plus the matching one in
/// `orion.h`, and the cost is linear.
inline constexpr int kMaxMaskComponents = 4;

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
};

/// One component of the mask group, in the form a person manipulates — a center
/// and an angle rather than the two endpoints the linear gradient's maths wants.
/// `DevelopPipeline::apply` derives the shader's form from this.
///
/// research/masking.md §6.
struct MaskComponentEdit {
    int   kind = 0;                    // 0 off, 1 linear, 2 radial, 3 brush, 4 matte, 5 range, 6 color
    /// params::MaskCompose. The group folds from zero, the additive identity,
    /// so the first component's op has no effect when it is Add and zeroes the
    /// group when it is Subtract or Intersect — subtracting from nothing is
    /// nothing. The interface should not offer an op on the first row.
    int   compose = 0;
    bool  invert = false;
    /// Hidden by the eye button. The component keeps every setting and simply
    /// stops contributing — which costs nothing, because a disabled node
    /// resolves to its first input and this node's first input *is* the fold so
    /// far. Skipping a component is therefore exactly not running it.
    bool  hidden = false;
    /// Begins a new layer. A layer is a run of consecutive components with its
    /// own coverage and its own local adjustments; component 0 always starts
    /// one. research/masking.md §6 and ROADMAP's per-layer decomposition.
    bool  startsLayer = false;

    float center[2]{0.5f, 0.5f};
    float angle = 0.0f;                // radians, both gradient kinds
    float length = 0.5f;               // linear: zero-to-full distance
    float radius[2]{0.3f, 0.3f};       // radial semi-axes
    float feather = 0.5f;
    float roundness = 2.0f;

    /// Luminance range (kind 5), in stops of log2 Rec.2020 luminance on the
    /// reference image. research/masking.md §4b. The defaults are a band around
    /// middle gray, which is where a photographer reaching for one starts.
    float rangeLo = -2.0f;
    float rangeHi = 2.0f;
    float rangeSoft = 1.0f;

    /// Color range (kind 6): the picked shade in scene-linear Rec.2020 RGB,
    /// plus a Euclidean tolerance in Oklab chromaticity (a/L, b/L) and how far
    /// its edge ramps. research/masking.md §4c.
    ///
    /// The default target is a mid gray, which selects every neutral in the
    /// frame at any brightness — a visible, explicable starting state rather
    /// than an empty mask that looks like the feature not working. The
    /// tolerance's default is a little under the closest pair of ordinary
    /// photographic colors the research measured (tarmac and skin, 0.126).
    float color[3]{0.18f, 0.18f, 0.18f};
    float colorTol = 0.10f;
    float colorSoft = 0.05f;

    /// The brush, when `kind` is 3. The dab centers are *not* here: they are a
    /// variable-length list and this struct is compared on every slider tick, so
    /// carrying them would make every tick walk every stroke.
    /// `setBrushStroke` owns them per component; this is only the revision.
    float brushRadius = 0.08f;         // normalized
    float brushFlow = 0.5f;            // 0..1 per dab
    float brushHardness = 0.5f;        // 0 soft, 1 hard-edged
    unsigned brushRevision = 0;        // bumped whenever the stroke changes

    /// Defaulted rather than a hand-written field list, deliberately. A field
    /// added later joins the comparison automatically; a hand-written list
    /// silently omits it, which is exactly how `lutStrength` once shipped a
    /// slider that did nothing at all.
    bool operator==(const MaskComponentEdit&) const = default;
};

/// One spot: a disc taken from elsewhere in the frame. research/spot-removal.md.
///
/// ⚠ **Both centers are in FRAME coordinates, and a spot is the one thing here
/// that wants them.** A mask is placed *against* a subject and stays where the
/// photographer put it on screen; dust is *on the sensor* and is part of the
/// picture, so a spot has to follow the subject through a later crop or quarter
/// turn. Those are opposite behaviors and they need opposite storage.
///
/// The click is therefore converted once, when the spot is placed, by
/// `displayedToFrame` — the same `mask::toFrame` a mask's center goes through,
/// applied at placement rather than at render. Converting on every render would
/// give the mask's behavior, and would also need the geometry in the
/// staleness comparison, which is a trap this file has already fallen into
/// twice.
struct SpotEdit {
    float destX = 0.5f, destY = 0.5f;
    float srcX  = 0.5f, srcY  = 0.5f;
    /// In normalized *x*, converted against the frame's width so a spot is a
    /// disc rather than an ellipse on a non-square frame.
    float radius = 0.02f;
    float feather = 0.5f;
    /// Heal takes the destination's tone, clone does not. The two operations
    /// differ by exactly this.
    bool  heal = true;

    bool operator==(const SpotEdit&) const = default;
};

struct Adjustments {
    // White balance. Defaults are replaced with the camera's own estimate when
    // a file is opened, so "as shot" is where every image starts.
    WhiteBalance wb{};

    // Scene-linear tone, in order of the pipeline.
    float exposureEv = 0.0f;
    float highlights = 0.0f;   // -1..1, negative recovers
    float shadows    = 0.0f;
    float whites     = 0.0f;
    float blacks     = 0.0f;

    float vibrance   = 0.0f;   // -1..1
    float saturation = 0.0f;   // -1..1, 0 is untouched

    // Color mixer, eight bands: red, orange, yellow, green, aqua, blue,
    // purple, magenta. Each -1..1.
    std::array<float, 8> hueShift{};
    std::array<float, 8> satShift{};
    std::array<float, 8> lumShift{};

    /// Extra quarter turns clockwise on top of the camera's own orientation.
    int rotateQuarters = 0;

    /// Fine rotation in degrees, applied after the quarter turns. Positive
    /// rotates the image clockwise, which is what a "straighten" control means.
    float straightenDeg = 0.0f;

    /// Perspective correction, each -1..1, all three folded into the geometry
    /// node's single sampling pass. research/perspective.md.
    ///
    /// Zero on all three is not merely a small correction: it takes the branch
    /// the kernel took before this existed, so the render is bit-identical to
    /// one with no perspective in the build at all.
    float perspectiveVertical   = 0.0f;
    float perspectiveHorizontal = 0.0f;
    float perspectiveAspect     = 0.0f;

    /// Crop rectangle in normalized post-rotation coordinates. The full frame
    /// is origin (0,0) size (1,1).
    float cropX = 0.0f, cropY = 0.0f;
    float cropW = 1.0f, cropH = 1.0f;

    /// While the crop tool is open, render the whole straightened frame and let
    /// the UI draw the crop rectangle over it. That is how Photoshop and
    /// Lightroom behave: you see what you are cutting away, and straightening
    /// rotates the picture under a stationary rectangle rather than zooming in.
    bool cropPreview = false;

    /// The preview canvas, in the same coordinates as the crop rectangle. It
    /// has to cover the frame's rotated bounding box, which depends on both
    /// the angle and the frame's aspect — a fixed factor could not, and at
    /// anything past about 17 degrees on a 3:2 frame it clipped the corners.
    ///
    /// Supplied by the UI rather than derived here: the crop overlay has to
    /// land on the same rectangle, and two derivations is exactly how the
    /// handles and the pixels drifted apart before.
    float previewX = 0.0f, previewY = 0.0f;
    float previewSize = 1.0f;

    /// Lens corrections. Manual for now: the lensfun database would fill these
    /// in from the lens the EXIF names, and the maths is the same either way.
    float lensDistortion = 0.0f;   // -1..1, poly3 k1
    float lensVignette   = 0.0f;   // -1..1, p_a
    float lensCaRed      = 0.0f;   // -1..1
    float lensCaBlue     = 0.0f;   // -1..1

    /// A measured profile from the lens database, in place of the sliders.
    /// `lensPoly` is ptlens a, b, c; `lensVignettePa` is p_a, p_b, p_c. Both
    /// are physical coefficients at this frame's focal length and aperture,
    /// not normalized control positions. See pipe/LensDatabase.h.
    bool  lensProfile = false;
    float lensPoly[3]{};
    float lensVignettePa[3]{};

    /// Highlight reconstruction, 0..1. Off by default — see the note on
    /// Engine.highlightRecovery in the app.
    float highlightRecovery = 0.0f;

    /// Three-way color grading, as ASC CDL per tonal zone. Each entry is a
    /// wheel's puck position (x, y) in the unit disc plus that zone's slope.
    /// research/color-grading.md.
    float gradeShadow[3]{};      // x, y, luminance
    float gradeMidtone[3]{};
    float gradeHighlight[3]{};

    /// Balance, -1..+1 — decision #101. The zones the three wheels act on are
    /// Gaussians fixed at -2.5 / 0 / +2.5 EV; this slides all three centres
    /// together, so a photographer decides how much of the picture counts as
    /// shadow. Positive is toward the highlights.
    ///
    /// ⚠ **Not a reason to run the node.** With the wheels centred it changes
    /// weights that multiply zero, so it must never switch grading on by
    /// itself — decisions #82 and #92 are both that failure.
    float gradeBalance = 0.0f;

    /// The **creative** vignette — research/vignette.md, decision #103.
    ///
    /// ⚠ Nothing to do with `lensVignette` above, which *removes* a falloff a
    /// lens measured. This one puts one in, after the grade, on purpose. The
    /// two never read each other and a photograph can carry both.
    ///
    /// `vignetteAmount` is the exposure change at the corner of the
    /// **composition** in stops — negative darkens, which is the usual
    /// direction — and zero costs the shader two instructions it skips.
    ///
    /// `vignetteFieldAngle` is the half-diagonal field angle in degrees of the
    /// lens whose natural cos^4 falloff is being imitated: wide is a broad
    /// vignette that reaches well into the frame, narrow is one that stays in
    /// the extreme corners.
    float vignetteAmount     = 0.0f;    // stops at the corner, -3..3
    float vignetteFieldAngle = 45.0f;   // degrees, 10..70

    /// Profiled wavelet denoise. Strengths are multiples of the measured
    /// noise level, so 1.0 means "shrink coefficients smaller than one sigma"
    /// rather than an arbitrary amount — which is what makes the same setting
    /// behave the same way on a clean frame and a very noisy one.
    float denoiseLuma   = 0.0f;   // 0..4, 0 disables the whole chain
    float denoiseColor = 0.0f;   // 0..4, applied on top of luma

    /// How much of the creative LUT to apply, 0..1. The LUT itself is not an
    /// adjustment — it is a file, set through `setCreativeLut`.
    float lutStrength = 1.0f;

    /// Film grain. `research/film-grain.md`, decision #81.
    ///
    /// `grainAmount` is the peak standard deviation in display units — peak
    /// because the Boolean model's variance law puts the noise in the midtones
    /// and none of it at either end. Zero takes the shader's early exit, which
    /// is what makes the node free when it is off.
    ///
    /// `grainSize` is the grain radius in *frame* pixels, so it is keyed to the
    /// negative: a crop enlarges the grain the way enlarging more of a negative
    /// does, rather than resampling it with the output.
    float grainAmount = 0.0f;   // 0..~0.06
    float grainSize   = 1.5f;   // frame pixels

    // ── Local adjustments (M4) ────────────────────────────────────────────
    //
    // A mask is its parameters, not an image: normalized coordinates in, alpha
    // out, so it survives a resize and an export matches the preview it was
    // made on. research/masking.md.
    /// The mask group: a list of components folded left in listed order into one
    /// coverage, and **the adjustment below is applied once** through it.
    /// research/masking.md §6 — never apply the same +1 stop twice because two
    /// masks overlap.
    ///
    /// `maskCount` is how many of these are live; the rest are ignored and their
    /// nodes disabled, so an unused component costs a texture and no time.
    /// Zero means no mask at all, which the shader is told through `maskActive`
    /// rather than by writing full coverage — so the fold can start from zero.
    ///
    /// Several *groups*, each with its own adjustment, is a later story: it is
    /// more nodes of the same shape, and the graph already expresses it.
    std::array<MaskComponentEdit, kMaxMaskComponents> maskComponents{};
    int   maskCount = 0;

    /// Dust and blemishes. research/spot-removal.md. Applied between the lens
    /// correction and sharpening, so a healed patch is sharpened along with its
    /// surroundings rather than pasted in already sharp.
    std::array<SpotEdit, 64> spots{};
    int   spotCount = 0;

    /// Guided feathering of the folded group, 0..1 — research/masking.md §4.
    ///
    /// Pulls the coverage boundary onto whatever edge in the photograph lies
    /// near it, and leaves it alone where there is no edge to snap to. Zero is
    /// the identity and disables all seven of its nodes, so a photograph that
    /// does not ask for it pays their textures and none of their time.
    ///
    /// A property of the *group*, not of a component: the boundary a
    /// photographer wants snapped is the one they can see, which is the fold.
    float maskRefine = 0.0f;

    /// Paint the mask's coverage over the picture, so it can be placed by eye.
    /// A viewing aid — `Engine` forces it off around an export.
    bool  maskOverlay = false;

    /// What the mask does. Scales the *parameter*, so alpha 0.5 with +1 EV is
    /// exactly 2^0.5 — not a blend between two rendered frames.
    /// One layer's local adjustments. Pointwise only — research/masking.md §2b
    /// says what cannot be here and why.
    struct LocalEdit {
        float exposureEv = 0.0f;
        float contrast = 0.0f;
        float saturation = 0.0f;
        /// A color cast where the mask covers, **not** a white balance.
        float warmth = 0.0f;
        float tint = 0.0f;
        bool operator==(const LocalEdit&) const = default;
    };
    /// ⚠ One per layer, so the subject can be graded one way and the sky
    /// another. Index 0 is what a single-group photograph used to carry, which
    /// is what makes the sidecar migration a rename rather than a conversion.
    std::array<LocalEdit, kMaxMaskComponents> layers{};

    /// Single-image exposure fusion, 0..1 — shadow lift that keeps local
    /// contrast. The value is a power applied to the emitted gain, so zero is
    /// bit-exactly the identity. research/exposure-fusion.md.
    float fusion = 0.0f;

    /// Dehaze, 0..1. Maps onto the paper's own omega, so zero is exactly the
    /// identity and one is the value He, Sun & Tang fixed for every result in
    /// their paper. research/dehaze.md.
    float dehaze = 0.0f;

    /// Local Laplacian clarity, -1..1. Negative smooths detail, positive
    /// increases its contrast; the slider is the published alpha exponent, and
    /// its endpoints land on the paper's own illustrated values. Thirty-two
    /// nodes hang off this one float, so zero switches the whole chain off
    /// rather than running it at no strength. research/local-laplacian.md.
    float clarity = 0.0f;

    // Capture sharpening. Sits just after the demosaic.
    float sharpenAmount  = 0.0f;   // 0..2
    float sharpenRadius  = 1.0f;   // pixels
    float sharpenMasking = 0.0f;   // 0..1, higher protects flat areas

    // Display transform and look.
    float contrast   = 1.0f;
    ToneCurveSpec curve{};
};

class DevelopPipeline {
public:
    DevelopPipeline(gpu::Device& device, const std::string& shaderDir,
                    const raw::BayerImage& image);

    /// True when this pipeline can be reused for `image` — same dimensions and
    /// same CFA layout. Rebuilding costs sixteen shader compiles and about
    /// 2.5 GiB of texture allocation, so reusing is the difference between
    /// switching photos in milliseconds and in seconds.
    [[nodiscard]] bool canReload(const raw::BayerImage& image) const noexcept;

    /// Swaps in a new image without touching the compiled graph.
    void reload(const raw::BayerImage& image);

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

    [[nodiscard]] std::uint32_t width()  const noexcept { return width_; }
    [[nodiscard]] std::uint32_t height() const noexcept { return height_; }

private:
    Pipeline      pipeline_;
    std::uint32_t width_ = 0, height_ = 0;
    int nLinearize_ = -1, nDirs_ = -1, nLpf_ = -1, nGreen_ = -1, nRgb_ = -1;
    int nSharpen_ = -1, nMatrix_ = -1, nLinear_ = -1, nDisplay_ = -1, nOrient_ = -1;
    int nGeometry_ = -1, nGrain_ = -1;
    int nHighlights_ = -1;

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
    bool         primed_ = false;
    WhiteBalance         asShot_{};
    std::array<float, 3> asShotMul_{1.0f, 1.0f, 1.0f};
    std::array<float, 3> asShotRef_{1.0f, 1.0f, 1.0f};
    float        xyzToCam_[9]{};
    params::Linearize linBase_{};
    std::uint32_t filters_ = 0;
    void applyImageParams(const raw::BayerImage&);

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
