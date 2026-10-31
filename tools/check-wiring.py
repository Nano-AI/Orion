#!/usr/bin/env python3
"""Check that mechanisms the product built are called by the product.

    ./tools/check-wiring.py

⚠ **Why this exists.** In the SwiftUI app, `Engine.showPlaceholder` was
written, drawn by the canvas under a comment reading *"held while a new photo
decodes"*, and promised by a second comment — and **its only caller in the tree
was the screenshot harness**. So for every one of the 210.9 ms a raw file takes
to decode (#151) the canvas went on showing the *previous* photograph, while
two comments described the opposite (#181). Every suite was green on the
mechanism being dead.

⚠ **This is a grep, and it says so** — the same standing as
`tools/check-gestures.py` (#110.3). It cannot check that the call is on the
right branch, in the right order, or with the right argument. It checks that a
mechanism with a *product* caller still has one.

⚠ **A harness caller does not count, and that is the whole idea.** The failure
was not a missing function — it was a function that looked used because a test
used it. So the harness files are excluded from the count on purpose.

**The sweep is the second half (#183).** It walks every method declared in a
product file, counts uses from product files and from harness files, and
reports any whose uses are **all** harness. Its first run over Swift found
nine: eight defensible and one real (`SyncSettings.pasted`, deleted).

⚠ **The allowlist is the point, not a suppression.** A method that is
legitimately harness-only has to say *why* in `HARNESS_ONLY`, so the next one is
a line somebody has to justify rather than a silent pass.

**It reads the Avalonia app's C# now** (`shell/`, decision #380): the Swift app
it was written for is retired. C# has no `func`, so a declaration is a line of
modifiers (`public static …`, `internal …`) then a name and a parenthesis —
unambiguous enough, because C# requires the modifiers or a return type there.
A *use* is any mention of the name outside its declaration, not only a call: an
Avalonia control hands methods around as delegates (`Button("Save", Save)`),
and counting only `Name(` would call every one of those dead. The harness is
`SelfTest*.cs`, `SharedProof.cs` (the `--proof` run), the screenshot posing
scenes (`CanvasScenes.cs`, `MaskScenes.cs`) and `Shared/Unattended.cs` (the
headless --selftest/--bench/--command/--screenshot runner, moved out of
Program.cs for this): the screenshot harness is #181's own shape.

⚠ **What this still does NOT do.** It is a regex, not a compiler: a call made
through reflection or a name built at run time is invisible to it, and two
methods sharing a name count as one (a product use of either hides the other).
Every allowlisted entry was checked by hand.

⚠⚠ **It stays away from C++ on purpose (#184).** The same sweep run by hand over
`engine/src` flagged 52 of 401 functions, mostly noise: C++ has no declaration
marker, and the C facade is called from the app, so a C++-only sweep calls it
dead. That one-off run found `radiusToFrame`, `lengthToFrame` and
`lengthAlong` without a product caller since #138; they are annotated, not
deleted.
"""

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
APP = ROOT / "shell"

# Files that are the harness rather than the product. A use from one of these
# is exactly the evidence that misled everybody the first time.
HARNESS = re.compile(r"^(SelfTest|SharedProof|CanvasScenes|MaskScenes|Unattended)")

# A regex over the product's source -> why the product must go on calling it.
EXPECTED = {
    # Run once at a window, never headless. Deleted, every photographer who
    # updates from the Swift app silently loses their presets and assistant
    # settings, and no test opens a window to notice.
    r"\bSettingsImport\.Run\s*\(":
        "the Swift app's presets and assistant settings reach this app only "
        "through it (decision #380)",
    # The selftest builds an Autosave of its own, so deleting the product's
    # install is green there.
    r"\bAutosave\.Install\s*\(":
        "edits reach the sidecar only once autosave is armed at the window",
    r"\bCurrentPhoto\.Install\s*\(":
        "current.json is how the MCP server's current_photo learns which photo "
        "is open; nothing headless writes it",
}

