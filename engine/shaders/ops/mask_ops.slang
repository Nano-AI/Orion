// Shared mask maths — the falloff, and how two coverages combine.
//
// research/masking.md §1 (falloff) and §6 (combining). Both live here because
// both are used by more than one caller and a second copy of either is a second
// place to fix a feather or a seam.
//
// The falloff *was* copied into mask_gradient.slang and mask_brush.slang, with a
// comment in each claiming it was shared. It was not: they were two identical
// functions under two names, which is exactly the drift this file prevents.

#ifndef ORION_MASK_OPS_SLANG
#define ORION_MASK_OPS_SLANG

/// Perlin's smootherstep, 6t^5 - 15t^4 + 10t^3.
///
/// C² where smoothstep is only C¹, and the difference is visible: smoothstep
/// leaves a faint Mach band at the feather boundary on a clear sky, because the
/// eye finds discontinuities in the second derivative. Perlin, "Improving
/// Noise", ACM TOG 21(3), 2002, where it is introduced as the improved
/// interpolant for exactly this reason.
float maskFalloff(float t)
{
    const float x = saturate(t);
    return x * x * x * (x * (x * 6.0f - 15.0f) + 10.0f);
}

// How a component folds into the coverage accumulated so far.
// research/masking.md §6. Components fold left in listed order, so add and
// subtract are order-sensitive as a sequence — which is deterministic, cheap,
// and what a photographer means by a list.
static const int kMaskAdd       = 0;
static const int kMaskSubtract  = 1;
static const int kMaskIntersect = 2;

/// Folds one component's coverage into the running coverage.
///
/// `prev` is what the components before this one produced; `own` is this
/// component's own alpha. The fold starts from zero — the additive identity —
/// so the first component in a group needs no special case at all: `add` from
/// zero is `max(0, own)`, which is `own`.
///
/// **Add is max, not screen.** research/masking.md §6 lists both, and the
/// difference is the whole point of the section's first sentence: never apply
/// the same +1 stop twice because two masks overlap. `max` has no buildup in
/// the overlap; screen (α₁ + α₂ − α₁α₂) does, so two strokes crossing at half
/// coverage would read as three-quarters there and the edit would visibly
/// strengthen along the seam.
float maskCompose(float prev, float own, int op)
{
    if (op == kMaskSubtract)  return prev * (1.0f - own);
    if (op == kMaskIntersect) return prev * own;
    return max(prev, own);   // kMaskAdd
}

#endif  // ORION_MASK_OPS_SLANG
