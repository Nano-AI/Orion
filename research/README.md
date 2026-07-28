# Orion — Algorithm Sources

**Every technically non-trivial filter in Orion must cite a published reference here.**
If it does not appear in these files, it is invention and belongs in
[`UNSOURCED.md`](UNSOURCED.md) until it is either replaced or defended.

This folder exists because the colour pipeline shipped a bug — a purple cast on
every image — caused by matrices that looked plausible and were wrong. That
class of error is invisible to inspection and obvious to arithmetic. Citing the
source makes the constants checkable; testing the invariant makes them stay
right.

## The bar

A citation qualifies when it is:

1. **Published** — a paper, a standard, or reference source code with a stable URL.
2. **Dated** — so a newer result can supersede it and we can tell when.
3. **Established** — used in shipping software or widely cited, not merely available.
4. **Reputable** *(preferred, not required)* — a lab, standards body, or a
   maintainer of a serious open implementation.

Where a claim rests on something weaker than this, say so in the entry. An
honest "widely used, no formal publication" beats a citation that does not
support what it is attached to.

## Files

| File | Covers |
|---|---|
| [`colour-pipeline.md`](colour-pipeline.md) | Scene-referred design, camera matrices, white balance, AgX display transform |
| [`demosaic.md`](demosaic.md) | CFA interpolation, and the gap between what we ship and RCD proper |
| [`tone-and-local-contrast.md`](tone-and-local-contrast.md) | Guided filter, local shadows/highlights, curves |
| [`detail.md`](detail.md) | Sharpening and denoising |
| [`UNSOURCED.md`](UNSOURCED.md) | **Honest register of what is still invention** |

## How to challenge an entry

If independent research contradicts something here, that research wins. Open
the relevant file, replace the citation, and note what changed and why in the
entry's **History** section. Superseded entries are struck through rather than
deleted, so the reasoning stays legible.

## Licensing note

darktable and RawTherapee are GPL, so their **code** cannot be copied into
Orion. Their **algorithms** can be implemented from published descriptions —
mathematics is not copyrightable. Where an entry derives from a GPL project's
documented approach rather than its source, the entry says so explicitly.
