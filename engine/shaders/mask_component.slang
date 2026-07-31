// One mask component: its own coverage, folded into the group's running alpha.
//
// research/masking.md §1 (the primitives) and §6 (combining them). A component
// is a linear gradient, a radial gradient or a brush stroke, plus how it
// combines with the components listed before it.
//
// **One kernel for all three primitives, and one node per component.** They
// differ only in how a position becomes a coverage; the falloff, the inversion,
// the compose and the output are shared. The previous shape was a gradient node
// and a brush node chained per component, which existed only because the graph
// cannot swap a node's kernel per render — so the brush node passed through for
// gradients and the gradient node passed through for brushes. Two nodes, one of
// them always idle, and a contortion to explain. A branch inside one kernel is
// the thing that was actually wanted. planning/DECISIONS.md #62.
//
// A mask is its parameters, not an image: normalized coordinates in, R16Float
// alpha out, so a mask survives a resize and an export matches the preview it
// was made on. R16 rather than R8 because a low-flow brush is *meant* to build
// up from many overlapping dabs and eight bits bands visibly once a few dozen
// stack — the same class of error as the 8-bit resize that quietly cost the
// export its depth.

#include "ops/mask_ops.slang"

struct MaskComponentParams
{
    uint2  size;
    int    kind;        // 0 off, 1 linear, 2 radial, 3 brush, 4 matte, 5 range, 6 colour
    int    invert;      // inverts *this component's* coverage, before the fold

    int    compose;     // kMaskAdd | kMaskSubtract | kMaskIntersect
    int    count;       // brush dabs this pass
    /// Continues the stroke in `src` instead of starting a new one, and skips
    /// the fold. A stroke longer than `count` dabs is several components nose to
    /// tail; without this the continuation would `max` against its own earlier
    /// half, which is not source-over and would lose the buildup a low-flow
    /// brush is for. Nothing sets it yet — see the truncation warning in
    /// DevelopPipeline — but it is the shape the fix takes.
    int    accumulate;

    float  nibPx;       // brush nib radius, in frame pixels — one per stroke
    float  flow;        // 0..1, how much one dab lays down
    float  hardness;    // 0 soft, 1 hard-edged
    float  feather;     // radial, 0..1
    float  roundness;   // radial superellipse exponent; 2 is an ellipse
    float  angle;       // radial rotation, radians
    float  _pad;

    float2 zero;        // linear: where the effect is zero
    float2 full;        // linear: where it is full
    float2 centre;      // radial
    float2 semi;        // radial semi-axes, normalized

    /// Luminance range (kind 5), in **stops** — log2 of Rec.2020 luminance on
    /// the reference image. `rangeLo` and `rangeHi` are the band's edges and
    /// `rangeSoft` is how many stops each edge takes to ramp. The two edges are
    /// independent, so pushing one past the frame's range turns the band into a
    /// high-pass or a low-pass. research/masking.md §4b.
    float  rangeLo;
    float  rangeHi;
    float  rangeSoft;
    /// Added to the measured stops so the band's numbers mean what the
    /// photographer is *looking at*. See the kind-5 branch.
    float  rangeBias;

    /// How many dabs sit on one row of the dab texture. See the brush branch:
    /// the stroke is a 2D texture read as a 1D list, because a stroke long
    /// enough to matter does not fit in a constant block at all.
    int    dabStride;
    int    _pad3[3];

    /// Matte (kind 4): the live rectangle of the aux texture. A matte is
    /// uploaded at whatever size its producer had, into a texture allocated for
    /// the largest one, so the kernel has to be told how much of it is real.
    /// Zero disables the branch rather than sampling an empty texture.
    uint2  matteSize;

