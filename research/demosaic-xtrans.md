# X-Trans — the licensing question first, and it has an answer

Written 2026-08-02 for M5's line "X-Trans support (Markesteijn)", which has sat
on the roadmap since the first milestone list and had never been investigated.

**Research and decomposition only. Nothing was built, no build was run, and
neither test suite was run — there was nothing to test.** That was the brief and
it is the outcome.

`research/demosaic.md` holds the entry for the Bayer path that ships today; its
last line reads *"X-Trans sensors need Markesteijn instead; Bayer only for
now"*, and this file is what sits behind that sentence.

---

## 0. The recommendation, in one sentence

**Ship X-Trans by calling LibRaw's own `xtrans_interpolate` on the CPU at
decode** — it is Markesteijn's algorithm, it is already installed on this
machine, and it is **LGPL-2.1 / CDDL-1.0, not GPL** — rather than porting
Markesteijn to Slang, which is the story everyone assumes this line is.

---

## 1. ⚠ The licensing question, answered honestly

The brief's question was the right one:

> Does a description of this algorithm exist outside GPL source code, in a form
> someone could implement from — a paper, a patent, an article, a
> specification, the author's own writing?

**Two answers, and the second one makes the first one not matter.**

### 1a. No published description of Markesteijn's algorithm exists

Searched, and found nothing that meets `research/README.md`'s bar — published,
dated, established. What exists is:

| Artefact | What it is | Meets the bar? |
|---|---|---|
| Frank Markesteijn's original code | Contributed to dcraw; Dave Coffin merged it. No accompanying paper, no write-up, no author's blog | ❌ Source only |
| dcraw's `xtrans_interpolate` | Coffin's C, one function, no derivation | ❌ Source only |
| RawTherapee's `xtransdemosaic.cc` | GPL-3. RawPedia describes *what it does* (1-pass/3-pass), not *how* | ❌ GPL source; the prose is a user manual |
| darktable's `xtrans.c` | GPL-3, adapted by Dan Torop from Markesteijn | ❌ GPL source |
| darktable manual, demosaic module | "Markesteijn 1-pass / 3-pass", quality-vs-speed guidance | ❌ Not a description |

⚠ **So the honest finding is: the only extant description of Markesteijn's
algorithm *is* source code.** There is no paper to implement from. Reading
darktable's or RawTherapee's `xtrans.c` and re-typing it in Slang would be
copying GPL code, not implementing a published algorithm from its description,
and `CLAUDE.md` forbids exactly that. **That route is closed.**

### 1b. ⚠ But Markesteijn's code also ships under LGPL-2.1 / CDDL-1.0 — inside LibRaw

This is the finding that changes the shape of the story, and it was checked on
this machine rather than remembered.

`/opt/homebrew/opt/libraw/include/libraw/libraw.h:451`:

```cpp
  void xtrans_interpolate(int);
```

`/opt/homebrew/Cellar/libraw/0.22.2/COPYRIGHT`:

> LibRaw is free software; you can redistribute it and/or modify it under the
> terms of the one of two licenses as you choose:
> 1. GNU LESSER GENERAL PUBLIC LICENSE version 2.1
> 2. COMMON DEVELOPMENT AND DISTRIBUTION LICENSE (CDDL) Version 1.0

and `LibRaw/src/demosaic/xtrans_demosaic.cpp` carries the same dual grant in its
own header, plus the attribution *"Frank Markesteijn's algorithm for Fuji
X-Trans sensors"*.

⚠ **The GPL demosaic code is in a different repository.** LibRaw historically
split its interpolators: AMaZE, DCB variants, LMMSE, AFD and the rest lived in
`LibRaw-demosaic-pack-GPL2` / `-GPL3`, which is why "LibRaw's demosaics are
GPL" is a thing people say. **`xtrans_interpolate` is not one of those.** It is
in the LGPL/CDDL core, because it came from dcraw and dcraw's non-RESTRICTED
code carries no such encumbrance — the COPYRIGHT file says so in as many words:
*"LibRaw do not use RESTRICTED code from dcraw.c"* (the RESTRICTED set is the
Foveon and secret-decoder functions, not the demosaics).

**Orion already links LibRaw** (`engine/CMakeLists.txt:49-61`, `libraw_r`,
Homebrew, dynamically). LGPL-2.1's condition for a proprietary program is
dynamic linking plus the ability to relink — which is exactly the arrangement
already in place for every other thing LibRaw does here. **Nothing about the
licence model changes.** And CDDL-1.0 is per-file copyleft, so even a vendored
static build has a documented route.

### 1c. What that means for the story

| Route | Licence | Verdict |
|---|---|---|
| Port Markesteijn to Slang from darktable/RawTherapee | GPL-3 | ❌ **Forbidden.** No published description to launder it through |
| Reimplement Markesteijn from a paper | — | ❌ **Impossible.** No paper exists |
| **Call LibRaw's `xtrans_interpolate`** | **LGPL-2.1 / CDDL-1.0** | ✅ **Open, and already a dependency** |
| Implement a *different*, published X-Trans method on the GPU | see §3 | ⚠ Possible, worse quality, more work |
| Don't support X-Trans | — | ⚠ A real option; see §6 |

---

## 2. What breaks today, concretely

*(to be filled — trace of `linearize`, `rcd:*`, `estimateNoise`, white balance)*

## 3. The published alternatives

*(to be filled)*

## 4. Node and memory cost

*(to be filled)*

## 5. Testing without an X-Trans camera in the room

*(to be filled)*

## 6. Honest limits

*(to be filled)*

---

## History

- **2026-08-02** — Written. Research only; nothing built.
