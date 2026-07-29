# Creative LUTs — .cube, and tetrahedral interpolation

M3's third story. Two separate things: a file format to read, and an
interpolation to choose.

---

## The format

`.cube`, from Adobe's *Cube LUT Specification*. What Orion's reader accepts,
and what it refuses:

| Keyword | Handling |
|---|---|
| `TITLE "..."` | Read; surrounding quotes stripped. Optional, and most files omit it — the panel falls back to the file's own name |
| `LUT_3D_SIZE n` | The 3D grid's edge. Orion accepts 2…65 |
| `LUT_1D_SIZE n` | Accepted, 2…65536, and **lifted onto a 3D grid** — see below |
| `DOMAIN_MIN r g b` | Defaults to `0 0 0` |
| `DOMAIN_MAX r g b` | Defaults to `1 1 1`; refused if not above `DOMAIN_MIN` |
| `#` | Comment to end of line, anywhere |
| blank lines | Ignored |

**Red varies fastest** in the data block, then green, then blue. This is the
single detail worth testing rather than reading: getting it backwards swaps red
and blue in every LUT the product ever loads, and the result looks like a
deliberate cross-process look rather than a bug. `testCreativeLut` asserts that
entry 1 of a 2³ table is `(1, 0, 0)`.

**Why 65 is the ceiling.** The grid is uploaded into one texture allocated at
the largest accepted edge, so changing LUT is an upload and never a recompile.
17, 33 and 65 are the sizes creative LUTs actually ship at; 65³ costs 4.4 MB,
which is nothing beside the frame buffers. A larger file is **refused by name**
rather than truncated into something that looks nearly right.

**A 1D LUT is lifted onto the 3D grid.** A 1D LUT applies the same curve to
each channel independently, which *is* a separable 3D LUT — so evaluating it at
the grid nodes is exact at those nodes, and what happens between them is what
happens between them for any other LUT. One code path downstream is worth more
than the memory a 33³ grid costs. The alternative, a second sampling path in the
display kernel, is a branch that only some files exercise.

**Errors name the line.** A LUT that will not load is the user's file being
wrong, not the app misbehaving, so `parseCube` returns `line 3: '1 1 nonsense'
is not a number` rather than a boolean, and the panel shows it.

---

## Tetrahedral interpolation, and why not the hardware's trilinear

A 3D LUT is a sparse grid; every pixel lands between grid points, so the
interpolation *is* the filter. Two choices matter:

**Trilinear** reads all eight corners of the enclosing cell and weights them by
the product of the fractional coordinates. It is what GPU hardware does for
free with a 3D texture and `filter::linear`.

**Tetrahedral** splits the cell into six tetrahedra, works out which one the
sample falls in from the ordering of the fractional coordinates, and
interpolates between that tetrahedron's four corners.

The difference is not speed, and it is not accuracy in the abstract — **on any
function that is linear across the cell the two agree exactly.** The difference
shows up where a LUT has a *hard boundary* in it: a key, a hue restriction, a
crushed-black rolloff, most film emulations. Trilinear reads four corners that
lie on the far side of that boundary and mixes them in; tetrahedral reads only
corners in the same simplex as the sample, so it cannot.

That is why "it looks right" is not evidence here, and why the test does not use
a gentle LUT. `testCreativeLut` builds a table that is zero at every corner
except `(1,1,1)`, where the two disagree by construction: tetrahedral returns
the *smallest* of the three fractional coordinates, trilinear their *product*.
At (0.6, 0.5, 0.4) that is 0.4 against 0.12.

### The decomposition

With fractional coordinates `f` in the cell and corners `c[dr][dg][db]`, walk
from `c000` to `c111` along the three unit steps in descending order of their
fractions. Six orderings, six tetrahedra:

```
f.r > f.g > f.b :  c000 + f.r(c100−c000) + f.g(c110−c100) + f.b(c111−c110)
f.r > f.b > f.g :  c000 + f.r(c100−c000) + f.b(c101−c100) + f.g(c111−c101)
f.b > f.r > f.g :  c000 + f.b(c001−c000) + f.r(c101−c001) + f.g(c111−c101)
f.b > f.g > f.r :  c000 + f.b(c001−c000) + f.g(c011−c001) + f.r(c111−c011)
f.g > f.b > f.r :  c000 + f.g(c010−c000) + f.b(c011−c010) + f.r(c111−c011)
f.g > f.r > f.b :  c000 + f.g(c010−c000) + f.r(c110−c010) + f.b(c111−c110)
```

Each is a convex combination of four corners, and every case reduces to `c000`
at `f = 0` and `c111` at `f = 1`, so the grid points themselves are reproduced
exactly. That is what the identity-LUT test checks — and it is the check that
caught the packing bug below.

---

## Where it sits, and how it is stored

**Last in the display kernel**, after AgX and after the tone curve. A creative
look is applied to a *finished* picture: a `.cube` authored for grading expects
display-referred input, and running it before the curve would mean the curve
reshapes whatever the look did.

**Inside `develop_display.slang`, not as its own node.** It is pointwise, and
`STATUS.md` is emphatic about this — every extra pointwise pass is a 194 MB
round trip at 24 MP. Measured: a look changes **2 nodes and 7 ms**, because only
the display and geometry nodes recompute.

**The grid lives in a 2D texture**, `x = r` and `y = b·size + g`, because a 3D
texture would be the only one in the engine and tetrahedral interpolation does
its own fetching anyway — the hardware's filtering is the thing being avoided,
so there is nothing to gain from a sampler.

⚠️ **The row stride is the LUT's own edge, not the texture's width.** The
texture is allocated at 65 to hold the largest grid; the packing is `b·size + g`
and the shader recomputes the same expression. Using the texture width in one
place and the size in the other puts every blue slice in the wrong row — which
renders as a plausible colour cast rather than as anything obviously broken.
That bug was written and then caught, by the identity-LUT check and nothing else.

---

## Sourcing status

| Item | Status |
|---|---|
| The `.cube` grammar and semantics as implemented | Implemented against the format as it is universally documented and as every LUT in circulation is written. **The specification document itself was not retrieved during this session** — see `UNSOURCED.md` |
| Red-varies-fastest ordering | Same. Asserted by test, which is the check that matters, but the assertion encodes the belief rather than a quotation |
| Tetrahedral decomposition | The six-case form above is standard in colour management and is what the code implements; the canonical published comparison of trilinear/prism/pyramid/tetrahedral subdivision is believed to be Kasson, Nin, Plouffe & Hafner, *Performing Color Space Conversions with Three-Dimensional Linear Interpolation*, Journal of Electronic Imaging 4(3), 1995. **Not retrieved and not verified this session** — `UNSOURCED.md` |

The interpolation's *correctness* does not rest on the citation — the identity
and simplex properties are checked directly, and the reduction to `c000`/`c111`
at the cell corners is arithmetic. What the citation would add is the evidence
that tetrahedral is the right choice among the four subdivisions, which is a
claim this file currently makes without a source.
