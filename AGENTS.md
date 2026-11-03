# Orion

Orion is a fast, subscription-free desktop RAW photo editor: one Avalonia app for macOS, Windows and Linux, built by a solo developer.
It ships as alpha pre-releases on GitHub; Windows and Linux are not yet supported releases.
It is source-available, never redistributed.

**Stack:** C++20 engine; Metal compute on macOS, Dawn (WebGPU in C++) on Windows (D3D12) and Linux (Vulkan); Slang shaders; LibRaw decode; OpenColorIO and lcms2 colour; XMP sidecars as the source of truth; the app in `shell/` on Avalonia 12 and .NET 10; Velopack installers and updates from GitHub Releases, packaged by `tools/package-app.sh` (DMG), `tools/package-windows.sh` (Setup.exe and zip) and `tools/package-linux.sh` (AppImage).
A release must carry the update feed Velopack writes beside the packages.

## Communication

- Write short, factual, technical sentences.
- Never claim a function, file, behaviour or convention exists without a file and line or a command that shows it.
- Say when the evidence is missing, and what would settle it.

## Before editing

- Read `AGENTS.md` and `planning/STATUS.md`.
- Read a file in full before changing it.
- Read the surrounding code and the current gates instead of trusting remembered commands.
- Preserve worktree changes you did not make.

## Hard constraints

- **No Vulkan.** Metal renders on macOS and Dawn renders everywhere else. The one exception is a single small Linux file that allocates the exportable memory and semaphore Dawn and Avalonia share; nothing else calls Vulkan.
- **Maintainability is a hard requirement.** One node is one small shader (50 to 150 lines). Adding a feature is a repeatable 3-file change. No file reaches 1000 lines. The developer must be able to hand-edit any file.
- **Prefer mature libraries** over hand-rolled code, even at some performance cost.
- **No GPL code or libraries** (notably exiv2). LGPL (LibRaw) only when dynamically linked or relinkable.
- **Licence.** Orion is under PolyForm Internal Use plus permission for personal use and pull requests: never redistribute or sell it, and never publish any revision under an open licence.
- **Algorithm sourcing.** Every technically non-trivial filter cites a published, dated, established source in `research/`. A filter without one is listed in `research/UNSOURCED.md` until it is sourced or defended.
  - Implementing a published algorithm from its description is fine; say so in the entry. Copying GPL code (darktable, RawTherapee) is not.
  - A module inside a GPL program is not a published algorithm: where the only description is its source code, it cannot be implemented here.
  - A citation is not patent clearance, and freedom to operate has never been assessed. If a filter's only sources are patents, ask the developer before writing code.
  - Test the invariant a cited constant implies, so the numbers stay checkable.
- **Engine boundary.** The engine is reached only through `engine/include/orion/orion.h` and the shim over it, `shell/native/shim.h` and `shim.cpp` (built as `liborion_shim`, called by P/Invoke). Both are POD only: no templates, no move-only types. No C++ exception may escape either: the facade's entry points run in `guard()` (`engine/src/CApiGuard.h`), the shim's allocating ones in `shim_guard()`.
- **Every command is registered by name** (`--commands`), so it can be driven without the UI.
- **Nothing pops up.** Every test and gate runs headless (`--selftest`, `--screenshot`, `--command`, headless Chrome for `web/`); never `open` a file or a visible browser tab. `--proof` opens a window: say so and ask first.
- **Guard heavy runs.** One heavy build, gate or bench at a time, with at least 30% of memory free and load under 8, run under `nice`.
- **macOS 14 is the floor.**
- **No language is required or banned.** Choose a language or library on maintainability and speed, and say why in the pull request. What exists stays unless moving it wins on those two.

## Implementation

- Make the smallest change that satisfies the request.
- Every code change ships with a test that fails without it; `.claude/skills/testing/SKILL.md` says where it goes and how to prune.
- A bug fix starts by reproducing the bug end to end, the way a user hits it.
- Run the narrowest test first, then every gate the change touches.
- The GPU tests matter most: maths tests pass on code that renders garbage, because they never touch a texture.
- Speed and memory are judged by `tools/bench.py`. Benchmark every milestone; a latency regression blocks new features.
- One story per coding session.

## Documentation

- Write the minimum. Code is written to be read without comments.
- A comment says only what the code cannot: an invariant, a workaround and its reason, a surprising behaviour of a dependency, a coupling to code elsewhere, a term the module defines.
- Never narrate change history or address a reviewer in a comment.
- Delete comments that restate names, types or control flow, and delete stale ones in code you edit.
- Nothing else is written down unless it is a rule or a hazard. Planning docs are dense tables over prose.

| Path | Holds |
|---|---|
| `planning/STATUS.md` | where the project stands and what is next; update it at the end of every session and keep it to one page |
| `planning/VISION.md` | what Orion is and is not |
| `planning/ARCHITECTURE.md` | the stack and the engine's design |
| `planning/FEATURES.md` | the feature map and each feature's target |
| `planning/RESEARCH.md` | library and algorithm findings, with sources |
| `planning/LICENSE-AUDIT.md` | every dependency's licence and its obligations |
| `research/` | the algorithm sources the sourcing rule cites; read it before touching a filter |

