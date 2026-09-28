# Performance audit and repository cleanup — first delivery

The active goal remains a whole-codebase performance, RAM, UX, masking and
file-handling audit, with measured Lightroom comparison. This delivery does
not establish parity or complete that goal. The user additionally requested
repository cleanup, subagent orchestration, memory restraint and a push.

## Global constraints

- Preserve originals, sidecars, mattes, snapshots and third-party licences.
- No Rust, Vulkan, GPL code, new filter mathematics or speculative abstractions.
- Keep active adjustment caches responsive and rendered pixels unchanged.
- GPU jobs run serially. Use reduced images for memory stress; no repeat of the
  initial full 42 MP stress run. Stop if memory pressure becomes problematic.
- Run all nine repository gates before claiming this delivery works.
- No history rewrite. Push reviewed local changes after verification.

## Task 1 — Release inactive GPU caches

Finish the in-flight Pipeline cache fix and its GPU regression/benchmark.
Ownership: Pipeline.{h,cpp}, TexturePool.h, tests_pipeline.cpp, harness/main test
registration and the memory benchmark. Verify disabled nodes release storage,
the idle pool actually shrinks, re-enable/reload/export preserve pixels, and
warm fusion/exposure caching survives. Retain the small memory-cycle benchmark
with paired before/after measurements at the same resolution. Report physical
footprint separately from texture payloads. No full-resolution stress rerun.

## Task 2 — Remove repository artifacts

Remove confirmed generated outputs, including the tracked truncated
`.orion-curved.png-DQKn`. Preserve photographer data. Narrow ignore patterns
and route default benchmark output under build/. Record removed bytes and
check that site assets remain tracked. Changes and report already produced by
repo_cleanup; independent review remains.

## Task 3 — Record audit findings and coverage

Masking and desktop/file-handling agents trace current callers, report concrete
defects and list files read. Distinguish stale scene fixtures and historical
claims from current evidence. Do not claim a complete repository audit.
Record high-priority findings as next work, preserving the full active goal.

## Task 4 — Verify, review and push

One agent builds and runs the nine gates serially with memory monitoring.
Update STATUS (prune recent sessions), DECISIONS and feedback index from actual
evidence. A fresh agent reviews the complete diff; resolve important findings.
Commit the delivery, fast-forward main if unchanged, and push as requested.
