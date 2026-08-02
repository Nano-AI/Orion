# Orion — Algorithm Sources

**Every technically non-trivial filter in Orion must cite a published reference here.**
If it does not appear in these files, it is invention and belongs in
[`UNSOURCED.md`](UNSOURCED.md) until it is either replaced or defended.

This folder exists because the color pipeline shipped a bug — a purple cast on
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
| [`color-pipeline.md`](color-pipeline.md) | Scene-referred design, camera matrices, white balance, AgX display transform |
| [`camera-profiles.md`](camera-profiles.md) | The rest of a DNG profile — BaselineExposure and HueSatMap |
| [`demosaic.md`](demosaic.md) | CFA interpolation, and the gap between what we ship and RCD proper |
| [`demosaic-xtrans.md`](demosaic-xtrans.md) | M5's X-Trans line — why Markesteijn has no published description, why that stopped mattering, and what decision #29 charges for it. **Research only; nothing built** |
| [`tone-and-local-contrast.md`](tone-and-local-contrast.md) | Guided filter, local shadows/highlights, curves |
| [`highlight-reconstruction.md`](highlight-reconstruction.md) | Clipped highlights beyond the window fit — why the segmentation pass is not needed, and the Dirichlet solver that replaces it |
| [`local-laplacian.md`](local-laplacian.md) | Clarity — Paris et al. 2011, and Aubry et al.'s fast approximation |
| [`dehaze.md`](dehaze.md) | The dark channel prior, and the guided filter as its refinement |
| [`luts.md`](luts.md) | `.cube` files and tetrahedral interpolation |
| [`exposure-fusion.md`](exposure-fusion.md) | Shadow lift that keeps local contrast — Mertens; Hessel & Morel |
| [`auto-enhance.md`](auto-enhance.md) | Percentile auto-levels, and what has no published target |
| [`color-grading.md`](color-grading.md) | Three-way grading wheels, as ASC CDL per tonal zone — and why split toning is not a second panel |
| [`vignette.md`](vignette.md) | The **creative** vignette — cos⁴ natural falloff, post-crop. Not the lens correction |
| [`detail.md`](detail.md) | Sharpening and denoising |
| [`denoise-learned.md`](denoise-learned.md) | M5's Core ML denoise — the domain question, the licences, and why it is not a graph node. **Research only; nothing built** |
| [`lens-corrections.md`](lens-corrections.md) | Distortion, TCA and vignetting; the vendored lensfun database |
| [`perspective.md`](perspective.md) | Keystone correction — the 4-point homography, the zoom, and why it is not its own node |
| [`masking.md`](masking.md) | Plan of record for M4's local edits — not built |
| [`deep-research-2026-07-27.md`](deep-research-2026-07-27.md) | A research run's raw findings, referenced by several entries above |
| [`UNSOURCED.md`](UNSOURCED.md) | **Honest register of what is still invention** |

Keep this table complete. It went eight files stale once, which is how a
codebase ends up with research nobody reads because nobody knows it is there.

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
