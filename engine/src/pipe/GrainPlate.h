#pragma once

// The film grain plate — research/film-grain.md, decision #81.
//
// A precomputed field of correlated noise. AV1's architecture (Norkin &
// Birkbeck, DCC 2018) carrying the Boolean model's statistics (Newson, Delon &
// Galerne, CGF 36(8), 2017): synthesise the correlated field once, then apply it
// per pixel with an intensity-dependent scale. The alternative — evaluating the
// disc process per pixel — is orders of magnitude outside a 16 ms budget at
// 24 Mpx.
//
// ⚠ **Every number here has to be reproducible on any machine, or export stops
// matching preview.** So the generator is written out rather than borrowed:
//
//   - PCG32 (O'Neill, HMC-CS-2014-0905, 2014) instead of `std::mt19937`, and
//     Box–Muller (Box & Muller, Ann. Math. Stat. 29(2), 1958) instead of
//     `std::normal_distribution` — the latter's *algorithm* is
//     implementation-defined, so libc++ and libstdc++ give different fields
//     from the same seed.
//   - The mip chain is box-filtered here, not by `generateMipmaps`, whose
//     filter is unspecified.
//
// ⚠ **The chain is stacked vertically into one 2D texture** rather than using
// real mip levels, because the aux-texture API has none and adding them would
// be a change to the GPU layer for no gain: the shader has to filter by hand
// anyway (a hardware sampler's precision is not specified across GPU families),
// and a closed-form row offset is less machinery than a mip descriptor.

#include <cmath>
#include <cstdint>
#include <vector>

namespace orion::pipe::grain {

/// Level 0 is this square. 2048 is enough that the wrap is invisible across a
/// 6024-pixel frame at any grain size the Size slider offers.
inline constexpr int kPlateSize = 2048;

/// Levels 0..11, the last being a single texel.
inline constexpr int kPlateLevels = 12;

/// Stacked height. `levelOffset(11) + 1 == 4095`, so 4096 holds the chain with
/// a row to spare.
inline constexpr int kPlateHeight = 4096;

/// First row of level `l` in the stacked texture.
///
/// Closed form of `sum of kPlateSize >> k, k < l`: the partial sum of a
/// geometric series. Level 0 starts at 0, level 1 at 2048, level 2 at 3072.
/// ⚠ The shader computes this the same way, from the same expression. Two
/// derivations of one offset is how a level gets read from the wrong rows.
inline constexpr int levelOffset(int l) {
    return kPlateSize * 2 - ((kPlateSize * 2) >> l);
}

/// PCG32. O'Neill (2014), the `pcg32_random_r` variant, written out so the
/// stream cannot change under us.
struct Pcg32 {
    std::uint64_t state = 0x853c49e6748fea9bULL;
    std::uint64_t inc   = 0xda3e39cb94b95bdbULL;

    explicit Pcg32(std::uint64_t seed) {
        state = 0;
        inc = (seed << 1u) | 1u;
        next();
        state += 0x853c49e6748fea9bULL;
        next();
    }

    std::uint32_t next() {
        const std::uint64_t old = state;
        state = old * 6364136223846793005ULL + inc;
        const std::uint32_t xorshifted =
            static_cast<std::uint32_t>(((old >> 18u) ^ old) >> 27u);
        const std::uint32_t rot = static_cast<std::uint32_t>(old >> 59u);
        return (xorshifted >> rot) | (xorshifted << ((~rot + 1u) & 31u));
    }

