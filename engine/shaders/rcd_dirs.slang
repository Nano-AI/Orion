// RCD step 1 — vertical/horizontal direction discrimination.
//
// Reference: Luis Sanz Rodríguez, https://github.com/LuisSR/RCD-Demosaicing (MIT).
// Adopted as darktable's default Bayer demosaic.
//
// Output is VH_Dir in 0..1. Near 1 means the vertical statistic dominates —
// the signal changes fast vertically — so green should be interpolated
// horizontally, along the smooth direction.

#include "cfa.slang"
#include "ops/rcd_stat.slang"

struct DirParams
{
    uint2 size;
};

[shader("compute")]
[numthreads(16, 16, 1)]
void rcdDirs(uint3 tid : SV_DispatchThreadID,
             uniform Texture2D<float>    cfa,
             uniform RWTexture2D<float>  dirs,
             uniform ConstantBuffer<DirParams> p)
{
    if (tid.x >= p.size.x || tid.y >= p.size.y) return;

    const int2  c  = int2(tid.xy);
    const uint2 sz = p.size;

    const float vStat = rcdDirectionStat(
        tapClamped(cfa, c + int2(0, -4), sz), tapClamped(cfa, c + int2(0, -3), sz),
        tapClamped(cfa, c + int2(0, -2), sz), tapClamped(cfa, c + int2(0, -1), sz),
        tapClamped(cfa, c, sz),
        tapClamped(cfa, c + int2(0,  1), sz), tapClamped(cfa, c + int2(0,  2), sz),
        tapClamped(cfa, c + int2(0,  3), sz), tapClamped(cfa, c + int2(0,  4), sz));

    const float hStat = rcdDirectionStat(
        tapClamped(cfa, c + int2(-4, 0), sz), tapClamped(cfa, c + int2(-3, 0), sz),
        tapClamped(cfa, c + int2(-2, 0), sz), tapClamped(cfa, c + int2(-1, 0), sz),
        tapClamped(cfa, c, sz),
        tapClamped(cfa, c + int2( 1, 0), sz), tapClamped(cfa, c + int2( 2, 0), sz),
        tapClamped(cfa, c + int2( 3, 0), sz), tapClamped(cfa, c + int2( 4, 0), sz));

    dirs[tid.xy] = vStat / (vStat + hStat);
}
