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
| `compare-shows-wrong-image.txt` | Passes, and cannot see the reported bug — see below |
| `rotate-then-compare.txt` | Passes, same limitation |

## ⚠️ What a scenario cannot see

It measures `engine.outputTexture`, which is the *edited* render. Compare composites
two textures in the canvas view, so a compare bug that lives in that compositing
is invisible here and these two passing scenarios are **not** evidence that the
reported behaviour is fine. Catching that needs the split to be measured through
the canvas, which is the next thing this runner should learn.
