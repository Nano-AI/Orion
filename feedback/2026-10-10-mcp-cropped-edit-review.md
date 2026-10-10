# Five MCP findings from editing a cropped photograph

An outside agent edited a cropped Chicago frame (`DSC00221.ARW`) through the
MCP tools and reported five problems.
Each one is reproduced or refuted below, with the measurement.
Four were valid; one was already settled and only undocumented.

## The findings, as reported

1. Two coordinate systems.
   The measuring and preview tools use the cropped view, but masks are placed on the full, uncropped frame.
   Every position had to be converted by hand using the crop values, and no tool does it.
   If the crop changes later, these masks will not move with it.
2. Cannot tell where the global vignette is centred: on the crop or on the full frame.
3. Layer tint is reversed compared with global tint.
   Raising tint on a mask layer moves colour toward green; the global slider pushes toward magenta.
   Suspected before, now confirmed.
4. Reading the current settings writes a proposal.
   The only way to see the current edit settings was an empty `propose_edit`, which writes a proposal file.
5. Masks do not explain their purpose.
   Feathering on the cut-out oval made it protect much less than its size suggested; at 0.8 the vignette still darkened the bottom of the buildings.

## Verdicts

| # | Verdict | What was true |
|---|---|---|
| 1 | Valid tooling gap; the design half is correct | Masks are frame-anchored on purpose (#112, #203): a recrop leaves a mask on the subject it was placed on, which is the behaviour the reviewer feared losing. What was missing is the conversion: `detect_faces` carried a box into frame space, but nothing did it for an arbitrary region. |
| 2 | Already settled, undocumented | Decision #103: the vignette's circle is the crop's (`DevelopOutput.cpp:170-174`, `tests_vignette.cpp` `testCompositionCircle`). The agent vocabulary said nothing about it. |
| 3 | Real product bug | `develop_linear.slang` multiplied green UP for positive layer tint. The global axis (`WhiteBalance.cpp:70-73`, positive is magenta) and the layer slider's own green-to-magenta track label (`TrackTint.localTint`) both said the opposite. The GPU test at `tests_linear.cpp:386-398` had pinned the wrong sign. |
| 4 | Valid | No read-only verb existed; `apply` with empty edits was the only reader and it writes `.proposed.json`. |
| 5 | Documentation | The `maskNote` said "feather is how far inside the boundary the ramp starts", which is correct and easy to misread: at 0.8 only the inner 20% of the radius is fully covered, so a subtract row protects far less than its outline. |

## Evidence

**Tint (finding 3).** `overexposed-background.ARW`, one kind-2 radial at 0.5,0.5 with radius 0.6 and feather 0, one layer with tint 1.0, read with `--agent stats --region 0.4,0.4,0.2,0.2`:

| Run | red | green | blue | luma |
|---|---|---|---|---|
| No edit | 0.1289 | 0.0847 | 0.0126 | 0.0889 |
| Before the fix, tint +1 | 0.0888 | **0.0965** | 0.0083 | 0.0885 |
| After the fix, tint +1 | 0.1855 | **0.0673** | 0.0196 | 0.0890 |
| After, the pre-fix proposed file (no marker) | 0.0888 | 0.0965 | 0.0083 | 0.0885 |
| After, the same file with `localTintAxis: 1` | 0.1855 | 0.0673 | 0.0196 | 0.0890 |

Before, +1 tint raised green. After, green falls and red and blue rise, with luma within 0.1%.
The file without a marker renders identically to the pre-fix render, so a legacy sidecar keeps its approved look.
Reverting the shader sign fails both new GPU asserts; disabling the decoder conversion fails four viewport asserts.

**Region (finding 1).** Through the real MCP server on a temp copy of the same file, a 5320 x 7968 quarter-turned portrait.
`get_stats` with `region: [0.4,0.4,0.2,0.2]` returned `frame {centerX 0.5, centerY 0.5, radiusX 0.1, radiusY 0.1}`; the non-square `[0.1,0.4,0.4,0.1]` returned `frame {centerX 0.55, centerY 0.30, radiusX 0.05, radiusY 0.20}`, the axes swapped as a turned frame demands.
That frame object pasted into a kind-2 radial (feather 0.1) under a -3 EV layer:

| Region | As shot | Proposed |
|---|---|---|
| inside the region | 0.1652 | 0.0152 |
| same band, other side | 0.3707 | 0.3707 |
| same columns, higher up | 0.5976 | 0.5976 |

**State (finding 4).** `get_edits` on the same copy returned `source: "as-shot"`, `hasProposed: false` and a 52-key state; the directory afterwards held only the RAW.
An empty `propose_edit` returned a state equal to it in every key and value, and against a stale binary the new gate steps failed with "region has no frame object" and "unknown verb 'state'" before passing on the fixed one.

## What changed

- **Layer tint follows the global axis** (#290).
  Positive is magenta on both sliders now.
  A sidecar written before this carried the old sign, and a scan of the photographer's library found 18 of 228 sidecars with a nonzero layer tint (-0.45 to +0.35), so a bare sign flip would have changed 18 finished photographs.
  `DevelopState.localTintAxis` gates it the way `maskSpace` and `process` do: absent with a nonzero layer tint means legacy, and the decoder negates on the way in so the picture renders as it was approved.
  The encoder always writes 1.
- **`get_stats` with `region` returns `frame`** (#291): the region's centre and half-extents carried into frame space through the one implementation `detect_faces` uses, ready to paste into a kind-2 radial.
- **`get_edits` and `Orion --agent state`** (#292): a read-only view of the current or proposed settings that writes nothing.
- **Vocabulary**: `vignetteAmount` now says it is centred on the crop; the radial's `feather` note states the 1 - feather rule in numbers; `layers` states the tint axis.
- **`tools/worktree-setup.sh`** refused every worktree since SwiftTerm was checked in under `third_party` (#266), because it tried to link the whole directory.
  It now links only the untracked toolchain entries and a rerun is harmless.

## Follow-up the same day: the first proposal flipped a new tint (#293)

Minutes after the fix was built, the photographer's own agent session set a face-layer tint of +0.05 and `propose_edit` returned -0.05 beside `localTintAxis: 1`.
Cause: `mergeEdits` overlaid the edits as raw JSON onto a base written before the marker existed and decoded only the result, so the decoder read the agent's new-convention value as legacy.
Only the first proposal of an edit could hit it; later ones merged onto a file that already carried the marker.
The base is now decoded and re-encoded before the overlay.
Reproduced with a hand-made legacy base: tint +0.05 came back -0.05 before and +0.05 after; an edit not naming `layers` returned the base's converted -0.2 both times.

## Limits

- None of the 18 real sidecars was opened in the app before and after; the bit-identical legacy render was shown on the sample file only.
- The face path after the refactor has no sample with a face to run on; it is covered by the pure viewport tests and the arithmetic being unchanged.
- The layer cast's 0.30 strength is not cited in `research/`; it was found, not fixed.
- The editing skill outside this repository that recorded "layer tint is inverted" was updated the same day; recipes written under the old sign need their layer tint sign flipped.
