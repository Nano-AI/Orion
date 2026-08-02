# X-Trans — the licensing question first, and it has an answer

Written 2026-08-02 for M5's line "X-Trans support (Markesteijn)", which has sat
on the roadmap since the first milestone list and had never been investigated.

**Research and decomposition only. Nothing was built, no build was run, and
neither test suite was run — there was nothing to test.** That was the brief and
it is the outcome. ⚠ The tree could not have been built in any case:
`planning/STATUS.md` records `third_party/slang` destroyed on 2026-08-02, so no
shader compiles on this machine today. Nothing below is measured on hardware;
every number is labelled in §4.4.

`research/demosaic.md` holds the entry for the Bayer path that ships today. Its
last line reads *"X-Trans sensors need Markesteijn instead; Bayer only for
now"*, and this file is what sits behind that sentence.

---

## 0. The recommendation, in one sentence

**Call LibRaw's own `xtrans_interpolate` — it is Markesteijn's algorithm, it is
already installed on this machine, and it is LGPL-2.1 / CDDL-1.0 rather than
GPL** — and then stop, because the *licence* is not what makes this expensive:
the cost is that decision #29 pins the highlight clip between white balance and
demosaic, so a demosaic that leaves the GPU takes the white-balance slider with
it (§4.2).

---

## 1. ⚠ The licensing question, answered honestly

The brief's question was the right one to ask first:

> Does a description of this algorithm exist outside GPL source code, in a form
> someone could implement from — a paper, a patent, an article, a
> specification, the author's own writing?

**Two answers, and the second one makes the first one stop mattering.**

### 1a. No published description of Markesteijn's algorithm exists

Searched, and nothing found meets `research/README.md`'s bar — published, dated,
established. What exists is:

| Artefact | What it is | Meets the bar? |
|---|---|---|
| Frank Markesteijn's original code | Contributed to dcraw; Coffin merged it. No accompanying paper, no author write-up, no blog | ❌ Source only |
| dcraw's `xtrans_interpolate` | Coffin's C, one function, no derivation and no comments beyond attribution | ❌ Source only |
| RawTherapee's `xtransdemosaic.cc` | **GPL-3.** RawPedia describes *what to pick* (1-pass vs 3-pass), not how either works | ❌ GPL source; the prose is a user manual |
| darktable's `src/iop/demosaic/xtrans.c` | **GPL-3**, adapted from Markesteijn by Dan Torop | ❌ GPL source |
| darktable / Ansel manual, demosaic module | "Markesteijn 1-pass / 3-pass", quality-versus-speed guidance | ❌ Not a description |
| Fujifilm's patents | Cover the **array** — the 6×6 layout, its aperiodicity, the claim that it needs no OLPF | ❌ Describes the sensor, not how to interpolate it. And Markesteijn is an independent contributor, not Fujifilm |
| `xtransdemosaicking.blogspot.com` (2016–2018) | An anonymous author's hybrid: **Markesteijn for luma, frequency-domain chroma**, which became darktable's Markesteijn+FDC. Discusses Markesteijn as a black box it calls | ❌ Describes the *addition*, not Markesteijn |

⚠ **The honest finding is that the only extant description of Markesteijn's
algorithm is source code.** There is no paper to implement from. Reading
darktable's or RawTherapee's `xtrans.c` and re-typing it in Slang is copying GPL
code, not implementing a published algorithm from its description, and
`CLAUDE.md` forbids exactly that. **That route is closed and stays closed.**

This is worth stating flatly because it is the answer to a question that gets
asked as though it must have a yes. It does not. The most widely deployed
demosaic for this sensor family has never been written down.

### 1b. ⚠ But the same code also ships under LGPL-2.1 / CDDL-1.0, inside LibRaw

This is the finding that changes the shape of the story, and it was checked on
this machine rather than remembered.

`/opt/homebrew/opt/libraw/include/libraw/libraw.h:451` — LibRaw 0.22.2, the
build Orion already links:

```cpp
  void xtrans_interpolate(int);
```

`/opt/homebrew/Cellar/libraw/0.22.2/COPYRIGHT`, verbatim:

