#!/usr/bin/env python3
"""The Windows and Linux packages' licence and personal-data gates (decision #376).

    tools/package-gates.py SYSTEM APP LICENCES BUILD SHELL

SYSTEM is windows or linux; APP the unpacked package; LICENCES the folder inside
it that holds the licence texts, with NOTICE one level up; BUILD the CMake build
(vcpkg_installed/ in it); SHELL the Avalonia project. package-app.sh's gates for
macOS, ported:

1. Collect: every vcpkg port's copyright, the NuGet packages' licences and
   notices at the versions packages.lock.json resolves, the .NET runtime's
   third-party notices, the fonts' OFL. A text that is not where it should be
   stops the build.
2. Every file in APP maps to the licence texts it needs (RULES). A file no rule
   knows is an error, not a pass: a dependency walk adds what it adds.
3. No GPL. Orion is source-available, never redistributed (#359, as #243 before it), and cannot carry GPL code, linked
   dynamically or not; LGPL only in a shared library.
4. No home directory of anyone but the CI build accounts, and, off CI, not the
   builder's home or git e-mail.
"""
import fnmatch
import json
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

# (basename patterns, licence texts in LICENCES, a word NOTICE must contain)
RULES = [
    (["Orion", "Orion.exe", "Orion.dll", "Orion.deps.json", "Orion.runtimeconfig.json",
      "orion_shim.dll", "liborion_shim.so", "AppRun", "orion.desktop", "orion.svg", ".DirIcon"], [], "Orion"),
    (["System.*", "Microsoft.*", "mscorlib.dll", "netstandard.dll", "WindowsBase.dll", "createdump", "createdump.exe",
      "coreclr.dll", "libcoreclr.so", "clrjit.dll", "libclrjit.so", "clrgc*.dll", "libclrgc*.so", "clretwrc.dll",
      "hostfxr.dll", "libhostfxr.so", "hostpolicy.dll", "libhostpolicy.so", "mscordaccore*.dll", "libmscordaccore.so",
      "mscordbi.dll", "libmscordbi.so", "mscorrc.dll", "msquic.dll", "libSystem.*.so", "libcoreclrtraceptprovider.so"],
     ["dotnet-runtime-THIRD-PARTY-NOTICES.txt"], ".NET runtime"),
    (["Avalonia.Fonts.Inter.dll"], ["OFL-1.1.txt"], "Inter"),
    (["Avalonia*.dll", "MicroCom.Runtime.dll", "Tmds.DBus.Protocol.dll"], [], "Avalonia"),
    (["av_libglesv2.dll"], ["ANGLE-LICENSE.txt"], "ANGLE"),
    (["SkiaSharp.HarfBuzz.dll"], ["SkiaSharp.HarfBuzz-MIT.txt"], "SkiaSharp.HarfBuzz"),
    (["SkiaSharp.dll", "libSkiaSharp.dll", "libSkiaSharp.so"], ["SkiaSharp-MIT.txt", "libSkiaSharp-THIRD-PARTY-NOTICES.txt"], "SkiaSharp"),
    (["HarfBuzzSharp.dll", "libHarfBuzzSharp.dll", "libHarfBuzzSharp.so"],
     ["HarfBuzzSharp-MIT.txt", "libHarfBuzzSharp-THIRD-PARTY-NOTICES.txt"], "HarfBuzzSharp"),
    (["NeoSmart.Unicode.dll"], ["Unicode.net-MIT.txt"], "Unicode.net"),
    (["Iciclecreek.Avalonia.Terminal.dll"], [], "Iciclecreek"),
    (["Velopack.dll"], [], "Velopack"),
    (["XTerm.NET.dll"], [], "XTerm.NET"),
    (["Porta.Pty.dll", "libporta_pty.so"], [], "Porta.Pty"),
    (["Wcwidth.dll"], [], "Wcwidth"),
    (["conpty.dll", "OpenConsole.exe"], [], "ConPTY"),
    (["d3dcompiler_47.dll"], [], "d3dcompiler_47.dll"),
    # vcpkg's, by port: the copyright file it installs, verbatim.
    (["raw_r.dll", "libraw_r.so*", "libraw.so*"], ["libraw.txt"], "LibRaw"),
    (["opencv_*.dll", "libopencv_*.so*"], ["opencv4.txt"], "OpenCV"),
    (["jpeg62.dll", "turbojpeg.dll", "libjpeg.so*", "libturbojpeg.so*"], ["libjpeg-turbo.txt"], "libjpeg-turbo"),
    (["libpng16*.dll", "libpng16*.so*"], ["libpng.txt"], "libpng"),
    (["tiff.dll", "libtiff.so*"], ["tiff.txt"], "LibTIFF"),
    (["liblzma.dll", "liblzma.so*"], ["liblzma.txt"], "XZ"),
    (["z.dll", "zlib1.dll", "libz.so*"], ["zlib.txt"], "zlib"),
    (["lcms2*.dll", "liblcms2.so*"], ["lcms.txt"], "Little CMS"),
    (["jxl*.dll", "libjxl*.so*"], ["libjxl.txt"], "libjxl"),
    (["hwy*.dll", "libhwy*.so*"], ["highway.txt"], "Highway"),
    (["brotli*.dll", "libbrotli*.so*"], ["brotli.txt"], "Brotli"),
]
NOT_SHIPPED = ["*.pdb"]                     # debug databases carry build paths
DIRS_OF_ORION = ("shaders", "data")         # the engine's own; lensfun is in NOTICE
BUILD_ACCOUNTS = {"runner", "runneradmin"}  # GitHub Actions' Linux and Windows accounts
# ⚠ One name excused, in one file, as package-app.sh excuses its two: MicroCom.Runtime.dll,
# prebuilt on NuGet by its author, carries his build machine's /home/kekekeks/ (its
# PDB path). It names the library's author, as published on nuget.org, not a user of
# Orion, and nothing here can rebuild it. Any other name, in it or elsewhere, still fails.
EXCUSED = {("MicroCom.Runtime.dll", "kekekeks")}