    /// Colour range (kind 6): the picked shade, as scene-linear Rec.2020 RGB.
    ///
    /// ⚠ Stored as RGB and converted *here*, not converted once on the host.
    /// A host-side conversion would be a second implementation of the Oklab
    /// transform, and two copies of a colour transform is a thing this codebase
    /// has been bitten by twice. Twenty flops against a 24 MP pass buys the
    /// guarantee that the target and the pixel cannot disagree.
    /// research/masking.md §4c.
    float  colourR;
    float  colourG;
    float  colourB;
    /// Euclidean radius in (a/L, b/L). Ordinary photographic colours sit
    /// 0.12–0.60 apart, so the interesting travel is the first third.
    float  colourTol;
    /// How wide the edge ramps, in the same units.
    float  colourSoft;
    /// ⚠ Non-zero when this component **begins a layer**. The fold then starts
    /// from nothing rather than from `src`, so a stack of components becomes
    /// several independent coverages rather than one. A layer is a run of
    /// consecutive components, which is how the row list already reads.
    int    startsLayer;
};

/// One dab's coverage at normalized distance `d` from its centre, where 1 is
/// the rim.
///
/// `hardness` moves where the falloff starts, exactly as the radial gradient's
/// feather does — hardness 0 ramps from the centre, hardness 1 is a disc with
/// only the last sliver softened. The sliver is deliberate: a truly hard circle
/// aliases into a visible staircase at any radius, and the mask is multiplied
/// into a parameter where that staircase becomes a banded edit.
float dabCoverage(float d, float hardness)
{
    const float h = clamp(hardness, 0.0f, 0.98f);
    return 1.0f - maskFalloff((d - h) / max(1.0f - h, 1e-3f));
}

/// Chromaticity in Oklab, as (a/L, b/L). research/masking.md §4c.
///
/// Björn Ottosson, *"A perceptual color space for image processing"* (2020),
/// normatively specified by the W3C in CSS Color Module Level 4 as `oklab()`.
///
/// ⚠ **Why the ratio, and not a and b themselves.** Oklab's nonlinearity is a
/// pure cube root with no linear toe and no division by a white point, so
/// scaling the input by k scales L, a and b uniformly by k^(1/3) — which makes
/// a/L and b/L *exactly* invariant under exposure. That identity is what lets
/// this metric work on a scene-linear input that is unbounded above 1.0, where
/// there is no honest reference white to normalise against. It also means a
/// shade in shadow and the same shade in sun are one colour, and that every
/// neutral collapses to the origin.
///
/// Lightness is deliberately not in the distance: kind 5 is that axis already,
/// and §6's fold composes the two with intersect.
///
/// The matrix is Ottosson's XYZ->LMS composed with Rec.2020->XYZ, which is the
/// inverse of the engine's own `kXyzToRec2020` and agrees with the published
/// BT.2020 matrix to 3e-8. One 3x3, three cube roots, one more 3x3.
float2 oklabChroma(float3 rgb)
{
    // ⚠ Negatives are possible: the working space is wider than the camera's
    // gamut and a saturated stimulus lands outside it. cbrt is odd, so this is
    // clamped rather than signed — a negative primary is not a colour anybody
    // picked, and letting it through puts the chromaticity somewhere no real
    // pixel is.
    const float3 c = max(rgb, 0.0f);

    const float l = 0.6166884417f * c.r + 0.3601590705f * c.g + 0.0230433072f * c.b;
    const float m = 0.2651401962f * c.r + 0.6358564847f * c.g + 0.0990302685f * c.b;
    const float s = 0.1001506451f * c.r + 0.2040043234f * c.g + 0.6963246874f * c.b;

    const float l_ = pow(l, 1.0f / 3.0f);
    const float m_ = pow(m, 1.0f / 3.0f);
    const float s_ = pow(s, 1.0f / 3.0f);

    const float L = 0.2104542553f * l_ + 0.7936177850f * m_ - 0.0040720468f * s_;
    const float a = 1.9779984951f * l_ - 2.4285922050f * m_ + 0.4505937099f * s_;
    const float b = 0.0259040371f * l_ + 0.7827717662f * m_ - 0.8086757660f * s_;

    // ⚠ A floor on L, or the ratio explodes on near-black pixels and the mask
    // fills with sensor noise. For a neutral, L is exactly Y^(1/3), so L = 0.1
    // is a linear luminance of 1e-3 — about seven and a half stops below middle
    // grey, which on a fourteen-stop raw is the floor of the noise rather than
    // a colour. research/UNSOURCED.md.
    return float2(a, b) / max(L, 0.1f);
}

