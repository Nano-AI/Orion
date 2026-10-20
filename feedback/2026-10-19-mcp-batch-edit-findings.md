# MCP batch edit findings (climbing batch-2, 2026-10-19)

Findings from driving Orion through the MCP server across a 70-frame batch (Sony A7R III, 42 MP ARW, ISO 1000-16000).
Parallel agents froze a 36 GB M4 Max at about 70 GB of memory twice.
Section A was measured in this session against `build/Orion.app` at `9ca76c0`.
Section B lists quirks recorded in earlier sessions (`~/.claude/skills/orion-editing/SKILL.md`); they were not re-verified today, so reproduce each one before fixing it.

## A. Measured today

### A1. The MCP server has no concurrency limit and no timeout (P1)

`mcp/server.ts` `orion()` runs one `execFile` of the full app binary per tool call, with no queue and no timeout.
Each call is a separate process that renders the full frame, so N concurrent tool calls cost N times the peak below.
Seven agents (1-3 calls each in flight) reached about 70 GB on a 36 GB machine and froze it.
Fix: a semaphore around `orion()` (1-2 in flight, configurable), a timeout with kill on `execFile`, and kill children when the server exits.

### A2. A single agent call peaks at 4-14 GB (P1)

Measured with `/usr/bin/time -l` (peak memory footprint) on `DSC00760.ARW` (5320x7968):

| Call | Peak footprint |
|---|---|
| `proxy` on a frame with no sidecar | 4.30 GB |
| `proxy` with sidecar (denoise only, no masks) | 5.90 GB |
| `stats --state` with globals only (curve, HSL, grade, vignette) | 8.21 GB |
| `stats --state` with 4 mask rows (3 Person-matte rows + 1 colour range), `maskRefine` 0 | 11.48 GB |
| same, `maskRefine` 1 | 13.74 GB |
| same, padded to all 8 rows | 14.09 GB |
| `proxy --region` with the full proposal | 14.25 GB |

