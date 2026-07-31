// Film grain — research/film-grain.md, decisions #81 and #82.
//
// A precomputed correlated field (AV1's architecture, Norkin & Birkbeck 2018)
// weighted by the Boolean model's own variance law (Newson, Delon & Galerne,
// CGF 36(8), 2017). The plate is built on the host by `GrainPlate.h`; this
// samples it.
//
// ⚠ **An op rather than a node, and that was measured rather than assumed.**
// #81 called for one pointwise node after the display transform. Built that
// way it cost **+4.3 ms on every exposure tick** — a fourth node on the drag
// path, a second full-resolution pass, and 194 MB more of intermediates —
// which took the M0 gate from 10.42 ms to 16.01 ms and failed it. Grain is off
// by default, so that was a permanent tax for an optional look. Folded into
// `develop_display` it costs nothing when Amount is 0 and needs no extra
// texture at all. #82 records the reversal.
//
// Keeping the mathematics here rather than inline in `develop_display.slang`
// is what keeps that shader readable and this testable on its own terms.

#pragma once

/// Level 0 of the plate is this square, and the chain is stacked below it.
/// ⚠ Must equal `grain::kPlateSize` in `GrainPlate.h`, which static_asserts it.
static const int PLATE_SIZE = 2048;
static const int PLATE_LEVELS = 12;

/// First row of level `l` in the stacked plate.
///
/// ⚠ The same closed form `grain::levelOffset` uses on the host, written the
/// same way. Two derivations of one offset is how a level gets read from the
/// wrong rows — which would be a plausible field over the wrong grain size.
int grainLevelOffset(int l)
{
    return PLATE_SIZE * 2 - ((PLATE_SIZE * 2) >> l);
}

/// One bilinear tap inside a level, wrapped.
///
/// ⚠ Hand-rolled rather than a hardware sampler. Filtering precision is not
/// specified across GPU families, so a sampler could make an export differ from
/// the preview it was made on — by *device*, which is the least debuggable
/// difference there is.
float grainPlateBilinear(Texture2D<float> plate, float2 uv, int level)
{
    const int n = max(PLATE_SIZE >> level, 1);
    const int row = grainLevelOffset(level);

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

/// The grain offset to add to a display-referred pixel of luminance `y`.
///
/// `framePx` is the pixel's position in **frame** coordinates, so the field is
/// keyed to the negative rather than to the output: a crop enlarges the grain
/// the way enlarging more of a negative does, and it does not swim when the
/// preview pans. `gridStep` is frame pixels per node pixel — 1 in the full
/// graph, the decimation factor in the preview.
float grainOffset(Texture2D<float> plate, float2 framePx, float y,
                  float amount, float grainSize, float gridStep)
{
    if (amount <= 1e-6f) return 0.0f;

    const float size = max(grainSize, 1e-3f);
    const float2 uv = framePx / (float(PLATE_SIZE) * size);

    // How far down the chain this pixel's footprint reaches. One node pixel
    // covers `gridStep` frame pixels; a grain is `size` of them.
    const float lod = clamp(log2(max(gridStep / size, 1e-6f)),
                            0.0f, float(PLATE_LEVELS - 1));
    const int lo = int(floor(lod));
    const int hi = min(lo + 1, PLATE_LEVELS - 1);
    const float t = lod - float(lo);

    const float g = lerp(grainPlateBilinear(plate, uv, lo),
                         grainPlateBilinear(plate, uv, hi), t);

    // ⚠ The Boolean model's own pointwise statistics: a grain field of mean
    // coverage u has Bernoulli variance u(1-u), so it is loudest in the
    // midtones and silent at both ends. That is not taste — it is also why the
    // clamp that follows cannot turn grain into a mean shift, because where
    // clipping could bite, sigma is already zero.
    const float sigma = amount * sqrt(max(y * (1.0f - y), 0.0f));
    return sigma * g;
}
