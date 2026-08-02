// Harmonic fill, the pull half — research/highlight-reconstruction.md.
//
// Reference: Rouf, Lau & Heidrich (2012), "Gradient Domain Color Restoration of
// Clipped Highlights", International Workshop on Projector-Camera Systems
// (PROCAMS) 2012. Their §3.2 estimates the color of a clipped region by solving
// a Laplace equation over the region with the region's own rim as a Dirichlet
// condition:
//
//     grad^2 rho = 0   over Omega,   rho|dOmega = f|dOmega
//
// That single equation is why this node does not segment anything. A Laplace
// solve with Dirichlet data is *local*: nothing crosses a pixel that is not in
// Omega, so each connected blown region is already solved on its own, with its
// own boundary, without ever being labelled. The connected-component pass that
// "segmentation-based reconstruction" appears to require is not needed to get
// the behavior it was wanted for.
//
// The solver is the pull-push interpolant of Gortler, Grzeszczuk, Szeliski &
// Cohen, "The Lumigraph", SIGGRAPH 1996, §3.5.1 — pull the known values up a
// pyramid carrying their weight, then push them back down into the gaps. It
// reaches across a hole of any width in O(N), which is the whole point: the
// window fit in `highlights.slang` reaches 12 pixels and declines beyond that.
//
// ⚠ Rouf et al. run a **multigrid** solve, which carries a residual correction
// this does not. Pull-push is therefore an approximation to their harmonic
// solution, not that solution; the deviation is measured rather than assumed —
// `testHighlightFill` reports it against a Gauss-Seidel reference run to
// convergence, and research/highlight-reconstruction.md records the number.
//
// ⚠ **Storage is premultiplied**: rgb holds w*f and a holds w, where w is 1 at a
// pixel whose color is known and 0 inside the hole. Premultiplication is what
// makes the push a single source-over, and it is what keeps every value in the
// pyramid a convex combination of known inputs — which is the discrete maximum
// principle, and the reason this cannot invent a color brighter or more
// saturated than the rim it read.

struct HlPullParams
{
    uint2 outSize;   // this (coarse) level
    uint2 inSize;    // the level below
};

/// The tent's adjoint at a factor of two, separable. It is the analysis filter
/// matching the bilinear synthesis in `hl_push`, which is the pairing Gortler
/// et al. use; taps sum to one, so a constant pulls to itself and the fill of a
/// uniform rim is that rim exactly.
static const float kTent[4] = { 0.125f, 0.375f, 0.375f, 0.125f };

[shader("compute")]
[numthreads(16, 16, 1)]
void hlPull(uint3 tid : SV_DispatchThreadID,
            uniform Texture2D<float4>   src,
            uniform RWTexture2D<float4> dst,
            uniform ConstantBuffer<HlPullParams> p)
{
    if (tid.x >= p.outSize.x || tid.y >= p.outSize.y) return;

    // Output texel t covers input texels 2t and 2t+1, so the four taps sit at
    // 2t-1 .. 2t+2 and the filter is centered on the pair rather than on one of
    // them. Centering it on 2t instead shifts the pyramid half a texel per
    // level, which walks the fill sideways as the level count changes.
    const int2 c  = int2(tid.xy) * 2;
    const int2 hi = int2(p.inSize) - 1;

    float4 acc = float4(0.0f, 0.0f, 0.0f, 0.0f);
    for (int dy = 0; dy < 4; ++dy)
    {
        const int sy = clamp(c.y + dy - 1, 0, hi.y);
        for (int dx = 0; dx < 4; ++dx)
        {
            const int sx = clamp(c.x + dx - 1, 0, hi.x);
            acc += kTent[dy] * kTent[dx] * src[int2(sx, sy)];
        }
    }

    // ⚠ Gortler et al.'s pull carries a w = min(1, sum) cap and this does not,
    // deliberately. Their weights are scattered-sample confidences that can sum
    // past one; here the taps are a partition of unity — sum(kTent)^2 == 1 — so
    // this pass is an *average* of weights already in [0, 1], and an average of
    // values at most one is at most one. The cap is unreachable.
    //
    // It was written first and then removed, because removing it changed
    // nothing any check could see: the mutation that deletes an unreachable
    // branch is invisible by construction, and a line that cannot execute is
    // worse than no line — it reads as a guard somebody is relying on.
    //
    // What would make it reachable is an input weight above one. There is no
    // such thing in this convention, and the later pieces do not introduce one:
    // Rouf et al.'s Eq. 8 confidence is a piecewise cubic on [0, 1].

    dst[tid.xy] = acc;
}
