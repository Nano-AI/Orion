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

## The three surfaces a scenario can measure

Which one a report lives on is usually the whole difficulty. Each of these was
added because a bug was invisible to the ones before it.

| Surface | How | Catches |
|---|---|---|
| The edited render | `measure <region> <name>` | anything the pipeline computes |
| The canvas | `measure <region> <name> canvas` | the compare split, which composites **two** textures in the blit — invisible to the render alone |
| The interface's own model | `maskcheck <cells> <ev>` | the mask the overlay *draws* disagreeing with the coverage the engine *renders* |

`measure ... canvas` renders through `CanvasBlit` — the real shader, the real
transform — rather than reimplementing the split on the CPU. `maskcheck`
classifies with `CanvasLayout.maskAlpha`, the overlay's own transcription of the
mask kernel. Both are deliberately the *actual* code the interface uses: a
stand-in would be a second implementation with its own bugs and none of the
first's.

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

## What a scenario still cannot see

The brush has no closed form for the overlay to draw, so `maskcheck` refuses it;
a brush stroke is checked by placing dabs at known coordinates and measuring a
grid instead. And nothing here drives a real window, so gesture routing —
which handle a press grabs, whether a drag tracks the cursor — remains the
business of `orion-viewport-tests`.
