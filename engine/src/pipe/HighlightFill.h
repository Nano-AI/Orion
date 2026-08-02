#pragma once

// Harmonic fill of a clipped region — research/highlight-reconstruction.md.
//
// Rouf, Lau & Heidrich (2012), "Gradient Domain Color Restoration of Clipped
// Highlights", International Workshop on Projector-Camera Systems (PROCAMS)
// 2012, §3.2: estimate the color of a clipped region by solving
//
//     grad^2 rho = 0   over Omega,   rho|dOmega = f|dOmega
//
// This header holds two implementations of that, and the difference between
// them is the point of the file.
//
//   - `harmonic()` solves it properly, by Gauss-Seidel run to convergence. It
//     is the ground truth. It is far too slow for a 24 Mpx frame in a 16 ms
//     budget and is never on the render path.
//   - `pullPush()` is the CPU twin of `hl_pull.slang` and `hl_push.slang`, the
//     interpolant that *is* on the render path.
//
// ⚠ The reason both exist is that the shipping solver is an approximation and
// this project does not ship an approximation whose error is a hope. Rouf et
// al. use a multigrid solve, which carries a residual correction pull-push does
// not. `testHighlightFill` runs both over the same fixtures and reports the
// deviation, so the number is in the test output on every run rather than in
// somebody's memory of a session.
//
// ⚠ **What this file does NOT do, deliberately.** It fills a scalar/color field
// over a hole. It does not detect clipping, does not decide which pixels are
// known, and is not wired into the develop graph. Those are the later pieces
// costed in `planning/ROADMAP.md`, and the ordering is on purpose: a solver
// with a measured error is worth having on its own, a half-wired node is not.

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace orion::pipe::hlfill {

/// One RGBA sample in the solver's storage convention.
///
/// ⚠ **Premultiplied**: `v` holds `w * f` and `w` holds the weight — 1 where the
/// color is known, 0 inside the hole. This is the convention both shaders use,
/// and it is what makes the push a single source-over and every intermediate a
/// convex combination of known inputs.
struct Sample {
    float v[3] = {0.0f, 0.0f, 0.0f};
    float w    = 0.0f;
};

/// A level of the pyramid.
struct Level {
    int                 width  = 0;
    int                 height = 0;
    std::vector<Sample> texels;

    Sample&       at(int x, int y)       { return texels[std::size_t(y) * width + x]; }
    const Sample& at(int x, int y) const { return texels[std::size_t(y) * width + x]; }
};

/// The tent's adjoint at a factor of two. ⚠ Must equal `kTent` in
/// `hl_pull.slang`; the two are compared by `testHighlightFill` rather than
/// merely asserted to be equal in a comment.
inline constexpr float kTent[4] = {0.125f, 0.375f, 0.375f, 0.125f};

/// How many levels a frame of this size needs for the fill to reach across it.
/// Stops at 1x1: a hole cannot be wider than the image, so a level that is one
/// texel across has already reached everywhere.
inline int levelsFor(int width, int height) {
    int levels = 1;
    while (width > 1 || height > 1) {
        width  = std::max(1, (width + 1) / 2);
        height = std::max(1, (height + 1) / 2);
        ++levels;
    }
    return levels;
}

/// `hl_pull.slang`, on the host. Coarse level from the one below.
inline Level pull(const Level& fine) {
    Level out;
    out.width  = std::max(1, (fine.width + 1) / 2);
    out.height = std::max(1, (fine.height + 1) / 2);
    out.texels.assign(std::size_t(out.width) * out.height, Sample{});

    for (int y = 0; y < out.height; ++y) {
        for (int x = 0; x < out.width; ++x) {
            Sample acc{};
            for (int dy = 0; dy < 4; ++dy) {
                const int sy = std::clamp(y * 2 + dy - 1, 0, fine.height - 1);
                for (int dx = 0; dx < 4; ++dx) {
                    const int   sx = std::clamp(x * 2 + dx - 1, 0, fine.width - 1);
                    const float k  = kTent[dy] * kTent[dx];
                    const Sample& s = fine.at(sx, sy);
                    acc.v[0] += k * s.v[0];
                    acc.v[1] += k * s.v[1];
                    acc.v[2] += k * s.v[2];
                    acc.w    += k * s.w;
                }
            }
            // ⚠ No w = min(1, sum) cap, matching `hl_pull.slang` — the taps are
            // a partition of unity, so this is an average of weights already in
            // [0, 1] and cannot exceed one. The shader carries the argument.
            out.at(x, y) = acc;
        }
    }
    return out;
}

/// `hl_push.slang`, on the host. This level, with the one below shown through
/// wherever this level's weight is short.
inline Level push(const Level& fine, const Level& coarse) {
    Level out = fine;

    for (int y = 0; y < fine.height; ++y) {
        for (int x = 0; x < fine.width; ++x) {
            const float fx = (float(x) + 0.5f) * 0.5f - 0.5f;
            const float fy = (float(y) + 0.5f) * 0.5f - 0.5f;
            const int   x0 = int(std::floor(fx));
            const int   y0 = int(std::floor(fy));
            const float ax = fx - float(x0);
            const float ay = fy - float(y0);

            const int xa = std::clamp(x0,     0, coarse.width  - 1);
            const int xb = std::clamp(x0 + 1, 0, coarse.width  - 1);
            const int ya = std::clamp(y0,     0, coarse.height - 1);
            const int yb = std::clamp(y0 + 1, 0, coarse.height - 1);

            const Sample& c00 = coarse.at(xa, ya);
            const Sample& c10 = coarse.at(xb, ya);
            const Sample& c01 = coarse.at(xa, yb);
            const Sample& c11 = coarse.at(xb, yb);

            const auto blend = [&](float a, float b, float c, float d) {
                return (a + (b - a) * ax) + ((c + (d - c) * ax) - (a + (b - a) * ax)) * ay;
            };

            Sample up{};
            for (int k = 0; k < 3; ++k) {
                up.v[k] = blend(c00.v[k], c10.v[k], c01.v[k], c11.v[k]);
            }
            up.w = blend(c00.w, c10.w, c01.w, c11.w);

            const Sample& f = fine.at(x, y);
            const float   t = 1.0f - f.w;

            Sample& o = out.at(x, y);
            for (int k = 0; k < 3; ++k) o.v[k] = f.v[k] + t * up.v[k];
            o.w = f.w + t * up.w;
        }
    }
    return out;
}

/// The factor the pyramid is solved at, relative to the picture.
///
/// ⚠ **This is a memory decision, and it was measured before it was taken.**
/// ROADMAP costed the chain at full resolution: 13 levels, level zero 194 MB at
/// 24 MP, ~516 MB for the two chains. Solving on a grid `kSolveScale` times
/// coarser and lifting the answer back costs `kSolveScale^2` less — 32 MB at
/// four — and the node count barely moves, because the level count is
/// logarithmic in the frame.
///
/// The reason it is allowed at all is what §3.2 asks for: `rho` is the solution
/// of `grad^2 rho = 0`, which is *harmonic*, so it has no detail to lose. It is
/// smooth by construction and the only place it varies quickly is at the rim,
/// where it is pinned to data the apply pass reads at full resolution anyway.
/// `testHighlightFillGpu` sweeps the factor against the Gauss-Seidel solution
/// every run and prints what each one costs, so this constant is checkable
/// rather than asserted.
///
/// Four, not eight or sixteen, for the same reason the guided filter and the
/// dehaze chain both use four: it is where the measured error stops being
/// smaller than the pull-push approximation the solver already carries.
inline constexpr int kSolveScale = 4;

/// How close to the ceiling a channel must read to count as clipped, as a
/// fraction of it. The same 0.97 `DevelopPipeline::apply` already hands
/// `highlights.slang`, and it must stay the same: the two nodes are the two
/// halves of one coverage — Masood et al.'s window fit over the partial case and
/// this fill over the fully blown one — and a pixel that only one of them thinks
/// is clipped is a pixel neither of them handles.
inline constexpr float kClipGamma = 0.97f;

/// How bright a pixel must be, as a fraction of the ceiling, to be evidence
/// about a highlight's color.
///
/// ⚠ This is `highlights.slang`'s `kShoulder`, at its value and for its reason:
/// it is Masood et al.'s Omega, the shoulder of the saturated region rather than
/// the general neighborhood. Around a small light at night the neighborhood is
/// almost all dark background, and a color taken from background pixels
/// describes the background. The shipping node learned that as a purple halo.
inline constexpr float kShoulder = 0.35f;

/// Restrict a premultiplied field onto a grid `scale` times coarser, by
/// box-averaging. Level zero of the pyramid, when the pyramid is solved small.
///
/// ⚠ Averaging the *premultiplied* pair is what makes this correct rather than
/// convenient: `sum(w*f)/N` over `sum(w)/N` is the mean of the known colors in
/// the block and ignores the hole entirely, so a coarse texel straddling a rim
/// carries the rim's color and a weight saying how much of it was known. An
/// un-premultiplied average would mix black into it in proportion to the hole.
inline Level restrictBy(const Level& fine, int scale) {
    Level out;
    out.width  = std::max(1, (fine.width  + scale - 1) / scale);
    out.height = std::max(1, (fine.height + scale - 1) / scale);
    out.texels.assign(std::size_t(out.width) * out.height, Sample{});

    const float norm = 1.0f / float(scale * scale);
    for (int y = 0; y < out.height; ++y) {
        for (int x = 0; x < out.width; ++x) {
            Sample acc{};
            for (int dy = 0; dy < scale; ++dy) {
                const int sy = std::min(y * scale + dy, fine.height - 1);
                for (int dx = 0; dx < scale; ++dx) {
                    const int sx = std::min(x * scale + dx, fine.width - 1);
                    const Sample& s = fine.at(sx, sy);
                    for (int k = 0; k < 3; ++k) acc.v[k] += s.v[k];
                    acc.w += s.w;
                }
            }
            for (int k = 0; k < 3; ++k) acc.v[k] *= norm;
            acc.w *= norm;
            out.at(x, y) = acc;
        }
    }
    return out;
}

/// Bilinear lift of a coarse field back onto the full grid. ⚠ Must match
/// `hl_apply.slang`'s sampling, half texel for half texel; the two are compared
/// by `testHighlightFillGpu`.
inline Level lift(const Level& coarse, int width, int height, int scale) {
    Level out;
    out.width  = width;
    out.height = height;
    out.texels.assign(std::size_t(width) * height, Sample{});

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const float fx = (float(x) + 0.5f) / float(scale) - 0.5f;
            const float fy = (float(y) + 0.5f) / float(scale) - 0.5f;
            const int   x0 = int(std::floor(fx));
            const int   y0 = int(std::floor(fy));
            const float ax = fx - float(x0);
            const float ay = fy - float(y0);

            const int xa = std::clamp(x0,     0, coarse.width  - 1);
            const int xb = std::clamp(x0 + 1, 0, coarse.width  - 1);
            const int ya = std::clamp(y0,     0, coarse.height - 1);
            const int yb = std::clamp(y0 + 1, 0, coarse.height - 1);

            const auto blend = [&](float a, float b, float c, float d) {
                const float top = a + (b - a) * ax;
                const float bot = c + (d - c) * ax;
                return top + (bot - top) * ay;
            };

            Sample& o = out.at(x, y);
            for (int k = 0; k < 3; ++k) {
                o.v[k] = blend(coarse.at(xa, ya).v[k], coarse.at(xb, ya).v[k],
                               coarse.at(xa, yb).v[k], coarse.at(xb, yb).v[k]);
            }
            o.w = blend(coarse.at(xa, ya).w, coarse.at(xb, ya).w,
                        coarse.at(xa, yb).w, coarse.at(xb, yb).w);
        }
    }
    return out;
}

