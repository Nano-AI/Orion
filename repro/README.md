# Reproductions

One file per reported bug, run by the app itself:

    ./build/Orion.app/Contents/MacOS/Orion --scenario repro/<name>.txt

Exits nonzero when an `expect` fails, so a report becomes a file that fails until
the bug is fixed and then stays as the regression test. The grammar is documented
at the top of `app/Scenario.swift`.

A scenario drives `Engine`, `CanvasLayout` and `TargetedAdjust` — the same objects
the interface drives — and never reaches around them into the pipeline. A runner
that poked the pipeline directly would exercise code already known to work and
miss the view-model layer, which is where these failures are.

## What each one says right now

| Scenario | Verdict |
|---|---|
| `undo-after-auto.txt` | Passes — fixed: Auto recorded no history entry at all |
| `eyedropper-color-mixer.txt` | Passes — fixed: `sampleAt` read an 8-bit texture as half float |
| `compare-shows-wrong-image.txt` | Passes — measured through the canvas now, not around it |
| `rotate-then-compare.txt` | Passes — same, and it took saturation as well as luma to make it honest |
| `geometry-while-comparing.txt` | Passes — fixed: the held original is re-taken when the geometry moves |
| `mask-alignment.txt` | Passes — fixed: a radial mask's semi-axes were swapped on every odd quarter turn |
| `eyedropper-latency.txt` | A measurement, not an assertion: 2.4 µs a read |
| `slider-drag-cost.txt` | A measurement, and an open story: 9.4 / 65.7 / 116.4 ms a tick |
| `eyedropper-under-a-crop.txt` | Passes — fixed: the scene sample undid the quarter turn and not the crop |
| `preview-carries-the-mask.txt` | Passes — fixed: strokes, mattes and LUTs never reached the preview graph |
| `matte-does-not-follow-the-photo.txt` | Passes — fixed: a reused graph kept the previous photo's matte |
| `analysis-render-has-no-overlay.txt` | Passes — fixed: Vision was handed the red coverage overlay |
| `matte-survives-a-reopen.txt` | Passes — fixed: a raster matte was written nowhere, so a Subject, Person or Sky row reopened present and empty |
| `sky-mask.txt` | Passes — ⚠ its refusal half asserted nothing until 2026-07-31: it never called `select`, so it was green whatever the detector did |
| `dehaze-reaches-the-picture.txt` | Passes — ⚠ closes the gap where dehaze could be **deleted** from the product and all three suites plus the bench stayed green |
| `auto-applies-every-field.txt` | Passes — ⚠ Auto writes five fields and `undo-after-auto.txt` caught **none** of the five being dropped |
| `perspective-carries-the-mask.txt` | Passes — the three fields through the whole loop, the zoom leaving no hole, and a mask still on its subject with the correction up. ⚠ Its mask half sits deliberately at the **edge** of the range the first-order extent is exact over (`UNSOURCED.md` §24) |
| `export-depth-and-sharpening.txt` | Passes — the three export controls that fail invisibly. ⚠ Its metadata half is one-sided by necessity: the sample frames have no GPS, so location itself is asserted in `orion-tests` against a stand-in file |
| `snapshot-survives-a-reopen.txt` | Passes — a saved version survives a reopen, restores every part of the edit including the crop and the dust, and is one ⌘Z away from the edit it replaced. ⚠ Its dust check has a *bare* reading taken before the spot exists, because an equality between two readings of a spot that never moved a pixel is green either way |
| `snapshot-keeps-its-matte.txt` | Passes — fixed: the matte sweep deletes what the sidecar does not name, so a version saved with a Subject mask restored a mask **covering nothing**. ⚠ The check is `snapshot missing … 0` plus the three bands against a bare baseline; removing `.union(pinned)` from `MatteStore.sweepAfterLoad` prints seven failures and the exact silent picture-change |

## The surfaces a scenario can measure

Which one a report lives on is usually the whole difficulty. Each of these was
added because a bug was invisible to the ones before it.

| Surface | How | Catches |
|---|---|---|
| The edited render | `measure <region> <name>` | anything the pipeline computes |
| The canvas | `measure <region> <name> canvas` | the compare split, which composites **two** textures in the blit — invisible to the render alone |
| The preview graph | `measure <region> <name> preview` | what the photographer sees *during* a drag, which the settled picture cannot show |
| The analysis render | `measure <region> <name> analysis` | the picture handed to Vision — neutralised geometry, no overlay — which nothing on screen ever shows |
| The interface's own model | `maskcheck <cells> <ev>` | the mask the overlay *draws* disagreeing with the coverage the engine *renders* |
| The written file | `probe <path> <property> <name>` | depth, GPS, IPTC place, camera EXIF and acutance of a file that was actually exported — the only surface that can see a control which is wired to nothing, because every one of these failures still opens as the photograph |

**The library has no surface here at all**, because a scenario drives `Engine`
and `Library` is not on that path. Its equivalent is a mode of its own:

    ./build/Orion.app/Contents/MacOS/Orion --library-open <folder>

