# The retired repro sweep, mapped (decision #380)

`repro/*.txt` ran through the SwiftUI app's `--scenario`, which drove its view
models. With that app gone they no longer run. This table says what covers
each one now, so anything lost is lost on purpose and on the record. The sweep
was not rebuilt (the coordinator's ruling, fix round 1).

How to read the "Now" column:

- **selftest**: `Orion RAW --selftest`. The quoted words are the check's own description.
- **orion-tests**: a `section(...)` in `apps/tests`.
- **check-agent**: a step in `tools/check_agent_steps.py`.
- **uncovered**: nothing checks it any more. The reason is given.

A row marked *partly* names what is still checked and what is not.

| Scenario | What it guarded | Now |
|---|---|---|
| agent-verbs | the Swift runner's own `maskrow`/`load`/`state` verbs | uncovered: these were verbs of the scenario language itself, retired with it; the agent CLI's `state` verb is check-agent `get_edits` |
| analysis-render-has-no-overlay | Vision got a red-tinted frame while Show mask was on | partly: selftest "the overlay is not an adjustment: no field carries it"; the analysis render itself is unchecked |
| auto-applies-every-field | Auto writes all five fields | selftest "Auto writes the sliders it owns, rereads them, and changes the render" |
| balance-survives-a-reopen | gradeBalance through sidecar and decode | partly: selftest "Session: all … field slots round-trip" + "decode ∘ encode is a fixpoint"; no reopen of balance through a real sidecar |
| brush-erases | Alt-painting erases a brush mask | selftest "a brush stroke brightens its mask and Alt-painting over it erases the coverage" |
| colour-range-mask | colour range mask renders | orion-tests "Color range masks (GPU)"; selftest "the colour picker takes the scene colour…" |
| compare-shows-white-balance | compare's original ignored WB | partly: selftest "at 45° the original shows on its side … and the edit on the other"; WB specifically uncovered |
| compare-shows-wrong-image | compare showed another photo/settings | selftest "compare on: the toolbar says so and the canvas shows the original on the left" |
| degrade-then-refine | sliders render a preview graph, then the full one | selftest "a swap shows the preview graph, and a rest renders the full one…", "mid-gesture the full render waits" |
| dehaze-reaches-the-picture | dehaze slider reaches the render | orion-tests "Dehaze, dark channel prior"; partly: no selftest drives the slider |
| desktop-interaction-reliability | Engine + ProposalWatcher disk/GPU workflow | selftest Parity proposal checks ("Approve writes the proposal to the sidecar…", "leaving a photo mid-proposal…") |
| eight-masks | eight mask rows end to end | orion-tests "Eight mask slots (GPU)"; selftest "a ninth mask row is refused" |
| export-depth-and-sharpening | bit depth, metadata, output sharpening | orion-tests "Output depth", "Export"; partly: metadata and output sharpening through the app's sheet uncovered |
| export-ignores-the-overlay | export carries no coverage overlay | selftest "the overlay is not an adjustment: no field carries it, so history and export never see it" |
| export-watermark | watermark placement and coverage | selftest watermark checks ("an anchored mark…", "the watermark reaches the encoder"); orion-tests "Export watermark" |
| eyedropper-color-mixer | eyedropper read an 8-bit texture as half float | partly: selftest "the targeted tool moves the array its mode names"; the sample's value uncovered |
| eyedropper-latency | an instrument (minchecks), not a test | uncovered: it measured and asserted nothing |
| eyedropper-under-a-crop | scene sample picked the wrong pixel under a crop | uncovered: no check samples under a crop. Worth porting if the eyedropper is touched |
| flat-frame-is-not-a-photograph | a one-colour render is named | selftest "a frame of one colour is named…", "the photo is not a flat frame" |
| geometry-while-comparing | compare's original under crop/rotate | partly: selftest compare checks at fit; under geometry uncovered |
| gesture-preview-agrees | the preview graph never changes what you get | selftest "once the overlay rests, the full graph follows its preview render"; orion-tests "A noisy shadow: … the preview renders it as the release does" |
| grain-survives-a-reopen | film grain through sidecar and decode | partly: as balance; orion-tests "Film grain wiring" |
| inspection | agent inspection verbs (#278) | check-agent `get_stats_region`, `get_proxy_region`, `detect_faces`; selftest "agent: --region …" |
| lens-choice-leaks-a-render | a render leaked between two photos | uncovered: the Swift Engine+Document race; the Avalonia session model differs (Speed checks: "Acquire never hands out a closed session") |
| lens-chosen-by-hand | a lens chosen by hand | selftest Tools lens profile check; orion-tests "Lens database" |
| local-adjustments | a mask carries more than exposure | orion-tests "Local adjustments (GPU)"; selftest "+1.5 EV on the radial brightens inside it only" |
| local-tone-bands | the four tone bands on a mask | orion-tests "Tone bands with the guide chain off (GPU)" |
| long-brush-stroke | strokes past 256 dabs | orion-tests "Long brush strokes" |
| mask-add-stacks | a new mask starts its own layer | selftest "a new mask starts at zero" |
| mask-alignment | radial misplaced under odd quarter turns | orion-tests "The frame-to-display map as one matrix", "Mask geometry" |
| mask-follows-the-frame | a mask stays on its subject as geometry moves | orion-tests "Perspective — masks and spots follow", "Brush dabs under crop and rotation" |
| mask-layers | independent layers | selftest "deleting a mask carries every later mask's adjustments…", "a moved mask takes its adjustments along" |
| mask-merge | merging keeps the upper mask's adjustments | selftest "a merge keeps the upper mask's adjustments" |
| mask-rows | eye button, reorder, rows list | selftest "a removed mask shifts the rest down a slot", "a reordered matte row keeps its own raster"; partly: eye button uncovered |
| mask-split-stale | unlinking must not carry the previous mask's edits | selftest "a split's second half starts clean" |
| mask-survives-the-fix | display-space-era masks land where they rendered | orion-tests "A legacy display-space mask, converted to frame space"; selftest "matte sidecar: masks without a marker are display-space" |
| mask-visibility | a hidden mask contributes nothing | uncovered in the app: no selftest toggles `hidden`; the engine's skip is in orion-tests "Masks (GPU)" only if hidden is passed through. Worth porting |
| matte-does-not-follow-the-photo | a matte leaked to the next photo | partly: selftest "a corrupt matte … reads as a missing file"; a leak across a swap is uncovered |
| matte-follows-the-frame | a raster matte under geometry | orion-tests "Matte masks (GPU)" |
| matte-survives-a-reopen | the matte saved and read back | selftest "a sidecar's matte is read back on open and brightens what it covers" |
| overlay-shows-the-edited-layer | Show mask paints the selected layer | selftest "the overlay paints the selected mask by default, only on the Mask tab" |
| perspective-carries-the-mask | perspective moves masks and spots | orion-tests "Perspective — masks and spots follow", "Perspective wiring" |
| preset-is-a-patch | a preset patches only its groups | selftest "undo takes the preset back in one step", "Save keeps the look in the presets file"; selftest Matte sync "Sync writes only the light keys" (the same patch rule) |
| preview-carries-the-mask | the preview graph got brushes and mattes | orion-tests "Brush prefix wiring", "Mask refine runs only where a layer ends (GPU)"; partly: matte in the preview graph uncovered |
| process-version | process 2 keeps a midtone off the Highlights flank | selftest "legacy sidecar: … no tone bands keeps process 2", "a legacy edit using its tone bands is not handed a process" |
| range-mask | luminance range mask | orion-tests "Range masks (GPU)" |
| rotate-then-compare | rotating during compare | partly: selftest compare checks; rotation during compare uncovered |
| select-adds-a-row | `select` adds a row, never overwrites | selftest "Subject writes its matte beside the photo first, then a row naming it" |
| sky-mask | the sky detector | selftest "Person and Sky either fill a row or say why not"; orion-tests "Align: a blank sky is a refusal" |
| sky-selection-42mp | sky on a 42 MP frame | uncovered: needs a 42 MP sample, not among the gate's samples |
| slider-drag-cost | an instrument, not a test | uncovered: it measured; `--bench` measures the same drag |
| snapshot-keeps-its-matte | a version keeps its matte | selftest "a version naming a matte that is not beside the photo says so", "undoing a version restore brings back the masks it replaced" |
| snapshot-survives-a-reopen | versions file round-trip | selftest "a version is saved trimmed, replaced by name, in Swift's shape…", "restoring keeps the working edit first" |
| spot-heal-vs-clone | heal against clone | orion-tests "Spot removal (GPU)"; selftest "Heal/Clone and Size set the next spot" |
| spots-are-draggable | spots move by drag | partly: selftest "a spot placed on the canvas reaches the engine…"; dragging an existing spot uncovered |
| subject-selection-42mp | subject on a 42 MP frame | uncovered: as sky-selection-42mp |
| subject-selection | Vision subject selection | selftest "Subject writes its matte…", "the selection reaches the picture" |
| undo-after-auto | Auto is one undo step | partly: selftest "an edit is one undo step named for its field"; Auto's own step uncovered |
| unreadable-sidecar-keeps-the-work | an unparsable sidecar costs nothing | selftest "a sidecar that exists but cannot be read is never written over", "an edit that is not JSON opens the photo as shot and says so" |
| vignette-follows-the-crop | the vignette follows the crop | orion-tests "The vignette follows the crop", "Creative vignette (GPU)" |

**Uncovered outright (8):** agent-verbs, eyedropper-latency, slider-drag-cost
(none of these three was a test of the product), eyedropper-under-a-crop,
lens-choice-leaks-a-render, mask-visibility, sky-selection-42mp and
subject-selection-42mp. The partial rows above are the rest of the loss. If
any of these is touched, port mask-visibility and eyedropper-under-a-crop
first: both were user reports.
