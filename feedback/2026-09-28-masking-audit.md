# Masking correctness and performance audit

2026-09-28. Read-only product audit; only this report was added. No application,
GPU suite, RAW decode, scenario sweep, or memory stress test was run. This follows
the developer's request to avoid further memory pressure during the wider audit.
Findings below are source-traced defects or costs, **not freshly measured render
results**. The parent audit owns Pipeline cache retention separately.

**Revalidated state:** `c31c9645b10ed590bc8e1fa35115ab007799d2c6`, after merging
upstream `bda5847`; initial audit baseline was `40618db`. All eight findings
remain open in this state. Relevant upstream diffs and current call sites were
read again; no tests, builds, GPU work or app launches were performed for this
revalidation. Line references below describe this current state.

## Ranked findings

| Priority | Finding | Confidence |
|---|---|---|
| P1 | Hiding a later mask applies its adjustments through the preceding mask | Definite from graph resolution and shader data flow; render reproduction pending |
| P1 | Removing/merging/reordering mask rows transfers unrelated layer adjustments | Definite state mutation; render reproduction pending |
| P1 | Raster masks do not follow row operations or undo; failed restore retains old coverage | Definite missing synchronization; render reproduction pending |
| P1 | Async Subject/Person/Sky completion can modify a different photo or mask | Definite unguarded async destination; scheduling reproduction pending |
| P2 | Refining one mask enables all eight refinement chains | Definite dispatch dependencies; no timing/RAM measurement |
| P2 | Global exposure reruns every geometric/brush/matte mask unnecessarily | Definite invalidation; no timing measurement |
| P2 | Brush storage grows after its rendered 16,384-dab limit | Definite cap mismatch; no long-stroke stress run |
| P2 | Brush cursor always draws a circle after perspective makes its paint noncircular | Definite geometry mismatch for anisotropic transforms; no UI capture |

### 1. Hiding a layer leaks the preceding layer's coverage

`engine/src/pipe/DevelopMask.cpp:260` disables every hidden component. A disabled
node resolves to its first input (`engine/src/pipe/Pipeline.cpp:203`), the previous
component's fold. However, `engine/src/pipe/DevelopOutput.cpp:333` still assigns a
hidden component as its layer's coverage endpoint and copies the layer's
adjustments at line 336. `engine/shaders/develop_linear.slang:212` then applies
every layer's adjustments through those textures, with no visibility check.

**Trigger:** two separate radial masks, A on the left at −1 EV and B on the right
at +1 EV; hide B. B's output now resolves to A's coverage, so B's +1 EV applies
over A. The eye removes B's original effect and changes A as well. When a hidden
first component has a visible continuation, that continuation can also inherit
the preceding layer because its `startsLayer` is false.

**Smallest complete correction:** preserve the zero-valued boundary of a hidden
layer start; do not allow a bypass to cross a layer boundary. A hidden starting
component can run as a zero/add reset, while hidden continuation components still
bypass. This must account for inversion and the first visible continuation.
Skipping the hidden layer's adjustments alone misses that second case.

**Check:** a small synthetic GPU test with two nonoverlapping graded layers:
hiding B leaves A byte-identical and restores B's region; add a hidden start plus
visible continuation variant. `repro/mask-visibility.txt` exercises only one
layer, where bypass reaches the zero base and therefore cannot see this defect.

### 2. Layer adjustments remain attached to array positions

`app/Engine+Mask.swift:262` removes a component, `:395` changes a split,
`:434` merges a mask, and `:502` swaps components, without remapping `layers`.
Only splits/additions append default adjustments. The renderer pairs run number
L with `adj.layers[L]` (`engine/src/pipe/DevelopOutput.cpp:323–346`).

**Trigger:** create three independent masks A/B/C with distinct grades, then
delete B. C becomes run 1 and immediately takes B's grade. Deleting A shifts every
survivor. Merging B into A likewise makes C inherit B. Reordering independent
mask rows swaps their geometry without their grades; the first row's stored
`startsLayer == false` can additionally collapse a run after moving downward.
An empty stack retains the former layer-0 adjustments, so its next new mask can
start graded rather than neutral.

**Smallest correction:** update the component list and its layer adjustment list
as one operation. Preserve every unaffected run's adjustment, insert/remove at
the affected layer index, and preserve boundaries when moving independent
masks. Decide explicitly which grade survives a deliberate merge; the existing
two-mask split/merge behavior intentionally remembers a split-off grade, so a
blanket `layers.remove` is not a sufficient compatibility decision.

