// Film grain — research/film-grain.md, decision #81.
//
// A precomputed correlated field (AV1's architecture, Norkin & Birkbeck 2018)
// weighted by the Boolean model's own variance law (Newson, Delon & Galerne,
// CGF 36(8), 2017). The plate is built on the host by `GrainPlate.h`; this
// samples it.
//
// ⚠ **This node is the last pointwise writer before geometry, and it is the one
// that quantises.** `develop_display` used to add the Bayer dither as its last
// act and write eight bits; grain has to be added to *unquantised* values, so
// the display node now writes float and the dither block lives here. At
// Amount 0 this kernel is a copy plus that dither — bit-identical to what
// `develop_display` used to produce, which is what keeps every existing
// `identical` export baseline valid.

#include "ops/dither_ops.slang"

/// Level 0 of the plate is this square, and the chain is stacked below it.
/// ⚠ Must equal `grain::kPlateSize` in `GrainPlate.h`.
static const int PLATE_SIZE = 2048;
static const int PLATE_LEVELS = 12;

struct GrainParams
{
    uint2  size;
    /// Nonzero when this node writes eight bits — the flag that used to live on
    /// the display node.
    uint   dither;
    /// Peak standard deviation in display units, already scaled from the
    /// slider. Zero disables the branch entirely.
    float  amount;

    /// Grain radius in frame pixels. The plate is sampled at
    /// `framePixel / size`, so this scales the field rather than rebuilding it.
    float  grainSize;
    /// Frame pixels per node pixel: 1 in the full graph, 16 in the preview.
    ///
    /// ⚠ This is what makes the two graphs sample **one field** rather than two
    /// realisations of it. Without it the preview would show the full per-pixel
    /// variance where the settled render averages 16x16 of them, and would read
    /// an order of magnitude grainier than the picture it previews.
    float  gridStep;
    float  _pad0;
};

/// First row of level `l` in the stacked plate.
///
/// ⚠ The same closed form `grain::levelOffset` uses on the host, written the
/// same way. Two derivations of one offset is how a level gets read from the
/// wrong rows — which would be a plausible field over the wrong grain size.
int levelOffset(int l)
{
    return PLATE_SIZE * 2 - ((PLATE_SIZE * 2) >> l);
}

/// One bilinear tap inside a level, wrapped.
///
/// ⚠ Hand-rolled rather than a hardware sampler. Filtering precision is not
/// specified across GPU families, so a sampler could make an export differ from
/// the preview it was made on — by *device*, which is the least debuggable
/// difference there is.
float plateBilinear(Texture2D<float> plate, float2 uv, int level)
{
    const int n = max(PLATE_SIZE >> level, 1);
    const int row = levelOffset(level);

    const float2 p = uv * float(n) - 0.5f;
    const float2 f = frac(p);
    const int x0 = int(floor(p.x)), y0 = int(floor(p.y));

    const int xa = ((x0 % n) + n) % n, xb = ((x0 + 1) % n + n) % n;
    const int ya = ((y0 % n) + n) % n, yb = ((y0 + 1) % n + n) % n;

    const float aa = plate[int2(xa, row + ya)];
    const float ba = plate[int2(xb, row + ya)];
    const float ab = plate[int2(xa, row + yb)];
    const float bb = plate[int2(xb, row + yb)];

    return lerp(lerp(aa, ba, f.x), lerp(ab, bb, f.x), f.y);
}

[shader("compute")]
[numthreads(16, 16, 1)]
void grain(uint3 tid : SV_DispatchThreadID,
           uniform Texture2D<float4>  src,
           uniform Texture2D<float>   plate,
           uniform RWTexture2D<float4> dst,
           uniform ConstantBuffer<GrainParams> p)
{
    if (tid.x >= p.size.x || tid.y >= p.size.y) return;

    float3 c = src[tid.xy].rgb;

    if (p.amount > 1e-6f)
    {
        // Frame coordinates, so the field is keyed to the negative rather than
        // to the output: a crop enlarges the grain the way enlarging more of a
        // negative does, and it does not swim when the preview pans.
        const float2 framePx = (float2(tid.xy) + 0.5f) * p.gridStep;
        const float2 uv = framePx / (float(PLATE_SIZE) * max(p.grainSize, 1e-3f));

        // How far down the chain this pixel's footprint reaches. One node pixel
        // covers `gridStep` frame pixels; a grain is `grainSize` of them.
        const float lod = clamp(log2(max(p.gridStep / max(p.grainSize, 1e-3f), 1e-6f)),
                                0.0f, float(PLATE_LEVELS - 1));
        const int lo = int(floor(lod));
        const int hi = min(lo + 1, PLATE_LEVELS - 1);
        const float t = lod - float(lo);

        const float g = lerp(plateBilinear(plate, uv, lo),
                             plateBilinear(plate, uv, hi), t);

        // ⚠ The Boolean model's own pointwise statistics: a grain field of mean
        // coverage u has Bernoulli variance u(1-u), so it is loudest in the
        // midtones and silent at both ends. That is not taste — it is why the
        // `saturate` below cannot turn grain into a mean shift, because where
        // clipping could bite, sigma is already zero.
        const float y = saturate(dot(c, float3(0.2126f, 0.7152f, 0.0722f)));
        const float sigma = p.amount * sqrt(max(y * (1.0f - y), 0.0f));

        // Monochrome, added equally to all three. Per-channel grain is what a
        // noisy sensor looks like; film is luminance.
        c += sigma * g;
    }

    // The dither, inherited from develop_display. It has to be last, after the
    // grain, or grain would be added to values that are already quantised.
    if (p.dither != 0u) c += bayerOffset(tid.xy);

    dst[tid.xy] = float4(saturate(c), 1.0f);
}
