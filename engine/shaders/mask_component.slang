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
    int    kind;        // 0 off, 1 linear, 2 radial, 3 brush
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

    float2 dabs[256];   // brush centres, normalized
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

[shader("compute")]
[numthreads(16, 16, 1)]
void maskComponent(uint3 tid : SV_DispatchThreadID,
                   uniform Texture2D<float>   src,   // coverage so far, 0..1
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

        for (int i = 0; i < p.count; ++i)
        {
            const float2 e = (q - p.dabs[i]) * toPx;

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

            // Source-over, not addition. Two dabs at full flow must give full
            // coverage and not two — adding would let a slow hand over the same
            // spot drive the alpha past 1 and clip, which reads as the brush
            // getting stronger the longer you hover.
            own += flow * dabCoverage(d, p.hardness) * (1.0f - own);
        }

        own = saturate(own);
    }

    // Invert applies to every kind, which it did not before: the old pair of
    // nodes dropped it for brushes, because the gradient node held the invert
    // and the brush node discarded that node's output. A fourth dead control of
    // the same class as the three found in session 2026-07-29n.
    if (p.invert != 0) own = 1.0f - own;

    // A continuation pass *is* the stroke it continues, so it replaces rather
    // than folds — folding would compose the stroke against its own first half.
    if (p.accumulate != 0) { dst[tid.xy] = saturate(own); return; }

    dst[tid.xy] = saturate(maskCompose(src[tid.xy], own, p.compose));
}