/// Rec.2020 luminance — ITU-R BT.2020-2 (10/2015), Table 3.
///
/// The same coefficients `guide_prep.slang` and `develop_display.slang` use.
/// A second definition of brightness in one program is a bug waiting for the
/// day the two disagree.
float luminance2020(float3 c)
{
    return dot(c, float3(0.2627f, 0.6780f, 0.0593f));
}

/// A matte sampled bilinearly at a normalized frame position.
///
/// ⚠ **The matte is in frame coordinates**, the same space this kernel works
/// in — uncropped, unturned, the whole sensor frame. That is a contract on
/// whoever *produces* it, not something this kernel corrects for, and it is
/// deliberate: a raster in displayed coordinates would need the crop, the
/// straighten and the quarter turns undone per pixel here, and it would carry
/// no data at all outside the crop rectangle. Keeping the kernel ignorant of
/// geometry means a matte survives a crop and a rotation for the same reason
/// the parametric components do.
///
/// Bilinear by hand because the mattes worth having are coarse — a segmentation
/// network runs at a fixed internal resolution well below the frame's — and a
/// nearest fetch would staircase the boundary before the guided refinement ever
/// saw it.
float sampleMatte(Texture2D<float> matte, float2 q, uint2 msize)
{
    if (msize.x == 0u || msize.y == 0u) return 0.0f;

    const float2 maxCoord = float2(msize) - 1.0f;
    const float2 c = clamp(q * float2(msize) - 0.5f, float2(0.0f, 0.0f), maxCoord);

    const int2 i0 = int2(floor(c));
    const int2 i1 = min(i0 + int2(1, 1), int2(maxCoord));
    const float2 f = c - float2(i0);

    const float top = lerp(matte[int2(i0.x, i0.y)], matte[int2(i1.x, i0.y)], f.x);
    const float bot = lerp(matte[int2(i0.x, i1.y)], matte[int2(i1.x, i1.y)], f.x);
    return saturate(lerp(top, bot, f.y));
}

