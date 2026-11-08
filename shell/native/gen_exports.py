#!/usr/bin/env python3
"""Writes the Windows shim's export list.

    gen_exports.py <orion.h> <shim.cpp> <out.def>

A DLL exports only what it is told to. Every C function the facade declares,
in orion.h and in each orion_*.h it includes, and every shim_* shim.cpp
defines, read from the sources so a new one cannot be forgotten.
tools/check-wiring.py holds every name the app P/Invokes to this list on every
system, so a declaration moved out of what this reads fails there, not only
as a crash on Windows.
"""
import re
import sys
from pathlib import Path

FACADE = re.compile(r"^[A-Za-z].*[ *](orion_[a-z0-9_]+)\(", re.M)
SHIM = re.compile(r"^[A-Za-z].*[ *](shim_[a-z0-9_]+)\(", re.M)
INTERNAL = {"shim_settle"}   # C++, internal to the shim


def headers(header: Path):
    text = header.read_text(encoding="utf-8")
    yield header
    for name in re.findall(r'^#include "(orion_[a-z0-9_]+\.h)"', text, re.M):
        yield from headers(header.parent / name)


def exports(header: Path, shim: Path):
    names = [n for h in headers(header) for n in FACADE.findall(h.read_text(encoding="utf-8"))]
    names += SHIM.findall(shim.read_text(encoding="utf-8"))
    return [n for n in dict.fromkeys(names) if n not in INTERNAL]


def main():
    header, shim, out = map(Path, sys.argv[1:4])
    out.write_text("EXPORTS\n    " + "\n    ".join(exports(header, shim)) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