**Check:** a CPU state test with three uniquely named/graded runs covering delete,
merge, split, and reorder; one small render check that C stays unchanged. Existing
`mask-split-stale.txt` and `mask-merge.txt` have only two runs and cannot detect
the third mask inheriting a vacated grade. `guideNeeded` also scans all retained
layer settings (`DevelopPipeline.cpp:377`), so orphan highlights/shadows can
keep the guide active after the last relevant mask has been removed.

### 3. Raster ownership is not reconciled with component state

Row deletion, insertion and reorder call `pushStrokes()` but never move/reload
matte textures (`app/Engine+Mask.swift:262`, `:455`, `:502`). Mattes are stored by
slot in `DevelopMask.cpp:639`; changing a component's `matteId` does not upload
anything. `Engine.assign` resends strokes only (`app/Engine.swift:607–624`), and
Undo/Redo/history jumps call `apply` (`:694–704`), which never restores mattes.
Ordinary editor photo open and explicit snapshot restore call `restoreMattes`;
the agent loader now does too (`app/AgentCLIDriver.swift:251`). That added loader
call does not reconcile a live editor's row mutations or history restoration.

**Triggers:** place a synthetic left-half matte at slot 1, remove slot 0, and the
surviving metadata now points at slot 0's stale/empty raster. Or regenerate a
selection A as B and undo: the state points to A's immutable PNG, but the GPU
still holds B. Redo similarly does not reconcile actual coverage.

There is another branch of the same defect: `restoreMattes` skips unreadable
files and rows without IDs without clearing the slot (`Engine+Mask.swift:322`).
Restoring a snapshot with a missing matte over an existing matte leaves the old
coverage live, despite the panel explicitly saying “this row covers nothing”
(`DevelopPanels+Mask.swift:35`).

**Smallest correction:** a single reconciliation path comparing each slot's
uploaded matte ID with the new state's ID; upload changed valid references and
clear missing/absent references before rendering. Invoke it for assignment and
row mutations, with the current photo identity available. Batch uploads under
`suspended` and render once; current `restoreMattes` renders once per matte, in
addition to the initial state render. Avoid full-size cached matte copies:
the immutable small PNGs already provide the source of truth.

**Check:** synthetic 16×16 left/right/ramp mattes; move, remove, insert, regenerate,
undo/redo, and restore a missing reference. Assert both untouched and changed
regions. The existing persistence scenarios cover reopen and snapshot restore,
not changing raster identity during ordinary undo or list edits.

### 4. Async selection uses the destination current at completion

`app/DevelopPanels+Mask.swift:60` awaits `SubjectMatte.generate`, which leaves the
main actor for inference (`SubjectMatte.swift:101`). On return, `findMatte` reads
the then-current `current` URL at line 73 and then-selected mask at lines 78–84.
It captures neither the source photo nor a target row token. `matteRunning`
disables the Add menu only; row selection, Remove, navigation, and other document
actions are not guarded by it.

**Trigger:** start Subject on photo A, switch to same-sized photo B while inference
runs; A's selection is written beside B and applied to B. On one photo, select
another mask while inference runs and the result replaces that row instead.

**Smallest correction:** capture a photo-generation token and target identity
before awaiting; reject stale completion before writing a PNG or changing state.
Cancel or invalidate on navigation and structural mask edits. Array index alone
is not identity when removal/reorder can happen during inference.

**Check:** delayed injectable selection result with a tiny alpha array; change
photo or target while suspended and assert no file/state mutation on completion.
`Scenario+Mask.swift:138` uses the blocking generator, so every existing `select`
scenario bypasses this scheduling window.

### 5. One refined mask schedules eight complete chains

`DevelopMask.cpp:564–570` enables seven nodes for **every** component slot whenever
any mask refines. `DevelopOutput.cpp:80–85` binds all eight refined outputs, so
the graph cannot infer from the shader's layer table which outputs go unread.
Unused slots still resolve to a real preceding mask and acquire their own
coefficient/refine chain.

**Trigger:** one radial and `maskRefine > 0`. This enables 48 quarter-resolution
passes and eight full-resolution passes rather than six and one. For the
7968×5320 frame, each chain's logical output sizes total
`72 × (1992 × 1330) + 2 × (7968 × 5320) = 275,533,440` bytes (~262.8 MiB).
Eight total ~2.053 GiB. **This is a sum of logical outputs, not process footprint
or simultaneous peak allocation**; pooling and cache policy affect residency.