`TexturePool.h` says the pool brings a 24 MP render to about 1.5 GiB of working set (#219), which scales to about 2.7 GB at 42 MP.
Even the simplest single-restore case (globals only, no sidecar) measured 6.41 GB.
The full proposal's 13.7 GB is almost exactly the "13861 MiB" no-pool figure quoted in `Pipeline.cpp` `maybeFree`.
Next step: log `Pipeline::allocatedBytes()` and `intermediateBytes()` per render in agent mode to attribute the gap by node.

### A3. One agent call renders 2-3 times, and later renders free nothing (P1)

`AgentCLIDriver.swift` `openEngine` restores the sidecar, then `--state`, then `restoreMattes`, and each step can render.
`Pipeline::maybeFree` frees a node's texture only "once, ever" (the `everFreed_` guard, kept for slider latency).
So every render after the first keeps every texture it rebuilds.
Measured, same state file:

| State file | One restore (copy with no sidecar) | Sidecar + `--state` |
|---|---|---|
| Full proposal | 11.34 GB | 13.74 GB |
| Globals only | 6.41 GB | 8.21 GB |

Fixes:
- Merge the sidecar and `--state` and load the mattes before rendering, so each call renders once.
- Give headless and one-shot users (agent CLI, batch export) a pipeline mode that releases after every render instead of once ever.

### A4. Guided mask refine is built per component, but the comment says once per group (P2)

The comment in `engine/src/pipe/DevelopMask.cpp` (around line 116) says "Six nodes once, not six per component".
The loop just below it builds six guide nodes (3 RGBA32Float + 3 RG32Float at guide resolution) plus a full-res R16Float refine node for each of the 8 component slots.
Measured cost of `maskRefine` 1 vs 0 on 4 rows: +2.26 GB.
Either the code should match the comment (refine the folded group once per layer) or the comment is stale.

### A5. Textures whose last reader is disabled stay resident (P3, small here)

`Pipeline.cpp` `maybeFree` documents this "ponytail": `lastReader_` counts disabled nodes, so such a texture is never freed.
The mask chain always builds 8 component slots, so a 4-row edit leaves disabled readers.
The measured effect was small: padding to all 8 rows changed the peak by +0.35 GB, so this is not the main driver.

### A6. Subject/person detection misses the climber on ordinary frames (P2)

Seen during this batch (agent reports, not instrumented):
- DSC00745 (ISO 1250, front-lit climber on a lit wall): both `person` and `subject` returned nothing, even when run on a brightened proposal (`state: proposed`). The agent fell back to radial masks.
- DSC00779 (backlit): `person` failed; `subject` on a brightened proposal worked.
- DSC00781 (backlit): `person` returned a 0.9% matte covering a bystander instead of the climber; `subject` worked.
- DSC00954: `person` returned 0.2% coverage; `subject` on a brightened proposal grabbed the wall (28%); only `subject` on the current render isolated the climber. DSC00951 and DSC01033 also needed `subject` after `person` failed.
- DSC00776 and DSC00778: `person` included the belayer, so the climber layer lifts him too. There is no way to pick one person out of a multi-person matte.

Possible fixes: pass Vision a contrast-normalised render, return one matte per detected instance so an agent can choose, and report per-instance coverage and bounding boxes.

## B. Recorded in earlier sessions (not re-verified today)

1. `propose_edit` silently accepts more than 8 `maskComponents`, but only the first 8 render or survive approval. It should reject with exit 2.
2. Stale proxy: a whole-frame `proxy --state` sometimes returns an older render while `stats` shows the edit. Re-requesting at a different `--max` fixes it, which suggests a cache keyed without the state.
3. Layer tint flips sign after Orion re-saves a proposal when the user edits masks (seen on 00093 after the #290/#293 fixes: +0.05 sent and confirmed, read back as -0.05).
4. Two writers to one `.proposed.json` merge onto each other silently (00447): there is no version check or lock between the app and agents.
5. `detect_subject` writes a new matte PNG on every call, even when byte-identical; `sync_edits` writes one PNG per matte row (3 identical per target).
6. Approving a proposal deletes mattes it no longer references, so an agent's next proposal that names an older matte fails until it re-detects.
7. Mattes are 1024 px on the long edge: on 42 MP frames a strong layer misses rim-lit hair tips and leaves a 2-4 px rim on arms (00718).
8. `person` detection fails on dark subjects (00702: legs only, 2%; `subject` found nothing until a +1.3 EV proposal) and on groups (00728: `found: false` with 11 faces).
9. Kind-6 colour range on skin causes mottling: with tolerance near the skin's own chroma, noisy pixels fall in and out of the selection, so the layer applies in blotches (00981; toggling the layer to zero removed them). Smoothing the chroma guide or coverage before thresholding would fix it.
10. `highlightRecovery` on a near-clipped overcast sky (blue clipped) produces a flat grey patch with a hard speckled edge (0601).
11. A linear gradient intersected (compose 2) with a kind-5 luminance range barely affected a sky, while the gradient alone worked (0601). Cause not isolated.
12. `denoiseLuma`/`denoiseColor` max out at 4 and still leave visible grain on high-ISO skies and walls; `texture` is inert at ISO 8000+.
13. Tool `path` defaults to the photo open in the app, which moves while an agent is working; a long agent session can edit the wrong frame unless every call passes `path`.

## Reproducing A2/A3

```sh
B=build/Orion.app/Contents/MacOS/Orion
P=/Volumes/wintermute/pictures/climbing/batch-2/DSC00760.ARW
/usr/bin/time -l $B --agent stats $P --state ${P%.ARW}.proposed.json 2>&1 >/dev/null | grep "peak memory"
```

For the one-restore variant, copy the ARW and its `*.orion-matte-*.png` files to a folder with no `.xmp`, then pass the same `--state`.

## Status, 2026-10-19 (#311-#313)

Measured on the same frame and the same commands as section A, `/usr/bin/time -l` peak footprint, sandbox off.

| Call | `9ca76c0` | after #312 | after #313 |
|---|---|---|---|
| `stats --state` (sidecar + proposal, 4 rows, refine 1) | 13.74 GB | 7.47 GB | **6.88 GB** |
| `stats` (sidecar only) | 5.38 GB | 5.34 GB | 5.34 GB |
| `proxy --state --max 2048` | 14.44 GB | 8.17 GB | **7.58 GB** |

The `stats` JSON is identical key for key before and after, and the proxy JPEG is byte-identical.

| Finding | Verdict | Where |
|---|---|---|
| A1 no concurrency limit, no timeout | **Fixed.** FIFO semaphore around every spawn, default one slot per 16 GiB of RAM (1-4), `ORION_MCP_CONCURRENCY`; `ORION_MCP_TIMEOUT_MS` (300 s) with SIGKILL and a `TIMED OUT:` error; children die on SIGTERM/SIGINT/SIGHUP and when the client's stdin closes. 7 new tests. | #311, `mcp/limits.ts` |
| A2 4-14 GB per call | **Halved, not closed.** What is left at 42 MP is one render's pooled working set (about 2.7 GB), about 1.4 GB of pinned full-resolution RGBA16F (`huesat`, `rgb`, `highlights`, `hlfill`, #156), LibRaw's buffers and the histogram readback. Unpinning in headless needs every `nodeOutput` reader audited. The per-render attribution log suggested above was not built; the arithmetic in #312 is the attribution. | #312 |
| A3 2-3 renders, later renders free nothing | **Fixed.** One blob restored (`--state` else the sidecar), mattes uploaded inside the restore's suspended window, `Pipeline::setReleaseEveryRender` on a headless engine with no preview graph. lldb counted 1 render per `stats --state`. | #312 |
| A4 refine per component | **Fixed.** Chains run on the slots that end a layer only; the comment matched to the code. | #313 |
| A5 disabled last reader keeps a texture | **Open.** +0.35 GB measured; the upgrade path in `Pipeline::maybeFree`'s comment still stands. | |
| A6 detection misses the climber | **Open.** Not a memory finding; untouched. | |
| B1-B13 | **Open.** None re-verified. | |

Found on the way: `BatchExportDriver.run` restored the sidecar's state and never uploaded its mattes, so a Subject or Sky row exported covering nothing, or under the previous photograph's raster through `reload`. Fixed in #312, in-app batch included; no test asserts it yet.