> LibRaw is free software; you can redistribute it and/or modify it under the
> terms of the one of two licenses as you choose:
>
> 1. GNU LESSER GENERAL PUBLIC LICENSE version 2.1
> 2. COMMON DEVELOPMENT AND DISTRIBUTION LICENSE (CDDL) Version 1.0
>
> LibRaw uses code from dcraw.c -- Dave Coffin's raw photo decoder, dcraw.c is
> copyright 1997-2018 by Dave Coffin […] **LibRaw do not use RESTRICTED code
> from dcraw.c**

`LibRaw/src/demosaic/xtrans_demosaic.cpp` carries that same dual grant in its own
header, and credits *"Frank Markesteijn's algorithm for Fuji X-Trans sensors"*
in the function's comment.

⚠ **The GPL demosaics are in a different repository, and that is why "LibRaw's
demosaics are GPL" is a thing people say.** LibRaw split its contributed
interpolators out into `LibRaw-demosaic-pack-GPL2` (AFD, LMMSE, from PerfectRaw)
and `LibRaw-demosaic-pack-GPL3` (AMaZE, Emil Martinec) — both separate GitHub
repositories, both now abandoned upstream, and neither built by Homebrew.
**`xtrans_interpolate` is not one of them.** It is in the LGPL/CDDL core because
it arrived through dcraw, and dcraw's non-RESTRICTED code carries no such
encumbrance — the RESTRICTED set is the Foveon and secret decoders, which the
COPYRIGHT explicitly says LibRaw does not use.

**Orion already links LibRaw** — `engine/CMakeLists.txt:49-61`, `libraw_r`, the
thread-safe build, found in Homebrew and linked dynamically. LGPL-2.1's condition
for a closed program is dynamic linking plus the ability to relink, which is
precisely the arrangement already in place for every other thing LibRaw does
here. **Nothing about Orion's licence model changes.** And CDDL-1.0 is per-file
copyleft, so even a future vendored static build has a documented route that a
static LGPL link does not.

### 1c. What that leaves

| Route | Licence | Verdict |
|---|---|---|
| Port Markesteijn to Slang from darktable / RawTherapee | GPL-3 | ❌ **Forbidden.** No published description to launder it through |
| Reimplement Markesteijn from a paper | — | ❌ **Impossible.** No paper exists |
| **Call LibRaw's `xtrans_interpolate`** | **LGPL-2.1 / CDDL-1.0** | ✅ **Open, and already a dependency.** §4 |
| Implement Rafinazari & Dubois on the GPU | Paper is published; no code needed | ⚠ Legal, lower quality, ~10× the work. §3 |
| Don't support X-Trans | — | ⚠ A real option, and cheaper than a half-built one. §6 |

---

## 2. What breaks today, concretely

X-Trans is **rejected at decode**, deliberately — `engine/src/raw/RawImage.cpp:165`:

```cpp
    if (idata.filters == 9) { … throw std::runtime_error(
        "X-Trans sensors use a 6x6 mosaic, and Orion's demosaic is Bayer "
        "only. …"); }
```

`LIBRAW_XTRANS` is `9` (`libraw_const.h:715`), and it is a **sentinel, not a
pattern**: the real CFA lives in `imgdata.idata.xtrans[6][6]` and
`xtrans_abs[6][6]` (`libraw_types.h:202-203`). Everything below is what happens
if that `throw` is deleted and nothing else changes. It is not a list of things
that would look slightly wrong.

### 2.1 The root: `cfaChannel` decodes a 32-bit word that does not exist

`engine/shaders/cfa.slang`, mirrored on the CPU by `BayerImage::channelAt`:

```
uint shift = (((p.y << 1) & 14) | (p.x & 1)) << 1;
return (filters >> shift) & 3;
```

Two bits per cell over an 8-row × 2-column period — dcraw's `FC` macro, a 2×2
pattern by construction. Handed `filters = 9 = 0b1001` it returns **1 (green) at
every even column of every even row, 2 (blue) at every odd column of even rows,
and 0 (red) everywhere else**. Not a degraded pattern: a fabricated one, applied
to a sensor whose actual layout in each 6×6 cell is **20 green, 8 red, 8 blue**
(Rafi Nazari 2017, §3.2). It would produce a picture, and the picture would be
wrong everywhere in a way no assertion in either suite looks for.

