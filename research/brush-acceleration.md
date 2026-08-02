# Brush dab rejection — hierarchical bounding volumes

**Status:** implemented 2026-07-31, `engine/shaders/mask_component.slang` kind 3.

## The problem, measured

`mask_component.slang` evaluates a brush component by looping, **at every output
pixel, over every dab in the stroke**, rejecting on a bounding square before it
computes a distance. On a 6024×4024 frame (24.2 Mpx) the cost of re-rendering
that node:

| Dabs | Node re-render | Loop alone |
|---|---|---|
| 0 | 12.0 ms | — (a radial-mask drag is 11.8 ms: same node, no loop) |
| 2 | 14.1 ms | ~2.1 ms |
| ~300 (one frame-width) | 152.3 ms | ~140 ms |
| ~2400 (eight frame-widths) | 2148.4 ms | ~2136 ms |

Linear in dab count, ~0.5–0.9 ms per dab. The cap is 16,384 dabs and the frame
budget is 16 ms.

⚠ **The cost is the fetch, not the arithmetic.** Each dab is one texture fetch
*per pixel*: 2400 dabs over 24.2 Mpx is ~58 billion fetches. Any fix has to
remove fetches, not simplify the inner test — which is why the existing
"reject on the square first" comment describes a real saving that is nonetheless
beside the point.

⚠ **The earlier note in `planning/STATUS.md` said 110–138 ms and called it
"inside the budget today".** That figure came from the bench probe's own short
stroke; measured across stroke lengths it understates the eight-frame-width case
by about 15×. Recorded here because a performance number attached to one fixture
is a number about the fixture.

## The method

Hierarchical bounding volumes, in the ordinary sense: bound groups of primitives,
test the bound once, and skip the group when the bound cannot contain a hit.

