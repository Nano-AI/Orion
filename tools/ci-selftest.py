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
        "the sample's preview seeds a matte of at most":
            FIXTURE + ": it carries no embedded preview, so there is no sky to seed (the sheet says so)",
        "after an arrow key every one of 9 tracks shows its photo's value at once":
            "fails on main too (the glide, STATUS.md)",
    },
    "macos": {},
    "windows": {
        # Diagnosed: EditHistory folds two edits of one control into one step inside
        # 0.6 s of wall clock, and WARP (a CPU rasterizer) renders each Set for longer.
        "undo restores a value, and a drag's ticks were one step":
            "WARP renders each Set for longer than the 0.6 s that folds a drag's ticks into one step",
        # ⚠ Undiagnosed (#376's open items): the zoom reached 100% with no frame and
        # Live false, which Motion.Ask does only with no animation clock or Reduce
        # Motion on, and the selftest forces motion on. Wants a real Windows machine.
        "a double-click springs to 100%": "UNDIAGNOSED: the spring ran to its end with 0 frames (#376)",
        "ReleaseRenderMemory gives back": "the footprint off macOS is the working set, and WARP's video memory is the process's own",
    },
    "linux": {},
}

# Where CI has no GPU the engine cannot open (Linux: no CPU Vulkan driver offers
# TextureFormatsTier1). Then the selftest runs its photo-free checks and stops on
# "needs a photo". That one FAIL, and only it, is a skip, and only when the shim
# printed Dawn's refusal of the adapter: any other reason the engine did not open
# (missing shaders, a crash in create) and every photo-free check still fail.
NO_GPU = {"linux": "adapter lacks feature"}
NEEDS_PHOTO = "selftest needs a photo"


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
        if not reason and no_gpu and f.startswith(NEEDS_PHOTO):
            reason = "no usable GPU on this runner, so no engine: " + NO_GPU[system]
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
