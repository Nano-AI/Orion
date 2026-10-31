#!/usr/bin/env python3
"""Run the three command-line modes that nothing else runs.

    ./tools/check-modes.py            (ORION_SAMPLES names a folder of RAWs)

⚠ **Why this exists.** Each command-line mode is a short dispatch in
`Program.cs`, and **deleting one used to be green on every gate** (#121,
mutations M1/M3/M4; #179). `tools/check-screens.py` closes `--screenshot`,
`tools/check-agent.py` closes `--agent`. These are the others:

  --batch-export   exports a list of photographs into a folder and exits 1 if
                   any of them failed (each with its sidecar's edit).
  --hdr-merge      merges frames into one DNG, then opens and renders that DNG
                   and exits 1 if it will not.
  --stack          stacks frames into one linear DNG, then opens and renders
                   it and exits 1 if it will not. Run on one sample copied
                   three times: identical frames, which noise mode aligns
                   trivially, so a failure is the pipeline's and not the
                   scene's.

⚠ **Ported to the Avalonia app (decision #380), and two modes did not come.**
The Swift app's `--save-safety` (20 checks of autosave's departure paths) is now
the selftest's Sidecar checks (`SelfTest.Sidecar.cs`: with a writer that
fails, Open, Next, Previous and a folder Load all refuse to leave the photo,
Trash moves nothing, the footer names the failure, and the owed write lands
once it can). Lost from it: the filmstrip and gallery click paths, which go
through the same refusal but are not driven. Its
`--library-open` opened one folder cold, warm and indexless and compared them:
the Avalonia app has no SQLite index, so there is no warm path to disagree with
the cold one, and that property is gone with the index rather than lost.

⚠ **What this does NOT check:**

  - that the exported pixels are *right*. The size floor catches a blank frame
    and nothing subtler; the export path's correctness is `orion-tests` and the
    selftest's.
  - `--screenshot` (`check-screens.py`) or `--agent` (`check-agent.py`).

⚠ **How each guard here was verified.** Deleting either dispatch from
`Program.cs` is caught — **by the timeout**, not an exit code, because a
deleted dispatch makes Orion open a window rather than exit. The floors were
proved only by raising their constants and watching the branch fire.
"""

import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
ORION = ROOT / "shell" / "bin" / "Release" / "net10.0" / "Orion"
SAMPLES = Path(os.environ.get("ORION_SAMPLES", ROOT / "samples"))
os.environ.setdefault("DOTNET_ROOT", "/opt/homebrew/opt/dotnet/libexec")

# ⚠ `_PIC8095.ARW` is deliberately not in this list. It has people in the plaza
# at its base, and while an export into a temporary directory is not a
# publication, the simplest way to keep that frame out of anything shipped is
# not to render it in a script somebody may later copy.
#
# Two frames, the first two of these that exist: `samples/` is not the same
# folder on every machine, and the `_PIC` names exist on no volume here
# (2026-10-10), so the gate named files nobody had and exited 2 for everyone.
EXPORT_CANDIDATES = ["_PIC8220.ARW", "_PIC8148.ARW",
                     "overexposed-background.ARW", "underexposedsubject.ARW"]
EXPORTS = [c for c in EXPORT_CANDIDATES if (SAMPLES / c).is_file()][:2]
if len(EXPORTS) < 2:
    EXPORTS = EXPORT_CANDIDATES[:2]

# A JPEG of a 24 MP photograph is megabytes. A blank or single-colour frame
# compresses to tens of kilobytes, which is the failure this catches — an export
# that "succeeded" and wrote nothing worth having. It is a floor and not an
# oracle, and the docstring says so.
MIN_EXPORT_BYTES = 500_000

# A merged fp16 DNG of a 24 MP frame is ~140 MB; a header with no strips is
# kilobytes. Same floor-not-oracle reasoning as MIN_EXPORT_BYTES.
MIN_MERGE_BYTES = 10_000_000

# A stacked linear DNG of a 24 MP frame is tens of MB; a header with no strips
# is kilobytes. Same floor-not-oracle reasoning as the two above.
MIN_STACK_BYTES = 10_000_000

# Measured 2026-08-07 on the Swift app: --batch-export 1.55s for two frames.
# ⚠ Generous by a wide margin on purpose — deleting a dispatch does not make
# the process exit, it makes Orion **open a real window and wait for a
# person**, and this timeout is the only thing that ends it. Keep it well above
# the real cost and well below anybody's patience.
# (--hdr-merge decodes and demosaics every frame and runs a CPU merge; on two
# 24 MP frames it is the slowest mode here, and still far inside a minute
# twice over.)
TIMEOUT = 120


def run(args, problems, what):
    """Run Orion with these arguments. None on a hang, which is reported here."""
    try:
        return subprocess.run([str(ORION)] + args, capture_output=True,
                              text=True, timeout=TIMEOUT)
    except subprocess.TimeoutExpired:
        problems.append(
            f"{what} did not finish in {TIMEOUT}s — it is likely waiting in a "
            f"window rather than doing the work. Check that Program.cs "
            f"still dispatches on it")
        return None


def main():
    if not ORION.is_file():
        print(f"check-modes: no binary at {ORION}\n"
              f"  Build first: shell/native/build.sh && dotnet build -c Release shell", file=sys.stderr)
        return 2
    missing = [p for p in EXPORTS if not (SAMPLES / p).is_file()]
    if missing:
        print(f"check-modes: no sample photograph(s): {', '.join(missing)} "
              f"in {SAMPLES} (set ORION_SAMPLES)",
              file=sys.stderr)
        return 2

    problems = []
    notes = []

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

    # --stack: one sample three times (identical frames, so alignment is
    # trivial) into one DNG that must itself open.
    with tempfile.TemporaryDirectory() as tmp:
        frames = []
        for n in "abc":
            frames.append(os.path.join(tmp, f"{n}.ARW"))
            shutil.copyfile(SAMPLES / EXPORTS[0], frames[-1])
        stacked = os.path.join(tmp, "stacked.dng")
        r = run(["--stack", stacked, "--mode", "noise"] + frames, problems, "--stack")
        if r is not None:
            out = (r.stderr or "") + (r.stdout or "")
            if r.returncode != 0:
                detail = out.strip()[-500:] or "(no output)"
                problems.append(f"--stack exited {r.returncode}\n"
                                f"      {detail}")
            elif not os.path.isfile(stacked):
                problems.append(
                    "--stack exited 0 and wrote nothing. The mode asserts "
                    "its own output opens, so this means the assertion is gone")
            elif os.path.getsize(stacked) < MIN_STACK_BYTES:
                problems.append(
                    f"--stack wrote {os.path.getsize(stacked)} bytes, under "
                    f"the {MIN_STACK_BYTES} floor. A full-resolution DNG does "
                    f"not fit there; a header with no strips does")
            else:
                mb = os.path.getsize(stacked) / 1_048_576
                notes.append(f"--stack {mb:.0f} MB DNG")

    if problems:
        print(f"check-modes: {len(problems)} problem(s)\n")
        for p in problems:
            print(f"  {p}\n")
        return 1

    print(f"check-modes: {'; '.join(notes)}.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
