// The display transform and the look, in one pass.
//
// AgX maps scene-linear to display, then the tone curve shapes the result.
// Both are pointwise, and splitting them cost a 194 MB rgba16f round trip for
// nothing. Fusing also means AgX never has to quantise to 8-bit before the
// curve sees it — the intermediate stays in registers at full float precision.
//
// There is exactly ONE display transform in the pipeline, and this is it.

#include "ops/dither_ops.slang"

struct DisplayParams
{
    float contrast;     // slope of the sigmoid at middle gray
    float pivot;        // middle gray in log2, typically -2.5
    uint  curveIdentity;// skip the LUT entirely when the curve is untouched
    uint  resolution;   // LUT entries per row
    uint2 size;
    // Nonzero when this node writes eight bits, which it does whenever film
    // grain is off. With grain on it writes RGBA16Float and develop:grain
    // rounds instead — see ops/dither_ops.slang.
    uint  dither;
    uint  lutSize;      // creative LUT grid edge; 0 when none is loaded
    float lutStrength;
    float _pad[3];
    float4 lutMin;      // the file's DOMAIN_MIN
    float4 lutMax;
};

/// A creative 3D LUT, with tetrahedral interpolation.
///
/// Kasson, Nin, Plouffe & Hafner compared the ways a cube can be subdivided for
/// three-dimensional interpolation; tetrahedral is the one that wins, and it is
/// what color management has used since. research/luts.md.
///
/// Why not the hardware's trilinear filtering, which is free: trilinear is a
/// different function, not a cheaper implementation of the same one. It reads
/// all eight corners of the cell, and on a LUT with a hard boundary in it — a
/// key, a hue restriction, most film emulations — the four corners on the far
/// side of that boundary bleed across it. Tetrahedral reads four corners that
/// all lie in the same simplex as the sample, so it cannot.
///
/// The grid is stored in a 2D texture because a 3D one would be the only 3D
/// texture in the engine: x is red, and y packs blue and green as b*size + g.
/// One corner of the cell, clamped to the grid.
float3 cubeCorner(Texture2D<float4> cube, int3 base, int3 d, uint size)
{
    const int w = int(size);
    const int3 q = min(base + d, int3(w - 1, w - 1, w - 1));
    return cube[int2(q.x, q.z * w + q.y)].rgb;
}

float3 sampleCube(Texture2D<float4> cube, float3 c, uint size,
                  float3 domainMin, float3 domainMax)
{
    const float n = float(size - 1u);
    const float3 t = saturate((c - domainMin) / max(domainMax - domainMin,
                                                    float3(1e-6f, 1e-6f, 1e-6f)));
    const float3 x = t * n;

    const int3 i0 = int3(min(floor(x), float3(n - 1.0f, n - 1.0f, n - 1.0f)));
    const float3 f = x - float3(i0);

    const float3 c000 = cubeCorner(cube, i0, int3(0, 0, 0), size);
    const float3 c111 = cubeCorner(cube, i0, int3(1, 1, 1), size);

    // The six tetrahedra, selected by the ordering of the fractional
    // coordinates. Each case walks c000 to c111 along the three unit steps in
    // descending order of their fractions, which is exactly the simplex the
    // sample falls in.
    if (f.r > f.g)
    {
        if (f.g > f.b)
        {
            const float3 a = cubeCorner(cube, i0, int3(1, 0, 0), size);
            const float3 b = cubeCorner(cube, i0, int3(1, 1, 0), size);
            return c000 + f.r * (a - c000) + f.g * (b - a) + f.b * (c111 - b);
        }
        if (f.r > f.b)
        {
            const float3 a = cubeCorner(cube, i0, int3(1, 0, 0), size);
            const float3 b = cubeCorner(cube, i0, int3(1, 0, 1), size);
            return c000 + f.r * (a - c000) + f.b * (b - a) + f.g * (c111 - b);
        }
        const float3 a = cubeCorner(cube, i0, int3(0, 0, 1), size);
        const float3 b = cubeCorner(cube, i0, int3(1, 0, 1), size);
        return c000 + f.b * (a - c000) + f.r * (b - a) + f.g * (c111 - b);
    }
    if (f.b > f.g)
    {
        const float3 a = cubeCorner(cube, i0, int3(0, 0, 1), size);
        const float3 b = cubeCorner(cube, i0, int3(0, 1, 1), size);
        return c000 + f.b * (a - c000) + f.g * (b - a) + f.r * (c111 - b);
    }
    if (f.b > f.r)
    {
        const float3 a = cubeCorner(cube, i0, int3(0, 1, 0), size);
        const float3 b = cubeCorner(cube, i0, int3(0, 1, 1), size);
        return c000 + f.g * (a - c000) + f.b * (b - a) + f.r * (c111 - b);
    }
    const float3 a = cubeCorner(cube, i0, int3(0, 1, 0), size);
    const float3 b = cubeCorner(cube, i0, int3(1, 1, 0), size);
    return c000 + f.g * (a - c000) + f.r * (b - a) + f.b * (c111 - b);
}

static const float kMinEv = -12.47393f;
static const float kMaxEv =   4.026069f;

// Working space is linear Rec.2020; AgX is defined against Rec.709 primaries,
// so convert first. Skipping this step is not a subtle error — it silently
// rotates every hue.
static const float3x3 kRec2020ToRec709 = float3x3(
     1.6605f, -0.5876f, -0.0728f,
    -0.1246f,  1.1329f, -0.0083f,
    -0.0182f, -0.1006f,  1.1187f);

