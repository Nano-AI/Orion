# Mask and I/O test-oracle audit

2026-09-28. Source-only review at `a9be273`; no build, compiler, app, test,
GPU, network or installation work was run because kernel memory pressure was
at warning level 2. Findings are test defects or proof limits, not new product
bug reports. The known mask and file-handling defects remain in the other
2026-09-28 feedback reports.

## Exact read roster

All 17 assigned files were read completely. Line counts are current `wc -l`.

| File | Lines | File | Lines |
|---|---:|---|---:|
| `apps/tests/tests_align.cpp` | 174 | `apps/tests/tests_brush.cpp` | 827 |
| `apps/tests/tests_brush_accum.cpp` | 443 | `apps/tests/tests_brush_spacing.cpp` | 391 |
| `apps/tests/tests_dng.cpp` | 548 | `apps/tests/tests_guide_eps.cpp` | 135 |
| `apps/tests/tests_hdr_merge.cpp` | 214 | `apps/tests/tests_io.cpp` | 926 |
| `apps/tests/tests_mask.cpp` | 956 | `apps/tests/tests_mask_geom.cpp` | 809 |
| `apps/tests/tests_mask_matte.cpp` | 592 | `apps/tests/tests_mask_pullback.cpp` | 336 |
| `apps/tests/tests_mask_range.cpp` | 629 | `apps/tests/tests_mask_slots.cpp` | 110 |
| `apps/tests/tests_merge.cpp` | 247 | `apps/tests/tests_merge_render.cpp` | 140 |
| `apps/tests/tests_spot.cpp` | 246 | **Total** | **7,723** |

Narrow product reads only to check these tests' claims: `HdrMerge.cpp/.h`,
`DngWriter.cpp`, `MergeRender.cpp/.h`, `DevelopMask.cpp`. Algorithm context:
`research/hdr-merge.md`, `research/masking.md` and the corresponding
`research/UNSOURCED.md` notes. Existing mask findings were cross-checked
against `feedback/2026-09-28-masking-audit.md`.

## Ranked findings

| Priority | Test defect and consequence | Smallest useful correction |
|---|---|---|
| **P1** | **Fixed `/tmp` fixture names overwrite and delete unrelated files.** `tests_hdr_merge.cpp:81-86` removes `/tmp/orion-hdr-merged.dng` before the test; `:156-159` and `:211-213` remove fixed input/output names. `tests_dng.cpp:131`, `:237-252`, `:265`, `:354`, `:430`, `:492-547` write/remove other fixed names; `tests_io.cpp:509-813` writes fixed JPEG/PNG names and never cleans them. `writeDngLinear` uses a fixed sibling `.part` and then `rename` (`DngWriter.cpp:299-328`), so overlapping runs also contend on the same partial path. A second suite or any pre-existing file under those names can be clobbered; early returns in the DNG tests leave fixtures behind. This is a test-side data-safety problem independent of the product output-collision findings. | Make one unique temporary directory per test invocation, put every fixture and `.part` below it, and clean that directory with scope-bound cleanup. Never remove a path the test did not create. |
| **P1** | **The facade HDR test's “alignment” pixels are blind to its nine-pixel shift.** `tests_hdr_merge.cpp:35-55` places the reference highlight at `300 <= x < 380` and shifts only the short frame by `+9`; the unclipped short-frame block is therefore at `309 <= x < 389` if alignment is skipped. Yet `:132-140` checks `(340,100)`, inside *both* blocks, and `(295,100)`, outside *both*. The header claims those checks would catch skipped alignment; they cannot distinguish the stated mutation. `testAlignRecoversHomography` separately checks the aligner, but this facade test has no proof that the merge uses its result. | Check a point just inside the reference's left edge, such as `(303,100)`, where only a correctly aligned short frame can recover `1.6/H`; also check a point in the shifted-only strip, such as `(383,100)`, against the background. Validate by temporarily bypassing alignment when execution is safe. |
| **P2** | **The cancellation test can pass without a cancellation, and “whole file” means only nonzero bytes.** `tests_hdr_merge.cpp:178-195` starts the canceller before `run`, accepts either a throw or success, and on success asks only `file_size(out) > 0`. `HdrMerge::run` clears the flag on entry (`HdrMerge.cpp:208`), so a cancel that wins before entry is erased; its last `step` is before the write (`:320-354`), so a later cancel may be ignored. Neither outcome proves cancellation was observed; a truncated nonempty DNG passes the success branch. | Arrange a controlled cancel after `run` begins and assert the cancelled outcome plus absent output. Keep an uncancelled success control that reopens/decodes its DNG. Check `.part` within the unique fixture directory. |
| **P2** | **The mask-refine test cannot detect a changed shipping epsilon.** `tests_mask_matte.cpp:273` hard-codes `kEps = 0.01` and `:333-361` dispatches its private seven-node chain. `DevelopMask.cpp:232` independently pushes `0.01f` to the product graph. The test's edge thresholds at `tests_mask_matte.cpp:572-589` are useful for its private chain, but changing only the product value leaves all of them green. This is a parameter-wiring proof gap, not a dispute with He/Sun/Tang's filter or Orion's stated empirical choice in `research/UNSOURCED.md` §20. | Give the product value one named constant that the test dispatches, and retain the independent half-stop/tenth-stop behavioral bounds; or drive those bounds through a tiny `DevelopPipeline` render. |

## What the current assertions do prove

The broad mask suite has several good nonvacuous oracles. `tests_brush.cpp`
compares accelerated and unaccelerated GPU output and then supplies bad bounds
as a positive control (`:121-165`). `tests_brush_accum.cpp` pairs full-frame
byte comparisons with counters proving the incremental path ran (`:124-174`,
`:334-413`). `tests_mask_geom.cpp:709-804` explicitly asserts its keystone
fixture has nonzero off-diagonal terms before judging the conjugated Jacobian;
`tests_mask_matte.cpp:151-169` tests a nonzero border texel so an unclamped
read cannot masquerade as a valid zero. `tests_mask_range.cpp:563-600` checks
the shadow-floor behavior on the GPU after noting that CPU-model-only checks
were self-confirming. `tests_merge.cpp:208-230` opens the deghost gate as a
positive control. These are real assertions, not generic coverage claims.

The known product gaps are not closed by this test set: direct matte uploads
in `tests_mask_matte.cpp` do not drive live row mutation/undo/reload; the
two-row layer-break fixture in `tests_mask.cpp:841-956` does not test a hidden
later layer borrowing the prior coverage or a third layer inheriting another
row's grade. Those cases and their minimal reproductions are already specified
in the masking audit, so they are not ranked again here. For resource bounds,
`tests_merge_render.cpp:56-60` checks `MergeRender::gpuBytes(7952,5304)` against
1.2 GiB; the actual demosaic/render checks run at 32×32 (`:23-24`). The 42 MP
assertion is a logical texture-byte estimate (`MergeRender.h:43-47`), not an
allocation, peak footprint or full-resolution throughput measurement. The
current memory-pressure pause leaves that physical claim open.

No mutation, compile, render or nine-gate run was made on these findings. The
HDR blind-point result is derived from the fixture intervals, not a measured
mutation outcome; the proposed replacement pixels still need a safe run before
being accepted as a regression oracle.