/// The whole interpolant: pull to a single texel, push back down.
///
/// Returns level zero. Divide `v` by `w` for the color; `w` is 1 everywhere
/// unless the input was empty, since the coarsest level reaches every pixel.
inline Level pullPush(const Level& input) {
    std::vector<Level> chain;
    chain.push_back(input);
    const int levels = levelsFor(input.width, input.height);
    for (int l = 1; l < levels; ++l) chain.push_back(pull(chain.back()));

    for (int l = levels - 2; l >= 0; --l) {
        chain[std::size_t(l)] = push(chain[std::size_t(l)], chain[std::size_t(l) + 1]);
    }
    return chain.front();
}

/// The interpolant as the graph runs it: restrict by `scale`, solve there, lift
/// the answer back. `scale == 1` is exactly `pullPush`.
///
/// The host twin of the whole node chain — the mask node's box average, the
/// pull/push pyramid, and the apply node's bilinear read, in that order.
inline Level pullPushScaled(const Level& input, int scale) {
    if (scale <= 1) return pullPush(input);
    const Level small = pullPush(restrictBy(input, scale));
    return lift(small, input.width, input.height, scale);
}

/// The ground truth: Laplace with Dirichlet data, by Gauss-Seidel.
///
/// ⚠ Never on the render path. It is here so the shipping solver's error is a
/// measured number. Iterates until the largest single-pixel change falls below
/// `tolerance` or `maxSweeps` is spent; a hole H pixels across needs O(H^2)
/// sweeps to converge, which is exactly why the render path is a pyramid.
///
/// Red-black ordering is deliberately *not* used: this is a reference, and the
/// plainest correct loop is the one least likely to be subtly wrong.
inline Level harmonic(const Level& input, float tolerance = 1e-6f,
                      int maxSweeps = 200000) {
    Level out = input;
    const int w = input.width, h = input.height;

    // Start the unknowns at the mean of the known data. The fixed point does
    // not depend on this, only the number of sweeps to reach it.
    float mean[3] = {0.0f, 0.0f, 0.0f};
    float known   = 0.0f;
    for (const Sample& s : input.texels) {
        if (s.w > 0.0f) {
            for (int k = 0; k < 3; ++k) mean[k] += s.v[k] / s.w;
            known += 1.0f;
        }
    }
    if (known == 0.0f) return out;
    for (int k = 0; k < 3; ++k) mean[k] /= known;

    for (Sample& s : out.texels) {
        if (s.w == 0.0f) {
            for (int k = 0; k < 3; ++k) s.v[k] = mean[k];
            s.w = 1.0f;
        }
    }

    for (int sweep = 0; sweep < maxSweeps; ++sweep) {
        float worst = 0.0f;
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                if (input.at(x, y).w > 0.0f) continue;   // Dirichlet: fixed

                // Neumann at the image border, by clamping the stencil. A hole
                // that touches the edge then has no data flowing in from
                // outside the picture, which is the honest boundary condition
                // for a photograph.
                const int xm = std::max(x - 1, 0), xp = std::min(x + 1, w - 1);
                const int ym = std::max(y - 1, 0), yp = std::min(y + 1, h - 1);

                Sample& s = out.at(x, y);
                for (int k = 0; k < 3; ++k) {
                    const float n = 0.25f * (out.at(xm, y).v[k] + out.at(xp, y).v[k] +
                                             out.at(x, ym).v[k] + out.at(x, yp).v[k]);
                    worst = std::max(worst, std::fabs(n - s.v[k]));
                    s.v[k] = n;
                }
            }
        }
        if (worst < tolerance) break;
    }
    return out;
}

}  // namespace orion::pipe::hlfill
