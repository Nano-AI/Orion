#!/usr/bin/env python3
"""Check that mechanisms the product built are called by the product.

    ./tools/check-wiring.py

⚠ **Why this exists.** `Engine.showPlaceholder` was written, drawn by
`OrionApp+Canvas` under a comment reading *"held while a new photo decodes"*,
and referred to by a second comment in `OrionApp+Files` promising *"one runloop
turn, so the placeholder actually paints before the synchronous decode
begins"*. **Its only caller in the tree was the screenshot harness**, and
`clearPlaceholder` had no caller at all. So for every one of the 210.9 ms a raw
file takes to decode (#151) the canvas went on showing the *previous*
photograph, while two comments described the opposite.

Nothing could catch it. Both suites are green on the mechanism being dead —
`apps/tests` and `apps/bench` are pure C++, and `orion-viewport-tests` compiles
a Swift list with **zero** `OrionApp*` files (#121), so no test in the
repository can drive the open path at all.

⚠ **This is a grep, and it says so** — the same standing as
`tools/check-gestures.py`, which exists for the same structural reason (#110.3).
It cannot check that the call is on the right branch, in the right order, or
with the right argument. It checks that a mechanism with a *product* caller
still has one, which is the regression that actually happened.

⚠ **A harness caller does not count, and that is the whole idea.** The failure
here was not a missing function — it was a function that looked used because a
test used it. So `Screenshot*.swift`, `Scenario*.swift`, `ViewportTests*.swift`
and `LibraryProbe.swift` are excluded from the count on purpose.

**The sweep is the second half, added 2026-08-07 (#183).** The list above is
declared; this file now also *discovers*. It walks every `func` declared in a
product file, counts calls from product files and from harness files, and
reports any whose callers are **all** harness. That is `showPlaceholder`'s exact
shape, and running it the first time found **nine**: eight defensible, each now
carrying its reason in `HARNESS_ONLY` below, and one real — `SyncSettings.pasted`,
a wrapper around `Preset.applied(to:)` whose only caller was a test while the
product went through `Engine.apply(preset:)`. It looked like the paste path and
was not one. Deleted.

⚠ **The allowlist is the point, not a suppression.** A function that is
legitimately harness-only has to say *why* here, so the next one that appears is
a line somebody has to write a justification for rather than a silent pass.

⚠ **What this still does NOT do.** It is a regex over Swift source, not a
compiler: a call made through a stored closure, a selector, a protocol witness
or a SwiftUI key path is invisible to it, so it can call a *used* function
unused. Every entry below was checked by hand. It also only looks at `app/` —
the C++ engine has its own reachability question and this is not it.
"""

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
APP = ROOT / "app"

# Files that are the harness rather than the product. A call from one of these
# is exactly the evidence that misled everybody the first time.
HARNESS = re.compile(r"^(Screenshot|Scenario|ViewportTests|LibraryProbe)")

# symbol -> why the product must go on calling it.
EXPECTED = {
    "showPlaceholder":
        "a raw decode is synchronous and took 210.9 ms when measured (#151); "
        "without this the canvas shows the PREVIOUS photograph for all of it, "
        "and the flat-frame case puts a correct picture over a wrong one",
    "clearPlaceholder":
        "the still must come down when the render lands, and on the failure "
        "path too — otherwise a photograph that would not open leaves its "
        "thumbnail sitting over the one that is still loaded",
    # ⚠ `--open` is the **fifth** command-line mode, and #177 and #179 both
    # called them four while closing the others. It is the only one that cannot
    # be a gate: it opens a real window, steps a list with a dwell, and never
    # exits — it exists so a person can reproduce a fault visible only on
    # screen, which is how the flat brown rectangle was found. So there is no
    # exit code to check and no oracle to write, and this is what is left: the
    # dispatch must still be there. Deleting it is otherwise green everywhere.
    "openFromCommandLine":
        "`--open` is the only path that drives the real MTKView, and the bug it "
        "was built for was reproducible no other way. It cannot be gated — it "
        "never exits — so a grep on its wiring is the whole of the coverage",
}