It opens the folder three times in one process — cold, warm, and with no index
at all — and fails when the warm pass did not *hit*, or when the three disagree
about any field a listing shows. ⚠ Its first draft asserted `misses == 0`, which
a `Library` that never consults the index satisfies perfectly: zero attempts,
zero misses, and a four-times-slower open reported as a pass. The seventh
instance of the class this file exists to record.

`measure ... canvas` renders through `CanvasBlit` — the real shader, the real
transform — rather than reimplementing the split on the CPU. `maskcheck`
classifies with `CanvasLayout.maskAlpha`, the overlay's own transcription of the
mask kernel. Both are deliberately the *actual* code the interface uses: a
stand-in would be a second implementation with its own bugs and none of the
first's.

⚠️ **"The picture changed" is one assertion, however many things the control
writes.** The Auto button sets five fields. `undo-after-auto.txt` asserted the
frame moved and that one undo put it back — and every one of the five
assignments could be deleted on its own with that file still green, because the
remaining four still move the frame. Measured, 5 for 5. When a control writes
more than one thing, assert the things, not the outcome: that is what the
`control` verb is for.

⚠️ **A GPU test proves the mathematics; only a scenario proves it is reachable.**
`apps/tests` dispatches each kernel directly with parameters it sets itself, so
it is blind to the wiring — whether the node is scheduled, whether the slider's
value arrives, whether the gate lets it run. Dehaze was disabled at the host in
one line and `orion-tests` (525), `orion-viewport-tests` (3437) and the bench all
stayed green with the feature gone from the product. Every control needs one
check that starts at `Engine`.

⚠️ **A check written *around* an inconvenience usually asserts nothing.** The sky
scenario wanted to pin that a night frame is refused; `select` throws on a
refusal and a throw fails the run, so it settled for asserting the picture was
unchanged after "asking" — without asking. With no mask row a local exposure
does nothing, so it was green by construction, and the claim in `STATUS.md` that
rested on it was false. The verb it needed took nine lines. When a check has to
be phrased as the absence of an effect, ask what makes the direct assertion
awkward and fix *that*.

⚠️ **A fixture with no mid-values cannot see a mid-value bug.** Every matte
fixture here was binary — a disc, a half-plane — and a binary matte survives a
wrong colour space, a wrong bit depth and a wrong byte order, because 0 and 1
land on 0 and 1 however the curve between them is mangled. The `ramp` shape
exists for that reason, and the persistence bug it guards against (grey being
gamma-managed on the way to disk, so 0.5 comes back 0.735) is invisible to every
other fixture in this folder.

⚠️ **The runner has to read what the interface reads, not what is convenient.**
`pick` derived its hue band from the *display* colour while `ImageCanvas` derives
it from the *scene* colour — two different samples down two different code paths
— so the eyedropper scenario exercised the one that could not go wrong. That is
the same fidelity gap the `crop` verb had when it skipped `commitCropEdit`, and
it is worth checking for whenever a verb stands in for a gesture.

## ⚠️ Two lessons these files paid for

**One number per patch is not a signature.** `expect a == b` between two
recordings compares mean saturation as well as mean luma, because a
rotate-while-comparing check passed against an entirely different picture: the
two frames agreed on mean luma to 0.0035, inside the one-code tolerance, while
differing by 0.28 in saturation.

**A test of coverage has to be two-sided.** `maskcheck` demands that cells the
interface draws *clear* come back bit-identical, not merely that covered cells
moved. A mask shifted by a tenth of the frame still darkens roughly the right
region and still looks plausible in a screenshot; what it cannot do is leave the
clear cells untouched.

⚠️ **The suite can be green because it was never in a position to fail.** A
running app compiled a shader rebuilt underneath it, bound five textures to a
kernel that wanted six, and rendered every mask as blank — while every mask test
passed, because the tests ran the binary the shader was built with. No scenario
here could have caught that: they all exercise the matched pair. What caught it
was the session log, which dated the photo open against the process start; what
prevents it now is `testBindingCount`, which asks Metal what the *compiled*
kernel needs. When a class of bug is invisible to a suite by construction, the
fix is an assertion at the boundary, not more coverage inside it.

⚠️ **A scenario that counts must start from a known state.** A version file lives
beside the photograph and outlives the run, so `snapshot count 1` passed on the
first run of `snapshot-survives-a-reopen.txt` and failed on the second — and it
failed *loudly*, which is the only reason it was not shipped. `snapshot clear` is
the first line of both version scenarios for that reason. A check whose answer
depends on what was run before it is not a check.

## What a scenario still cannot see

The brush has no closed form for the overlay to draw, so `maskcheck` refuses it;
a brush stroke is checked by placing dabs at known coordinates and measuring a
grid instead. And nothing here drives a real window, so gesture routing —
which handle a press grabs, whether a drag tracks the cursor — remains the
business of `orion-viewport-tests`.
