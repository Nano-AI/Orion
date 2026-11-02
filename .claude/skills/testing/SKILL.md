---
name: testing
description: Use when writing, placing or pruning the tests for any Orion code change, including a bug fix; covers where a test goes, how it is written, and the mandatory pruning pass before the commit is squashed.
---

# Testing

`AGENTS.md` lists the gates and when to run them.
This is where a new test goes, how to write it, and how to prune what you wrote.

## Part one: placing and writing

| What changed | Where its test goes |
|---|---|
| Engine maths, a filter, a GPU render | `apps/tests/` (`orion-tests`): a `void testX()` in the `tests_<subject>.cpp` that holds its fixture, declared in `harness.h`, called from `main.cpp`. A new file is added to `apps/tests/CMakeLists.txt`. Assert with `report(ok, what, detail)` or `CHECK(cond)`. A test that needs the GPU is named `...Gpu`. |
| The app's behaviour: panels, sidecars, library, canvas, commands | The selftest in `shell/Shared/SelfTest*.cs`: a `Check(ok, what)` in the area's partial, reached from `SelfTest.Run`. A check one CI system cannot pass goes in `tools/ci-selftest.py`'s `SKIP`, with its reason. |
| A command-line mode (`--batch-export`, `--hdr-merge`, `--stack`, a new one) | `tools/check-modes.py` |
| A gesture that moves the picture | `tools/check-gestures.py`'s `EXPECTED` |
| A tool tab or what it draws | `tools/check-screens.py`, plus a selftest check for the controls |
| A mechanism only a window calls | `tools/check-wiring.py`'s `EXPECTED` |
| The MCP server's protocol | `mcp/server.test.ts` or `mcp/server.limits.test.ts` against `mcp/test/fake-orion.sh` |
| The MCP server against a real render | a step in `tools/check_agent_steps.py`'s `STEPS` |
| The site (`web/`) | `tools/check-site.py` |
| A constant cited from `research/` | an `orion-tests` check of the invariant the source implies |

Rules for every test:

- It asserts something a user or caller could see break: a pixel, a file, an exit code, a value in a sidecar, a line of output.
- It takes the real path over a mock: a real GPU render, a real RAW from `ORION_SAMPLES` or a generated fixture, a real file on disk.
  A fake stands in only where the real thing cannot run headless.
- Its failure message names what broke, so a red line needs no debugger to read.
- A bug fix starts with the bug reproduced end to end, as a user hits it.
  The test is written from that reproduction and fails before the fix.
- A test that needs a window is not a test here; find the headless path (`--selftest`, `--screenshot`, `--command`).

## Part two: the pruning pass

Mandatory once the tests pass and before the commit is squashed.
Re-read every test you added or changed, and delete any that fails one of these:

| | Delete it when |
|---|---|
| a | It duplicates another test's failure: the same change to the code would fail both. Keep the clearer one. |
| b | It tests the language, the standard library, a framework or a third-party library rather than Orion. |
| c | It asserts an implementation detail no user or caller could observe, so a correct rewrite would fail it. |
| d | It still passes with the feature or fix reverted. |
| e | It compares against a large expected-output blob with no oracle saying why those bytes are right. |
| f | It runs code without asserting anything about the result. |

### The mutation check (d)

Do it for every new test, not by reading:

1. Revert the production change and keep the tests: `git stash push -- <production files>`, or comment out the fix.
2. Rebuild and run the narrowest command that holds the test.
3. It must fail, on the line you expect. A test that stays green is rewritten until it fails, or deleted.
4. Restore the change (`git stash pop`) and run it again: it passes.

For a grep gate, the mutation is deleting the call it guards.

### The size check

New and changed test lines should not exceed the production lines changed (`git diff --stat origin/main...HEAD`).
When they do, the pull request says why in "How it was checked".

### The report

The pull request's "How it was checked" carries three lines:

```
Tests added: <names, and the command that runs them>
Pruned: <names deleted and the letter of the rule each failed, or "none">
Mutation: <each new test failed with the change reverted, or which did not and what was done>
```