- Work is tracked in [GitHub issues](https://github.com/Nano-AI/Orion/issues): one parent issue per piece of work, with a checklist, no sub-issue per item.
- Write an issue as "issue N". A bare `#N` in code or `research/` is a row of the retired decision ledger: read it with `git show 1d02003:planning/DECISIONS.md` (rows 1 to 413).

## Commits

- One change is one commit. Commit as often as you like locally, then squash to one before pushing: `git reset --soft $(git merge-base origin/main HEAD)` and one `git commit -s`.
- The subject is `area: what changed`, lower case, one line, no body. Areas: `engine:`, `shell:`, `mcp:`, `tools:`, `web:`, `docs:`.
- Sign off with `-s`. No co-author or agent trailer.
- Never rewrite `main` or a branch someone else has pulled. Force-push only your own branch, with `--force-with-lease`.

## Pull requests

- One change per pull request, from a branch named `<area>/<topic>`.
- The description follows `.claude/skills/pull-request/SKILL.md`.

| Change | Pull request | Before it lands |
|---|---|---|
| **Docs only**: Markdown, `planning/`, `research/` | No. Batch the session's doc edits into one commit and push it to `main`. | Nothing: no rebuild, no gates. CI skips `.md`, `planning/`, `research/` and `web/`. |
| **Code**: everything else (`engine/`, `shell/`, `mcp/`, `tools/`, CMake, `.github/`) | Yes, opened with `tools/open-pr.sh BODY_FILE --base main --title "..."`, merged once CI is green. | The script rebuilds engine, shim and shell, runs every gate below (each under `$ORION_GUARD`, else `nice -n 10`), and opens the pull request only if all pass, with their last lines in the body. Never `gh pr create` a code pull request by hand. |
| **Site**: `web/` | No; `main` deploys it (`.github/workflows/pages.yml`). | `./tools/check-site.py` and a headless look at 390 and 1440 px wide. No rebuild. |

- A code change's docs ride in its pull request; a doc-only push onto an open pull request skips the build (`build.yml`'s `changes` job).
- GitHub merges by squash (merge commits are allowed, rebase is not), deletes the branch on merge, and refuses force-pushes to `main`.

## Tests and gates

Build first: `cmake --build build`, then `shell/native/build.sh` and `dotnet build -c Release shell` (`shell/README.md`).
A fresh worktree has neither `build/` nor `mcp/node_modules`: build them or link them from a checkout that has them (`check-agent` fails with "Server closed stdout" without `node_modules`).
Gates that render take photographs from `ORION_SAMPLES` (a folder of RAWs, default `samples/`); `check-agent` and `open-pr.sh` use the first of `_PIC8095`, `overexposed-background`, `_PIC8220` found there.
None of them opens a window.

| Command | Proves |
|---|---|
| `./build/apps/tests/orion-tests` | engine maths and real GPU renders |
| `shell/bin/Release/net10.0/Orion RAW --selftest` | the app's invariants, headless |
| `shell/bin-native/present_test RAW` | macOS: the zero-copy surface matches the CPU readback |
| `./tools/check-gestures.py` | every gesture that moves the picture still arms the preview graph |
| `./tools/check-screens.py` | every tool tab renders headless, non-blank, distinct from the others and stable (slowest gate) |
| `./tools/check-modes.py` | `--batch-export`, `--hdr-merge` and `--stack` still run and render what they wrote |
| `./tools/check-wiring.py` | every mechanism the product must call has a product caller |
| `./tools/check-agent.py` | the MCP server drives a real render and a real approve |
| `./tools/check-site.py` | the landing page: links resolve, register unchanged, no dead GitHub URLs, no personal data |
| `(cd mcp && npm test)` | the MCP server against a fake app (`mcp/test/fake-orion.sh`) |
| `tools/bench.py` | speed and memory: run `--save-baseline` on the base, then a plain run on the change; `--ab TREE` interleaves two builds and is the stronger comparison. A change is slower only when the shift's confidence interval clears zero and the shift is at least 5%. |

- Run every gate before claiming a code change works. Docs need none.
- CI (`.github/workflows/build.yml`) builds and runs `orion-tests` and the selftest on all three systems for pushes to `main` and `ci/**` and for pull requests. `tools/ci-selftest.py` holds the selftest to a stated per-system skip list; the Linux runner has no GPU, so its GPU checks skip.
  Except a Windows pull request: WARP, a CPU rasterizer, takes two hours over the full run, so there it is `orion-tests --cpu-only` and `--kernels` only, and the full suite and selftest run on the push to `main` (or `ci/**`) and nightly. A Windows-only failure therefore shows up after merging unless the branch is `ci/**`.
- `check-gestures.py` and `check-wiring.py` are greps, not compilers: they catch a deleted call, not a wrong one, and reflection or a name built at run time is invisible to them. Triage their reports by hand.
- A call from the harness does not count as use: `check-wiring.py` reports every method in `shell/` whose only callers are `SelfTest*`, the screenshot scenes or `Unattended.cs`, and a legitimate one says why in `HARNESS_ONLY`.
- A deleted command-line dispatch opens a window instead of exiting, so `check-modes.py` and `check-screens.py` catch it by timeout.
- `check-screens.py` cannot tell whether a tab draws the right controls; a person reads the PNG.

## Skills

Task workflows live in `.claude/skills/`; `.claude/skills/README.md` lists them.
A skill adds steps for one task and never restates this file.