GPL = re.compile(r"GNU (AFFERO )?GENERAL PUBLIC LICENSE|^\s*License:\s*A?GPL|SPDX-License-Identifier:\s*A?GPL", re.M)
LGPL = re.compile(r"GNU (LESSER|LIBRARY) GENERAL PUBLIC LICENSE")
HOME = re.compile(rb"(?:/Users/|/home/|[A-Za-z]:\\+Users\\+)([^/\\\s\x00\"'<>:*?|]+)")


def fail(errors, what):
    errors.append(what)
    print(f"package-gates: {what}", file=sys.stderr)


def locked(shell: Path, name: str) -> str:
    deps = json.loads((shell / "packages.lock.json").read_text())["dependencies"]
    for target in deps.values():
        for key, value in target.items():
            if key.lower() == name.lower():
                return value["resolved"]
    raise SystemExit(f"package-gates: {name} is not in packages.lock.json")


def collect(system: str, licences: Path, build: Path, shell: Path, errors):
    licences.mkdir(parents=True, exist_ok=True)

    def take(out: str, src: Path):
        if src.is_file():
            shutil.copyfile(src, licences / out)
        else:
            fail(errors, f"the licence for {out} is not at {src}")

    for c in sorted(build.glob("vcpkg_installed/*/share/*/copyright")):
        port = c.parent.name
        if not port.startswith("vcpkg-"):   # vcpkg's own build helpers ship nothing
            take(f"{port}.txt", c)
    nuget = Path(os.environ.get("NUGET_PACKAGES", Path.home() / ".nuget" / "packages"))
    native = "Win32" if system == "windows" else "Linux"   # the NuGet native-asset package per system

    def nupkg(name: str) -> Path:
        return nuget / name.lower() / locked(shell, name)

    take("SkiaSharp-MIT.txt", nupkg("SkiaSharp") / "LICENSE.txt")
    take("libSkiaSharp-THIRD-PARTY-NOTICES.txt", nupkg(f"SkiaSharp.NativeAssets.{native}") / "THIRD-PARTY-NOTICES.txt")
    take("HarfBuzzSharp-MIT.txt", nupkg("HarfBuzzSharp") / "LICENSE.txt")
    take("libHarfBuzzSharp-THIRD-PARTY-NOTICES.txt", nupkg(f"HarfBuzzSharp.NativeAssets.{native}") / "THIRD-PARTY-NOTICES.txt")
    take("Unicode.net-MIT.txt", nupkg("Unicode.net") / "LICENSE")
    take("SkiaSharp.HarfBuzz-MIT.txt", nupkg("SkiaSharp.HarfBuzz") / "LICENSE.txt")
    if system == "windows":
        take("ANGLE-LICENSE.txt", nupkg("Avalonia.Angle.Windows.Natives") / "LICENSE")
    dotnet = Path(os.environ.get("DOTNET_ROOT") or Path(shutil.which("dotnet") or "").resolve().parent)
    take("dotnet-runtime-THIRD-PARTY-NOTICES.txt", dotnet / "ThirdPartyNotices.txt")
    take("OFL-1.1.txt", shell / "Assets" / "Fonts" / "OFL.txt")


