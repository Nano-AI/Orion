// Local Laplacian, steps 4 and 5 — pick two pyramids, interpolate, collapse.
//
// Aubry et al. step 4: for each coefficient, read g from the input's Gaussian
// pyramid, find j and a with g = (1-a)*gamma_j + a*gamma_{j+1}, and linearly
// interpolate that coefficient between the two precomputed remapped pyramids.
// Step 5 — the collapse — is folded into the same pass, so the output Laplacian
// pyramid is never written out: out_l = L_l + expand(out_{l+1}), coarse to fine.
//
// The Laplacian coefficient itself is formed here rather than stored, for the
// same reason: L_l[r_j] = G_l[r_j] - expand(G_{l+1}[r_j]), and both operands
// are already being read.
//
// Picking a component and expanding commute — expansion is a weighted sum and
// picking is a projection — so the expand runs once on the packed float4 and
// the component is selected afterwards, rather than twice on scalars.

#include "ops/llf_ops.slang"

struct LlfCollapseParams
{
    uint2 size;         // this level
    uint2 coarseSize;   // level + 1
    float gammaStep;
    int   gammaCount;
    float _pad[2];
};

[shader("compute")]
[numthreads(16, 16, 1)]
void llfCollapse(uint3 tid : SV_DispatchThreadID,
                 uniform Texture2D<float>   gauss,    // g at this level
                 uniform Texture2D<float4>  fine0,    // gammas 0-3, this level
                 uniform Texture2D<float4>  fine1,    // gammas 4-7
                 uniform Texture2D<float4>  fine2,    // gammas 8-11
                 uniform Texture2D<float4>  fine3,    // gammas 12-15
                 uniform Texture2D<float4>  coarse0,  // the same, level + 1
                 uniform Texture2D<float4>  coarse1,
                 uniform Texture2D<float4>  coarse2,
                 uniform Texture2D<float4>  coarse3,
                 uniform Texture2D<float>   coarseOut,  // collapsed, level + 1
                 uniform RWTexture2D<float> dst,
                 uniform ConstantBuffer<LlfCollapseParams> p)
{
    if (tid.x >= p.size.x || tid.y >= p.size.y) return;

    int3 cx, cy; float3 wx, wy;
    llfExpand1D(int(tid.x), cx, wx);
    llfExpand1D(int(tid.y), cy, wy);

    const int2 hi = int2(p.coarseSize) - 1;

    float4 up0 = float4(0.0f, 0.0f, 0.0f, 0.0f);
    float4 up1 = float4(0.0f, 0.0f, 0.0f, 0.0f);
    float4 up2 = float4(0.0f, 0.0f, 0.0f, 0.0f);
    float4 up3 = float4(0.0f, 0.0f, 0.0f, 0.0f);
    float  upOut = 0.0f;

    for (int b = 0; b < 3; ++b)
    {
        const int sy = clamp(cy[b], 0, hi.y);
        for (int a = 0; a < 3; ++a)
        {
            const int   sx = clamp(cx[a], 0, hi.x);
            const float w  = wx[a] * wy[b];
            if (w == 0.0f) continue;

            const int2 q = int2(sx, sy);
            up0   += w * coarse0[q];
            up1   += w * coarse1[q];
            up2   += w * coarse2[q];
            up3   += w * coarse3[q];
            upOut += w * coarseOut[q];
        }
    }

    int   j;
    float frac;
    llfBracket(gauss[tid.xy], p.gammaStep, p.gammaCount, j, frac);

    const float4 g0 = fine0[tid.xy];
    const float4 g1 = fine1[tid.xy];
    const float4 g2 = fine2[tid.xy];
    const float4 g3 = fine3[tid.xy];

    const float l0 = llfPick(j,     g0, g1, g2, g3) - llfPick(j,     up0, up1, up2, up3);
    const float l1 = llfPick(j + 1, g0, g1, g2, g3) - llfPick(j + 1, up0, up1, up2, up3);

    dst[tid.xy] = lerp(l0, l1, frac) + upOut;
}
