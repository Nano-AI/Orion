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

A fourth, `versions`, is rendered twice and required to come back byte-identical
— see `STABLE`. That is a check on the *harness*, not on the panel.

⚠ **What this does NOT check**, said plainly so nobody reads more into a green
run than is there:

  - the other ~34 scenes. They *pose*; their oracle is a person looking at a
    PNG, and this cannot supply one.
  - that `versions` draws the right rows. A wrong panel is byte-identical to
    itself and passes the stability check.
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

# Scenes rendered twice and required to come back byte-identical.
#
# ⚠ **`versions` is here because it was the one scene that could not be
# compared against anything, including itself.** Its rows were built from
# `Date()` and the panel prints an absolute clock time, so two runs of one
# binary differed by 2,380 bytes in the timestamp glyphs alone — 37 of 38 scenes
# were stable and this was the 38th. A posing scene's only possible oracle is a
# frame compared against another frame, and a scene that disagrees with itself
# has ruled that out in advance.
#
# ⚠⚠ **This is the weaker half of that check and must not be read as the whole
# of it.** Rendering twice and demanding agreement went **green on the mutation
# that puts `Date()` back**: the panel formats with `.short` time style, whose
# resolution is one minute, and two renders seconds apart nearly always land
# inside the same one. It catches that defect about one run in twenty. The
# deterministic catch is inside the scene — `assertVersionsDoNotShowTheClock`,
# which asserts the rows are years old rather than seconds old, and does not
# depend on when it runs.
#
# What this adds on top is the class the scene's own check cannot see: a random
# id, an unsettled layout, a thumbnail arriving late — anything that varies
# between two runs without being a date. That is a defect in the *harness*, and
# the harness is what every posing scene's review depends on.
#
# ⚠ **It checks stability, not correctness.** A `versions` panel drawing the
# wrong three rows, or none, is byte-identical to itself and passes here.
STABLE = {
    "versions":
        "the frame disagrees with itself across two runs, so something in it "
        "is a clock, a random value or an unsettled layout",
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

    def shoot(scene, out):
        """Render one scene. Returns its CompletedProcess, or None on a hang —
        which is reported here rather than by the caller, because the reason is
        the same whichever check asked for the frame."""
        cmd = [str(ORION), "--screenshot", out, "--scene", scene,
               "--photo", str(PHOTO)]
        try:
            return subprocess.run(cmd, capture_output=True, text=True,
                                  timeout=TIMEOUT)
        except subprocess.TimeoutExpired:
            # ⚠ Almost always one thing: the process is sitting in front of a
            # window instead of rendering one. `--screenshot` is checked in
            # `OrionApp.init`, and if that check is gone the flag is ignored and
            # Orion launches normally — which looks like a hang from here and is
            # really a deleted command-line mode.
            problems.append(
                f"{scene} did not finish in {TIMEOUT}s — it is likely waiting "
                f"in a window rather than rendering one. Check that "
                f"OrionApp.init still dispatches on --screenshot")
            return None

    with tempfile.TemporaryDirectory() as tmp:
        for scene, why in SCENES.items():
            r = shoot(scene, os.path.join(tmp, f"{scene}.png"))
            if r is None:
                continue
            if r.returncode != 0:
                # The scene's own message is the useful half — it names the
                # missing command or the equal frames. Carry it through rather
                # than replacing it with an exit code.
                detail = (r.stderr or r.stdout).strip() or "(no output)"
                problems.append(f"{scene} exited {r.returncode} — {why}\n"
                                f"      {detail}")

        for scene, why in STABLE.items():
            frames = []
            for pass_no in (1, 2):
                out = os.path.join(tmp, f"{scene}-{pass_no}.png")
                r = shoot(scene, out)
                if r is None:
                    break
                if r.returncode != 0:
                    detail = (r.stderr or r.stdout).strip() or "(no output)"
                    problems.append(f"{scene} exited {r.returncode} while "
                                    f"being rendered for the stability check\n"
                                    f"      {detail}")
                    break
                frames.append(Path(out).read_bytes())

            if len(frames) == 2 and frames[0] != frames[1]:
                same = sum(1 for a, b in zip(*frames) if a == b)
                problems.append(
                    f"{scene} is not byte-stable — {why}\n"
                    f"      {len(frames[0])} bytes against {len(frames[1])}, "
                    f"{len(frames[0]) - same} of them differing")

    if problems:
        print(f"check-screens: {len(problems)} scene(s) failed\n")
        for p in problems:
            print(f"  {p}\n")
        return 1

    print(f"check-screens: {len(SCENES)} asserting scene(s) green "
          f"({', '.join(SCENES)}); {len(STABLE)} byte-stable "
          f"({', '.join(STABLE)}).")
    return 0


if __name__ == "__main__":
    sys.exit(main())
