#!/usr/bin/env python3
"""The Linux package's glibc floor (decision #404).

    tools/check-glibc-floor.py DIR [--floor 2.35]

Reads every ELF file under DIR (an unpacked AppImage, or its AppDir) and fails if
any of them asks for a glibc symbol version newer than the floor, or if any file
we build asks for libstdc++ at all (a GLIBCXX_ or CXXABI_ version, or a NEEDED
libstdc++.so). The floor is the oldest glibc the AppImage starts on: Ubuntu
22.04 ships 2.35 and Debian 12 2.36, and a package built on Ubuntu 24.04 asked
for GLIBC_2.38 and GLIBCXX_3.4.32, so neither could run it (#396).

Microsoft's prebuilt .NET runtime and app host link libstdc++.so.6 and cannot be
relinked, so those files, and only those, by name, may need it, up to what
Ubuntu 22.04's own libstdc++ (GCC 12) provides: GLIBCXX_3.4.30 and CXXABI_1.3.13.
Any other file that needs libstdc++ fails, whoever built it.

A file of ours that links the runtime in must keep it private (--exclude-libs):
.NET's runtime has the system's libstdc++ in the same process, and two copies
that both export their entry points interpose on each other. So it also fails a
non-Microsoft file that exports one of the runtime's own entry points.

It reads the version needs from each file's .gnu.version_r section, which is what
the dynamic loader checks, with no binutils: a version the file does not list is
one the loader will never ask for.
"""
import argparse
import struct
import sys
from pathlib import Path

SHT_DYNAMIC, SHT_DYNSYM, SHT_GNU_VERNEED = 6, 11, 0x6FFFFFFE
DT_NEEDED = 1

# Microsoft.NETCore.App.Runtime.linux-x64 and .Host.linux-x64 (the app host is
# renamed to the app's name by dotnet publish). Measured at 10.0.11: they ask for
# at most GLIBCXX_3.4.22, CXXABI_1.3.7 and GLIBC_2.27.
MICROSOFT = {"Orion", "createdump", "libclrgc.so", "libclrgcexp.so", "libclrjit.so", "libcoreclr.so",
             "libcoreclrtraceptprovider.so", "libhostfxr.so", "libhostpolicy.so", "libmscordaccore.so",
             "libmscordbi.so"}
CEILING = {"GLIBCXX": "3.4.30", "CXXABI": "1.3.13"}   # Ubuntu 22.04's libstdc++6 (GCC 12)
# Defined only by libstdc++ and libgcc themselves: exported, the static copy leaked.
RUNTIME = {"__cxa_throw", "__cxa_allocate_exception", "__gxx_personality_v0", "_ZSt9terminatev",
           "_Unwind_RaiseException", "_Unwind_Resume"}


def version(v: str):
    return tuple(int(x) for x in v.split("."))


def needs(path: Path):
    """(NEEDED libraries, {library: [version names]}, exported runtime entry points)
    of a 64-bit little-endian ELF, or None for a file that is not an ELF."""
    data = path.read_bytes()
    if data[:4] != b"\x7fELF":
        return None
    if data[4] != 2 or data[5] != 1:
        raise ValueError("not a 64-bit little-endian ELF; the package is x86_64 only")
    shoff, = struct.unpack_from("<Q", data, 0x28)
    shentsize, shnum = struct.unpack_from("<HH", data, 0x3A)
    sections = [struct.unpack_from("<IIQQQQIIQQ", data, shoff + i * shentsize) for i in range(shnum)]

    def string(table, offset):
        start = sections[table][4] + offset
        return data[start:data.index(b"\0", start)].decode()

    needed, versions, exported = [], {}, set()
    for _name, kind, _flags, _addr, offset, size, link, _info, _align, _entsize in sections:
        if kind == SHT_DYNAMIC:
            for i in range(0, size, 16):
                tag, value = struct.unpack_from("<qQ", data, offset + i)
                if tag == DT_NEEDED:
                    needed.append(string(link, value))
        elif kind == SHT_DYNSYM:
            for i in range(0, size, 24):
                name, info, _other, shndx = struct.unpack_from("<IBBH", data, offset + i)
                if shndx and info >> 4 in (1, 2) and name:   # defined, global or weak
                    symbol = string(link, name)
                    if symbol in RUNTIME:
                        exported.add(symbol)
        elif kind == SHT_GNU_VERNEED:
            at = offset
            while True:
                _ver, count, file, aux, nxt = struct.unpack_from("<HHIII", data, at)
                names, a = versions.setdefault(string(link, file), []), at + aux
                for _ in range(count):
                    _hash, _flags2, _other, name, anext = struct.unpack_from("<IHHII", data, a)
                    names.append(string(link, name))
                    a += anext
                if not nxt:
                    break
                at += nxt
    return needed, versions, exported


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("dir", type=Path)
    parser.add_argument("--floor", default="2.35")
    args = parser.parse_args()
    floor = version(args.floor)
    errors, highest, scanned = [], {}, 0

    def note(family, number, where):
        if family not in highest or version(number) > version(highest[family][0]):
            highest[family] = (number, where)

    for f in sorted(p for p in args.dir.rglob("*") if p.is_file() and not p.is_symlink()):
        rel = f.relative_to(args.dir)
        try:
            found = needs(f)
        except (ValueError, struct.error, IndexError, UnicodeDecodeError) as e:
            errors.append(f"{rel}: unreadable ELF ({e})")
            continue
        if found is None:
            continue
        scanned += 1
        exempt = f.name in MICROSOFT
        needed, versions, exported = found
        if exported and not exempt:
            errors.append(f"{rel} exports the C++ runtime's {', '.join(sorted(exported))};"
                          " link it with -Wl,--exclude-libs,libstdc++.a:libgcc.a:libgcc_eh.a")
        for lib in needed:
            if lib.startswith("libstdc++") and not exempt:
                errors.append(f"{rel} needs {lib}; link it with -static-libstdc++")
        for lib, names in versions.items():
            for name in names:
                family, _, number = name.partition("_")
                if not number[:1].isdigit():
                    continue
                if family in CEILING:
                    note(family, number, str(rel))
                    if not exempt:
                        errors.append(f"{rel} needs {name} from {lib}; link it with -static-libstdc++")
                    elif version(number) > version(CEILING[family]):
                        errors.append(f"{rel} needs {name} from {lib}, above Ubuntu 22.04's {family}_{CEILING[family]}")
                elif family == "GLIBC":
                    note(family, number, str(rel))
                    if version(number) > floor:
                        errors.append(f"{rel} needs {name} from {lib}, above the floor GLIBC_{args.floor}")
    if scanned == 0:
        errors.append(f"no ELF file under {args.dir}")
    for e in errors:
        print(f"check-glibc-floor: {e}", file=sys.stderr)
    for family, (number, where) in sorted(highest.items()):
        print(f"check-glibc-floor: highest {family}_{number} ({where})")
    if errors:
        print(f"check-glibc-floor: {len(errors)} problem(s) in {scanned} ELF files; the floor is GLIBC_{args.floor},"
              f" and only Microsoft's runtime may need libstdc++", file=sys.stderr)
        return 1
    print(f"check-glibc-floor: {scanned} ELF files run on glibc {args.floor}; only Microsoft's runtime needs libstdc++")
    return 0


if __name__ == "__main__":
    sys.exit(main())