> J. H. Clark, **"Hierarchical geometric models for visible surface
> algorithms"**, *Communications of the ACM* 19(10), pp. 547–554, October 1976.
> [doi:10.1145/360349.360354](https://doi.org/10.1145/360349.360354)

Clark's §3 is the argument this uses directly: enclosing a set of primitives in a
volume, and rejecting the whole set with one test against that volume, turns a
linear scan into a scan over groups. S. M. Rubin and T. Whitted, *"A
3-dimensional representation for fast rendering of complex scenes"*, SIGGRAPH
'80, pp. 110–116, is the same idea built into a hierarchy for ray tracing and is
where the axis-aligned bounding box became the standard volume.

This is a one-level hierarchy, not a tree. A tree buys nothing here: dabs arrive
in a stream and are already spatially coherent along the stroke, so consecutive
runs give tight boxes without any partitioning step, and a tree's traversal state
would not fit the "one node, one small shader" rule.

### Applied here

Dabs are grouped into runs of **64 consecutive** entries. The host computes each
run's axis-aligned box over the dab *centres* and uploads it to a second
auxiliary texture, one RGBA32F texel per block: `(minX, minY, maxX, maxY)`. The
kernel loops over blocks, tests the box, and skips 64 fetches on a miss.

**Why 64.** The trade is the bounds-walk floor — `count/64` fetches that every
pixel pays — against the wasted inner work when a block is entered. At the
16,384 cap, 64 gives a 256-fetch floor; 16 would give 1024, which is worse than
the disease at high counts; 256 makes every hit drag in 256 dabs and lets a
curving run's box go slack. Bounds fetches are the same texel for every thread in
a warp, so they stay cache-resident exactly as the dab fetches do today.

⚠ **Consecutive runs, walked in order, inner loop in order.** That is not an
implementation convenience — paint is source-over and erase is destination-out
and **the two do not commute**, so the dabs affecting a pixel must be applied in
the order they were laid. Grouping by index range preserves that for free, which
is the reason this is a block scheme and not a per-tile binning: a tile binner
has to re-establish order per tile, which costs a stable counting scatter or a
sort — three kernels and a prefix sum inside a static graph.

## ⚠ Why the rejection is bit-identical, and not merely close

The change must not alter one pixel, because that is the invariant the test
asserts. The subtlety is that the obvious implementation breaks it.

**The wrong way** is to expand each box by the nib radius on the host, in pixel
space, and test containment. `fl(q·W) − fl(c·W)` is not `fl((q−c)·W)` — floating
point does not distribute — so an expanded-box test is a *different computation*
from the per-dab test and would disagree on some pixel near some rim.

**The right way** is to test the unexpanded centre bounds on the GPU in the
identical expression shape the per-dab test uses:

```
per dab:    skip if abs((q.x - c.x)    * W) > r
per block:  skip if     (bmin.x - q.x) * W  > r   ||   (q.x - bmax.x) * W > r
```

For any dab in the block, `c.x ≥ bmin.x`. Floating-point subtraction is
correctly rounded and therefore monotone, so `fl(bmin.x − q.x) ≤ fl(c.x − q.x)`;
multiplying by a positive `W` is monotone; negation is exact. So whenever the
block test fires, every dab in it satisfies the per-dab test and takes the
existing `continue` — which performs **no floating-point operation on `own`**.
Skipping the block therefore executes exactly the instruction stream on `own`
that the full loop would have. The comparison is strict `>` on both sides,
matching.

A false positive — a pixel inside the box that misses every dab in it — costs
time and nothing else, because the inner per-dab tests still run.

⚠ **The bounds must be computed from the float32 values actually uploaded**, not
from the double-precision originals before the geometry transform. A box built
from higher-precision inputs can round to a tighter bound than the stored centre,
and the monotonicity argument above then does not hold.

### ⚠ One mutation survives, and it is not a defect

Changing the block test's `>` to `>=` passes every check. That is worth stating
rather than hiding, and the reason it passes is an argument rather than a gap in
the fixture.

At exact equality the block's nearest dab sits exactly `r` away in x. Every dab
in the block therefore has `|e.x| ≥ r`, so `d = length(e)/r ≥ 1`, so every one of
them takes the existing `d >= 1.0f` continue and composites nothing. Skipping the
block and walking it produce the same coverage.

`>` is kept anyway, because that argument leans on `sqrt` at the boundary: for a
radius where `fl(sqrt(fl(r²)))` lands a hair below `r`, `d` is a hair below 1 and
the dab contributes a value of order 1e-8 — which vanishes in R16F, but "vanishes
in practice" is a weaker claim than "cannot happen". The strict comparison needs
no such argument, so it is the one in the code.

## What it is worth, and what defeats it

At ~300 dabs a stroke touches roughly one block for most pixels, so the loop
should fall from ~140 ms to the order of 10 ms. At the 16,384 cap the 256-fetch
floor dominates and the result is an undo hitch rather than a frame rate.

⚠ **A frame-filling scribble defeats it entirely.** Every block's box spans the
frame, no reject ever fires, and the cost returns to today's plus about 1.6% for
the bounds walk. That is the honest worst case and it is not fixable by a tighter
volume — it is fixable by not re-evaluating the whole stroke, which is the
separate change costed in `planning/ROADMAP.md`.

## The predicate, built 2026-08-01 — `params::unchangedPrefix`

Session one of the incremental accumulator. **Nothing reads its answer yet**, and
that is the design: its failure mode is a stale coverage rendering a completely
plausible brushstroke, so it is built and attacked while nothing depends on
trusting it. Decision #102; `planning/ROADMAP.md` carries the two-session split.

It answers one question — *how many leading dabs of this stroke are the ones
already on the GPU* — by keeping the texels of the previous upload and comparing
them dab for dab.

| Property | Why it is that and not the cheaper thing |
|---|---|
| Walks the dabs | "the count did not shrink, so the prefix held" is wrong on **undo three, paint three different**: same count, different prefix, and the picture that renders is a brushstroke, just not the photographer's |
| Compares the **post-transform** texels | a crop, a straighten or a quarter turn moves every centre through `mask::toFrame` while the stored stroke and its revision sit untouched. Same reason `buildDabBounds` takes the texels |
| `memcmp`, not `==` | the claim is that identical bits give identical coverage, which needs no argument about `-0.0f == 0.0f` or `NaN != NaN` |
| All four floats a dab | the erase flag rides in `z`, and source-over and destination-out **do not commute** — a dab that changed from one to the other invalidates every dab after it |
| Rejects on nib, flow, hardness, kind | one radius covers the whole stroke, so widening the nib re-lays every dab already down while every centre stays where it was |

⚠ **The check that the fast path was *taken*.** A predicate that answers 0 forever
is correct and useless, and would make session two a no-op that passes
everything. `testBrushPrefixWiring` asserts 80 of 160 dabs are already on the GPU
after an append — and asserts, through a counter, that the predicate is what
produced that number rather than an answer left over from an earlier event.
`apply` skips a component whose edit did not change, so a forgotten
`brushRevision` bump would leave a stale prefix reading exactly like a fast path.

## ⚠ What this does *not* fix

Painting appends dabs, and every appended dab re-runs the loop over all dabs laid
so far. Rejection makes each of those passes cheaper; it does not make them
fewer. The fix for that is an incremental accumulator — keep the coverage and
composite only the new dabs — which is a **stateful** change: a persistent R32F
texture per brush component (~97 MB at 24 Mpx) and a host predicate deciding when
the prefix is unchanged. Its failure mode is a stale accumulator producing a
completely plausible brushstroke, so it is costed in the roadmap rather than
bolted on here.

## ⚠ The cost is box *area*, not dab count — and one fixture hid that

Added 2026-08-01, after a measurement that appeared to contradict this file.

`orion-bench`'s brush probe reported `mask:0` at **29.00 ms for 60 dabs and
21.96 ms for 960** and that was read as "sixteen times the dabs, no more
expensive — the rejection works, so the mask kernel is not what makes painting
linear". It is the wrong conclusion, and the fault is the fixture.

What the kernel pays is not the dab count. It is

    Σ over blocks of  (pixels inside that block's box) × 64 dab fetches

and the probe grew the dab count by **subdividing a stroke of fixed extent** —
the same sine wave, sampled sixteen times as finely. That multiplies the block
count by sixteen and divides each box's area by sixteen. The product is
invariant, so the probe measured a constant and would have measured one for any
block size, any nib and any frame.

No hand makes that stroke. Dab spacing is fixed by the nib, so **appending is the
only way a stroke grows**: more dabs is a longer path over more of the picture,
and each new block of 64 arrives with a box the same size as the last. The block
count grows, the boxes do not shrink, and the product is linear.

Measured on `_PIC8220.ARW` with both shapes in one process, interleaved
(`apps/bench/main.cpp`, "Brush refined" against "Brush appended"):

| Stroke shape | `mask:0`, full graph | `mask:0`, preview graph |
|---|---|---|
| refined, 60 → 960 dabs (fixed extent) | 24.16 → 19.35 ms | 1.54 → 1.31 ms |
| appended, 49 → 294 dabs (49 a line, 6 lines) | 2.65 → **34.88 ms** | 0.17 → **2.23 ms** |

Six times the dabs is thirteen times the cost, on **both** graphs. Resolution has
nothing to do with it: the preview is 1/16 the pixels and 1/16 the milliseconds,
with the same slope. The host side is flat and negligible beside it —
`setBrushStroke` ×2 is 0.001 ms and `apply` ×2 is 0.057 ms at either length.

### The third term the "why 64" trade did not have

`64` was chosen against the bounds-walk floor and against a box going slack on a
curving run. There is a third term, and it only appears once a component holds
**more than one stroke**: 64 dabs is more than one stroke, so a block straddles a
pen-up and its box spans the **empty gap between two strokes**.

Six strokes across the frame, 49 dabs each, moved apart while the dab count, the
block count and the painted area are all held identical — the block boxes are the
only thing that changes:

| Gap between strokes | ms per pointer event at 294 dabs |
|---|---|
| 0 (all six retraced on one line) | 0.4 |
| 0.02 frame heights | 0.6 |
| 0.10 | 0.8 |
| 0.15 | 0.9 |

Monotone in the gap, and the ratios track the box height `(gap·H + 2r) / 2r`
within the resolution of the timer. So the frame-filling scribble above is not
the only case that defeats the boxes: **two strokes far apart in one component do
too, for 64 dabs either side of the boundary.**

This is recorded rather than acted on. Padding each stroke out to a block
boundary would tighten the boxes, but it spends up to 63 of the 16,384 dab slots
per stroke, needs a third meaning for the dab's `z` channel, and buys a constant
factor on a cost that is still linear. Not re-evaluating the stroke at all is the
fix, and it is the roadmap's.