// AgX inset and outset, from Troy Sobotka's reference implementation.
//
// Every row sums to 1, which is the property that matters: it means neutral
// gray stays neutral. An earlier pair of matrices here did not have it — the
// outset mapped (1,1,1) to roughly (0.84, 0.94, 1.22), lifting blue well above
// green and casting the whole image purple. The test in orion-tests asserts
// neutrality now, so that cannot recur silently.
static const float3x3 kInset = float3x3(
    0.84247906f, 0.07843360f, 0.07922375f,
    0.04232824f, 0.87846864f, 0.07916613f,
    0.04237565f, 0.07843360f, 0.87914297f);

static const float3x3 kOutset = float3x3(
     1.19687900512017f,   -0.0980208811401368f, -0.0990297440797205f,
    -0.0528968517574562f,  1.15190312990417f,   -0.0989611768448433f,
    -0.0529716355144438f, -0.0980434501171241f,  1.15107367264116f);

// Six-term polynomial fit of the AgX sigmoid over the normalized range.
float agxCurve(float x)
{
    const float x2 = x * x;
    const float x4 = x2 * x2;
    return  15.5f    * x4 * x2
          - 40.14f   * x4 * x
          + 31.96f   * x4
          -  6.868f  * x2 * x
          +  0.4298f * x2
          +  0.1191f * x
          -  0.00232f;
}

// Linear interpolation between LUT entries, so 256 entries are smooth enough
// that no banding survives the 8-bit output encode.
float sampleLut(Texture2D<float> lut, float v, uint row, uint resolution)
{
    const float x = saturate(v) * float(resolution - 1u);
    const uint  i = uint(floor(x));
    const uint  j = min(i + 1u, resolution - 1u);
    return lerp(lut[uint2(i, row)], lut[uint2(j, row)], x - float(i));
}

[shader("compute")]
[numthreads(16, 16, 1)]
void developDisplay(uint3 tid : SV_DispatchThreadID,
                    uniform Texture2D<float4>   src,
                    uniform Texture2D<float>    lut,
                    uniform Texture2D<float4>   cube,
                    uniform RWTexture2D<float4> dst,
                    uniform ConstantBuffer<DisplayParams> p)
{
    if (tid.x >= p.size.x || tid.y >= p.size.y) return;

    float3 c = max(src[tid.xy].rgb, 0.0f);

    // ── AgX ────────────────────────────────────────────────────────────────
    c = max(mul(kRec2020ToRec709, c), 0.0f);
    c = mul(kInset, c);

    c = clamp(log2(max(c, 1e-10f)), kMinEv, kMaxEv);
    c = (c - kMinEv) / (kMaxEv - kMinEv);

    // Contrast pivots about middle gray, so raising it does not also shift
    // overall brightness.
    const float pivotNorm = (p.pivot - kMinEv) / (kMaxEv - kMinEv);
    c = saturate((c - pivotNorm) * p.contrast + pivotNorm);

    c = float3(agxCurve(c.r), agxCurve(c.g), agxCurve(c.b));
    c = saturate(mul(kOutset, c));

    // ── Tone curve ─────────────────────────────────────────────────────────
    // Row 0 is master, rows 1-3 are per-channel. Master applies first, matching
    // how every editor's curve panel behaves.
    if (p.curveIdentity == 0u)
    {
        c.r = sampleLut(lut, c.r, 0u, p.resolution);
        c.g = sampleLut(lut, c.g, 0u, p.resolution);
        c.b = sampleLut(lut, c.b, 0u, p.resolution);

        c.r = sampleLut(lut, c.r, 1u, p.resolution);
        c.g = sampleLut(lut, c.g, 2u, p.resolution);
        c.b = sampleLut(lut, c.b, 3u, p.resolution);
    }

    // ── Creative LUT ───────────────────────────────────────────────────────
    // Last, and deliberately: a look is applied to a finished picture. Running
    // it before the tone curve would mean the curve reshapes whatever the look
    // did, and a .cube authored against a display-referred image expects to see
    // one.
    //
    // Inside this kernel rather than as its own node, because it is pointwise
    // and every extra pointwise pass is a 194 MB round trip at 24 MP. Same rule
    // that fused AgX and the curve.
    if (p.lutSize >= 2u)
    {
        const float3 looked = sampleCube(cube, c, p.lutSize,
                                         p.lutMin.rgb, p.lutMax.rgb);
        c = lerp(c, looked, saturate(p.lutStrength));
    }

    // No sRGB encode here. The AgX polynomial is a fit of the *display*
    // rendering transform — its output is already display-referred, so
    // applying a transfer function on top encodes the signal twice. That is
    // what washed the image out: middle gray landed at 0.74 instead of 0.50,
    // lifting everything and flattening color along with it.
    // ⚠ The dither is applied by whichever node writes the eight bits, and by
    // only that one. That is this node when grain is off and `develop:grain`
    // when it is on — grain has to be added to unquantised values, so with the
    // slider up this node writes float and the rounding happens one node later.
    // `DevelopPipeline::retargetOutputChain` is the single place that decides,
    // and the table it holds is why this is a flag rather than a fact.
    if (p.dither != 0u) c += bayerOffset(tid.xy);

    dst[tid.xy] = float4(saturate(c), 1.0f);
}
