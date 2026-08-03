#!/usr/bin/env python3
"""Check that planning/DECISIONS.md is a usable index of the tree.

Run it with the other gates:

    ./tools/check-decisions.py

Exits nonzero and prints every problem it found.

⚠ **Why this exists.** `STATUS.md`'s queue offered already-shipped work as the
next story three times. The first two were duplicate copies of the queue that
disagreed with each other (decision #135). The third was worse and quieter: the
queue's last open item was "Americanising the persisted keys, needs sign-off",
and the migration had shipped a day earlier — but the decision that closed it,
#112, had never been written into the ledger, so nothing anywhere said so except
a comment inside `EditHistory.swift`. A session that trusted the queue would have
re-done a schema migration on the photographer's sidecars.

So the ledger is not prose, it is an index, and an index with a hole in it is
worse than no index: it reads as complete from either end. Three things are
checked, and each of them is a failure that actually happened here:

1. **No number is used twice.** Two rows numbered 71 sat under a header warning
   about a duplicate 96 that did not exist.
2. **Every `#N` the tree cites has a row.** #110, #112 and #115 were cited
   eighteen times between them in code and prose, and none had ever been
   written.
3. **Every gap is declared.** 22, 23, 24 are numbers that were skipped, not
   decisions that were lost. Saying so in the file is what lets this check tell
   one from the other, instead of every reader re-deriving it.

⚠ **`.claude/worktrees/` is excluded, and that is load-bearing.** Those are
checkouts of this same repository, so a single citation in one file counts once
per worktree. The stale header's "twelve files cite decision #96" was one file
counted twelve times, and the wrong conclusion was drawn from it.
"""

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
LEDGER = ROOT / "planning" / "DECISIONS.md"

# Where a decision may be cited. `planning/HISTORY.md` is included: it is the
# archive of this same ledger's sessions, so a number it names must exist.
SEARCH_DIRS = ["engine", "app", "apps", "research", "repro", "planning", "tools"]
SEARCH_FILES = ["CLAUDE.md", "README.md"]
SUFFIXES = {".swift", ".cpp", ".h", ".hpp", ".mm", ".c", ".m", ".slang", ".md", ".txt", ".py"}

ROW = re.compile(r"^\|\s*(\d+)([a-z]?)\s*\|")
DECLARED_GAPS = re.compile(r"\*\*Declared gaps:\s*([0-9,\s]+)\.?\s*\*\*")
# "#137", "decision #96" — but not "#1234567" and not a markdown heading.
CITATION = re.compile(r"(?<![\w#])#(\d+)(?![\d.])")


def ledger_numbers(text):
    """Every row's number, in file order, as (number, suffix, line)."""
    out = []
    for i, line in enumerate(text.splitlines(), 1):
        m = ROW.match(line)
        if m:
            out.append((int(m.group(1)), m.group(2), i))
    return out


def declared_gaps(text):
    m = DECLARED_GAPS.search(text)
    if not m:
        return set()
    return {int(n) for n in re.findall(r"\d+", m.group(1))}


def sources():
    for d in SEARCH_DIRS:
        base = ROOT / d
        if not base.is_dir():
            continue
        for p in base.rglob("*"):
            # ⚠ Worktrees are copies of this repository. Counting them turns one
            # citation into a dozen and has already produced a false conclusion
            # in this file's own header.
            if ".claude" in p.parts or "build" in p.parts:
                continue
            if p.is_file() and p.suffix in SUFFIXES:
                yield p
    for f in SEARCH_FILES:
        p = ROOT / f
        if p.is_file():
            yield p


def main():
    if not LEDGER.is_file():
        print(f"check-decisions: no ledger at {LEDGER}", file=sys.stderr)
        return 2

    text = LEDGER.read_text(encoding="utf-8")
    rows = ledger_numbers(text)
    if not rows:
        print("check-decisions: the ledger has no rows — is the table format still `| N | ... |`?",
              file=sys.stderr)
        return 2

    problems = []

    # 1. No number used twice. A letter suffix (`71b`) makes a row distinct, and
    #    is the recorded way to keep a collision addressable rather than silent.
    seen = {}
    for n, suffix, line in rows:
        key = f"{n}{suffix}"
        if key in seen:
            problems.append(
                f"duplicate row #{key}: lines {seen[key]} and {line}. "
                f"Give one of them a letter suffix (#{n}b) and say why in the header — "
                f"a number is an identifier, so two rows sharing one make both unciteable.")
        seen[key] = line

    numbers = {n for n, _, _ in rows}
    plain = {n for n, suffix, _ in rows if suffix == ""}

    # 2. Every citation resolves.
    gaps = declared_gaps(text)
    cited = {}
    for path in sources():
        try:
            body = path.read_text(encoding="utf-8", errors="ignore")
        except OSError:
            continue
        rel = path.relative_to(ROOT)
        for line_no, line in enumerate(body.splitlines(), 1):
            # ⚠ A ledger row's *own* number is not a citation of itself — but the
            # rest of the row is prose like any other, and a row citing a
            # decision that does not exist is exactly as broken as a comment
            # doing it. So strip the leading `| N |` and keep scanning, rather
            # than skipping the line. Skipping it was the first version and it
            # left the ledger unable to check itself.
            if path == LEDGER:
                m = ROW.match(line)
                if m:
                    line = line[m.end():]
            for m in CITATION.finditer(line):
                n = int(m.group(1))
                if n > max(numbers) + 50:
                    continue      # a big number is a pixel count, not a decision
                cited.setdefault(n, []).append(f"{rel}:{line_no}")

    for n in sorted(cited):
        if n in numbers:
            continue
        if n in gaps:
            problems.append(
                f"#{n} is declared a gap but {len(cited[n])} place(s) cite it: "
                f"{', '.join(cited[n][:3])}. Either it is a real decision that needs a row, "
                f"or those citations mean something else.")
            continue
        where = ", ".join(cited[n][:4])
        more = "" if len(cited[n]) <= 4 else f" (+{len(cited[n]) - 4} more)"
        problems.append(
            f"#{n} has no row but is cited {len(cited[n])} time(s): {where}{more}. "
            f"Write the row, or declare the number a gap in the header.")

    # 3. Every gap is declared. An undeclared hole is indistinguishable from a
    #    decision somebody forgot to write, which is exactly what happened.
    for n in range(1, max(plain) + 1):
        if n in plain or n in gaps:
            continue
        problems.append(
            f"#{n} is missing from the ledger and is not declared a gap. "
            f"Add the row, or add {n} to the `**Declared gaps:**` list with the reason.")

    if problems:
        print(f"check-decisions: {len(problems)} problem(s) in planning/DECISIONS.md\n")
        for p in problems:
            print(f"  {p}\n")
        return 1

    span = f"1-{max(plain)}"
    print(f"check-decisions: {len(rows)} rows ({span}), "
          f"{len(gaps)} declared gap(s), {len(cited)} number(s) cited by the tree — all resolve.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
