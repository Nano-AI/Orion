# Orion

Fast, subscription-free desktop RAW photo editor. Ships on macOS; Windows and Linux are in progress through the Avalonia shell. Modern minimal UI, capable of dramatic edits. Built by a solo developer. Source-available, never redistributed (#359).

## Read these first
Everything lives in `planning/`. On a fresh session, read in this order:

1. **`planning/STATUS.md`** — where we are right now, what's in flight, what's blocked. **Always start here.** Keep it short enough that "start here" is a thing anyone can do: the current state, the gap table, and the recent sessions. Older sessions move to `planning/HISTORY.md`, which is the archive and is **not** part of the read order.
2. `planning/VISION.md` — what Orion is and isn't
3. `planning/DECISIONS.md` — every settled choice + why. Don't relitigate these.
4. `planning/ARCHITECTURE.md` — stack, engine design, open questions
5. `planning/ROADMAP.md` — milestones, epics, stories
6. `planning/FEATURES.md` — feature map with target milestones
7. `planning/RESEARCH.md` — algorithm and library findings with sources
8. `planning/UI-DECISION.md` — UI shell evaluation (settled)
9. `research/` — **algorithm sources.** Read before touching any filter.
10. `feedback/` — **outside review and self-assessment.** Every critique of this
    repository, and the investigations that came out of them, kept together so
    the criticism is as findable as the plan. Read `feedback/README.md` first.

**Work to do is tracked as [GitHub issues](https://github.com/Nano-AI/Orion/issues)**,
not in the docs: issue 23 parity (the Avalonia shell catching up with the Swift
app, then the Swift app retired), issue 24 Windows and Linux builds, issue 22
region-of-interest rendering, issue 5 lens correction, issue 6 JPEG XL export.
`STATUS.md` points at them. Write them as "issue N": a bare `#N` is a decision.
One parent issue per piece of work, with a checklist; no sub-issue per item.

## Algorithm sourcing — non-negotiable

**Every technically non-trivial filter must cite a published reference in
`research/`.** Anything not cited belongs in `research/UNSOURCED.md` until it is
replaced or defended. A citation must be published, dated, and established in
practice.

This rule exists because plausible-looking constants shipped a purple cast on
every image. Cite the source so the numbers are checkable, and test the
invariant so they stay right.

Do not copy GPL code (darktable, RawTherapee). Implementing a *published
algorithm* from its description is fine — mathematics is not copyrightable — but
say so in the entry.

⚠ **That sentence is about copyright, and it is silent about patents.** "Cited"
does not mean "safe to ship": a method can be published, dated, established in
practice, correctly attributed, and still be covered by a live patent. **Freedom
to operate has never been assessed in this project** — the rule above was
written about provenance and correctness after the purple cast, and nothing
here has ever asked the other question. Decision #173, which found the
alternatives to one unbuilt filter are largely US patents, is where it surfaced.

So: **cite as before, and do not read a citation as clearance.** If a filter's
only sources are patents, that is a question for the developer before any code
is written, not a technical decision to be settled by measurement.

⚠ And a related trap, same decision: **a module inside a GPL program is not
automatically a published algorithm.** Where the only description of a method is
its source code, there is nothing to implement *from* except the thing that must
not be copied — "I read darktable and reimplemented it" is copying with extra
steps.

## Hard constraints — do not violate

- **No Vulkan.** Too much boilerplate for a solo maintainer. Metal on macOS today; Dawn (WebGPU in C++, #317, #354) is the cross-platform GPU layer. Shaders authored in Slang. One exception (#355): on Linux, one small file may allocate the
  exportable memory and semaphore that Dawn and Avalonia share. Dawn (#317) does all the rendering; nothing else may call Vulkan.
- **Maintainability is a hard requirement.** One node = one small shader (50–150 lines). Adding a feature should be a repeatable 3-file change. No 1000-line anything. The developer must be able to hand-edit any file later.
- **Prefer mature libraries** over hand-rolled code, even at some performance cost.
- **Avoid GPL libraries** (notably exiv2). ⚠ Orion is source-available, not open
  source (decision #359: PolyForm Internal Use plus personal use and pull requests; never redistribute or sell). A work
  under that licence cannot incorporate GPL code at all. LGPL code like LibRaw
  may be used only when dynamically linked or relinkable. Publishing any revision
  under an open licence is a one-way door: it happened once by mistake (#358).

## Stack
C++20 engine · Metal GPU compute (Dawn next, #317) · Slang shaders · LibRaw decode · OpenColorIO + lcms2 color · SQLite index · XMP sidecars as source of truth.

Two front ends over one engine:

| | Where | State |
|---|---|---|
| **Avalonia 12 / .NET 10** (#315, #316) | `spikes/avalonia-shell/` | the future of the app on all three systems; Mac DMG via `tools/package-avalonia.sh` (#353); about 44 commands still `stub(#315)`, waiting on their Swift code to be ported (issue 23) |
| SwiftUI/AppKit | `app/` | the shipping Mac app (`tools/package-app.sh`); retired once issue 23 lands |

**UI rules:** the engine is reached only through the hand-written C facade `engine/include/orion/orion.h` (the shell via `native/` → `liborion_shim.dylib`): POD only, no templates, no move-only types, and **no C++ exception may escape it (it would terminate the process)**. The canvas gets the engine's picture with no CPU copy — an IOSurface on `MTLSharedEvent`s into Avalonia's Metal renderer (#357) — and falls back to a CPU copy headless. Every command is registered with a name (`--commands`), so it can be driven without the UI. In the Swift app, view models are plain `@Observable` objects with zero SwiftUI types inside. Dear ImGui is for internal debug tooling only, never product UI. macOS 14+ floor.

## Tests

```
./build/apps/tests/orion-tests     engine maths + real GPU renders
./build/orion-viewport-tests       canvas geometry
./tools/check-decisions.py         the decision ledger indexes the tree
./tools/check-gestures.py          every gesture still arms the preview graph
./tools/check-screens.py           the three interface scenes that assert
./tools/check-modes.py             --library-open and --batch-export still run
./tools/check-wiring.py            mechanisms the product must actually call
./tools/check-agent.py             the MCP server drives a real render and a real approve
./tools/check-site.py              the landing page: links resolve, register unchanged, no dead GitHub URLs, no personal data
```

For the Avalonia shell (`spikes/avalonia-shell/README.md` has the build):

```
dotnet run -c Release --project spikes/avalonia-shell -- --selftest RAW   the shell's invariants, headless
spikes/avalonia-shell/bin-native/present_test RAW                          zero-copy surface vs CPU readback
python3 spikes/avalonia-shell/compare.py check                             slice file ownership
```

Run all of them before claiming anything works. The GPU tests matter most: pure
maths tests pass happily on code that renders garbage, because they never touch
a texture. Two shipped bugs — a torn frame and a purple cast — were invisible to
inspection and obvious to a five-line assertion.

`check-decisions.py` is the odd one out and is here because the planning docs
have their own failure mode: **the queue in `STATUS.md` has offered
already-shipped work as the next story three times.** The third time, the
decision that closed the queue item had never been written down at all, so a
session that trusted the queue would have re-run a schema migration over the
photographer's sidecars. It fails on a duplicate number, on a `#N` the tree
cites that has no row, and on a gap nobody declared.

`check-gestures.py` is a grep and says so: a SwiftUI `DragGesture` closure
cannot be driven from either suite (#110.3), so deleting a `beginInteraction`
call is green everywhere. It catches the deletion and claims nothing more. It
exists because a `ROADMAP.md` table asserting four gestures did *not* arm was
the premise of a performance audit, and all four had been fixed without it
being updated.

`check-screens.py` launches the application three times, so it is the slow one.
It runs the `--screenshot` scenes that *assert* — the menu bar AppKit really
builds, the Detail panel's controls below the fold, and the footer's failure
line. **The other ~35 scenes pose rather than assert and this cannot run them**:
their oracle is a person looking at a PNG. ⚠ It exists because those three
checks were written against named mutations (#125) and then invoked by nobody:
`--scenario` is what the repro sweep drives, and `--screenshot` had no gate at
all (#121, #177).

`check-modes.py` is the same story for the last two command-line modes. Both
already assert — `--library-open` opens one folder cold, warm and indexless in
one process and fails when they disagree; `--batch-export` exits 1 when a
photograph fails — and neither was run by anything, so deleting either
four-line dispatch in `OrionApp.init` was green everywhere (#121, #179).
⚠ A deleted dispatch does not make Orion exit; it makes Orion **open a window**,
so both gates catch that by timeout rather than by exit code.

`check-wiring.py` is the third grep, and its rule is worth knowing on its own:
**a call from the harness does not count.** `Engine.showPlaceholder` was built,
drawn by the canvas, described by two comments as holding the picture while a
new photo decodes — and its only caller in the tree was the screenshot harness,
so the canvas showed the *previous* photograph for the whole 210.9 ms of a cold
open (#151, #181). A mechanism that looks used because a test uses it is the
shape this catches. It **sweeps** as well as declares (#183): every `func` in a
product file whose callers are all harness is reported, and a legitimate one has
to say why in `HARNESS_ONLY`. That found `SyncSettings.pasted` on its first
run — a second spelling of the paste path that only a test called. ⚠ It is a
regex, not a compiler: a call through a closure, selector or key path is
invisible to it, so triage by hand before believing it.

## Working agreement
- One roadmap story per coding session. Update `STATUS.md` at the end of every session — this is what makes context loss survivable. **Prune it in the same breath:** it reached 4,643 lines over 56 sessions before anyone noticed that a recovery point nobody can read is not one. Six or so recent sessions is plenty; the rest belongs in `HISTORY.md`.
- Log any new decision in `DECISIONS.md` with its reason.
- Benchmark every milestone. Latency regressions block new features.
- Planning docs stay concise. Dense tables over prose.
- **Nothing pops up.** The developer tiles windows with AeroSpace: tests run headless (`--selftest`, `--screenshot`, `--command`, headless Chrome for `web/`), never `open` or a visible browser tab. `--proof` and `check-screens.py` open windows; say so and ask first.
- **Guard heavy runs.** A bench, gate or build that eats memory has crashed the laptop: one heavy run at a time, ≥30% memory free, load under ~8, `nice`d.