### 2.2 Node by node

| Where | What a 6×6 CFA does to it |
|---|---|
| `linearize.slang` | Indexes `black[c]` and `whiteBalance[c]` by `cfaChannel`. With `c` fabricated, **every pixel gets another channel's black point and another channel's white-balance gain.** The clip that decision #29 exists for still fires, at a ceiling computed from the right gains applied to the wrong pixels |
| `BayerImage::blackLevels` | Its own doc comment already says it: *"Larger patterns (X-Trans is 6x6) are averaged, and X-Trans is rejected at decode anyway."* The 6×6 `cblack` collapses to a mean, and any real per-channel spread becomes a shadow cast no control removes |
| `rcd:dirs` (`rcd_dirs.slang`, `ops/rcd_stat.slang`) | Pattern-agnostic arithmetic — it takes only `size` — but the **statistic is not pattern-agnostic**. RCD's direction discrimination is the energy of a high-pass filter along each axis over the mosaic, and it is meaningful because in Bayer every row alternates G with exactly one of R/B. In X-Trans a row can be `G G B G G R` or `B G G R G G`: the along-axis sequence changes colour composition with a period of 6, so the "energy" it reports is mostly the CFA, not the picture. It runs, it produces numbers, the numbers are about the filter array |
| `rcd:lowpass` (`rcd_lpf.slang`) | A 3×3 binomial that *deliberately* mixes all three colours because in Bayer any 3×3 has a fixed colour census, making it an unbiased local luminance. In X-Trans the census is **not fixed**: a 3×3 centred inside one of the 2×2 green blocks is nearly all green, one centred on the R/B cross is not. The estimate acquires a **6×6-periodic colour bias**, and step 3 divides by it |
| `rcd:green` (`rcd_green.slang`) | Assumes green sits on a quincunx — every non-green pixel's four cardinal neighbours are green. **False in X-Trans**: green is 20/36 in blocks and diagonals, and a red site can have red or blue cardinal neighbours. `N_Est`, `S_Est`, `E_Est`, `W_Est` would each be built from whatever colour happened to be there |
| `rcd:red/blue` (`rcd_rb.slang`) | Assumes at an R site the missing B is on the diagonals and vice versa, and reuses step 1's discrimination at G sites. X-Trans has **six distinct site classes** in the 6×6, not three, with different neighbour relations in each. The P/Q diagonal discrimination has no counterpart |
| `estimateNoise` (`raw/NoiseProfile.cpp:44`) | ⚠ **The one with a comment that names its own assumption.** It samples `row[x]`, `row[x+2]`, `row[x+4]` — *"Three samples two apart, so all three are the same color in any 2x2 CFA."* A stride of 2 in X-Trans **crosses colours**, so the second difference `p − 2q + r` no longer annihilates a ramp: it measures the scene's own R–G–B differences as noise. `a` and `b` come out inflated, the fit reports `measured = true`, and the denoiser over-smooths on evidence it invented. The block-average black point (`(black[0..3])/4`) is separately wrong for a 6×6 pattern |
| The white-balance path (`DevelopPipeline.cpp:1269-1293`) | `lin.whiteBalance[c]` is per CFA channel, so it inherits §2.1 exactly. `whiteClipFor(gains)` itself is per-channel scalar arithmetic and is **fine** — it is the only part of the head that survives a 6×6 unchanged |
| `decimate` (`RawImage.cpp`) | ⚠ Builds the preview mosaic **cell by cell in 2×2 units** and carries `filters` across unchanged, with a doc comment warning that a stride which is not a multiple of the cell "does not look soft, it looks like a color-swapped nightmare". A 6×6 cell decimated in 2×2 units is that nightmare, on the preview graph that runs while a slider moves |
| `canReload` (`DevelopPipeline.cpp:735`) | Compares `image.filters == filters_`. Every X-Trans body reports `9`, so **two different X-Trans sensors compare equal** and a reload would keep the previous frame's pattern. Cosmetic today, a real trap the moment `9` is accepted |
| `patternString()` | Names the top-left 2×2. Meaningless on a 6×6, and it is what the tests print |

