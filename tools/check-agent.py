#!/usr/bin/env python3
"""Drive the Orion MCP server end-to-end against a copied sample RAW.

    ./tools/check-agent.py

⚠ **Why this exists.** The eighth gate validates that the MCP server correctly
shells to the `--agent` verbs, that proposed edits round-trip JSON, and that
a human approval commits to the sidecar. It runs against a GPU render and a
real image file, not a stub. Neither `mcp/server.test.ts` (which runs against
`mcp/test/fake-orion.sh` on every change) nor the binary's own unit tests can
catch if the glue between them is wrong.

⚠ **The steps themselves live in `check_agent_steps.py`**, next door. They were
here until this file passed a thousand lines, which CLAUDE.md forbids outright;
what is left here is the sample discovery, the server's lifetime and the list.
A step is a function `(session, ctx) -> str | None` and its name is what the
`ok` line says, so adding one is an entry in that module's `STEPS`.

Seven checks per the spec, Part C:
1. get_stats → width > 0, rating == 0
2. get_proxy maxPx 512 → JPEG (FF D8), long edge ≤ 512
3. propose_edit {exposureEv: 1.0} → proposed file exists with "exposureEv":1
4. get_proxy state proposed → succeeds
5. approve_edit → sidecar exists with exposureEv, proposed file gone
6. set_flag rating 4 → get_stats reports rating 4
7. reject_edit (no proposal) → deleted false, no error

...and the inspection half, added when the MCP surface picked up the retired
socket surface's mechanisms:

15. get_stats state proposed → a real GPU render of the PROPOSAL, whose mean
    differs from the current one after an exposure proposal
16. get_stats region → every RegionStats field present, luma in 0..1, and a
    `frame` ellipse (centre inside 0..1, radii > 0) such that a region nested
    inside it yields smaller radii on both axes and a centre inside its
    ellipse - a check that holds whatever the sample's orientation
17. get_proxy region → a JPEG whose aspect matches the region's and whose long
    edge is inside maxPx
18. detect_faces → the shape, with an empty array allowed (neither sample has
    a face)
19. invert, on a real render. ⚠ This one is here because an agent editing two
    photographs through these tools reported `invert` as broken - a radial
    with it set "selecting nothing". It is not broken, and nothing but a GPU
    render could have said so: a radial with `invert: false` and a -3 EV layer
    darkens the CENTRE and leaves the corner alone, and `invert: true` does
    exactly the opposite. Measured on overexposed-background.ARW: centre
    0.0889 -> 0.0080 / corner 0.1439 unchanged, against centre 0.0889
    unchanged / corner 0.1439 -> 0.0111. What actually misleads a reader is
    that the mask is in FRAME space while a region is in DISPLAY space, and a
    turned frame makes those different axes - so a gradient aimed at the frame's
    top lands on the display's left.
    The gate now uses tools/fixtures/mask-invert.dng, a generated 64x64
    non-dark frame: local sample aliases can point to night photographs whose
    black corners cannot meaningfully satisfy a relative darkening assertion.
    Both regions must start above 0.05 and below 0.95; the same four mask
    assertions still run through the real MCP server and GPU render.
20. a subject matte, on a real render. Unlike 19, this one WAS broken: the
    tools never uploaded a kind-4 row's PNG, so a subject layer covered
    nothing (#285). A synthetic centre matte under a -3 EV layer must darken
    CENTRE and leave the corner within one 8-bit code.
21. a matte whose file is gone → refused, naming the PNG, not rendered as a
    mask that selects nothing.

...and the read-only settings read, last so no earlier step's proposed file
is in play (it works on its own copy and rejects what it proposed):

22. get_edits → before any proposal, source sidecar or as-shot, hasProposed
    false and no `.proposed.json` written (an empty propose_edit was the only
    way to read settings before, and it wrote one); after propose_edit
    {exposureEv: 1.0}, state proposed → source "proposed", exposureEv == 1.

...and the two verbs that act on more than the settings:

23. detect_subject kind subject → when found, a kind-4 row whose matte PNG
    exists beside the copy, which validates in propose_edit under a -3 EV
    layer and renders in get_stats state proposed; then rejected. Found
    nothing is a pass with a note: the plumbing is asserted, not the model.
24. sync_edits → from an approved exposureEv 1.0 copy to another: a proposal
    holding exposureEv 1 by default, and under commit a sidecar get_edits
    reads back as source "sidecar", exposureEv 1.

...and denoise, run straight after get_edits:

25. denoise_region → get_stats region carries `noise` >= 0; a proposal of
    denoiseLuma 3, denoiseColor 3, denoiseDetail 0.5 reads `noise` under
    state proposed no higher than as shot on the same patch (<=, because
    the sample may already be clean; both numbers go in the note); and a
    layer carrying denoiseLuma 2 under a kind-2 radial validates and comes
    back in the proposal. research/denoise-masked.md sections 2.4 and 3.

...and what the camera recorded:

26. capture_fields → get_stats carries `capture` with `iso` a number >= 0,
    `shutter` > 0 seconds and `camera` non-empty, and no field present as
    0 or "". current_photo_set also writes a `capture` into current.json
    and requires current_photo to hand it back unchanged.
27. high_iso_recipe → describe_edits carries denoiseNlm, denoiseSmoothness and
    texture with their ranges; texture 1.5 is rejected; the research/high-iso.md
    §5 recipe leaves a flat patch's `noise` no higher than it found it
"""

