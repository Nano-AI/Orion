// Highlight reconstruction — cross-channel correlation.
//
// Reference: Masood, Zhu & Tang (2009), "Automatic Correction of Saturated
// Regions in Photographs using Cross-Channel Correlation", Computer Graphics
// Forum 28(7), 1861-1869. Restated in research/deep-research-2026-07-27.md §1.
//
// A sensor clips per channel, not per pixel. Point a camera at a bright sky and
// the red channel saturates first while green and blue still hold detail — so
// the pixel's *ratio* between channels changes, and a white cloud comes out
// magenta. The correction is to notice that within a small neighbourhood the
// channels are close to linearly related, fit that relation on the pixels that
// are still valid, and use it to say what the clipped channel would have read:
//
//     C_c(p) = α·C_u(p) + β        fitted by ordinary least squares over Ω
//
// with C_u the unclipped reference and Ω the valid pixels nearby.
//
// The threshold is per channel and lives after white balance, which is the
// point the research file marks as the part that must not be got wrong: white
// balance scales red and blue *upward* relative to green, so their clipping
// levels routinely exceed 1.0. Testing against a single level after white
// balance is itself a source of false color.

struct HighlightParams
{
    uint2 size;
    float clipR, clipG;    // per-channel saturation, post-white-balance
    float clipB, gamma;    // gamma in 0.95..0.99: how close to clip counts
    float strength;        // 0 disables
    float _pad;
};

/// Window over which the linear model is fitted. Masood et al. use 7-15; a
/// stride keeps the tap count affordable while still reaching that far, and
/// the fit only needs the *distribution* of nearby valid pixels, not every one.
static const int kRadius = 12;
static const int kStride = 3;

/// Below this many valid pixels the fit says more about luck than about the
/// scene, and a bad fit invents color rather than recovering it.
static const float kMinSamples = 8.0f;

/// A reconstructed channel is allowed this much headroom above its clipping
/// level. Real highlight detail lives within a stop or two; beyond that a
/// runaway fit is the likelier explanation.
static const float kMaxGain = 2.0f;

/// How bright a valid pixel must be, as a fraction of where it would clip, to
/// count toward the fit.
///
/// This is Masood et al.'s Ω: the *boundary* of the saturated region, not the
/// general neighbourhood. Around a small light at night the neighbourhood is
/// almost all dark background, and a relation fitted on background pixels
/// describes the background — extrapolating it out to highlight brightness
/// produced a colored halo round every light in the frame.
static const float kShoulder = 0.35f;

/// How far past the brightest pixel that informed the fit the estimate may
/// reach. A linear model is only evidence over the range it was fitted on.
static const float kMaxExtrapolation = 1.6f;

