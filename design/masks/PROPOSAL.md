# Masks: one place to see them, one gesture to place them

Stream u4, round 2 (2026-10-28). The developer's words: *"Masks should be showing
by default. I can't even edit the masks… None of that is showing on top and I feel
like the mask layering system has to just be more intuitive."*

## What is wrong today (Avalonia shell, base `f978807`)

Root cause first: **the canvas overlay edits a stub that nothing renders.**
`MaskOverlay` drags `MaskModel.Placement`, a record no code ever sends to the
engine, while the panel's rows (`MaskGroup`) do reach `SetMaskGroup`. Add a radial
in the panel and the canvas shows nothing; pose a radial on the canvas and the
photo does not change. Everything below is downstream of joining the two.

Heuristic read of the base (Nielsen, 0–4; impeccable `critique` lens, single-context):

| # | Heuristic | Score | Finding (severity) |
|---|---|---|---|
| 1 | Visibility of status | 0 | No handles, no coverage shown after Add; "Show mask" off by default (P0) |
| 2 | Match to real world | 2 | Cards and shapes read right; "Into the selected mask" is jargon (P2) |
| 3 | Control and freedom | 1 | Mask edits never reach undo: `EditHistory` snapshots the field table and mask rows are not in it (P1) |
| 4 | Consistency | 2 | Lightroom's Add/Subtract per mask is the convention; here combining hides in a context menu (P1) |
| 5 | Error prevention | 1 | Removing a mask shifts every later mask onto the wrong adjustments (`local_*` stay indexed by old layer) (P1) |
| 6 | Recognition over recall | 1 | Which mask is which can only be read off the list; nothing on the photo (P1) |
| 7 | Flexibility | 2 | Sliders for every number, but no direct placement (P1) |
| 8 | Minimalism | 3 | Panel is dense but every control is live |
| 9 | Error recovery | 2 | Refusals reach the status line |
| 10 | Help | 2 | "Drag on the photo to paint." exists for the brush only |
| | **Total** | **16/40** | **Poor**: the core loop (add, see, place) is broken |

## How established editors present it (public docs only)

| Editor | List | Combine | Overlay | On-canvas |
|---|---|---|---|---|
| Lightroom Classic | Masks panel: each mask a row with its components beneath | **Add / Subtract buttons on the selected mask**; Alt turns them to Intersect | `O` toggles; **hover a mask or component shows its overlay**; Shift+O cycles colour | A **pin** per mask selects it; radial and linear handles; brush `[` `]` size, Shift for feather, **Alt erases** |
| Capture One | Layers list, each with one mask and its own adjustments | Draw (B) and Erase (E) on the layer's mask | Always / only while drawing / never (`M`) | Gradient, radial, brush cursors |
| Photomator / Pixelmator | Adjustment layers, one selection each | Brush adds, erase subtracts | Shown while selecting | Gradient handles: blue moves, grey changes softness |

Sources: jkost.com "Making Selective Edits Using the Masking Tools in Lightroom
Classic" (2022); Capture One support articles 360002615317 and community posts on
"Only Display Mask When Drawing"; Pixelmator Photomator user guide, "Masking and
selections". Behaviour only; no code was read.

## The model (inside what the engine renders)

The engine's model is already right (research/masking.md §6, #197, #208): up to 8
components folded left, each `add`/`subtract`/`intersect`, `starts_layer` cutting
runs into **masks**, each mask with its own `local_*` adjustments. Keep it; make it legible.

