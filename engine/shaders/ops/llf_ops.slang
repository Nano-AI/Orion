// Local Laplacian filters — the shared point-wise maths.
//
// Paris, Hasinoff & Kautz, "Local Laplacian Filters", SIGGRAPH 2011, and the
// fast approximation of Aubry, Paris, Hasinoff, Kautz & Durand, ACM TOG 33(5),
// 2014. research/local-laplacian.md carries the reasoning; this file carries
// the equations, so that the remapping used to build the pyramids and the one
// used to rebuild level zero on the fly are provably the same function.

// The Burt & Adelson (1983) 5-tap generating kernel, which is the pyramid the
// 2011 paper says it uses (§5.1).
static const float kBurt[5] = { 0.0625f, 0.25f, 0.375f, 0.25f, 0.0625f };

/// Paris et al. Eq. 1 and Eq. 2, specialized to detail manipulation (§5.2):
/// fd(D) = D^alpha, and fe(a) = a — the identity, so edges pass through
/// untouched and this is a detail control rather than a tone control.
///
/// With fe the identity, Eq. 2 collapses to r(i) = i exactly, which is why
/// there is no second branch below: beyond sigmaR the function *is* identity.
///
/// `noiseLo`/`noiseHi` are §5.2's "Reducing Noise Amplification": when
/// alpha < 1 the power curve has unbounded slope at the origin, so the
/// smallest-amplitude signal in the frame — the noise — receives the largest
/// gain of anything in the picture. The paper's answer is to blend back to the
/// identity below a threshold, and it states every result in the paper was
/// computed with it. It is part of the filter, not an optimization.
float llfRemap(float i, float g, float sigmaR, float alpha,
               float noiseLo, float noiseHi)
{
    const float d = i - g;
    const float a = abs(d);
    if (a > sigmaR) return i;          // re with fe = identity

    const float dn = a / max(sigmaR, 1e-8f);   // in [0, 1]
    float fd = pow(max(dn, 1e-8f), alpha);

    if (alpha < 1.0f)
    {
        // tau is measured on the un-normalized amplitude, because "1% of the
        // maximum intensity" is a statement about the image, not about sigmaR.
        const float tau = smoothstep(noiseLo, noiseHi, a);
        fd = tau * fd + (1.0f - tau) * dn;
    }

    return g + sign(d) * sigmaR * fd;
}

/// Which of the eight gamma levels bracket `v`, and how far between them it
/// sits. Aubry et al. step 4(ii): g = (1 - a)*gamma_j + a*gamma_{j+1}.
void llfBracket(float v, float gammaStep, int count, out int j, out float a)
{
    const float t = clamp(v / max(gammaStep, 1e-8f), 0.0f, float(count - 1));
    j = min(int(floor(t)), count - 2);
    a = t - float(j);
}

/// Component `j` of a gamma stack packed four to a texture.
float llfPick(int j, float4 s0, float4 s1, float4 s2, float4 s3)
{
    float4 v = s0;
    const int stack = j >> 2;
    if (stack == 1) v = s1;
    else if (stack == 2) v = s2;
    else if (stack == 3) v = s3;

    const int k = j & 3;
    if (k == 0) return v.x;
    if (k == 1) return v.y;
    if (k == 2) return v.z;
    return v.w;
}

/// Burt & Adelson expand by two, separably. Derived rather than approximated:
/// out(x) = 2 * sum_m w(m) * G((x - m)/2) over integer (x - m)/2 splits by
/// parity into (1/8, 6/8, 1/8) on even samples and (1/2, 1/2) on odd ones.
/// Both sum to one, so a constant expands to itself — which is the property
/// that stops the collapse from drifting the image's brightness.
///
/// Any expand operator reconstructs exactly as long as analysis and synthesis
/// use the same one; this is the one the paper's pyramid implies.
void llfExpand1D(int x, out int3 c, out float3 w)
{
    const int h = x >> 1;
    if ((x & 1) == 0)
    {
        c = int3(h - 1, h, h + 1);
        w = float3(0.125f, 0.75f, 0.125f);
        return;
    }
    // The odd case has only two contributing samples; the third carries zero
    // weight so both branches can share one loop.
    c = int3(h, h + 1, h + 1);
    w = float3(0.5f, 0.5f, 0.0f);
}
