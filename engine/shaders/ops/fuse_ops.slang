// Simulated Exposure Fusion — the point-wise maths, shared by every kernel that
// needs it.
//
// Hessel, "Simulated Exposure Fusion", IPOL 9 (2019) 469-484, equations 3, 4, 5,
// 9 and 10. Kept in one file because the simulated image and its weight are
// evaluated in different kernels and must agree exactly; the C++ mirror is
// pipe/ExposureFusion.h, and orion-tests checks the two against each other.

struct FusePlan
{
    int   images;     // M
    int   bright;     // N
    int   dark;       // N*
    int   span;       // Nmax = max(N, N*)
    float alpha;
    float beta;
    float lambda;
    float sigma;
};

/// [H279] Eq. (4).
float fuseRho(int k, FusePlan p)
{
    const int total = max(p.bright + p.dark, 1);
    return 1.0f - p.beta * 0.5f - float(k + p.dark) * (1.0f - p.beta) / float(total);
}

float fuseFactor(int k, FusePlan p)
{
    return pow(p.alpha, float(abs(k)) / float(max(p.span, 1)));
}

/// [H279] Eq. (3). Both branches are >= 1 — the dark end is reached by shifting
/// intensities toward the bottom, not by simulating a shorter exposure.
float fuseSimulate(float t, int k, FusePlan p)
{
    const float a = fuseFactor(k, p);
    return (k < 0) ? a * (t - 1.0f) + 1.0f : a * t;
}

/// [H279] Eq. (5). Identity inside the restrained range, 1/x^2 decay outside.
float fuseClip(float t, float center, FusePlan p)
{
    const float d = t - center;
    const float m = abs(d);
    if (m <= p.beta * 0.5f) return t;

    const float a = p.beta * 0.5f + p.lambda;
    const float b = p.beta * 0.5f - p.lambda;
    return sign(d) * (a - p.lambda * p.lambda / (m - b)) + center;
}

/// [H279] Eq. (10) — the derivative of fuseClip, which *is* the contrast
/// measure. Hessel & Morel replace Mertens' Laplacian filter with it, so there
/// is no filtering pass per simulated image at all.
float fuseClipSlope(float t, float center, FusePlan p)
{
    const float m = abs(t - center);
    if (m <= p.beta * 0.5f) return 1.0f;

    const float b = p.beta * 0.5f - p.lambda;
    const float d = m - b;
    return p.lambda * p.lambda / (d * d);
}

/// The simulated image k at intensity t.
float fuseRemap(float t, int k, FusePlan p)
{
    return fuseClip(fuseSimulate(t, k, p), fuseRho(k, p), p);
}

/// [H279] Eq. (9). From the IPOL text: the WACV camera-ready prints these
/// branch conditions swapped, contradicting its own earlier equation.
float fuseContrast(float t, int k, FusePlan p)
{
    return fuseFactor(k, p) * fuseClipSlope(fuseSimulate(t, k, p), fuseRho(k, p), p);
}

/// Mertens et al. section 3.1, on one channel.
float fuseWellExposed(float t, FusePlan p)
{
    const float d = t - 0.5f;
    return exp(-(d * d) / (2.0f * p.sigma * p.sigma));
}

/// The unnormalized weight of image k.
float fuseWeight(float t, int k, FusePlan p)
{
    return fuseWellExposed(fuseRemap(t, k, p), p) * fuseContrast(t, k, p);
}