**Smallest correction:** enable refinement only at the endpoints actually read
by live layers; recompute that predicate when count/breaks/visibility change,
not only when the global refinement Boolean changes. Disabled unused refine
outputs can already resolve to their component.

**Check:** use a tiny synthetic pipeline and inspect `lastRun()`: one layer runs
one seven-node chain, two layers two, and changing layer boundaries while refine
stays nonzero remains byte-equivalent to forced recomputation. Measure the saved
cost only after this structural check, at a bounded size.

### 6. Exposure invalidates mask data that does not read exposure

The equality guard at `DevelopMask.cpp:279–282` includes global exposure for
every kind. `rangeBias` is consumed only by luminance range (kind 5); a radial,
gradient, brush or matte is unchanged, but all their mask nodes are dirtied
on every exposure tick. A brush's parameter block is rebuilt with `firstDab = 0`,
so the whole saved stroke is evaluated again during those ticks.

**Smallest correction:** include exposure in this comparison only for kind 5.
**Check:** tiny pipeline `lastRun()` asserts no mask component executes on a
global exposure-only change for kinds 1/2/3/4/6, while kind 5 does; compare the
render against a forced run. The existing accumulator safeguards correctly
reject stale prefixes; they do not remove this avoidable invalidation.

### 7. Rendered brush limit does not bound stored brush data

`Engine+Brush.swift:69–78` appends without a cap. Both native pipelines copy the
entire stroke (`engine/src/Engine.cpp:143`, `DevelopMask.cpp:589–597`). Only the
render upload truncates to 16,384 dabs (`DevelopMask.cpp:347–348`), logging to
stderr at `:505`. The C facade still returns `ORION_OK` (`CApi.cpp:300–302`).

**Trigger:** sufficiently many paint/erase passes on one component. New paint
stops appearing, but Swift state, C++ buffers, sidecar serialization, and retained
undo snapshots keep growing. At the minimum UI radius 0.01, 16,384 dabs represent
about 41 normalized frame-widths at spacing 0.25, accumulated across strokes.

**Smallest correction:** enforce the same named limit before appending/copying
and publish a visible refusal. Retain the prior valid stroke; do not silently
drop existing saved data. **Check:** tiny CPU buffers at cap−1/cap/cap+1, including
erase and restored long input. `long-brush-stroke.txt` checks the former 256-dab
cap, not behavior at today's actual cap.

### 8. Perspective brush cursor does not show the painted footprint

`CanvasLayout.brushCursor` always emits a circle using the minimum of two local
scales (`app/CanvasLayout.swift:814–826`). The shader stamps a circle in **frame
pixels**, then geometry warps it. An aspect squeeze produces an ellipse; a
keystone has position-dependent scale. Taking the smaller scale and drawing a
screen circle loses the other axis, rotation/shear and finite-radius curvature.

**Smallest correction:** sample the frame-pixel nib's boundary and map every
point through `PictureMap.framePoint`, as radial outlines already do. The map
also needs the frame aspect to convert a pixel circle to normalized coordinates.
**Check:** a pure viewport test under nonuniform aspect and off-axis keystone,
comparing cursor points against transformed frame-circle samples. A test that
only demands the cursor remain round asserts the wrong contract after a warp.

## Historical failures and previously fixed work

The September 15 report does not name the eleven failing scene assertions, and
its temporary `desktop-ui-audit/sweep.json` was not found in the inspected `/tmp`
paths. Their exact individual causes cannot be recovered from that report alone.

The fixture mismatch **is confirmed without decoding**: current symlinks map
`_PIC8220.ARW → Pictures/moon/DSC09502.ARW`, `_PIC8148.ARW → DSC09504.ARW`, and
`_PIC8095.ARW → DSC09506.ARW`. `range-mask.txt`, `colour-range-mask.txt`,
`subject-selection.txt`, and `sky-mask.txt` assert against a dealership, car,
tarmac, or treeline. They are no longer evaluating the photographs they describe.
Restore/identify the intended fixtures before changing product code to satisfy
those assertions. Ten missing fixtures and this mismatch are verification
problems, not proof that ten or eleven corresponding filters are broken.