import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from check_agent_steps import STEPS, Context, Session, spawn   # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
ORION = ROOT / "build" / "Orion.app" / "Contents" / "MacOS" / "Orion"
SAMPLES = ROOT / "samples"
# The first of these that exists: `samples/` is not the same folder on every
# machine.
SAMPLE_CANDIDATES = ["_PIC8095.ARW", "overexposed-background.ARW", "_PIC8220.ARW",
                     "_PIC8148.ARW", "underexposedsubject.ARW"]
SAMPLE_RAW = next((c for c in SAMPLE_CANDIDATES if (SAMPLES / c).is_file()), SAMPLE_CANDIDATES[0])


def run_steps(ctx: Context, problems: list) -> int:
    """Every step against one warm server, in order. A step that raises is a
    failure like any other - the gate never dies on one bad call."""
    proc = spawn(ROOT, {"ORION_BIN": str(ORION)})
    passed = 0
    try:
        session = Session(proc)
        session.handshake()
        for step in STEPS:
            ctx.note = None
            try:
                why = step(session, ctx)
            except Exception as e:
                why = str(e)
            if why is None:
                print(f"ok  {step.__name__}" + (f" ({ctx.note})" if ctx.note else ""))
                passed += 1
            else:
                problems.append(f"{step.__name__}: {why}")
    except Exception as e:
        problems.append(f"the session itself failed: {e}")
    finally:
        proc.terminate()
        try:
            proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            proc.kill()
    return passed


def main():
    if not ORION.is_file():
        print(f"check-agent: no binary at {ORION}\n"
              f"  Build first: cmake --build build", file=sys.stderr)
        return 2
    if not (SAMPLES / SAMPLE_RAW).is_file():
        print(f"check-agent: no sample at {SAMPLES / SAMPLE_RAW}", file=sys.stderr)
        return 2

    problems: list[str] = []
    with tempfile.TemporaryDirectory() as tmp:
        tmpdir = Path(tmp)
        # The photograph and its sidecar are COPIED: a gate that writes a
        # sidecar into samples/ would change what the next run starts from.
        shutil.copy2(SAMPLES / SAMPLE_RAW, tmpdir / SAMPLE_RAW)
        sidecar = SAMPLES / f"{SAMPLE_RAW[:-4]}.xmp"
        if sidecar.exists():
            shutil.copy2(sidecar, tmpdir / sidecar.name)

        ctx = Context(ROOT, ORION, SAMPLES, SAMPLE_RAW, tmpdir)
        passed = run_steps(ctx, problems)

    # ...and that copying is asserted rather than trusted.
    status = subprocess.run(["git", "status", "--porcelain", str(SAMPLES)],
                            cwd=str(ROOT), capture_output=True, text=True)
    if status.stdout.strip():
        problems.append(f"samples/ modified:\n{status.stdout}")

    if problems:
        print(f"check-agent: {len(problems)} problem(s)\n", file=sys.stderr)
        for problem in problems:
            print(f"  {problem}\n", file=sys.stderr)
        return 1

    print(f"check-agent: all {passed}/{len(STEPS)} checks passed.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
