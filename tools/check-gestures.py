#!/usr/bin/env python3
"""Check that every gesture that moves the picture arms the preview graph.

Run it with the other gates:

    ./tools/check-gestures.py

Exits nonzero and names the file that stopped arming.

⚠ **Why this is a grep and not a test, which is the uncomfortable part.**
A gesture arms by calling `engine.beginInteraction()` inside a SwiftUI
`DragGesture` closure, and *nothing in this repository can drive one*. That was
attempted and written up in decision #110.3: an off-screen `NSHostingView` lays
the control out and hit-tests it, but `NSEvent.mouseEvent` through
`NSApplication.sendEvent` never reaches the recognizer, and CGEvent-backed
events need a real on-screen window and the real cursor. Deleting `ColorWheel`'s
call was measured green across the whole suite.

So the choice is a grep or nothing, and nothing is what there was. This cannot
prove a gesture arms *correctly* — it proves the call has not been deleted,
which is the regression that actually happened twice.

⚠ **The reason it exists now.** `ROADMAP.md` carried a table saying four of
these did not arm — `CropOverlay`, `SpotOverlay`, `CurveEditor`, `ColorWheel` —
and used it as the premise for a performance audit. All four had been fixed by
2026-08-03 and the table had never been updated. A gate that is only a
documentation table goes stale silently; this one is executable, so it cannot.

⚠ **What this does NOT check**, said plainly so nobody reads more into a green
run than is there:

  - that `beginInteraction` is called on the *right* branch of the gesture
  - that `endInteraction` is always reached, including on cancel
  - that the preview graph is actually cheaper, which is `orion-bench`'s job
  - anything at all about `AnalogTrack`, which arms via `AdjustmentSlider`
    rather than itself
"""

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
APP = ROOT / "app"

# Each entry: the file, and why it is expected to arm. The reason is carried so
# a failure says what the gesture costs, not merely that a string is missing.
EXPECTED = {
    "AdjustmentSlider.swift":
        "every slider in the application drags through it",
    "ColorWheel.swift":
        "a grading wheel drag re-renders the full graph per tick otherwise "
        "(9.6 ms against 1.2 ms armed, decision #110.2)",
    "CurveEditor.swift":
        "dragging a curve point is a full-resolution render per tick otherwise",
    "CropOverlay.swift":
        "the crop rectangle is the geometry being changed, so every tick "
        "rebuilds it",
    "SpotOverlay.swift":
        "placing and dragging a spot re-renders the heal pass per tick",
    "MaskOverlay.swift":
        "painting and placing a mask re-renders the mask chain per tick",
}

BEGIN = "beginInteraction"
END = "endInteraction"


def uncommented(text):
    """The source with // line comments and /* */ blocks removed.

    ⚠ Counting raw occurrences is what made the first version of this wrong:
    `ColorWheel` mentions `beginInteraction` in a comment above the call, and
    `CropOverlay` mentions `endInteraction` in one, so a naive grep reports two
    calls where there is one and would stay green if the real call went away.
    """
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    return "\n".join(re.sub(r"//.*$", "", line) for line in text.splitlines())


def main():
    if not APP.is_dir():
        print(f"check-gestures: no app directory at {APP}", file=sys.stderr)
        return 2

    problems = []
    checked = 0

    for name, why in sorted(EXPECTED.items()):
        path = APP / name
        if not path.is_file():
            problems.append(
                f"{name} is gone. If the control was renamed, rename it here "
                f"too; if it was deleted, drop the entry. Silence is the one "
                f"outcome this file exists to prevent.")
            continue

        body = uncommented(path.read_text(encoding="utf-8", errors="ignore"))
        checked += 1

        if BEGIN not in body:
            problems.append(
                f"{name} never calls {BEGIN}() outside a comment — {why}.")
        if END not in body:
            problems.append(
                f"{name} calls {BEGIN}() but never {END}() outside a comment. "
                f"A gesture that arms and never disarms leaves the canvas on "
                f"the preview graph after the pointer is up, which looks like "
                f"a permanently soft photograph.")

    if problems:
        print(f"check-gestures: {len(problems)} problem(s)\n")
        for p in problems:
            print(f"  {p}\n")
        return 1

    print(f"check-gestures: {checked} gesture(s) arm and disarm the preview graph.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
