# Creative LUTs — .cube, and tetrahedral interpolation

M3's third story. Two separate things: a file format to read, and an
interpolation to choose.

---

## The format

**Adobe Systems Incorporated, *Cube LUT Specification, Version 1.0*, published
September 2013**, © 2013, licensed CC BY-NC 3.0. The Adobe URL is dead; a live
copy is on the Internet Archive at
`web.archive.org/web/20201027210201/https://wwwimages2.adobe.com/content/dam/acom/en/products/speedgrade/cc/pdfs/cube-lut-specification-1.0.pdf`.
Its introduction notes the format "was originally developed by IRIDAS in 2003".

What Orion's reader accepts, and what it refuses:

| Keyword | Handling |
|---|---|
| `TITLE "..."` | Read; surrounding quotes stripped. Optional, and most files omit it — the panel falls back to the file's own name |
| `LUT_3D_SIZE n` | The 3D grid's edge. Orion accepts 2…65 |
| `LUT_1D_SIZE n` | Accepted, 2…65536, and **lifted onto a 3D grid** — see below |
| `DOMAIN_MIN r g b` | Defaults to `0 0 0` |
| `DOMAIN_MAX r g b` | Defaults to `1 1 1`; refused if not above `DOMAIN_MIN` |
| `#` | A comment **line**, not trailing text — see below |
| blank lines | Tolerated on read, though the spec does not list them |

**Red varies fastest** in the data block, then green, then blue. §7.2, verbatim:

> "The lines of table data shall be in ascending index order, with the first
> component index (Red) changing most rapidly, and the last component index
> (Blue) changing least rapidly."
>
> "NOTE: This ordering is the opposite of the typical in-memory order of
> multi-dimensional tables. An equivalent C index would be `r + N * g + N * N *
> b`."

The spec hands over the index expression, so there is nothing to derive. This is
still the detail most worth a test rather than a reading: backwards, it swaps
red and blue in every LUT the product loads and the result looks like a
deliberate cross-process look. `testCreativeLut` asserts entry 1 of a 2³ table
is `(1, 0, 0)`.

**Comments are whole lines.** §5.8: "Each comment line shall be formatted as
follows: `# <text>` … NOTE: A comment line does not have leading `<sp>`." A `#`
part-way through a line is *not* a comment — so `TITLE "Look #3"` keeps its
hash. Orion tolerates leading whitespace before the `#` on read, since a data
line cannot begin with one and refusing a file over indentation helps nobody.

⚠️ **Blank lines are not in §5.3's list of what a line may be**, but the
specification's own reference reader in Annex B skips them, and files in
circulation contain them. Tolerated on read; never written.

⚠️ **Carriage return is not a line separator.** §5.3 is explicit that files
using `\r` as the separator "are not valid cube files". A stray `\r` from CRLF
is a different matter and is trimmed, because otherwise every numeric field ends
in a character the parser stops at.

Other constraints the spec states, worth knowing before extending the reader:
lines are at most 250 bytes, numbers may not exceed ±1e37 and require a decimal
point, all keywords must appear before any table data, and each keyword at most
once. Table values are **not** constrained to 0…1 and need not be monotonic.

**Why 65 is the ceiling, when the spec allows 256.** §7.1 gives the range as
[2, 256] and immediately adds NOTE 2: "Some readers do not have enough memory
(200 MBytes) to support N = 256." Orion's grid is uploaded into one texture
allocated at the largest accepted edge, so changing LUT is an upload and never a
recompile — at 256 that texture would be 268 MB, permanently resident, for a
size nothing ships at. 17, 33 and 65 are what creative LUTs are actually
authored at; 65³ costs 4.4 MB. A larger file is **refused by name** rather than
truncated into something that looks nearly right.

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

**The format's own specification requires it.** §8: "The reader should use
linear interpolation for one-dimensional tables, and **tetrahedral
interpolation for three-dimensional tables**." It never defines the term, and
its bibliography cites only Selan, *GPU Gems 2* ch. 24 — which does not mention
tetrahedral interpolation at all. So the requirement is sourced and the
construction has to come from elsewhere.

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