[shader("compute")]
[numthreads(16, 16, 1)]
void maskComponent(uint3 tid : SV_DispatchThreadID,
                   uniform Texture2D<float>   src,   // coverage so far, 0..1
                   uniform Texture2D<float4>  reference,  // kind 5, see below
                   uniform Texture2D<float>   matte, // kind 4, frame coordinates
                   uniform Texture2D<float4>  dabs,  // kind 3: xy centre, z erases when set
                   uniform RWTexture2D<float> dst,
                   uniform ConstantBuffer<MaskComponentParams> p)
{
    if (tid.x >= p.size.x || tid.y >= p.size.y) return;

    // Pixel centres, in the normalized coordinates the mask is stored in — so a
    // mask survives a resize, and an export matches the preview it was made on.
    const float2 q = (float2(tid.xy) + 0.5f) / float2(p.size);

    // This component's own coverage. Zero for kind 0, so a component switched
    // off in the interface contributes nothing under any compose op rather than
    // silently covering the frame.
    float own = 0.0f;

    if (p.kind == 1)
    {
        // Two points carry position and angle together, so there is no separate
        // angle field to keep consistent with them.
        const float2 d = p.full - p.zero;
        const float denom = dot(d, d);
        const float t = (denom > 1e-9f) ? dot(q - p.zero, d) / denom : 1.0f;
        own = maskFalloff(t);
    }
    else if (p.kind == 2)
    {
        const float c = cos(p.angle), s = sin(p.angle);
        const float2 e = q - p.centre;
        const float u = ( c * e.x + s * e.y) / max(p.semi.x, 1e-6f);
        const float v = (-s * e.x + c * e.y) / max(p.semi.y, 1e-6f);

        // n = 2 is an ellipse; raising it walks toward a rounded rectangle.
        const float n = max(p.roundness, 0.1f);
        const float r = pow(pow(abs(u), n) + pow(abs(v), n), 1.0f / n);

        // r = 1 is the boundary. Feather is how far inside it the ramp starts.
        const float f = clamp(p.feather, 0.0f, 0.999f);
        own = 1.0f - maskFalloff((r - (1.0f - f)) / max(f, 1e-3f));
    }
    else if (p.kind == 3)
    {
        // A continuation pass builds on the coverage it was handed; a fresh
        // stroke starts from nothing.
        if (p.accumulate != 0) own = src[tid.xy];

        // In pixels, not in normalized coordinates. One normalized unit of x and
        // one of y are different numbers of pixels on any frame that is not
        // square, so measuring the dab in normalized space drew an ellipse — 3:2
        // on a 3:2 photograph — and made the Size slider stretch the nib instead
        // of growing it. A brush has to be round under the cursor.
        const float r = max(p.nibPx, 1e-6f);
        const float flow = saturate(p.flow);
        const float2 toPx = float2(p.size);

        // ⚠ The stroke lives in a *texture*, not in this parameter block, and
        // that is the whole reason a long one is no longer truncated.
        //
        // Metal's `setBytes` caps a constant block at four kilobytes. The block
        // was already two, so the list could hold 256 dabs — about one stroke
        // across a frame — and everything past that was dropped with a warning
        // on stderr. The note in DevelopPipeline said the fix was more nodes
        // chained by the `accumulate` flag; it is not, and this is the cheaper
        // answer: an auxiliary texture is a binding the pipeline already
        // supports for the matte, it costs 128 KB for four components, and it
        // needs no chain, no spare component and no second code path.
        const int stride = max(p.dabStride, 1);
        for (int i = 0; i < p.count; ++i)
        {
            const float4 dab = dabs[int2(i % stride, i / stride)];
            const float2 centre = dab.xy;
            const float2 e = (q - centre) * toPx;

            // Reject on the square first. A dab covers a disc, so most pixels
            // miss most dabs, and this turns the common case into two compares
            // instead of a square root.
            if (abs(e.x) > r || abs(e.y) > r) continue;

            const float d = length(e) / r;

            // Speed, not correctness: `maskFalloff` saturates, so a pixel past
            // the rim already gets zero coverage and adds nothing. Deleting this
            // line changes no output — checked by mutation, and the suite
            // correctly does not flinch. It is here because the square reject
            // above admits the corners of the bounding box, which are 1.41
            // radii out.
            if (d >= 1.0f) continue;

            const float cov = flow * dabCoverage(d, p.hardness);

            // ⚠ Erase is **destination-out**, the exact inverse of the
            // source-over below, and that pairing is what makes the two
            // reversible against each other: painting takes the alpha a
            // fraction of the way to one, erasing takes it the same fraction of
            // the way to zero. Subtracting `cov` instead would drive the alpha
            // negative wherever a slow hand lingered, and the saturate at the
            // end would hide it as a hard hole rather than a soft one.
            //
            // Per *dab*, not per stroke: one component accumulates every stroke
            // ever laid on it, so the polarity has to travel with the dab. It
            // rides in the aux texture's third channel.
            if (dab.z > 0.5f) own -= cov * own;
            else              own += cov * (1.0f - own);
        }

        own = saturate(own);
    }
    else if (p.kind == 4)
    {
        // A raster component: a segmentation matte, or anything else that is an
        // image rather than a formula. The boundary it arrives with is as coarse
        // as its producer's resolution; the guided refinement (research/
        // masking.md §4) is what snaps it back onto the photograph's own edges,
        // and it is a separate control precisely so a matte can be used raw when
        // the refinement would hurt.
        own = sampleMatte(matte, q, p.matteSize);
    }
    else if (p.kind == 5)
    {
        // A band on what the pixel *is*, not on where it is.
        // research/masking.md §4b.
        //
        // ⚠ Read from the **reference** image — after white balance and the
        // camera matrix, before any user adjustment. Reading the edited result
        // instead would mean raising exposure through a highlight band grows
        // the band, which raises the exposure further: a feedback loop, not a
        // tool. Same reason the colour picker samples this texture.
        //
        // ⚠ And measured in **stops**. Scene-linear luminance is unbounded and
        // its interesting range is logarithmic, so a band of fixed linear width
        // is an enormous selection in the shadows and a sliver in the
        // highlights — unusable across most of a slider's travel.
        const float y = max(luminance2020(reference[tid.xy].rgb), 1e-6f);

        // ⚠ Biased by the global exposure, and this is what makes the control
        // usable rather than baffling. The reference is measured *before* the
        // tone controls, so its luminance is the scene's, not the screen's — on
        // a night frame lifted 2.6 stops, a band a photographer sets by looking
        // at the picture would sit two and a half stops away from anything and
        // select nothing at all. That is exactly what the first version did.
        //
        // Exposure is a multiply, so in log2 it is an add: one operation, exact,
        // and the same fact the guided chain relies on to stay cached. The
        // *measurement* still comes from the stable reference, so the band is
        // not chasing its own adjustment — the tone controls, the local
        // exposure this mask drives, and everything downstream leave it alone.
        const float ev = log2(y) + p.rangeBias;

        // Two independent edges, each a smootherstep ramp `rangeSoft` stops
        // wide. Independent so the band can be opened at either end: push
        // `rangeHi` past the frame's brightest pixel and it is a shadow
        // selection, push `rangeLo` below its darkest and it is a highlight one.
        const float soft = max(p.rangeSoft, 1e-3f);
        const float rise = maskFalloff((ev - p.rangeLo) / soft);
        const float fall = maskFalloff((p.rangeHi - ev) / soft);
        own = rise * fall;
    }
    else if (p.kind == 6)
    {
        // A band on what colour the pixel is. research/masking.md §4c.
        //
        // Same reference image as kind 5, and for the same reason: read the
        // edited result and shifting hue through the mask would move what the
        // mask selects.
        //
        // ⚠ No exposure bias here, and none is needed. Kind 5 needs one because
        // stops measured before the tone controls are not the stops on screen;
        // this metric is *exactly* invariant to exposure, so the number the
        // photographer set against the picture is the number the kernel wants.
        const float2 here = oklabChroma(reference[tid.xy].rgb);
        const float2 want = oklabChroma(float3(p.colourR, p.colourG, p.colourB));
        const float d = length(here - want);

        // One edge, not two: a colour band is a disc around the target rather
        // than an interval, so there is no far side to open. Full inside
        // `colourTol`, ramping to nothing `colourSoft` further out.
        const float soft = max(p.colourSoft, 1e-4f);
        own = 1.0f - maskFalloff((d - p.colourTol) / soft);
    }

    // Invert applies to every kind, which it did not before: the old pair of
    // nodes dropped it for brushes, because the gradient node held the invert
    // and the brush node discarded that node's output. A fourth dead control of
    // the same class as the three found in session 2026-07-29n.
    if (p.invert != 0) own = 1.0f - own;

    // A continuation pass *is* the stroke it continues, so it replaces rather
    // than folds — folding would compose the stroke against its own first half.
    if (p.accumulate != 0) { dst[tid.xy] = saturate(own); return; }

    // ⚠ A component that begins a layer ignores what came before it. Folding
    // into `src` regardless is what makes a stack one coverage; this is the one
    // line that makes it several.
    const float under = (p.startsLayer != 0) ? 0.0f : src[tid.xy];
    dst[tid.xy] = saturate(maskCompose(under, own, p.compose));
}
