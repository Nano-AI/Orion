#!/usr/bin/env python3
"""Did this change make Orion slower or hungrier? Decision #402.

    tools/bench.py [photos or folders...]          measure, and compare with this machine's baseline
    tools/bench.py --save-baseline                 measure, and make that the baseline
    tools/bench.py --ab ../other-tree              measure both trees, interleaved, and compare them
    tools/bench.py compare OLD.json NEW.json       compare two saved results
    tools/bench.py --list                          the scenarios

Photos default to ORION_SAMPLES (a folder, else `samples/`). The first photo is
the one dragged, panned, rendered and exported; the folder scenarios open a
folder of `--folder-size` (336) symlinks cycling through the photos, built in
the scratch folder, or a mirror of `--folder DIR` (symlinks again: the app never
opens DIR itself, so nothing can be written beside the photographer's files).

Each scenario runs `--repeats` times (5), one process a sample, the scenarios
interleaved round by round so a slow drift on the machine lands on all of them
alike; each warm scenario first runs once unrecorded. Every sample takes the GPU
lock (ORION_GPU_LOCK, when set) and runs niced; the tool refuses to start, and
waits between samples, while under 30% of memory is free or the load is 8 or
more. The statistics are in tools/bench_stats.py, and why they are those.

The baseline is per machine and card, `<baseline dir>/<host>-<gpu>.json`,
outside the repository (one machine's milliseconds mean nothing on another).
Exit 0: nothing slower. 1: something slower, outside the noise. 2: refused,
or a scenario never produced a number.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import os
import shutil
import subprocess
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import bench_run as run      # noqa: E402
import bench_stats as stats  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent

# name -> (what runs, what it opens, cold, what it answers)
SCENARIOS = {
    "open-cold": ("open", "folder", True, "a folder opened with its files out of the page cache: the window up, the first picture, the worst stall"),
    "open-warm": ("open", "folder", False, "the same folder again, its files cached"),
    "swap": ("swap", "photos", False, "an arrow to the neighbour (cached) and a jump past the cache"),
    "drag": ("drag", "first", False, "60 ticks of the exposure slider: the preview drag and the full one"),
    "pan": ("pan", "first", False, "a hand dragging the photo at 100%, region and whole-graph edits (`--bench --pan`, Unattended.Pan.cs)"),
    "render": ("render", "first", False, "the first full-resolution render, and the card's memory peak"),
    "export": ("export", "first", False, "the photo exported as the export sheet does, at its defaults"),
    "engine": ("engine", "first", False, "orion-bench: the engine alone on the shipping graph"),
}


def main() -> int:
    if len(sys.argv) > 1 and sys.argv[1] == "compare":
        cp = argparse.ArgumentParser(prog="bench.py compare", description="compare two saved results")
        cp.add_argument("old", type=Path)
        cp.add_argument("new", type=Path)
        cp.add_argument("--confidence", type=float, default=0.95)
        cp.add_argument("--min-effect", type=float, default=0.05)
        c = cp.parse_args(sys.argv[2:])
        rows = compare(json.loads(c.old.read_text()), json.loads(c.new.read_text()), "this", "this", c.confidence, c.min_effect)
        print(table(rows))
        return 1 if any(r["verdict"] == "SLOWER" for r in rows) else 0

    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0], formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("photos", nargs="*", type=Path, help="raw files or folders (default: ORION_SAMPLES)")
    ap.add_argument("--scenarios", default=",".join(SCENARIOS), help="comma-separated (default: all)")
    ap.add_argument("--repeats", type=int, default=5, help="samples per scenario (default 5; 4 is the least that reaches 95%%)")
    ap.add_argument("--folder", type=Path, help="mirror this folder's raws for the folder scenarios")
    ap.add_argument("--folder-size", type=int, default=336, help="links in the built folder (default 336)")
    ap.add_argument("--tree", type=Path, default=ROOT, help="the build to measure (default: this checkout)")
    ap.add_argument("--ab", type=Path, metavar="TREE", help="also measure this tree, interleaved, and compare against it")
    ap.add_argument("--scratch", type=Path, default=ROOT / "build" / "bench", help="links, exports, results")
    ap.add_argument("--json", type=Path, help="where the results go (default: the scratch folder)")
    ap.add_argument("--baseline-dir", type=Path, default=Path(os.environ.get("XDG_STATE_HOME", Path.home() / ".local/state")) / "orion" / "bench")
    ap.add_argument("--save-baseline", action="store_true", help="make these results this machine's baseline")
    ap.add_argument("--confidence", type=float, default=0.95)
    ap.add_argument("--min-effect", type=float, default=0.05, help="the smallest shift worth a verdict, as a fraction (default 0.05)")
    ap.add_argument("--list", action="store_true")
    args = ap.parse_args()

    if args.list:
        for name, (_, _, _, what) in SCENARIOS.items():
            print(f"{name:10} {what}")
        return 0
    chosen = [s.strip() for s in args.scenarios.split(",") if s.strip()]
    if unknown := [s for s in chosen if s not in SCENARIOS]:
        ap.error(f"no scenario {', '.join(unknown)}; --list names them")
    if "open-cold" in chosen and not run.can_evict():
        print("open-cold: this system has no posix_fadvise to evict with; skipped")
        chosen.remove("open-cold")

    photos = gather(args.photos)
    if not photos:
        ap.error("no raw files: pass some, or set ORION_SAMPLES")
    args.scratch.mkdir(parents=True, exist_ok=True)
    folder = mirror(args.folder, photos, args.folder_size, args.scratch)
    builds = {"this": build(args.tree)}
    if args.ab:
        builds = {"base": build(args.ab), "this": builds["this"]}

    run.be_nice()
    try:
        before = run.check_machine(wait=0)
    except run.Refused as e:
        print(f"bench: refused: {e}")
        return 2
    m = run.machine()
    results = {
        "schema": 1, "when": time.strftime("%Y-%m-%dT%H:%M:%S%z"), "machine": m,
        "builds": {k: v["describe"] for k, v in builds.items()},
        "repeats": args.repeats, "at_start": before | run.vram(), "scenarios": {},
    }
    contexts = {name: context(name, photos, folder) for name in chosen}
    lists = {"folder": run.raws(folder), "photos": swap_list(photos), "first": photos[:1]}
    print(f"bench: {', '.join(chosen)} x {args.repeats} on {m['gpu']} ({m['host']}); "
          f"{len(photos)} photo(s), folder of {len(lists['folder'])}; builds: {', '.join(builds)}")
    try:
        measure(chosen, builds, lists, args, results, contexts)
    except run.Refused as e:
        print(f"bench: refused mid-run: {e}")
        return 2

    out = args.json or args.scratch / f"bench-{time.strftime('%Y%m%d-%H%M%S')}.json"
    rows = compare(results, results, "base", "this", args.confidence, args.min_effect) if args.ab else []
    baseline_file = args.baseline_dir / f"{m['key']}.json"
    if not args.ab and baseline_file.exists() and not args.save_baseline:
        rows = compare(json.loads(baseline_file.read_text()), results, "this", "this", args.confidence, args.min_effect)
        results["compared_with"] = str(baseline_file)
    elif not args.ab and not rows:
        rows = compare(results, results, None, "this")   # no baseline: the readings alone
    results["comparison"] = rows
    out.write_text(json.dumps(results, indent=1))
    print(table(rows))
    print(f"\nresults: {out}")
    never = [s for s in chosen for b in builds if not any(r.get("metrics") for r in results["scenarios"][s]["runs"][b])]
    if never:
        print(f"bench: no numbers from {', '.join(sorted(set(never)))}; nothing saved as a baseline")
        return 2
    if args.save_baseline:
        args.baseline_dir.mkdir(parents=True, exist_ok=True)
        baseline_file.write_text(json.dumps(results, indent=1))
        print(f"baseline: {baseline_file}")
    elif "compared_with" in results:
        print(f"baseline: {baseline_file}")
    if any(r["verdict"] == "slower?" for r in rows):
        print("bench: 'slower?' is a shift the interval cannot confirm: measure again with more --repeats, or --ab")
    return 1 if any(r["verdict"] == "SLOWER" for r in rows) else 0


def gather(given: list[Path]) -> list[Path]:
    if not given:
        given = [Path(os.environ.get("ORION_SAMPLES", ROOT / "samples"))]
    photos: list[Path] = []
    for p in given:
        photos += run.raws(p) if p.is_dir() else [p] if p.is_file() else []
    return [p.resolve() for p in photos]


def mirror(source: Path | None, photos: list[Path], size: int, scratch: Path) -> Path:
    """The folder the folder scenarios open: symlinks only, rebuilt when what they point at changes."""
    targets = run.raws(source) if source else [photos[i % len(photos)] for i in range(size)]
    tag = hashlib.sha1("\n".join(map(str, targets)).encode()).hexdigest()[:8]
    folder = scratch / f"folder-{len(targets)}-{tag}"
    if not folder.exists():
        tmp = folder.with_suffix(".tmp")
        shutil.rmtree(tmp, ignore_errors=True)   # an earlier run stopped half way
        tmp.mkdir(parents=True)
        for i, t in enumerate(targets):
            (tmp / f"IMG_{i + 1:04d}{t.suffix}").symlink_to(t.resolve())
        tmp.rename(folder)
    return folder


def swap_list(photos: list[Path]) -> list[Path]:
    """At least 24 entries, so the swap's half-list jump lands past both cache tiers (#318: about
    ten rendered either side); a repeated file is a separate photo to the app."""
    return [photos[i % len(photos)] for i in range(max(24, len(photos)))]


def build(tree: Path) -> dict:
    tree = tree.resolve()
    app, engine = tree / "shell/bin/Release/net10.0/Orion", tree / "build/apps/bench/orion-bench"
    head = subprocess.run(["git", "-C", str(tree), "describe", "--always", "--dirty"], capture_output=True, text=True).stdout.strip()
    return {"app": app, "engine": engine, "describe": {"tree": str(tree), "commit": head}}


def context(name: str, photos: list[Path], folder: Path) -> dict:
    """What a scenario's numbers depend on besides the build: a baseline taken on other photos is not comparable."""
    opens = SCENARIOS[name][1]
    if opens == "folder":
        links = run.raws(folder)
        said = {"folder": len(links), "targets": sorted({(p.resolve().name, p.resolve().stat().st_size) for p in links})}
    else:
        said = {"photos": [(p.name, p.stat().st_size) for p in (photos[:1] if opens == "first" else photos)]}
    return json.loads(json.dumps(said))   # as a saved baseline reads back: lists, not tuples