[shader("compute")]
[numthreads(16, 16, 1)]
void highlightRecover(uint3 tid : SV_DispatchThreadID,
                      uniform Texture2D<float4>   src,
                      uniform RWTexture2D<float4> dst,
                      uniform ConstantBuffer<HighlightParams> p)
{
    if (tid.x >= p.size.x || tid.y >= p.size.y) return;

    const float3 clip = float3(p.clipR, p.clipG, p.clipB);
    const float3 limit = clip * p.gamma;

    const float4 source = src[tid.xy];
    float3 c = source.rgb;

    const bool3 clipped = bool3(c.r >= limit.r, c.g >= limit.g, c.b >= limit.b);
    const int count = (clipped.x ? 1 : 0) + (clipped.y ? 1 : 0) + (clipped.z ? 1 : 0);

    if (p.strength <= 0.0f || count == 0)
    {
        dst[tid.xy] = source;
        return;
    }

    if (count == 3)
    {
        // Nothing left to correlate against. A fully blown highlight is white:
        // inventing a hue for it is worse than declining to.
        const float m = max(c.r, max(c.g, c.b));
        dst[tid.xy] = float4(lerp(c, float3(m, m, m), p.strength), source.a);
        return;
    }

    // The reference is the mean of whatever is still valid. Averaging two valid
    // channels rather than picking one is steadier: the fit then rests on twice
    // the signal, and neither channel's own noise steers it alone.
    float reference = 0.0f;
    float referenceCount = 0.0f;
    if (!clipped.x) { reference += c.r; referenceCount += 1.0f; }
    if (!clipped.y) { reference += c.g; referenceCount += 1.0f; }
    if (!clipped.z) { reference += c.b; referenceCount += 1.0f; }
    reference /= referenceCount;

    // Where the reference itself would clip, so "bright" can be judged against
    // the same combination of channels the reference is made of.
    float referenceLimit = 0.0f;
    if (!clipped.x) referenceLimit += limit.r;
    if (!clipped.y) referenceLimit += limit.g;
    if (!clipped.z) referenceLimit += limit.b;
    referenceLimit /= referenceCount;

    // Ordinary least squares over the valid pixels of the window. The same
    // channel combination defines the reference there as here, or the two sides
    // of the fit would not be measuring the same quantity.
    float n = 0.0f, su = 0.0f, suu = 0.0f;
    float maxU = 0.0f;
    float3 sc = float3(0.0f, 0.0f, 0.0f);
    float3 suc = float3(0.0f, 0.0f, 0.0f);
    float3 maxRatio = float3(0.0f, 0.0f, 0.0f);

    for (int dy = -kRadius; dy <= kRadius; dy += kStride)
    {
        for (int dx = -kRadius; dx <= kRadius; dx += kStride)
        {
            const int2 q = int2(tid.xy) + int2(dx, dy);
            if (q.x < 0 || q.y < 0 || q.x >= int(p.size.x) || q.y >= int(p.size.y))
            {
                continue;
            }

            const float3 v = src[q].rgb;
            // Only wholly valid pixels carry the relation between channels.
            if (v.r >= limit.r || v.g >= limit.g || v.b >= limit.b) continue;

            float u = 0.0f;
            if (!clipped.x) u += v.r;
            if (!clipped.y) u += v.g;
            if (!clipped.z) u += v.b;
            u /= referenceCount;

            // Only the shoulder of the highlight informs the fit.
            if (u < kShoulder * referenceLimit) continue;

            n += 1.0f;
            su += u;
            suu += u * u;
            sc += v;
            suc += u * v;

            maxU = max(maxU, u);
            maxRatio = max(maxRatio, v / max(u, 1e-6f));
        }
    }

    if (n < kMinSamples)
    {
        dst[tid.xy] = source;
        return;
    }

    // A linear model is evidence only over the range it was fitted on. Reaching
    // far past the brightest pixel that informed it is not recovery, it is
    // invention — and invention is what the halo was.
    if (reference > maxU * kMaxExtrapolation)
    {
        dst[tid.xy] = source;
        return;
    }

    const float denominator = n * suu - su * su;
    if (abs(denominator) < 1e-9f)
    {
        dst[tid.xy] = source;
        return;
    }

    const float3 alpha = (n * suc - su * sc) / denominator;
    const float3 beta = (sc - alpha * su) / n;
    const float3 estimate = alpha * reference + beta;

    // No channel may end up in a ratio to the reference that the neighbourhood
    // never showed. This is the guard that actually kills the halo: a fit can
    // be well conditioned and still imply a color nothing nearby exhibits.
    const float3 ceiling = min(float3(clip.r, clip.g, clip.b) * kMaxGain,
                               maxRatio * reference);

    // A clipped channel read *at least* its clipping level, so the estimate can
    // only ever raise it. Taking the maximum also makes a poor fit harmless
    // rather than actively wrong.
    float3 recovered = c;
    if (clipped.x) recovered.r = clamp(max(c.r, estimate.r), c.r, max(ceiling.r, c.r));
    if (clipped.y) recovered.g = clamp(max(c.g, estimate.g), c.g, max(ceiling.g, c.g));
    if (clipped.z) recovered.b = clamp(max(c.b, estimate.b), c.b, max(ceiling.b, c.b));

    dst[tid.xy] = float4(lerp(c, recovered, p.strength), source.a);
}