**Sakamoto, T. & Itooka, A., *Linear Interpolator for Color Correction*, U.S.
Patent 4,275,413**, assigned to Dainippon Screen Seizo K.K., priority
1978-03-30, granted **1981-06-23**. This is the origin of tetrahedral colour
interpolation and what later surveys cite for it. Column 10:

> "The unit cube is dissected into six tetrahedra by three planes which have a
> line in common which is the long diagonal of the unit cube… the conditions for
> the point P to lie within this tetrahedron are that x_f ≧ y_f ≧ z_f … the
> interpolated value U(P) is equal to
> **U(A)[1−x_f] + U(B)[x_f − y_f] + U(C)[y_f − z_f] + U(D)z_f.**"

With `f` the fractional coordinates and `c[dr][dg][db]` the corners, the six
cases as Orion implements them:

```
f.r > f.g > f.b :  c000 + f.r(c100−c000) + f.g(c110−c100) + f.b(c111−c110)
f.r > f.b > f.g :  c000 + f.r(c100−c000) + f.b(c101−c100) + f.g(c111−c101)
f.b > f.r > f.g :  c000 + f.b(c001−c000) + f.r(c101−c001) + f.g(c111−c101)
f.b > f.g > f.r :  c000 + f.b(c001−c000) + f.g(c011−c001) + f.r(c111−c011)
f.g > f.b > f.r :  c000 + f.g(c010−c000) + f.b(c011−c010) + f.r(c111−c011)
f.g > f.r > f.b :  c000 + f.g(c010−c000) + f.r(c110−c010) + f.b(c111−c110)
```

Expanded, each is `(1−a)c000 + (a−b)c_second + (b−c)c_third + c·c111` — exactly
the patent's form — and all six were checked term by term against its Table 2.
Every case is a convex combination of four corners, reduces to `c000` at `f = 0`
and `c111` at `f = 1`, and walks a monotone single-axis path from one to the
other.

⚠️ **The patent's Table 2 has a printing error, and anyone checking this source
will hit it.** In the first half of the table the headers `U(Xi·Yi+1·Zi)` and
`U(Xi·Yi·Zi+1)` are transposed relative to the body in rows 3–6, so read
literally it pairs non-adjacent vertices — `c010 → c101`, which is
geometrically impossible. The second half of Table 2 is printed correctly and
disambiguates every row, as does the verbatim prose above for row 1. Recorded
here so the next person to check the source does not conclude the code is wrong.

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
| The `.cube` grammar, defaults, size ranges and byte ordering | **Sourced.** Adobe, *Cube LUT Specification, Version 1.0*, September 2013, §§5–7 |
| The requirement to interpolate 3D tables tetrahedrally | **Sourced.** Same document, §8 |
| The six-tetrahedra decomposition | **Sourced.** Sakamoto & Itooka, U.S. Patent 4,275,413 (1981), col. 10 and Table 2 |
| That tetrahedral is *more accurate* than trilinear, prism or pyramid | ⚠️ **Not sourced.** See below |

The accuracy comparison is usually credited to **Kasson, J. M., Nin, S. I.,
Plouffe, W. & Hafner, J. L., "Performing color space conversions with
three-dimensional linear interpolation", *Journal of Electronic Imaging* 4(3),
226–250, July 1995, DOI 10.1117/12.208656.** That citation is confirmed against
DBLP, Crossref and Semantic Scholar — but **the paper itself has not been read
here**: it is not open access and no copy could be retrieved. A secondary source
(Vondran, HP Labs HPL-98-95, 1998) confirms only that it analyses trilinear,
prism and tetrahedral — not pyramid, and not the conclusion.

So Orion does not claim tetrahedral is the most accurate subdivision. It uses
tetrahedral because **the file format's own specification says to**, which is a
stronger reason for this particular decision anyway. `UNSOURCED.md` §12 carries
what remains open.

**Two dead ends recorded so nobody repeats them.** ICC.1:2022-05 and ICC.2:2019
contain zero occurrences of "tetrahedr" — the ICC specifications are not a
citation for this. Neither is *GPU Gems 2* ch. 24, despite being the Cube
specification's only bibliography entry.
