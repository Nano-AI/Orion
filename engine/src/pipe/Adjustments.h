/*  The develop adjustments — the plain data a photograph's edit is.
 *
 *  Split out of `DevelopPipeline.h` so that neither file is a thousand lines
 *  (decision #113). Nothing here knows about the graph: it is the struct the
 *  sidecar round-trips, the bench sweeps and the POD facade carries, and it
 *  compares field by field on every slider tick.
 */

#pragma once

#include "pipe/ToneCurve.h"
#include "pipe/WhiteBalance.h"

#include <array>

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

}  // namespace orion::pipe
