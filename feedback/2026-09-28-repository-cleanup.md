# Repository artifact cleanup — 2026-09-28

Removed **11 files, 403,266,463 bytes (384.59 MiB)** from the checkout.
Only one was tracked. Git history was not rewritten, so its old blob remains
in existing clones and history.

| Removed | Files | Bytes | Provenance |
|---|---:|---:|---|
| `.orion-curved.png-DQKn` | 1 | 21,016,576 | Truncated benchmark PNG, committed in `c1e3762` on 2026-08-03 |
| Root `orion-{curved,flat}.png`, `orion-{full,web}.jpg`, `orion-{full,print}.tif` | 6 | 382,174,419 | Benchmark curve and export probes; dated 2026-09-14 |
| `tools/__pycache__/*.pyc` | 3 | 65,224 | Regenerable Python bytecode |
| Root `.DS_Store` | 1 | 10,244 | Finder metadata |

## The named file

The tracked file contains a 6024 × 4024, 16-bit RGB PNG. Its final `IDAT`
chunk declares an end at byte 21,019,798, but the file stops at 21,016,576;
there is no `IEND`. It is an incomplete export, not a source photograph.

The matching writer is `bench::toneCurve` in `apps/bench/bench_gate.cpp`,
through `writeOut` → `writePng` → `writeImage` in
`engine/src/util/ImageWriter.mm`. That passes the destination URL to ImageIO
and finalizes it. The hidden filename with a random suffix is consistent with
ImageIO's temporary sibling left by an interrupted write; the interruption
itself was not observed. No process had the file open during cleanup.

The existing `orion-*.png` ignore rule missed the leading-dot temporary name.
`.gitignore` now covers the hidden temporary siblings for all five existing
benchmark image extensions. Published website images retain their exception.

## Scope and checks

Inspected tracked files and untracked files outside protected trees. No
nonempty, byte-identical tracked duplicates were found outside third-party
source. Historical planning/research/design documents are retained records,
not disposable copies. No additional tracked-file deletion was justified.

Original photos, XMP sidecars, mattes, snapshots, active build and distribution
directories, dependencies, local configuration, worktrees and tool indexes
were preserved. No cleanup daemon or broad deletion glob was added.

Verified the exact removed paths are absent; checked six benchmark ignore
cases and three source/website paths that must remain visible; scoped
`git diff --check` passed. The parent session runs the nine repository gates
after integrating the concurrent memory changes. This cleanup ran no GPU work.

The benchmark now defaults to `build/bench-output/orion`, creating that ignored
directory for normal runs. Explicit output prefixes remain supported; focused
fusion and memory modes do not create the output directory.
