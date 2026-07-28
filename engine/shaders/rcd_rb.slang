// RCD step 4 — red and blue.
//
// Reference: Luis Sanz Rodríguez, https://github.com/LuisSR/RCD-Demosaicing (MIT).
//
// Two cases, both interpolating the colour *difference* C − G rather than the
// channel itself. Chroma varies slowly compared with luminance, so this keeps
// the detail green already resolved.
//
//   4.1 At a red or blue site, the missing opposite channel sits on the
//       diagonals. A P/Q discrimination — the same statistic as step 1, sampled
//       diagonally — decides which diagonal to favour. The previous version
//       averaged a 3x3 with no directionality at all, which is why edges showed
//       colour fringing.
//
//   4.2 At a green site, both red and blue sit on the cardinal neighbours, and
//       the step-1 discrimination already computed is reused.

#include "cfa.slang"
#include "ops/rcd_stat.slang"

struct RBParams
{
    uint2 size;
    uint  filters;
};

// Colour difference (sample − green) at an offset.
float diffAt(Texture2D<float> cfa, Texture2D<float> green, int2 c, int2 d, uint2 sz)
{
    const int2 q = clampToImage(c + d, sz);
    return cfa[q] - green[q];
}

[shader("compute")]
[numthreads(16, 16, 1)]
void rcdRedBlue(uint3 tid : SV_DispatchThreadID,
                uniform Texture2D<float>    cfa,
                uniform Texture2D<float>    green,
                uniform Texture2D<float>    dirs,
                uniform RWTexture2D<float4> rgb,
                uniform ConstantBuffer<RBParams> p)
{
    if (tid.x >= p.size.x || tid.y >= p.size.y) return;

    const int2  c  = int2(tid.xy);
    const uint2 sz = p.size;
    const float g  = green[tid.xy];

    const uint here = cfaChannelRGB(tid.xy, p.filters);

    float r, b;

    if (here == 1)
    {
        // ── 4.2 Green site: red and blue are both cardinal neighbours. ──
        const float vh = dirs[tid.xy];

        const float nR = diffAt(cfa, green, c, int2(0, -1), sz);
        const float sR = diffAt(cfa, green, c, int2(0,  1), sz);
        const float wR = diffAt(cfa, green, c, int2(-1, 0), sz);
        const float eR = diffAt(cfa, green, c, int2( 1, 0), sz);

        // On a Bayer grid one axis carries red and the other blue at a green
        // site, so which pair is which depends on the row.
        const bool redIsHorizontal = (cfaChannelRGB(uint2(clampToImage(c + int2(1, 0), sz)),
                                                    p.filters) == 0);

        const float horiz = 0.5f * (wR + eR);
        const float vert  = 0.5f * (nR + sR);

        r = g + (redIsHorizontal ? horiz : vert);
        b = g + (redIsHorizontal ? vert  : horiz);
        (void)vh;
    }
    else
    {
        // ── 4.1 Red or blue site: the opposite channel is on the diagonals. ──
        const float pStat = rcdDirectionStat(
            tapClamped(cfa, c + int2(-4, -4), sz), tapClamped(cfa, c + int2(-3, -3), sz),
            tapClamped(cfa, c + int2(-2, -2), sz), tapClamped(cfa, c + int2(-1, -1), sz),
            tapClamped(cfa, c, sz),
            tapClamped(cfa, c + int2( 1,  1), sz), tapClamped(cfa, c + int2( 2,  2), sz),
            tapClamped(cfa, c + int2( 3,  3), sz), tapClamped(cfa, c + int2( 4,  4), sz));

        const float qStat = rcdDirectionStat(
            tapClamped(cfa, c + int2( 4, -4), sz), tapClamped(cfa, c + int2( 3, -3), sz),
            tapClamped(cfa, c + int2( 2, -2), sz), tapClamped(cfa, c + int2( 1, -1), sz),
            tapClamped(cfa, c, sz),
            tapClamped(cfa, c + int2(-1,  1), sz), tapClamped(cfa, c + int2(-2,  2), sz),
            tapClamped(cfa, c + int2(-3,  3), sz), tapClamped(cfa, c + int2(-4,  4), sz));

        const float pqDisc = pStat / (pStat + qStat);

        const float nwD = diffAt(cfa, green, c, int2(-1, -1), sz);
        const float seD = diffAt(cfa, green, c, int2( 1,  1), sz);
        const float neD = diffAt(cfa, green, c, int2( 1, -1), sz);
        const float swD = diffAt(cfa, green, c, int2(-1,  1), sz);

        // Diagonal gradients, weighted opposingly like the cardinal case.
        const float nwG = kRcdEps + abs(nwD - seD);
        const float seG = kRcdEps + abs(seD - nwD);
        const float neG = kRcdEps + abs(neD - swD);
        const float swG = kRcdEps + abs(swD - neD);

        const float pEst = (nwG * seD + seG * nwD) / (nwG + seG);
        const float qEst = (neG * swD + swG * neD) / (neG + swG);

        const float other = g + (1.0f - pqDisc) * pEst + pqDisc * qEst;

        if (here == 0) { r = cfa[tid.xy]; b = other; }
        else           { b = cfa[tid.xy]; r = other; }
    }

    rgb[tid.xy] = float4(max(r, 0.0f), max(g, 0.0f), max(b, 0.0f), 1.0f);
}