def measure(chosen, builds, lists, args, results, contexts) -> None:
    for name in chosen:
        results["scenarios"][name] = {"context": contexts[name], "runs": {b: [] for b in builds}}
    # Warm scenarios first run once, unrecorded: the page cache, the disk's own cache, the shader cache.
    for name in chosen:
        if not SCENARIOS[name][2]:
            for b in builds.values():
                sample(name, b, lists, args)
    for r in range(args.repeats):
        order = list(builds) if r % 2 == 0 else list(reversed(builds))   # ABBA: neither build always goes first
        for name in chosen:
            for b in order:
                got = sample(name, builds[b], lists, args)
                results["scenarios"][name]["runs"][b].append(got)
                bad = f"  ({got['error']})" if got.get("error") else ""
                print(f"  {r + 1}/{args.repeats} {name:9} {b:4} {brief(got['metrics'])}{bad}", flush=True)


def sample(name: str, b: dict, lists: dict, args) -> dict:
    what, opens, cold, _ = SCENARIOS[name]
    photos = lists[opens]
    machine = run.check_machine()
    with run.gpu_lock(os.environ.get("ORION_GPU_LOCK")):
        card = run.vram()
        residual = run.evict(photos) if cold else None
        if what == "engine":
            got = run.run_engine(b["engine"], photos[0], args.scratch / "engine")
        else:
            got = run.run_app(b["app"], what, photos, args.scratch / "export")
    got["machine"] = machine | {"vram_free_mib": card.get("free_mib")}
    if residual is not None:
        got["machine"]["resident_after_evict"] = round(residual, 4)
        if residual > 0.01:
            got["error"] = (got.get("error", "") + f" cold not verified: {residual:.1%} still cached").strip()
    return got