| Decision | Engine reach | Serves |
|---|---|---|
| A **mask is a card**; its shapes are rows with the op as a glyph (#208, kept) | `starts_layer`, `compose` | N2, N6 |
| The selected card carries **`+` Add and `−` Subtract** buttons, each a kind menu; Intersect lives on the row's op glyph | `compose` 0/1/2 | N4 (Lightroom's convention), N6 |
| Every row: eye, op glyph (click to change), kind; card: eye, name (double-click), context menu Move up/down, Delete | `hidden`, row order | N3, N7 |
| **Structural edits carry each mask's adjustments with it.** Remove, move, merge or split permute `local_*` by which old mask a new mask's first surviving row came from | `local_*[8]` | N5 |
| Mask rows and brush revisions join the **field table** (`mask_components.<field>[row]`), so the existing `EditHistory` snapshots and restores them without a line changed there; the shim keeps every stroke by revision and re-lays it when undo writes an old revision back | `mask_components`, `brush_revision`, `orion_engine_set_brush_stroke` | N3 |
| Names stay off the facade (#207): not undoable here | — | stated limit |

## On the canvas

| Behaviour | Detail | Principle |
|---|---|---|
| **Overlay on by default** while the Mask tab is up and a mask is selected; a session-wide "Show overlay" switch turns it off; leaving the tab removes it | engine red, 0.6, selected layer (#195) | N1; Apple *feedback* |
| The overlay is applied **only inside `shim_render`** and never left in the adjustment block, so an export can never bake it in (#57 by construction) | shim | N5 |
| **Hover a card** in the list: the overlay paints that mask until the pointer leaves, toggle on or off | `mask_overlay_layer` | N6; Lightroom |
| Selected shape: white line on a dark halo, handles; **other masks show a pin** at their centre: click selects, hover previews | — | N6, Apple *spatial consistency* |
| **New radial / linear / brush** from the panel's tool row arms a one-shot draw; a pill on the canvas says so and offers Cancel; **drag** on the photo draws it, a click drops the default size there | `kind`, geometry | N1, N7 |
| Radial: centre, four edges, rotate stem, **feather handle** on the inner ring; linear: both ends and centre | `center_*`, `radius_*`, `angle`, `feather`, `length` | Apple *direct manipulation* |
| Brush: ring at the cursor (outer = size, inner = hard core), `−` in it when erasing; **Alt erases** for the stroke; Size/Feather sliders show the ring on the photo | `brush_*`, per-dab erase | N1, N4 (Alt as in Lightroom) |

Apple's fluid model, decision by decision:

| Principle | Decision |
|---|---|
| Respond on pointer-down | A handle turns accent and grows the instant it is pressed, before any move; a brush press lays its first dab on the press |
| 1:1 tracking, grab offset | Every handle drag applies the pointer's *delta* from the grab to where the handle was, so a handle grabbed off-centre does not jump to the pointer |
| Rubber-banding | Past a limit (radius 0.02–1, length 0.05–1.5, centre inside the photo, feather 0–1) the outline follows with Apple's resistance curve while the engine renders the clamped value |
| Interruptible, velocity handoff | Release past a limit springs the outline back (critically damped, response 0.3 s) from its presented value, carrying the release velocity; pressing again mid-spring grabs it where it is |
| **Momentum: deliberately not** | A mask is a measurement, not a thrown card: a flick that kept moving would land the gradient somewhere the photographer did not aim. Release = rest |
| Spatial consistency | The draw pill enters from the canvas's bottom edge and leaves the same way (180 ms in, 140 ms out, ease-out); new pins appear where the shape was drawn |
| Reduced motion | macOS's Reduce Motion (read through the shim) makes the spring and the pill instant |

Motion, through the find-animation-opportunities gate (frequency, purpose, speed, function):

| # | Where | Purpose | Frequency | Motion |
|---|---|---|---|---|
| 1 | Release past a limit (`MaskOverlay.StartSpring`) | Feedback: the bound is real | Occasional | Critically damped spring, response 0.3 s, release velocity handed in; 16 ms ticks |
| 2 | Draw pill (`MaskOverlay.ShowPill`) | Spatial consistency | Occasional | Opacity 0→1 and translateY 16→0 px, 180 ms ease-out in, 140 ms out the same path |
| 3 | Selected shape's outline (`MaskOverlay.Refresh`) | Prevents a jarring change | Tens/day | Opacity 0→1, 150 ms ease-out; nothing moves |
| 4 | Handle press | Feedback | Tens/day | None: accent and +4 pt on pointer-down, instantly |

Rejected: handle growth on hover (tens a day, decoration); fading the engine
tint in and out (it is a render, not a layer: a cross-fade needs a second copy
of a 97 MB picture or a render per frame); staggering cards into the list
(functional list, rebuilt on every edit). Reduced motion turns rows 1–3 into
cuts.

## After this build (re-scored, same lens)

| # | Heuristic | Before | After | What moved it |
|---|---|---|---|---|
| 1 | Visibility of status | 0 | 3 | Overlay on by default, handles, pins, the draw pill, the brush ring |
| 2 | Match to real world | 2 | 3 | "Add" / "Subtract" on the card, "Feather" for the brush's soft edge |
| 3 | Control and freedom | 1 | 3 | Mask rows and strokes undo; Cancel on the pill. Names do not undo |
| 4 | Consistency | 2 | 3 | Lightroom's Add/Subtract, hover-to-show, Option erases |
| 5 | Error prevention | 1 | 3 | Adjustments follow their mask; the overlay cannot reach an export |
| 6 | Recognition | 1 | 3 | Kinds named as buttons; every mask has a pin on the photo |
| 7 | Flexibility | 2 | 3 | Direct placement plus the sliders; keyboard shortcuts still missing |
| 8 | Minimalism | 3 | 3 | The "Kind" switch is gone: a shape's kind is chosen when it is drawn |
| 9 | Error recovery | 2 | 2 | Unchanged |
| 10 | Help | 2 | 3 | The empty state says what a mask is and how to make one |
| | **Total** | **16/40** | **29/40** | **Good**: weak spots are keyboard and names-in-undo |

## Later (needs engine work, not built)

| Item | Engine change |
|---|---|
| Overlay colour and opacity (Lightroom's Shift+O) | `mask_overlay_color[3]`, `mask_overlay_opacity` in `OrionAdjustments`, read in `develop_linear.slang` |
| Union overlay for hover-all | a second mode for `mask_overlay_layer` |
| A real `MaskShape.BrushErase` | frozen `Shared/Engine/MaskShape.cs` (diff in the report); a side table stands in |
| Names and matte ids in undo | history over a document state, not the field table |
| Subject / Sky / Person detection | not ported (SubjectMatte.swift) |