Already present: frame-space anchoring, sidecar migration, rounded matte sizing,
full/preview brush fan-out, dirty-matte invalidation, accumulator owner/prefix
reconciliation, visibility render calls, layer-table invalidation on split,
PNG persistence and snapshot matte pinning. This audit does not propose rebuilding
any of them. Some source/repro comments still describe four slots, displayed-space
strokes, nonexistent chained-stroke continuation, or unsaved mattes; the live
implementation takes precedence over those comments.

**Merged upstream fixes, kept separate from these findings:**

- `AgentCLI.openEngine` restores referenced mattes after its final sidecar/state
  restore and reports an unreadable referenced PNG (`AgentCLIDriver.swift:243`).
  Agent proxy/stats/faces therefore no longer omit valid raster coverage on load.
- The blocking scenario/agent `select` command now adds a selection row when the
  selected row is a non-matte shape (`Scenario+Mask.swift:130`), protecting that
  shape from replacement. This is independent of the asynchronous UI destination
  problem in finding 4.
- Both mask-kind setters clear `matteId`/`matteSource` when changing away from
  kind 4 (`Engine+Mask.swift:122`, `:527`). This corrects stale metadata on shapes;
  it does not move or clear uploaded raster slots and does not fix finding 3.
- The current Pipeline releases disabled-node caches and trims idle pooled
  textures after a transition render. Its disabled-node bypass semantics remain
  unchanged, and enabled mask-refinement chains remain enabled, so findings 1
  and 5 still apply. Retention validation belongs to the parent audit.

## Coverage inventory and limits

| Area | Files inspected |
|---|---|
| Core mask implementation | `engine/src/pipe/DevelopMask.cpp` (complete); `engine/shaders/mask_component.slang`, `ops/mask_ops.slang` (complete); relevant mask/layer paths in `DevelopOutput.cpp`, `DevelopPipeline.cpp`, `ShaderParams.h`, `Pipeline.cpp`, `engine/src/Engine.cpp`, `CApi.cpp`, `engine/include/orion/orion.h` |
| App mask behavior | `app/Engine+Mask.swift`, `Engine+Brush.swift`, `Engine+MaskMigration.swift`, `MaskOverlay.swift`, `MaskLayers.swift`, `FrameDisplayMap.swift`, `MatteStore.swift` (complete); relevant sections of `SubjectMatte.swift`, `DevelopPanels+Mask.swift`, `DevelopPanels+MaskList.swift`, `Engine.swift`, `Engine+Document.swift`, `Engine+Render.swift`, `OrionApp+Canvas.swift`, `OrionApp+Files.swift`, `EditHistory.swift`, `CanvasLayout.swift`, `Scenario+Mask.swift` |
| Test inspection | Relevant assertions/callers in `ViewportTests+Mask.swift`, `ViewportTests+MaskLayers.swift`, `tests_mask.cpp`, `tests_brush.cpp`, `tests_brush_accum.cpp`, `tests_brush_spacing.cpp`; no claim that these suites were run |
| Merge revalidation | Diffs from `40618db` through `bda5847`/current HEAD; current mask mutation, restore, async selection and layer-render call sites; `AgentCLIDriver.swift:227–259`, relevant `Scenario+Mask.swift`, `DevelopDiff.swift`, and `Pipeline.cpp` changes; sample symlink targets checked again without decoding |
| Repro scripts read | `mask-alignment`, `mask-follows-the-frame`, `mask-survives-the-fix`, `perspective-carries-the-mask`, `mask-add-stacks`, `mask-merge`, `mask-split-stale`, `mask-rows`, `mask-layers`, `mask-visibility`, `eight-masks`, `brush-erases`, `long-brush-stroke`, `preview-carries-the-mask`, `range-mask`, `colour-range-mask`, `matte-follows-the-frame`, `matte-does-not-follow-the-photo`, `matte-survives-a-reopen`, `snapshot-keeps-its-matte`, `subject-selection`, `subject-selection-42mp`, `sky-mask`, `overlay-shows-the-edited-layer` (all `.txt`) |
| Sources/context | `AGENTS.md`, current `planning/STATUS.md`, relevant planning/decision sections; `feedback/README.md`, September 15 engine/UI audit; `research/masking.md`, brush-acceleration source/contract sections |

No algorithm, shader, app, test, fixture or planning file was changed. No claims
of passing gates, observed UI behavior, fresh latency or memory savings are made.
