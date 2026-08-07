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

⚠ **What this does NOT do:** find the *next* mechanism like this. The list below
is declared, not discovered — a sweep for every method whose only callers are in
the harness would be the real instrument, and it is not this. Add a row when a
mechanism is worth this protection.
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

    if problems:
        print(f"check-wiring: {len(problems)} problem(s)\n")
        for p in problems:
            print(f"  {p}\n")
        return 1

    print(f"check-wiring: {len(EXPECTED)} mechanism(s) still called by the "
          f"product.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
