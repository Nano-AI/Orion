# Contributing to Orion

Thanks for helping. Orion is source-available, not open source, so read the
licence part first. The rest is how the project works.

New here? Issues labelled
[good first issue](https://github.com/Nano-AI/Orion/labels/good%20first%20issue)
are small, name the files to touch and say how to check the result.

## The licence for contributions

[`LICENSE`](LICENSE) is the PolyForm Internal Use License 1.0.0 with two added
permissions (decision #359).

| You may | You may not |
|---|---|
| Use Orion and change it for your own work or your company's internal work, paid or unpaid | Distribute Orion to anyone, in source or binary form, changed or not, free or paid |
| Fork this repository on GitHub to send pull requests back | Keep a fork for any other purpose, or publish your own version |

**By submitting a contribution you grant the licensor a perpetual, worldwide,
irrevocable, royalty-free licence to use, change, relicense and distribute it as
part of Orion** (additional permission 2). If you can't agree to that, please
don't open a pull request.

## Rules every change follows

| Rule | Where it is written |
|---|---|
| A non-trivial filter cites a published, dated reference in `research/`. Anything uncited goes in `research/UNSOURCED.md`. | `CLAUDE.md`, Algorithm sourcing |
| A citation is not patent clearance. If a method's only sources are patents, ask on the issue before writing code. | `CLAUDE.md`, Algorithm sourcing |
| GPL code is never copied and GPL libraries are never used. LGPL (LibRaw) only when dynamically linked. Implementing a published algorithm from its paper is fine; reading darktable's source and rewriting it is copying. | `CLAUDE.md`, Hard constraints |
| A settled choice gets a row in `planning/DECISIONS.md` with its reason. Don't reopen a decision in a pull request; open an issue. | `planning/DECISIONS.md` |
| One node is one small shader (50 to 150 lines). No 1000-line files; anyone must be able to hand-edit any file. | `CLAUDE.md`, Hard constraints |

Say on an issue that you're taking it, so two people don't build the same thing.

## Build

macOS 14 or later on Apple silicon, with Xcode's Metal toolchain. The
prerequisites are in the [README](README.md#prerequisites).

```sh
cmake -S . -B build -G Ninja
cmake --build build
shell/native/build.sh
export DOTNET_ROOT=/opt/homebrew/opt/dotnet/libexec
dotnet build -c Release shell
```

Windows and Linux build with vcpkg and Dawn (`tools/build-dawn.sh`);
`.github/workflows/build.yml` has the exact steps for each.

Working in a second git worktree? Run `sh tools/worktree-setup.sh` inside it
first. It links the gitignored Slang toolchain and samples from the main
checkout, and refuses to run anywhere else.

## Test

Every gate is headless; none opens a window. The ones that render read RAW
files from `ORION_SAMPLES` (default `samples/`).

| Command | Checks |
|---|---|
| `./build/apps/tests/orion-tests` | engine maths and real GPU renders |
| `shell/bin/Release/net10.0/Orion RAW --selftest` | the app's invariants |
| `shell/bin-native/present_test RAW` | the zero-copy surface against the CPU readback (macOS) |
| `./tools/check-decisions.py` | the decision ledger indexes the tree |
| `./tools/check-gestures.py` | every gesture still arms the preview graph |
| `./tools/check-screens.py` | every tool tab renders, distinct and stable |
| `./tools/check-modes.py` | `--batch-export`, `--hdr-merge` and `--stack` still run |
| `./tools/check-wiring.py` | mechanisms the product must actually call |
| `./tools/check-agent.py` | the MCP server drives a real render and a real approve |
| `./tools/check-site.py` | the site: links resolve, no personal data |
| `(cd mcp && npm test)` | the MCP server against a fake app |

The GPU tests matter most. Pure maths tests pass on code that renders garbage,
because they never touch a texture.

## Send it

| Change | How it lands |
|---|---|
| Code: `engine/`, `shell/`, `mcp/`, `tools/`, CMake, `.github/` | A branch and a pull request, opened with `tools/open-pr.sh BODY_FILE --title "..."`. It rebuilds, runs every gate above and opens the pull request only if all pass, with their results in the body. CI then builds all three systems. |
| Docs only: Markdown, `planning/`, `research/`, `feedback/`, `docs/` | Maintainers push straight to `main`. From a fork, open a pull request; CI skips these paths. |
| Site: `web/` | As docs, after `./tools/check-site.py`. `main` deploys it. |

One change is one commit (decisions #408, #409). Commit as often as you like
while working, then squash before you push:

```sh
git reset --soft $(git merge-base origin/main HEAD)
git commit
```

Small pull requests get reviewed fastest. Fill in the pull request template.
