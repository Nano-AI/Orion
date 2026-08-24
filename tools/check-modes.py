#!/usr/bin/env python3
"""Run the two command-line modes that nothing else runs.

    ./tools/check-modes.py

⚠ **Why this exists.** Orion has four command-line modes, each a four-line
dispatch in `OrionApp.init`, and **deleting any one of them used to be green on
every gate** (#121, mutations M1/M3/M4). Only `--scenario` was exercised,
because only it is what the 42-file repro sweep runs. `tools/check-screens.py`
closed `--screenshot` (#177). These are the other two.

Both already assert — that was checked before writing this, not assumed:

  --library-open   opens one folder **three ways in one process** — cold, warm,
                   and with no database at all — and fails when the warm pass
                   missed, or when any of the three disagree about a field.
                   13 checks on `samples/`. It uses a throwaway database in the
                   temporary directory, so it does not disturb the
                   photographer's own index.
  --batch-export   exports a list of photographs into a folder and exits 1 if
                   any of them failed. Verified against a file that is not
                   there: `FAILED … Input/output error`, exit 1.

⚠ **What this does NOT check:**

  - that the exported pixels are *right*. The size floor below catches a blank
    frame and nothing subtler; the export path's correctness is `orion-tests`
    and the repro sweep's job.
  - that a warm open is *faster*. `LibraryProbe` deliberately refuses to assert
    that — the same binary has measured 8.97 and 44.53 ms an hour apart on this
    machine — and asserts the hit count and the field agreement instead, both
    of which are load-independent.
  - `--scenario` (the repro sweep) or `--screenshot` (`check-screens.py`).

⚠ **How each guard here was verified, because the two are not the same
strength.** Deleting either dispatch from `OrionApp.init` was run, and both are
caught — **by the timeout**, not by an exit code, because a deleted dispatch
makes Orion open a window rather than exit. The two *floors* were proved only by
raising their constants and watching the branch fire: no product mutation was
run that takes `LibraryProbe` below ten checks or makes an export blank. So the
floors are known to work and are **not** known to have ever been needed, which
is a weaker claim and is written here rather than left to be assumed.
"""

import os
import re
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
ORION = ROOT / "build" / "Orion.app" / "Contents" / "MacOS" / "Orion"
SAMPLES = ROOT / "samples"

# ⚠ `_PIC8095.ARW` is deliberately not in this list. It has people in the plaza
# at its base, and while an export into a temporary directory is not a
# publication, the simplest way to keep that frame out of anything shipped is
# not to render it in a script somebody may later copy.
EXPORTS = ["_PIC8220.ARW", "_PIC8148.ARW"]

# ⚠ **A floor, so a mode that stops asserting cannot pass by exiting 0.** This
# is #124's lesson in another place: 39 of 40 repro scenarios once exited 0
# while running *zero* checks. `LibraryProbe` prints one line per check, and if
# it ever prints none the exit code alone would still be 0. Set below the 13 it
# prints today, so adding a check does not break the gate and deleting most of
# them does.
MIN_LIBRARY_CHECKS = 10

# A JPEG of a 24 MP photograph is megabytes. A blank or single-colour frame
# compresses to tens of kilobytes, which is the failure this catches — an export
# that "succeeded" and wrote nothing worth having. It is a floor and not an
# oracle, and the docstring says so.
MIN_EXPORT_BYTES = 500_000

# A merged fp16 DNG of a 24 MP frame is ~140 MB; a header with no strips is
# kilobytes. Same floor-not-oracle reasoning as MIN_EXPORT_BYTES.
MIN_MERGE_BYTES = 10_000_000

# Measured 2026-08-07: --library-open 0.08s, --batch-export 1.55s for two
# frames. ⚠ Generous by a wide margin on purpose — deleting a dispatch does not
# make the process exit, it makes Orion **open a real window and wait for a
# person**, and this timeout is the only thing that ends it. Keep it well above
# the real cost and well below anybody's patience.
# (--hdr-merge decodes and demosaics every frame and runs a CPU merge; on two
# 24 MP frames it is the slowest mode here, and still far inside a minute
# twice over.)
TIMEOUT = 120

CHECK_LINE = re.compile(r"^\s+(ok|FAIL)\b", re.M)


def run(args, problems, what):
    """Run Orion with these arguments. None on a hang, which is reported here."""
    try:
        return subprocess.run([str(ORION)] + args, capture_output=True,
                              text=True, timeout=TIMEOUT)
    except subprocess.TimeoutExpired:
        problems.append(
            f"{what} did not finish in {TIMEOUT}s — it is likely waiting in a "
            f"window rather than doing the work. Check that OrionApp.init "
            f"still dispatches on it")
        return None