# Product methods the harness alone may use, and why each is allowed to be.
# ⚠ Adding a name here is a claim, checked by hand: a deliberate test hook or
# an offscreen path, not a mechanism the product forgot to call.
HARNESS_ONLY = {
    # The five the first C# run found (2026-10-29, #380), each read by hand.
    "Advance":
        "AnalogTrack steps its springs a frame at a time so the motion checks do "
        "not wait on the clock; its docstring says it is for tests",
    "Listed":
        "the synchronous spelling of the library's background mark read — the "
        "same Sidecar.Read and Apply the listing goes through, so a check can "
        "assert marks without waiting on a Task",
    "Has":
        "Icons' coverage question, asked by the selftest of every symbol the "
        "interface names; the product draws through Icons' paths directly",
    "orion_engine_export_size":
        "a facade import the watermark parity check measures with; the product's "
        "export writes the file and never needs its size first",
    # Found when the screenshot harness and the bench joined HARNESS (2026-10-29).
    "Print":
        "CacheReplay is --bench's trace replay of the cache policy; its report is "
        "for the bench and nothing else (its header says so)",
    "shim_mask_field_count":
        "the shim's count of OrionMaskComponent fields, the oracle the selftest "
        "holds Session.MaskFields to — not a product path",
    # #391: the stack's memory check.
    "EngineAvailableOverride":
        "a test seam: the selftest makes the engine's own memory check refuse "
        "whatever the machine has free, to prove the sheet shows the warning again",
}

MODIFIERS = ("public|private|internal|protected|static|override|virtual|sealed|"
             "async|unsafe|extern|partial|abstract|readonly")
DECLARATION = re.compile(
    rf"^[ \t]*(?:\[[^\]\n]*\][ \t]*)*(?:(?:{MODIFIERS})[ \t]+)+"
    r"(?:[\w<>\[\],.?()]+(?:[ \t]*[,<>][ \t]*[\w<>\[\],.?()]+)*[ \t]+)?"
    r"(\w+)[ \t]*(?:<[^<>()\n]*>)?[ \t]*\(", re.M)
KEYWORDS = {"if", "while", "for", "foreach", "switch", "return", "new", "using",
            "catch", "lock", "operator", "get", "set", "nameof", "typeof", "sizeof"}


def uncommented(text):
    """The source with comments removed and string literals blanked: a name in
    a comment or a message is not a use. Verbatim and raw strings are left; a
    method name inside one would be a false use, which errs green-side only."""
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    text = re.sub(r'"(?:[^"\\\n]|\\.)*"', '""', text)
    return "\n".join(re.sub(r"//.*$", "", line) for line in text.splitlines())


def main():
    if not APP.is_dir():
        print(f"check-wiring: no app directory at {APP}", file=sys.stderr)
        return 2

    sources = {}
    for path in sorted(APP.rglob("*.cs")):
        rel = path.relative_to(APP)
        if rel.parts[0] in ("bin", "obj"):
            continue
        sources[str(rel)] = uncommented(path.read_text(encoding="utf-8", errors="ignore"))

    def harness(name):
        return bool(HARNESS.match(Path(name).name))

    problems = []
    for pattern, why in sorted(EXPECTED.items()):
        if not any(re.search(pattern, body) for name, body in sources.items() if not harness(name)):
            problems.append(f"{pattern} matches no product file — {why}.\n"
                            f"      A call from the harness does not count: that is what made this look wired the first time.")

    # The sweep: every method declared in a product file, and who mentions it.
    declared, declarations = {}, {}
    for name, body in sources.items():
        for m in DECLARATION.finditer(body):
            symbol = m.group(1)
            if symbol in KEYWORDS:
                continue
            declarations[(name, symbol)] = declarations.get((name, symbol), 0) + 1
            if not harness(name):
                declared.setdefault(symbol, set()).add(name)

    found = []
    for symbol, where in sorted(declared.items()):
        product = harness_uses = 0
        mention = re.compile(rf"\b{re.escape(symbol)}\b")
        for name, body in sources.items():
            uses = len(mention.findall(body)) - declarations.get((name, symbol), 0)
            if uses <= 0:
                continue
            if harness(name):
                harness_uses += uses
            else:
                product += uses
        if harness_uses > 0 and product == 0:
            found.append((symbol, sorted(where), harness_uses))

    for symbol, where, uses in found:
        if symbol in HARNESS_ONLY:
            continue
        problems.append(
            f"{symbol}() is declared in {', '.join(where)} and used only by the harness ({uses} use(s)).\n"
            f"      Either the product forgot to call it — which is how the canvas spent 210.9 ms showing the wrong "
            f"photograph (#181) — or it is a deliberate test hook: add it to HARNESS_ONLY with the reason.\n"
            f"      ⚠ Check by hand first: this is a regex, and reflection or a name built at run time is invisible to it.")

    if problems:
        print(f"check-wiring: {len(problems)} problem(s)\n")
        for p in problems:
            print(f"  {p}\n")
        return 1

    print(f"check-wiring: {len(EXPECTED)} declared mechanism(s) still called by the product; swept "
          f"{len(declared)} product method(s), {len(found)} harness-only and all accounted for.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