⚠ **Not one of these fails loudly.** They each produce a plausible number from a
wrong premise, which is this repository's named failure mode — the purple cast
and the `Tang`/`Tappen` citation are the two entries in that ledger, and neither
was catchable by inspection.

---

## 3. The published alternative, and it is real

There **is** peer-reviewed, dated, open-access work on X-Trans specifically. It
is not Markesteijn's and it does not claim to beat it.

> M. Rafinazari and E. Dubois, "Demosaicking algorithm for the Fujifilm X-TRANS
> color filter array," *Proc. IEEE International Conference on Image Processing
> (ICIP 2014)*, Paris, October 2014, pp. 660–663. doi:
> [10.1109/ICIP.2014.7025132](https://doi.org/10.1109/ICIP.2014.7025132)

and, at full length and **open access**, the author's thesis:

> Mina Rafi Nazari, *Denoising and Demosaicking of Color Images*, Ph.D. thesis,
> School of Electrical Engineering and Computer Science, University of Ottawa,
> 2017. <http://hdl.handle.net/10393/35802>. **Chapter 3 §3.2** is the X-Trans
> method in full.

⚠ **Opened and read**, not cited from a search result — the PDF was downloaded
and Chapter 3 read, which is the standing rule since a third author named Tang
turned out to be Tappen in five files.

### What it does

Frequency-domain luma–chroma demultiplexing, the X-Trans extension of Leung,
Jeon & Dubois's LSLCD for Bayer (*IEEE TIP* 20(7):1885–1894, 2011). The CFA
signal is written as a sum of one luma and modulated chroma components:

- The 6×6 is treated as **3×6 with hexagonal periodicity**, the second half a
  shifted copy of the first (§3.2). `K = 18` components, of which **five rows of
  M are zero**, leaving thirteen.
- Luma is `q₁ = (2f_R + 5f_G + 2f_B)/9` — ⚠ **not** Bayer's luma, because the
  colour census of the cell is different.
- Chroma is extracted with **three baseband Gaussian filters, σ = 2.32 in both
  dimensions**, modulated to different band centres: one takes `q₂,q₃,q₁₀,q₁₁`,
  one `q₆,q₇,q₁₂,q₁₃`, one `q₁₄,q₁₅,q₁₆,q₁₇`. Luma is what is left:
  `q̂₁ = f_CFA − Σ`.
- The **adaptive** variant weights each of four symmetric chroma pairs by an
  inverse local-energy index — a Gaussian modulated to the midpoint between luma
  and that chroma, squared, then a 5×5 moving average — and rebuilds `q̂₂` as the
  normalised weighted sum, from which `q̂₃ = q̂₂*`, `q̂₁₀ = q̂₂*`, `q̂₁₁ = q̂₂`.
- Reconstruction is `f̂ = M†q̂`, `M†` the pseudo-inverse.

### ⚠ What its own numbers say, and what they do not

Table 3.1, mean PSNR over the 24 Kodak images:

| | Bayer LSLCD | X-Trans non-adaptive | X-Trans adaptive | X-Trans Bayer-like adaptive | X-Trans least-square |
|---|---|---|---|---|---|
| PSNR, dB | **39.8** | 34.80 | **36.50** | 36.27 | 36.35 |

The thesis's own conclusion: the X-Trans reconstructions are **3.3–5.0 dB below
Bayer LSLCD** on edges, while showing *fewer* false colours — "less color
components in the pattern results in more false color and better reconstructed
edges."

Three things that table does **not** say, and each is load-bearing:

1. ⚠ **It is not compared against Markesteijn, anywhere.** The thesis opens
   Chapter 3 with *"Due to the lack of published research on the Fujifilm
   X-Trans pattern"* — the literature gap §1a found, stated by the only people
   who published into it. **Shipping this means shipping an algorithm that has
   never been measured against the one every other tool uses.**
2. ⚠ **The mosaics are simulated.** Kodak's 768×512 sRGB images are resampled to
   an X-Trans layout; the thesis says explicitly that the dataset is "white
   balanced and gamma corrected" and that it therefore skips those steps.
   **Orion's domain is linear sensor counts at 26–40 MP.** That is the same
   class of domain error decision #111 found in the learned denoiser — a metric
   measured in one domain, quoted about another — one milestone earlier.
3. **No implementation is distributed.** It would be built from the description,
   which is legitimate and is exactly how `hl_pull`/`hl_push` were built from
   Rouf et al. — but it is a from-scratch build of thirteen complex-valued
   component extractions, not a port.

### The other published pieces, for completeness

| Work | Venue, year | Relevance |
|---|---|---|
| Leung, Jeon & Dubois, "Least-squares luma-chroma demultiplexing algorithm for Bayer demosaicking" | *IEEE TIP* 20(7):1885–1894, **2011**. doi:10.1109/TIP.2011.2107524 | The Bayer parent of the above. Reference software published by the authors |
| Jeon & Dubois, "Demosaicking of noisy Bayer-sampled color images with LSLCD and noise level estimation" | *IEEE TIP* 22(1):146–156, **2013**. doi:10.1109/TIP.2012.2214041 | The noisy case, which is where X-Trans is hardest |
| `xtransdemosaicking.blogspot.com`, Oct 2016 – Feb 2018 | Self-published | Markesteijn for luma + frequency-domain chroma after Dubois. Shipped in darktable 2.4.0 as **Markesteijn + FDC**. ⚠ Not usable: it is an *addition to* Markesteijn, so it inherits the problem rather than solving it, and the write-up is a build log rather than a derivation |
| Gharbi, Chaurasia, Paris & Durand, *Deep Joint Demosaicking and Denoising* | SIGGRAPH Asia **2016** | Already in `research/demosaic.md`. Learns a CFA-agnostic mapping, so it covers X-Trans in principle. ⚠ Decision #111's whole argument applies unchanged — the arithmetic there says a network of this class is 100× M0's frame budget, and weights inherit their training data's licence |

---

## 4. What it would cost, in this graph's units

### 4.1 The arithmetic base, validated against a measured number

At 24 MP (6024×4024 = 24,240,576 px), which is `_PIC8220` and the frame every
figure in `STATUS.md` is quoted on:

| Format | B/px | One full-resolution texture |
|---|---|---|
| `R16Uint` (the source mosaic) | 2 | 46.2 MiB |
| `R32Float` | 4 | 92.5 MiB |
| `RGBA16Float` | 8 | **184.9 MiB** |

⚠ That last row reproduces `research/highlight-reconstruction.md` §5b's
**measured** 185 MiB to within rounding, which is why the rest of this section is
arithmetic rather than a guess. `Pipeline::intermediateBytes` (`Pipeline.cpp:329`)
sums the source **plus** every node output, so the source counts toward the 7,186
MiB.

**The demosaic head today — 5 nodes, and the source:**

| | Format | MiB |
|---|---|---|
| source | `R16Uint` | 46.2 |
| `linearize` | `R32Float` | 92.5 |
| `rcd:dirs` | `R32Float` | 92.5 |
| `rcd:lowpass` | `R32Float` | 92.5 |
| `rcd:green` | `R32Float` | 92.5 |
| `rcd:red/blue` | `RGBA16Float` | 184.9 |
| **Total** | | **601.1 MiB — 8.4% of 7,186** |

### 4.2 ⚠ The finding that actually decides this, and it is not the licence

`DevelopPipeline.cpp:1269` says it in its own comment, and it was written for
Bayer:

> White balance rewrites the linearize block, which sits at the head of the
> graph — so moving temperature legitimately recomputes everything, **including
> the demosaic**. That is inherent: the demosaic interpolates white-balanced
> data.

That is inherent because of **decision #29**, which clips all three channels to a
common ceiling *after* white balance and *before* demosaic, so that RCD never
interpolates across an unclipped 2.2× red neighbour and spreads a magenta fringe
past the highlight's edge. `whiteClipFor` is re-evaluated on every temperature
change (`:1288`), and `hl_mask` re-derives the same ceiling from the same
function (`:1373`) precisely so the two cannot drift.

**So if the demosaic leaves the GPU, the white-balance slider leaves with it.**
On an X-Trans frame, moving temperature would mean re-running Markesteijn on the
CPU. At 3-pass that is plausibly seconds — unmeasured, see §4.4 — against M0's
16 ms.

Four ways out, and none of them is free:

| | Way out | What it costs |
|---|---|---|
| a | Demosaic at neutral gains, apply white balance as a GPU node afterwards | ⚠ **Reopens decision #29 and loses.** Clipping after demosaic is exactly the ordering #29 rejects, and highlight-reconstruction pieces 2–4 are all downstream of that clip and assume it happened |
| b | Accept it: on X-Trans, temperature is not a live slider — it re-decodes | Honest, testable, and a visibly worse product on one sensor family. **The default recommendation** |
| c | Cheap GPU X-Trans demosaic for the live preview, Markesteijn on commit | ⚠ Circular: a licence-clean GPU X-Trans demosaic is the thing that does not exist. It would be §3's LSLCD, which is a whole second feature |
| d | Demosaic once at as-shot gains, re-apply gains as a ratio afterwards | ⚠ **Do not.** Markesteijn's interpolation is linear in the samples but its direction and homogeneity decisions are not, so the ratio is an approximation of unknown size. It would look right. `UNSOURCED.md` §29 |

### 4.3 The two costings

X-Trans bodies are not 24 MP. The two live sensor sizes (manufacturer
specification, not measured here) are **26 MP** — 6240×4160 = 25,958,400 px,
X-T4/X-S20/X100V — and **40 MP** — 7728×5152 = 39,814,656 px, X-H2/X-T5.

⚠ **A finding that has nothing to do with the demosaic:** every full-resolution
node scales with pixel count, so the existing 173-node graph at 7,186 MiB becomes
**~7,700 MiB at 26 MP and ~11,800 MiB at 40 MP** before one X-Trans node exists.
Arithmetic, assuming proportionality — the pyramid chains are not exactly
proportional, so treat the second figure as ±5%. **The 40 MP number is a
constraint on this feature and it is not a demosaic problem.**

**Option A — LibRaw `xtrans_interpolate`, at 26 MP** (`R32Float` 99.0 MiB,
`RGBA16Float` 198.1 MiB, `R16Uint` 49.5 MiB):

| | Nodes | GPU MiB |
|---|---|---|
| Remove `linearize`, `rcd:dirs`, `rcd:lowpass`, `rcd:green`, `rcd:red/blue` | **−5** | −594.1 |
| Remove the `R16Uint` mosaic source | — | −49.5 |
| Add an `RGBA16Float` uploaded source in `rcd:red/blue`'s place | 0 | +198.1 |
| **Net** | **−5 nodes** | **−445.6 MiB** |

⚠ **The GPU graph gets smaller**, because the demosaic moves off it entirely.
The cost moves to host RAM — LibRaw's `imgdata.image` is 4 `ushort` per pixel,
198 MiB at 26 MP and 304 MiB at 40 MP, transient — and to wall-clock at open.
**168 nodes on an X-Trans frame against 173 on a Bayer one**, which is a node
count a bench probe can assert by name, the way `dehaze drag` does.

**Option B — Rafinazari & Dubois on the GPU, at 26 MP.** ⚠ **Every number here
is a guess**, and it is the shape of guess `highlight-reconstruction.md`'s
estimate was 16× wrong in:

| | Nodes *(guess)* | MiB *(guess)* |
|---|---|---|
| `linearize` (X-Trans-aware) | 1 | 99 |
| Three modulated Gaussians, σ = 2.32, separable, complex output (`RG32Float`) | 6 | 6 × 198 = 1,188 |
| Luma by subtraction | 1 | 99 |
| Adaptive weighting: modulated energy filter, square, 5×5 moving average, combine | 4 | 4 × 198 = 792 |
| `f̂ = M†q̂` | 1 | 198 |
| **Total** | **~13** | **~2,376** |

Against option A's −445.6 that is a swing of **~2.8 GiB**, which at 40 MP is ~4.6
GiB on top of 11.8. ⚠ **The chromas are complex-valued**, which is where the
memory goes and which is not obvious from the paper — a fact worth having found
before costing rather than after.

### 4.4 ⚠ Which of these numbers are measured, arithmetic, or guesses

The rule this table exists for: `research/highlight-reconstruction.md`'s estimate
was **16× out** because nobody measured before costing, and it was wrong in one
word. Nothing below was measured on hardware in this session — **the build is
down** — so the honest top row is short.

| Number | Status |
|---|---|
| `xtrans_interpolate` exists in LibRaw 0.22.2 and is LGPL-2.1/CDDL-1.0; `LIBRAW_XTRANS == 9`; the `xtrans[6][6]` tables; `dcraw_process` is public and `user_qual > 2` selects 3-pass | ✅ **Read out of this machine's installed headers and COPYRIGHT, and out of LibRaw's own source.** Solid |
| 20 green / 8 red / 8 blue per 6×6; K = 18, thirteen non-zero; σ = 2.32; the PSNR table; luma = (2R+5G+2B)/9 | ✅ **Read out of the opened thesis**, Chapter 3 |
| Every breakage in §2 | ✅ **Read out of Orion's own source**, file and line given for each |
| 185 MiB per full-res `RGBA16Float` node; 173 nodes / 7,186 MiB | ✅ **Measured**, in `STATUS.md` and `highlight-reconstruction.md` |
| Every MiB figure in §4.1 and §4.3; the −5 nodes / −445.6 MiB delta; 7,700 and 11,800 MiB at 26 and 40 MP | ⚠ **Arithmetic** from the row above. Solid, given proportionality — which the pyramid chains only approximately satisfy |
| 26 MP and 40 MP sensor dimensions | ⚠ **Manufacturer specification.** Not read off a file here, because there is no X-Trans file here |
| **Markesteijn 1-pass and 3-pass wall clock at 26 and 40 MP** | ⚠⚠ **UNKNOWN, and it is the number that decides the feature.** Piece 2 exists for it |
| Option B's entire table; host RAM beyond `imgdata.image`; whether `dcraw_process` with `output_color = 0` really leaves the data in linear camera RGB | ⚠ **Guesses and named unknowns** |
| How Markesteijn compares to LSLCD, or to RCD-on-Bayer, on a real frame | ⚠ **Nobody has published it.** §3 |

---

## 5. ⚠ How to test it without an X-Trans camera in the room

Orion's three sample frames are all Sony Bayer. This is a real obstacle and it
belongs in the plan.

**It is mostly solved, and the answer is a licence.**

> **raw.pixls.us** — the community raw sample repository, explicitly for
> open-source raw software support. Every file is contributed under **CC0**: the
> upload form's own wording is *"I declare that I own full rights to this file
> and I hereby release it under the cc0 license into the public domain."*
> Fetchable over HTTPS, or `git clone https://raw.pixls.us/data.lfs.git`, or
> git-annex. Fujifilm RAF is a category the project actively solicits, in **both
> the old uncompressed and the new compressed** forms.

CC0 is the strongest possible answer for this repository: no attribution burden,
no field-of-use limit, and unlike DND (`research/denoise-learned.md`: "non-commercial
purposes") nothing that a shipping product trips over.

**What the fixture set has to contain, and why each one:**

| Frame | Why it is not optional |
|---|---|
| One **uncompressed RAF** and one **compressed RAF** from the same body | LibRaw's unpackers differ. A compressed-only failure would look like a demosaic bug |
| One **X-Trans I** (X-Pro1, 2012) and one **X-Trans IV/V** (X-T4 or X-H2) | The 6×6 is common to all generations; the containers, black levels and 40 MP dimensions are not |
| ⚠ One **Bayer Fujifilm** — an X-A series or a GFX | **The control.** Without it, `filters == 9` versus `filters != 9` selection is untested, and a branch that always takes the X-Trans path on a Fuji file passes every X-Trans test |
| The existing three Sony frames | **The regression.** Every Bayer number in `STATUS.md` must be bit-identical after the head of the graph learns to branch |

**What can be tested without any file at all**, and this is the part worth doing
first:

- **A synthetic X-Trans mosaic.** Take a known RGB image, sample it through the
  `xtrans[6][6]` table, demosaic, compare against the original. That is exactly
  what Rafi Nazari's Kodak experiment is, and it makes the reconstruction's error
  a **number in the test suite** rather than an eyeball judgement — which is what
  neither project's X-Trans path has ever had. ⚠ Its own limit is
  §3's: a synthetic mosaic from an sRGB source is not a sensor, so it measures
  the interpolator and nothing about black levels, compression or noise.
- **The 6×6 phase invariant.** `decimate`'s doc comment already states the
  failure mode for Bayer — a stride off the cell boundary "does not look soft, it
  looks like a color-swapped nightmare". The X-Trans version of that assertion
  costs five lines and is the check that a preview-path bug cannot hide behind.
- **`estimateNoise` on a synthetic flat field** with known Poisson–Gaussian
  parameters. §2.2's stride bug is invisible on any real frame — it returns a
  plausible `a` and `b` — and obvious against a fit whose answer is known.

⚠ **What cannot be tested here at all:** whether the result *looks right*. There
is no reference decode of a Fuji frame in this repository and no second
implementation to compare against that is not GPL. A PSNR against a synthetic
mosaic is the honest ceiling, and the write-up should not pretend otherwise.

---

## 6. Honest limits, and the option to decline

- ⚠ **The white-balance collision (§4.2) is the real cost, and it is not
  fixable by choosing a better demosaic.** It follows from decision #29's
  ordering. Any X-Trans path that is not on the GPU has it; the only path that is
  on the GPU is §3's, which has never been compared to Markesteijn.
- ⚠ **`DevelopPipeline.cpp` is 2,549 lines against a stated ceiling of 1,000**
  and is the largest standing violation of a hard constraint. This feature
  branches the **head** of that graph. It should not start until the file is
  split, and `STATUS.md` already names the split as the next thing.
- **The quality question is unanswerable from the literature.** Nobody has
  published a comparison of Markesteijn against anything. Everyone ships it
  because everyone ships it, which is a reasonable prior and is not a measurement.
- **Not supporting X-Trans is a legitimate outcome**, and it is cheaper than a
  half-built one. `CLAUDE.md` calls maintainability a hard requirement and
  `STATUS.md` records "a half-threaded feature" as a failure by name. The
  existing `throw` at `RawImage.cpp:165` is a **good** error message: it says what
  the sensor is, why it is not supported, and where the work is tracked. A
  product that opens Sony and Canon files well and says so plainly about Fuji is
  better than one that opens a Fuji file into a slider that takes four seconds.
- ⚠ **If this is declined, the `throw` should stay and this file should stay
  with it.** The reason not to build it is the interesting part, and it is the
  part that gets lost.

---

## History

- **2026-08-02** — Written. **Research only; nothing built, no build run, no
  gate claimed** (and the tree could not be built — `third_party/slang` is
  destroyed). Decision **#114**; `UNSOURCED.md` §29; the piece table in
  `ROADMAP.md`. ⚠ The licensing premise the story was framed on turned out to
  be **half right and half beside the point**: there genuinely is no published
  description of Markesteijn's algorithm, so a Slang port from darktable or
  RawTherapee is closed — but the same code ships in LibRaw's LGPL-2.1/CDDL-1.0
  core, which Orion already links, so the licence is not what makes this
  expensive. What makes it expensive is decision #29's ordering: the clip sits
  between white balance and demosaic, so a CPU demosaic takes the temperature
  slider off the live path. Also found: the GPU graph gets **smaller** by 5 nodes
  and ~446 MiB, and a 40 MP X-Trans frame puts the *existing* graph at ~11.8 GiB
  independent of any of this.
