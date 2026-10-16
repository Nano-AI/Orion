// Non-local means on luminance, profiled. research/high-iso.md §4.
//
// Buades, Coll & Morel, "A non-local algorithm for image denoising", CVPR 2005;
// the pixelwise form and parameter table are their IPOL article (2011,
// doi:10.5201/ipol.2011.bcm_nlm). Implemented from the equations. ⚠ The IPOL
// reference code is GPL and was not read.
//
//     û(p)   = Σ_q w(p,q)·u(q) / Σ_q w(p,q)        over a (2r+1)² window
//     w(p,q) = exp(−max(d² − 2σ², 0) / h²)         h = k·σ
//     d²     = mean over the (2f+1)² patch of (u(p+j) − u(q+j))²
//     w(p,p) = max_q w(p,q)                         the reference pixel's weight
//
// Profiled: σ² = a·Y + b at this pixel's own 3x3 mean brightness, the capture
// chain's luma model, so the 2σ² subtraction holds at every tone rather than
// at the one σ an 8-bit table assumes.
//
// Luma only. The output is the input plus a scalar (Ŷ − Y) on all three
// channels; the Rec.2020 weights sum to one, so that moves luminance by
// exactly (Ŷ − Y) and leaves the chroma vector where it was.
//
// ⚠ The first threadgroup-memory kernel in the tree. The gather is 441
// offsets × 25 taps per pixel; read from a texture that is a background pass,
// read from a 40×40 tile loaded once per 16×16 group it is a node.

#include "cfa.slang"

struct NlmParams
{
    uint2 size;
    float noiseA;    // Poisson term, luma, after white balance
    float noiseB;    // Gaussian term
    float k;         // h = k·σ; the IPOL table's 0.40 is mid-slider
    int   radius;    // search half-width, ≤ kMaxRadius
    int   patch;     // patch half-width, ≤ kMaxPatch
    int   _pad;
};

static const int kMaxRadius = 10;
static const int kMaxPatch  = 2;
static const int kApron     = kMaxRadius + kMaxPatch;   // 12
static const int kTile      = 16 + 2 * kApron;          // 40

groupshared float tile[kTile * kTile];

float nlmLuma2020(float3 c)
{
    return dot(c, float3(0.2627f, 0.6780f, 0.0593f));
}

float tileAt(int x, int y)
{
    return tile[y * kTile + x];
}

[shader("compute")]
[numthreads(16, 16, 1)]
void nlmLuma(uint3 tid  : SV_DispatchThreadID,
             uint3 gtid : SV_GroupThreadID,
             uint3 gid  : SV_GroupID,
             uniform Texture2D<float4>   src,
             uniform RWTexture2D<float4> dst,
             uniform ConstantBuffer<NlmParams> p)
{
    // Every thread helps load the tile, including the ones past the image's
    // edge, which is why the early-out comes after the barrier.
    const int2 origin = int2(gid.xy) * 16 - kApron;
    const int  flat   = int(gtid.y) * 16 + int(gtid.x);
    for (int i = flat; i < kTile * kTile; i += 256)
    {
        const int2 q = clampToImage(origin + int2(i % kTile, i / kTile), p.size);
        tile[i] = nlmLuma2020(src[q].rgb);
    }
    GroupMemoryBarrierWithGroupSync();

    if (tid.x >= p.size.x || tid.y >= p.size.y) return;

    const int    cx = int(gtid.x) + kApron;
    const int    cy = int(gtid.y) + kApron;
    const float3 c  = src[tid.xy].rgb;
    const float  y0 = tileAt(cx, cy);

    // σ at this brightness. A 3x3 mean, so one noisy sample does not set its
    // own threshold.
    float mean = 0.0f;
    [unroll]
    for (int my = -1; my <= 1; ++my)
        [unroll]
        for (int mx = -1; mx <= 1; ++mx)
            mean += tileAt(cx + mx, cy + my);
    mean *= 1.0f / 9.0f;
    const float sigma2 = max(p.noiseA * max(mean, 0.0f) + p.noiseB, 1e-12f);
    const float h      = p.k * sqrt(sigma2);
    if (h <= 0.0f)
    {
        dst[tid.xy] = float4(c, 1.0f);
        return;
    }

    const int   r      = clamp(p.radius, 1, kMaxRadius);
    const int   f      = clamp(p.patch, 0, kMaxPatch);
    const float invH2  = 1.0f / (h * h);
    const float invN   = 1.0f / float((2 * f + 1) * (2 * f + 1));
    const float floor2 = 2.0f * sigma2;

    float sum = 0.0f, wsum = 0.0f, wmax = 0.0f;
    for (int oy = -r; oy <= r; ++oy)
    {
        for (int ox = -r; ox <= r; ++ox)
        {
            if (ox == 0 && oy == 0) continue;
            float d2 = 0.0f;
            for (int py = -f; py <= f; ++py)
            {
                for (int px = -f; px <= f; ++px)
                {
                    const float d = tileAt(cx + px, cy + py) - tileAt(cx + ox + px, cy + oy + py);
                    d2 += d * d;
                }
            }
            const float w = exp(-max(d2 * invN - floor2, 0.0f) * invH2);
            sum  += w * tileAt(cx + ox, cy + oy);
            wsum += w;
            wmax  = max(wmax, w);
        }
    }
    sum  += wmax * y0;
    wsum += wmax;

    const float yhat = (wsum > 0.0f) ? sum / wsum : y0;
    dst[tid.xy] = float4(c + (yhat - y0), 1.0f);
}