def rule_for(name: str):
    return next((r for r in RULES if any(fnmatch.fnmatchcase(name, p) for p in r[0])), None)


def check_files(app: Path, licences: Path, notice: str, errors):
    needed_by = {}
    for f in sorted(p for p in app.rglob("*") if p.is_file()):
        rel = f.relative_to(app)
        if licences in f.parents or f.name in ("LICENSE", "NOTICE") or "doc" in rel.parts:
            continue
        if any(fnmatch.fnmatchcase(f.name, p) for p in NOT_SHIPPED):
            fail(errors, f"{rel} must not ship")
            continue
        if any(part in DIRS_OF_ORION for part in rel.parts[:-1]):
            continue
        rule = rule_for(f.name)
        if rule is None:
            fail(errors, f"{rel} has no licence rule; name it in NOTICE and in tools/package-gates.py")
            continue
        _, texts, word = rule
        if word not in notice:
            fail(errors, f"{rel} needs NOTICE to name {word!r}")
        for t in texts:
            if not (licences / t).is_file():
                fail(errors, f"{rel} needs {t}, which is not in {licences.name}/")
            needed_by.setdefault(t, []).append(f.name)
    return needed_by


def check_gpl(licences: Path, needed_by, errors):
    for t in sorted(licences.iterdir()):
        text = t.read_text(errors="replace")
        m = GPL.search(text)
        if m:
            line = text.count("\n", 0, m.start()) + 1
            fail(errors, f"{t.name}:{line} is GPL ({m.group(0).strip()!r}); an Orion package cannot carry it (#359)")
        if LGPL.search(text):
            static = [n for n in needed_by.get(t.name, []) if not re.search(r"\.(dll|so)(\.|$)", n)]
            if static:
                fail(errors, f"{t.name} is LGPL and {static} are not shared libraries")


def check_people(app: Path, errors):
    literal = []
    if not os.environ.get("CI"):   # on CI the home is the build account's, matched below
        literal.append(str(Path.home()).encode())
        email = subprocess.run(["git", "config", "user.email"], capture_output=True, text=True).stdout.strip()
        if email:
            literal.append(email.encode())
    for f in sorted(p for p in app.rglob("*") if p.is_file()):
        data = f.read_bytes()
        names = {m.group(1).decode(errors="replace") for m in HOME.finditer(data)} - BUILD_ACCOUNTS
        names -= {name for file, name in EXCUSED if file == f.name}
        found = sorted(names) + [l.decode() for l in literal if l in data]
        if found:
            fail(errors, f"{f.relative_to(app)} names a person: {', '.join(found)}")


def main() -> int:
    system, app, licences, build, shell = sys.argv[1:6]
    app, licences, build, shell = Path(app), Path(licences), Path(build), Path(shell)
    errors = []
    collect(system, licences, build, shell, errors)
    notice = (licences.parent / "NOTICE").read_text()
    needed_by = check_files(app, licences, notice, errors)
    check_gpl(licences, needed_by, errors)
    check_people(app, errors)
    if errors:
        print(f"package-gates: {len(errors)} problem(s); the package is not made", file=sys.stderr)
        return 1
    print(f"package-gates: every file licensed, no GPL, nobody named ({system})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
