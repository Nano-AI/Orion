#!/usr/bin/env python3
"""Run the `--screenshot` scenes that assert, and fail when one of them does.

Run it with the other gates:

    ./tools/check-screens.py

⚠ **Why this exists.** Three scenes in `app/Screenshot+Checks.swift` are checks
rather than pictures — each was written against a named mutation that deleted
shipped interface with every check in the repository green (decision #125). But
`--screenshot` is not `--scenario`, and the 42-file sweep only drives
`--scenario`. So all three were *invoked by hand*, which means they ran on the
day they were written and not since. A check nobody runs is a comment.

⚠ **What each one is for**, so a red says what broke rather than which command
exited nonzero:

  menu          a `PhotoCommands` item is deleted or renamed — reads the real
                `NSApp.mainMenu` after handing the process back to `OrionApp`
  detail-tail   anything below the fold in the Detail panel stops overflowing,
                so the harness photographs the same thing scrolled as at rest
  render-failed the footer stops drawing `engine.lastFailure` — renders its own
                control twice in-process and compares, no reference image

⚠ **What this does NOT check**, said plainly so nobody reads more into a green
run than is there:

  - the other ~35 scenes. They *pose*; their oracle is a person looking at a
    PNG, and this cannot supply one.
  - that the menu items *fire*. `menu` asserts presence by title; firing one
    needs a photograph, a key window and focus (#110.3's shape).
  - the wording or colour of the failure line. `render-failed` pins that the
    footer's appearance depends on `lastFailure` at all.
  - `--batch-export` and `--library-open`, the two command-line modes still
    driven by no gate (#121).
"""

import os
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
ORION = ROOT / "build" / "Orion.app" / "Contents" / "MacOS" / "Orion"

# ⚠ The sample is named here rather than discovered. `render-failed` draws
# nothing without a photograph — the status line lives inside `if
# engine.isLoaded` — so a scene run against an empty editor would be checking an
# absent view, and the scene itself now refuses that case by name.
PHOTO = ROOT / "samples" / "_PIC8220.ARW"

# Each scene, and what its red means. The reason is carried so a failure says
# what the interface lost, not merely that a process exited 1.
SCENES = {
    "menu":
        "a command is missing from the menu bar AppKit actually builds",
    "detail-tail":
        "nothing overflows the panel column, so the scrolled frame and the "
        "frame at rest cover the same controls",
    "render-failed":
        "the footer no longer draws engine.lastFailure — either it renders the "
        "same with a failure planted as without one, or two different messages "
        "render the same frame",
}

# ⚠ **Measured, and generous by 10×.** On 2026-08-07 these took 1.98, 3.49 and
# 5.80 seconds — `menu` relaunches the whole application and `render-failed`
# lays the interface out four times, so they are not instant, but they are
# nowhere near a minute.
#
# The bound matters more than it looks. Deleting the `--screenshot` dispatch
# from `OrionApp.init` does not make these exit nonzero: the flag falls through,
# the application **opens a real window and waits for a person**, and the only
# thing that ends it is this timeout. At 300 seconds that mutation took a
# quarter of an hour to report; at 60 it takes three minutes. ⚠ Re-measure
# before raising this — a timeout tuned to the slowest scene is the difference
# between a gate and a hang.
TIMEOUT = 60


def main():
    if not ORION.is_file():
        print(f"check-screens: no binary at {ORION}\n"
              f"  Build first: cmake --build build", file=sys.stderr)
        return 2
    if not PHOTO.is_file():
        print(f"check-screens: no sample photograph at {PHOTO}",
              file=sys.stderr)
        return 2

    problems = []

    with tempfile.TemporaryDirectory() as tmp:
        for scene, why in SCENES.items():
            out = os.path.join(tmp, f"{scene}.png")
            cmd = [str(ORION), "--screenshot", out, "--scene", scene,
                   "--photo", str(PHOTO)]
            try:
                r = subprocess.run(cmd, capture_output=True, text=True,
                                   timeout=TIMEOUT)
            except subprocess.TimeoutExpired:
                # ⚠ Almost always one thing: the process is sitting in front of
                # a window instead of rendering one. `--screenshot` is checked
                # in `OrionApp.init`, and if that check is gone the flag is
                # ignored and Orion launches normally — which looks like a hang
                # from here and is really a deleted command-line mode.
                problems.append(
                    f"{scene} did not finish in {TIMEOUT}s — it is likely "
                    f"waiting in a window rather than rendering one. Check "
                    f"that OrionApp.init still dispatches on --screenshot")
                continue

            if r.returncode != 0:
                # The scene's own message is the useful half — it names the
                # missing command or the equal frames. Carry it through rather
                # than replacing it with an exit code.
                detail = (r.stderr or r.stdout).strip() or "(no output)"
                problems.append(f"{scene} exited {r.returncode} — {why}\n"
                                f"      {detail}")

    if problems:
        print(f"check-screens: {len(problems)} scene(s) failed\n")
        for p in problems:
            print(f"  {p}\n")
        return 1

    print(f"check-screens: {len(SCENES)} asserting scene(s) green "
          f"({', '.join(SCENES)}).")
    return 0


if __name__ == "__main__":
    sys.exit(main())