# The open's own readings, which every app scenario carries: the progress line leaves them out after the open.
PREAMBLE = {"load_ms", "window_ms", "responsive_ms", "first_picture_ms", "gpu_engine_mib", "rss_mib"}


def brief(metrics: dict) -> str:
    opened = "settled_ms" in metrics
    keep = [k for k in metrics if stats.unit(k) and (opened or k not in PREAMBLE)][:5]
    return "  ".join(f"{k}={metrics[k]:.4g}" for k in keep)


def values(results: dict, scenario: str, build: str) -> dict[str, list[float]]:
    out: dict[str, list[float]] = {}
    for r in results["scenarios"][scenario]["runs"].get(build, []):
        if r.get("ok") is False:
            continue   # the scenario threw or the process died: what it printed first is not a sample
        for k, v in r.get("metrics", {}).items():
            if isinstance(v, (int, float)) and not math.isnan(v):
                out.setdefault(k, []).append(float(v))
    return out


def compare(old: dict, new: dict, old_build: str | None, new_build: str,
            confidence: float = 0.95, min_effect: float = 0.05) -> list[dict]:
    rows = []
    for name, scenario in new["scenarios"].items():
        mine = values(new, name, new_build)
        theirs = {}
        note = ""
        if old_build and name in old["scenarios"]:
            if old is new or old["scenarios"][name]["context"] == scenario["context"]:
                theirs = values(old, name, old_build)
            else:
                note = "baseline photos differ"
        for metric in sorted(mine):
            row = {"scenario": name, "metric": metric, **stats.summarise(mine[metric])}
            if metric in theirs:
                row |= stats.verdict(metric, theirs[metric], mine[metric], confidence, min_effect)
                row["baseline_mad"] = stats.mad(theirs[metric])
            else:
                row["verdict"] = note or ("info" if not stats.unit(metric) else "no baseline")
            rows.append(row)
    return rows


def table(rows: list[dict]) -> str:
    def f(x: float) -> str:
        return "-" if x is None or (isinstance(x, float) and math.isnan(x)) else f"{x:.4g}"

    lines = [f"{'scenario':10} {'metric':28} {'median':>9} {'+-MAD':>7} {'n':>2} | {'baseline':>9} {'shift [CI]':>26}  verdict"]
    for r in rows:
        ci = ""
        if "shift" in r:
            lo, hi = r["lo"], r["hi"]
            ci = f"{r['shift']:+.4g} [{'-inf' if math.isinf(lo) else f'{lo:+.4g}'}, {'inf' if math.isinf(hi) else f'{hi:+.4g}'}]"
        lines.append(f"{r['scenario']:10} {r['metric']:28} {f(r['median']):>9} {f(r['mad']):>7} {r['n']:>2} | "
                     f"{f(r.get('baseline')):>9} {ci:>26}  {r['verdict']}")
    return "\n".join(lines)


if __name__ == "__main__":
    raise SystemExit(main())