def main():
    if not ORION.is_file():
        print(f"check-modes: no binary at {ORION}\n"
              f"  Build first: cmake --build build", file=sys.stderr)
        return 2
    missing = [p for p in EXPORTS if not (SAMPLES / p).is_file()]
    if missing:
        print(f"check-modes: no sample photograph(s): {', '.join(missing)}",
              file=sys.stderr)
        return 2

    problems = []
    notes = []

    # --library-open
    r = run(["--library-open", str(SAMPLES)], problems, "--library-open")
    if r is not None:
        out = (r.stderr or "") + (r.stdout or "")
        checks = len(CHECK_LINE.findall(out))
        if r.returncode != 0:
            failed = [ln.strip() for ln in out.splitlines()
                      if ln.strip().startswith("FAIL")]
            detail = "\n      ".join(failed) or out.strip()[-500:] or "(no output)"
            problems.append(f"--library-open exited {r.returncode}\n"
                            f"      {detail}")
        elif checks < MIN_LIBRARY_CHECKS:
            problems.append(
                f"--library-open exited 0 having run {checks} check(s), fewer "
                f"than the {MIN_LIBRARY_CHECKS} this gate requires. A mode that "
                f"asserts nothing exits 0 exactly like one that passes")
        else:
            notes.append(f"--library-open {checks} checks")

    # --batch-export
    with tempfile.TemporaryDirectory() as tmp:
        into = os.path.join(tmp, "exported")
        r = run(["--batch-export", into] + [str(SAMPLES / p) for p in EXPORTS],
                problems, "--batch-export")
        if r is not None:
            out = (r.stderr or "") + (r.stdout or "")
            if r.returncode != 0:
                detail = out.strip()[-500:] or "(no output)"
                problems.append(f"--batch-export exited {r.returncode}\n"
                                f"      {detail}")
            else:
                written = sorted(Path(into).glob("*")) if os.path.isdir(into) else []
                if len(written) != len(EXPORTS):
                    problems.append(
                        f"--batch-export exited 0 having written "
                        f"{len(written)} file(s) for {len(EXPORTS)} "
                        f"photograph(s). Exiting 0 is what it does when nothing "
                        f"*failed*, which is not the same as everything having "
                        f"been written")
                else:
                    small = [(f.name, f.stat().st_size) for f in written
                             if f.stat().st_size < MIN_EXPORT_BYTES]
                    if small:
                        listed = ", ".join(f"{n} at {b} bytes" for n, b in small)
                        problems.append(
                            f"--batch-export wrote {len(small)} file(s) under "
                            f"{MIN_EXPORT_BYTES} bytes ({listed}). A photograph "
                            f"does not compress that far; a blank frame does")
                    else:
                        kb = sum(f.stat().st_size for f in written) / 1024
                        notes.append(f"--batch-export {len(written)} files, "
                                     f"{kb:.0f} KB")

    # --hdr-merge: two of the samples into one DNG that must itself open.
    # The samples are different scenes, not a bracket — alignment refuses and
    # the merge degrades to reference-only, which is exactly the degradation
    # path worth gating: decode, demosaic, refusal, merge, write, reopen.
    with tempfile.TemporaryDirectory() as tmp:
        merged = os.path.join(tmp, "merged.dng")
        r = run(["--hdr-merge", merged] + [str(SAMPLES / p) for p in EXPORTS],
                problems, "--hdr-merge")
        if r is not None:
            out = (r.stderr or "") + (r.stdout or "")
            if r.returncode != 0:
                detail = out.strip()[-500:] or "(no output)"
                problems.append(f"--hdr-merge exited {r.returncode}\n"
                                f"      {detail}")
            elif not os.path.isfile(merged):
                problems.append(
                    "--hdr-merge exited 0 and wrote nothing. The mode asserts "
                    "its own output opens, so this means the assertion is gone")
            elif os.path.getsize(merged) < MIN_MERGE_BYTES:
                problems.append(
                    f"--hdr-merge wrote {os.path.getsize(merged)} bytes, under "
                    f"the {MIN_MERGE_BYTES} floor. A full-resolution fp16 DNG "
                    f"does not fit there; a header with no strips does")
            else:
                mb = os.path.getsize(merged) / 1_048_576
                notes.append(f"--hdr-merge {mb:.0f} MB DNG")

    if problems:
        print(f"check-modes: {len(problems)} problem(s)\n")
        for p in problems:
            print(f"  {p}\n")
        return 1

    print(f"check-modes: {'; '.join(notes)}.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
