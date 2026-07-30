# Spot Removal — dust and blemishes

**Scope, from `ROADMAP.md` M4: sensor dust and blemishes, not Photoshop-grade
healing.** That is not modesty, it is the design constraint. Dust sits on smooth
backgrounds — sky, a wall, out-of-focus foliage — and the case that makes
healing hard, a blemish straddling a strong edge, is the case this deliberately
does not solve. What it must not do is pretend otherwise.

---

## 1. Clone and heal are different operations

**Clone** copies pixels from elsewhere in the frame. It is exact and it is
visible: unless the source's brightness happens to match the destination's, the
patch shows as a disc, because the eye finds a discontinuity in a smooth
gradient at a contrast far below what it finds in texture.

**Heal** copies the source's *detail* and takes the destination's *tone*. On a
vignetted sky — where the brightness varies smoothly across the whole frame and
no two places match — that is the difference between invisible and obvious.

Both are wanted. Clone is right when the destination has structure worth
replacing outright.

## 2. The published framework, and why it is not built as published

**Pérez, Gangnet & Blake, "Poisson Image Editing", ACM TOG 22(3):313–318,
SIGGRAPH 2003.** The canonical formulation: paste the source's *gradient* field
and solve for the image whose gradients match it subject to the destination's
values on the boundary. Healing is exactly this with a disc-shaped region.

⚠ **It requires a sparse linear solve**, and this codebase has already refused
one for the same reason in `masking.md` §4 — a solver is not one node and one
small shader, and it is not a thing a solo maintainer can hand-edit later. The
refusal is consistent, not new.

**Farbman, Hoffer, Lipman, Cohen-Or & Lischinski, "Coordinates for Instant Image
Cloning", ACM TOG 28(3), SIGGRAPH 2009** is the published answer to that
objection. Mean-value coordinates give the Poisson result's *interpolant* in
closed form: the correction at an interior point is a weighted average of the
boundary differences, with weights that are a known function of geometry. No
solve, O(boundary) per pixel, and the paper's whole argument is that it is
visually indistinguishable from the solve for cloning.

## 3. What Orion builds: the constant term, and why that is enough here

Orion evaluates only the **zeroth-order term** of that interpolant — the mean of
the boundary difference, applied uniformly inside the disc:

```
correction = mean over the boundary of ( destination − source )
out(p)     = source(p + offset) + correction        // heal
out(p)     = source(p + offset)                     // clone
```

⚠ **This is a truncation of a published method, not a published method.** It is
recorded as Orion's own in `UNSOURCED.md` §21 with the reasoning, and the
reasoning is this: the mean-value interpolant reduces to exactly this constant
whenever the boundary difference is itself constant, and on the smooth
backgrounds dust lands on it very nearly is. What the higher-order terms buy is
the case where the boundary difference *varies* — a spot half on a sky and half
on a roofline — which is the case the scope excludes.

**The failure mode is therefore known and bounded**, which is the whole point of
truncating deliberately rather than by accident: place a spot across a strong
edge and the correction is wrong on both sides by half the edge's contrast. The
interface should not hide that, and the upgrade path is Farbman's full
interpolant, not a solver.

## 4. Working space

**In scene-linear light, before the tone controls.** A correction that is
additive in linear light is additive in the physical quantity the sensor
measured; the same offset applied after AgX would be a different correction to a
highlight than to a midtone, for reasons unrelated to the dust. This is the same
argument `masking.md` §2 makes for where a mask's alpha is applied, and NVIDIA's
*GPU Gems 3* ch. 24 "The Importance of Being Linear" is the general statement.

⚠ **After the lens correction, not before it** — and the first draft of this
section said the opposite, on the reasoning that a spot should sit upstream of
anything that moves pixels underneath it. Checking the graph settles it the
other way.

Lens correction is the one stage in the chain that *warps* rather than acting
pointwise. Put the node before it and a spot placed on screen has to be carried
back through the distortion polynomial, inverted, per spot. Put it after and the
spot lives in exactly the space masks already live in — `mask::toFrame` maps the
displayed picture to it, that transform is shared, tested and already handles
the crop, the straighten and the quarter turns, and everything between lens and
`develop:linear` is pointwise so none of it moves the spot.

The cost of choosing after is that changing the lens profile moves the content
under a fixed spot. That is the same trade a crop makes under a mask, it is a
setting touched once per lens rather than dragged, and it buys one shared
transform instead of two.

Placed between lens and sharpening, so the healed patch is sharpened along with
its surroundings rather than pasted in already sharpened.

## 5. Finding dust automatically is a separate problem

Not built, and worth naming so it is not confused with this. Automatic dust
detection is a *detection* problem — Zhou & Lin, "Removal of Image Artifacts Due
to Sensor Dust", CVPR 2007, models dust as a multiplicative attenuation and
recovers it from a set of frames sharing the same sensor. That is the right
reference if it is ever wanted, and it needs several frames from one body, which
is a library-level feature rather than an editing one.

## 6. What would change the plan

- Spots placed across edges becoming a common complaint → Farbman's full
  mean-value interpolant, which is the same node with a boundary loop instead of
  a boundary mean.
- Wanting content-aware fill rather than a chosen source → that is PatchMatch,
  which is Adobe-patented and already ruled out elsewhere in this repository.
