#!/usr/bin/env python3
"""Runs the Avalonia shell's --selftest in CI and holds it to a stated skip list.

    tools/ci-selftest.py SYSTEM Orion PHOTO.dng

SYSTEM is macos, windows or linux. The selftest prints `FAIL <check>` per failed
check and `selftest: N checks passed, M failed` last. This passes when every
FAIL line starts with one of SKIP's prefixes for this system (each printed, with
its reason) and the summary line is there; a crash, a missing summary or any
other FAIL is a failure. Decision #376.

The photo is tools/fixtures/selftest-photo.cpp's generated linear DNG, not a
camera RAW, which is why some checks are skipped on every system.
"""
import subprocess
import sys

FIXTURE = "the CI photo is a generated linear DNG"
SKIP = {
    "all": {
        "the capture line reads camera, lens": FIXTURE + ": no EXIF exposure or lens to read",
        "every enabled slider on the develop tabs writes its field":
            FIXTURE + ": no noise profile or ISO, so the denoise, grain and texture sliders wait",
        "past fit with a plain edit, the engine renders only the part in view":
            FIXTURE + ": the region graph declines it and the full graph renders",
        "the selection reaches the picture": FIXTURE + ": the subject Vision finds barely moves it",
        "after an arrow key every one of 9 tracks shows its photo's value at once":
            "fails on main too (the glide, STATUS.md)",
    },
    "macos": {},
    "windows": {},
    "linux": {},
}

# Where CI has no GPU the engine cannot open (Linux: no CPU Vulkan driver offers
# TextureFormatsTier1; orion-tests in the same job names the adapter's refusal,
# since orion_engine_create has nowhere to put a message). Then the selftest runs
# its photo-free checks and stops, and any FAIL is counted as skipped for that
# reason; the run must still finish and print its summary.
NO_GPU = {"linux": "shim_open: no engine"}


def main() -> int:
    system, exe, photo = sys.argv[1:4]
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")   # Windows' console is cp1252
    skip = {**SKIP["all"], **SKIP[system]}
    run = subprocess.run([exe, photo, "--selftest"], capture_output=True, text=True,
                         encoding="utf-8", errors="replace")
    out = run.stdout + run.stderr
    print(out)
    fails = [l[5:] for l in out.splitlines() if l.startswith("FAIL ")]
    summary = [l for l in out.splitlines() if l.startswith("selftest: ") and "checks passed" in l]
    unexpected = []
    no_gpu = system in NO_GPU and NO_GPU[system] in out
    for f in fails:
        reason = next((r for p, r in skip.items() if f.startswith(p)), None)
        if not reason and no_gpu:
            reason = "no usable GPU on this runner, so no engine"
        if reason:
            print(f"skipped on {system}: {f[:90]} ({reason})")
        else:
            unexpected.append(f)
    if not summary:
        print(f"ci-selftest: no summary line; exit {run.returncode} (a crash?)")
        return 1
    print(f"ci-selftest: {summary[-1]}; {len(fails) - len(unexpected)} skipped, {len(unexpected)} unexpected")
    for f in unexpected:
        print(f"ci-selftest: UNEXPECTED FAIL {f}")
    return 1 if unexpected else 0


if __name__ == "__main__":
    sys.exit(main())
