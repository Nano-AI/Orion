# The export watermark - one color through a coverage mask

**Where:** `engine/src/util/ImageWriter.mm` (`drawWatermark`), `app/WatermarkRaster.swift`, `app/Watermark.swift`.
Decision #284.

## Source

### The blend

**Porter and Duff's "over" operator.**
A mark of color `C` with coverage `m` over a pixel `D` is `m * C + (1 - m) * D`.

> T. Porter and T. Duff, **"Compositing Digital Images"**, *Computer Graphics* (SIGGRAPH '84 Proceedings) **18**(3), 253-259 (1984).
> [doi:10.1145/964965.808606](https://doi.org/10.1145/964965.808606)

Published, dated, and the definition every compositing system since has implemented, CoreGraphics included.
Orion does not write the arithmetic itself: the writer clips its context to the mask (`CGContextClipToMask`) and fills, and CoreGraphics performs the "over".
Opacity is multiplied into the mask before it crosses the facade, so `m` is coverage times opacity.

⚠ **The blend happens in the file's own encoding, not in linear light.**
That is what "over" means in every image editor's layer stack, and it is what makes 50% opacity look like half, but it is not physically a mix of light.
For a flat gray mark the difference is a slightly different perceived weight at a given opacity, which the opacity slider absorbs.

### The color

A single neutral gray, sRGB 0.5, stated in sRGB and converted by ColorSync into the export's space.
Neutral stays neutral in Display P3 and Adobe RGB because all three share the D65 white.
`testExportWatermark` decodes a Display P3 export and checks the channels agree.

### The layout

Trivial geometry, not an algorithm: a 3x3 anchor with a margin, or centered and rotated by `atan2(h, w)` so the mark runs along the frame's own diagonal, shrunk until its rotated box fits.
Text is shaped by CoreText and measured by its glyph ink, and an SVG is drawn by AppKit's native `NSImage` renderer.
Only alpha is kept, which is what makes a one-color mark a mask.

⚠ **An SVG with an opaque background becomes a gray box.**
The shape is its alpha, and a full-canvas background rectangle is alpha everywhere.
`brand/orion_logo_system_luminous.svg` is such a file; the editor says so under its file button.

## Not sourced, and not needing to be

The defaults - size 4% of the short edge, margin 3%, opacity 35%, diagonal span 60% - are interface choices with nothing to measure, like a font size.
They are not filter constants and are not listed in `UNSOURCED.md`.