    /// Uniform in (0, 1]. ⚠ Never zero: Box–Muller takes its logarithm.
    double uniform() {
        return (static_cast<double>(next()) + 1.0) * (1.0 / 4294967296.0);
    }
};

/// The whole stacked chain, `kPlateSize * kPlateHeight` floats, row-major.
///
/// Level 0 is unit-variance correlated noise; each level below is the box
/// average of the one above.
///
/// ⚠ **The lower levels are deliberately not renormalised.** Their standard
/// deviation falls as the field is averaged, and that fall *is* the physics the
/// preview needs: a preview pixel covering sixteen frame pixels should show the
/// variance sixteen frame pixels average to, not the variance of one. Scaling
/// each level back to unit std would make the 1/16 preview look exactly as
/// grainy as the full render, which is the bug the whole plate exists to avoid.
inline std::vector<float> buildPlate(std::uint64_t seed = 0x9e3779b97f4a7c15ULL) {
    std::vector<float> plate(static_cast<std::size_t>(kPlateSize) * kPlateHeight, 0.0f);

    // ── Level 0: white Gaussian, then band-limited so the grain has a size ──
    //
    // Uncorrelated per-pixel noise reads as a digital sensor. Grain's identity
    // is that it clumps, so the white field is blurred and renormalised; the
    // blur radius is what "one grain" means, and the Size slider scales the
    // plate's sampling rather than regenerating it.
    std::vector<float> white(static_cast<std::size_t>(kPlateSize) * kPlateSize);
    Pcg32 rng(seed);
    for (std::size_t i = 0; i < white.size(); i += 2) {
        const double u1 = rng.uniform(), u2 = rng.uniform();
        const double r = std::sqrt(-2.0 * std::log(u1));
        const double theta = 6.283185307179586476925286766559 * u2;
        white[i] = static_cast<float>(r * std::cos(theta));
        if (i + 1 < white.size()) white[i + 1] = static_cast<float>(r * std::sin(theta));
    }

    // Separable Gaussian, sigma 0.8, five taps, wrapped. Wrapping rather than
    // clamping keeps the plate tileable, which is what lets a 2048 square cover
    // a 6024-pixel frame without a seam.
    constexpr int kRadius = 2;
    const double sigma = 0.8;
    double k[2 * kRadius + 1];
    double ksum = 0.0;
    for (int t = -kRadius; t <= kRadius; ++t) {
        k[t + kRadius] = std::exp(-0.5 * (t * t) / (sigma * sigma));
        ksum += k[t + kRadius];
    }
    for (double& v : k) v /= ksum;

    std::vector<float> tmp(white.size());
    const auto wrap = [](int v) { return (v & (kPlateSize - 1)); };
    for (int y = 0; y < kPlateSize; ++y) {
        for (int x = 0; x < kPlateSize; ++x) {
            double acc = 0.0;
            for (int t = -kRadius; t <= kRadius; ++t)
                acc += k[t + kRadius] * white[static_cast<std::size_t>(y) * kPlateSize + wrap(x + t)];
            tmp[static_cast<std::size_t>(y) * kPlateSize + x] = static_cast<float>(acc);
        }
    }
    std::vector<float> level0(white.size());
    for (int y = 0; y < kPlateSize; ++y) {
        for (int x = 0; x < kPlateSize; ++x) {
            double acc = 0.0;
            for (int t = -kRadius; t <= kRadius; ++t)
                acc += k[t + kRadius] * tmp[static_cast<std::size_t>(wrap(y + t)) * kPlateSize + x];
            level0[static_cast<std::size_t>(y) * kPlateSize + x] = static_cast<float>(acc);
        }
    }

    // Renormalise level 0 to zero mean and unit standard deviation, so `amount`
    // means the same thing whatever the blur above happens to do to the
    // variance.
    double mean = 0.0;
    for (float v : level0) mean += v;
    mean /= static_cast<double>(level0.size());
    double var = 0.0;
    for (float v : level0) { const double d = v - mean; var += d * d; }
    var /= static_cast<double>(level0.size());
    const double inv = var > 0.0 ? 1.0 / std::sqrt(var) : 1.0;
    for (std::size_t i = 0; i < level0.size(); ++i)
        plate[i] = static_cast<float>((level0[i] - mean) * inv);

    // ── The chain: each level the 2x2 box average of the one above ──────────
    for (int l = 1; l < kPlateLevels; ++l) {
        const int src = kPlateSize >> (l - 1);
        const int dst = kPlateSize >> l;
        const std::size_t srcRow = static_cast<std::size_t>(levelOffset(l - 1));
        const std::size_t dstRow = static_cast<std::size_t>(levelOffset(l));
        for (int y = 0; y < dst; ++y) {
            for (int x = 0; x < dst; ++x) {
                const std::size_t a = (srcRow + y * 2) * kPlateSize + x * 2;
                const std::size_t b = a + 1;
                const std::size_t c = (srcRow + y * 2 + 1) * kPlateSize + x * 2;
                const std::size_t d = c + 1;
                plate[(dstRow + y) * kPlateSize + x] =
                    0.25f * (plate[a] + plate[b] + plate[c] + plate[d]);
                (void)src;
            }
        }
    }
    return plate;
}

}  // namespace orion::pipe::grain