# Product functions the harness alone may call, and why each is allowed to be.
# ⚠ Adding a name here is a claim, and the claim is checked by hand: that the
# function is a deliberate test hook or a deliberately offscreen path, not a
# mechanism the product forgot to call. Nine were triaged when the sweep was
# written; the ninth was not defensible and was deleted rather than listed.
HARNESS_ONLY = {
    "escapeForTests":
        "named for what it is — reaches Sidecar's escaping without a file",
    "executeForTests":
        "named for what it is — drives one SQL statement against PhotoIndex",
    "resetStats":
        "zeroes PhotoIndex's hit and miss counters so a test can measure one "
        "open rather than every open since launch",
    "setWideOutput":
        "its own docstring says so: the screen path is 8-bit and widening it "
        "costs 3.5 ms of a 16 ms budget, so this exists for the screenshot "
        "harness, which reads the output texture and measures to four decimals",
    "composite":
        "the offscreen render the scenario runner goes through, deliberately "
        "sharing the canvas's pipeline rather than a second one that happens "
        "to agree — CanvasBlit's header says as much",
    "generateBlocking":
        "the synchronous spelling of the subject matte, so a check does not "
        "have to drive an async inference to assert what it produced",
    "maskAlpha":
        "a model of the mask kernel's falloff, used as the oracle a rendered "
        "mask is graded against — it is not meant to be in the product path",
    "spec":
        "AdjustmentCatalogue's per-id lookup, which exists so a check can "
        "assert the catalogue covers every adjustment; the interface iterates "
        "`specs` instead",
}


def main():
    if not APP.is_dir():
        print(f"check-wiring: no app directory at {APP}", file=sys.stderr)
        return 2

    # Strip comments before counting. `check-gestures.py`'s first version did
    # not, and a call named in the prose above it kept the gate green after the
    # real call was deleted.
    #
    # ⚠ **Defensive here rather than load-bearing, checked rather than assumed.**
    # Both symbols *are* named in comments in `OrionApp+Files`, but in backticks
    # and without parentheses, so the pattern below misses them anyway — with
    # this stripping removed and the real call deleted the gate still goes red.
    # It earns its place against the comment somebody writes next: a line like
    # `// call clearPlaceholder() when the render lands` would otherwise be a
    # caller.
    def uncommented(text):
        text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
        return "\n".join(re.sub(r"//.*$", "", line) for line in text.splitlines())

    sources = {}
    for path in sorted(APP.glob("*.swift")):
        sources[path.name] = uncommented(
            path.read_text(encoding="utf-8", errors="ignore"))

    problems = []
    for symbol, why in sorted(EXPECTED.items()):
        product = []
        for name, body in sources.items():
            # The declaration is not a call. Count uses that are not `func x(`.
            calls = len(re.findall(rf"\b{symbol}\s*\(", body))
            calls -= len(re.findall(rf"\bfunc\s+{symbol}\s*\(", body))
            if calls <= 0:
                continue
            if HARNESS.match(name):
                continue
            product.append(f"{name}×{calls}")

        if not product:
            problems.append(
                f"{symbol}() is called by no product file — {why}.\n"
                f"      A call from the harness does not count: that is what "
                f"made this look wired the first time.")

    # The sweep. Every `func` declared in a product file, and who calls it.
    declared = {}
    for name, body in sources.items():
        if HARNESS.match(name):
            continue
        for m in re.finditer(r"\bfunc\s+([A-Za-z_]\w*)\s*[\(<]", body):
            declared.setdefault(m.group(1), set()).add(name)

    found = []
    for symbol, where in sorted(declared.items()):
        product = harness = 0
        for name, body in sources.items():
            calls = len(re.findall(rf"\b{re.escape(symbol)}\s*\(", body))
            calls -= len(re.findall(rf"\bfunc\s+{re.escape(symbol)}\s*\(", body))
            if calls <= 0:
                continue
            if HARNESS.match(name):
                harness += calls
            else:
                product += calls
        if harness > 0 and product == 0:
            found.append((symbol, sorted(where), harness))

    for symbol, where, harness in found:
        if symbol in HARNESS_ONLY:
            continue
        problems.append(
            f"{symbol}() is declared in {', '.join(where)} and called only by "
            f"the harness ({harness} call(s)).\n"
            f"      Either the product forgot to call it — which is how the "
            f"canvas spent 210.9 ms showing the wrong photograph (#181) — or "
            f"it is a deliberate test hook, in which case add it to "
            f"HARNESS_ONLY with the reason.\n"
            f"      ⚠ Check by hand first: this is a regex, and a call through "
            f"a closure, a selector or a key path is invisible to it.")

    if problems:
        print(f"check-wiring: {len(problems)} problem(s)\n")
        for p in problems:
            print(f"  {p}\n")
        return 1

    print(f"check-wiring: {len(EXPECTED)} declared mechanism(s) still called by "
          f"the product; swept {len(declared)} product function(s), "
          f"{len(found)} harness-only and all accounted for.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
