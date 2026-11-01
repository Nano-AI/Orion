"""How tools/bench.py takes one sample: the machine guard, the GPU lock, the
page-cache eviction, the card's memory sampler, and the two programs it runs
(the app's `--bench` scenarios and orion-bench). Decision #402.

Nothing here judges a number; tools/bench_stats.py does.
"""

from __future__ import annotations

import fcntl
import json
import os
import re
import shutil
import subprocess
import sys
import threading
import time
from contextlib import contextmanager
from pathlib import Path

RAW_EXTENSIONS = {"arw", "srf", "sr2", "dng", "nef", "nrw", "cr2", "cr3", "crw", "raf", "orf", "rw2",
                  "pef", "srw", "erf", "3fr", "fff", "iiq", "mrw", "raw", "rwl"}   # Program.RawExtensions

MIN_FREE_MEMORY = 0.30   # CLAUDE.md: a heavy run needs 30% of memory free
MAX_LOAD = 8.0           # and a load under about 8


class Refused(Exception):
    """The machine is not fit to measure on; nothing was run."""


def raws(folder: Path) -> list[Path]:
    return sorted(p for p in folder.iterdir() if p.is_file() and p.suffix.lstrip(".").lower() in RAW_EXTENSIONS)


# ── The machine ──────────────────────────────────────────────────────────────

def memory_free() -> float:
    """MemAvailable over MemTotal (Linux); vm_stat's free + inactive pages elsewhere."""
    try:
        info = dict(line.split(":", 1) for line in Path("/proc/meminfo").read_text().splitlines())
        return int(info["MemAvailable"].split()[0]) / int(info["MemTotal"].split()[0])
    except FileNotFoundError:
        out = subprocess.run(["vm_stat"], capture_output=True, text=True).stdout
        pages = {k.strip(): int(v.strip().rstrip(".")) for k, v in (l.split(":", 1) for l in out.splitlines()[1:] if ":" in l)}
        free = pages.get("Pages free", 0) + pages.get("Pages inactive", 0) + pages.get("Pages speculative", 0)
        total = int(subprocess.run(["sysctl", "-n", "hw.memsize"], capture_output=True, text=True).stdout) // os.sysconf("SC_PAGE_SIZE")
        return free / total


def check_machine(wait: float = 120) -> dict:
    """The guard, before every sample: waits up to `wait` seconds for the machine to calm, else refuses."""
    deadline = time.monotonic() + wait
    while True:
        free, load = memory_free(), os.getloadavg()[0]
        if free >= MIN_FREE_MEMORY and load < MAX_LOAD:
            return {"memory_free": round(free, 3), "load": round(load, 2)}
        if time.monotonic() > deadline:
            raise Refused(f"{free:.0%} of memory free (needs {MIN_FREE_MEMORY:.0%}) and load {load:.1f} (needs under {MAX_LOAD:g})")
        time.sleep(5)


def be_nice() -> None:
    """Niceness 10 at least, whatever the caller left it at (os.nice would add to it)."""
    if os.getpriority(os.PRIO_PROCESS, 0) < 10:
        os.setpriority(os.PRIO_PROCESS, 0, 10)


@contextmanager
def gpu_lock(path: str | None):
    """The machine's one-GPU-run-at-a-time lock (ORION_GPU_LOCK), held for one sample, so other
    runs interleave between samples rather than inside one."""
    if not path:
        yield
        return
    with open(path, "a+") as f:
        fcntl.flock(f, fcntl.LOCK_EX)
        try:
            yield
        finally:
            fcntl.flock(f, fcntl.LOCK_UN)


def machine() -> dict:
    gpu = "unknown GPU"
    if shutil.which("nvidia-smi"):
        gpu = subprocess.run(["nvidia-smi", "--query-gpu=name", "--format=csv,noheader"],
                             capture_output=True, text=True).stdout.strip().splitlines()[0]
    elif sys.platform == "darwin":   # Apple silicon: the chip is the GPU
        gpu = subprocess.run(["sysctl", "-n", "machdep.cpu.brand_string"], capture_output=True, text=True).stdout.strip() or gpu
    cpu = next((l.split(":", 1)[1].strip() for l in _read("/proc/cpuinfo").splitlines() if l.startswith("model name")), "")
    host = os.uname().nodename
    key = re.sub(r"[^a-z0-9]+", "-", f"{host} {gpu}".lower()).strip("-")
    return {"key": key, "host": host, "gpu": gpu, "cpu": cpu, "os": f"{os.uname().sysname} {os.uname().release}"}


def _read(path: str) -> str:
    try:
        return Path(path).read_text()
    except OSError:
        return ""


def vram() -> dict:
    """The card's free memory now and who else holds more than 256 MiB of it: a shared card's
    other tenants are the first suspect when a memory number moves."""
    if not shutil.which("nvidia-smi"):
        return {}
    first = subprocess.run(["nvidia-smi", "--query-gpu=memory.used,memory.total", "--format=csv,noheader,nounits"],
                           capture_output=True, text=True).stdout.splitlines()[0]   # one line a card: the first, as machine() names
    used, total = (int(x) for x in first.split(","))
    apps = subprocess.run(["nvidia-smi", "--query-compute-apps=pid,process_name,used_memory", "--format=csv,noheader,nounits"],
                          capture_output=True, text=True).stdout.splitlines()
    others = [f"{Path(a.split(',')[1].strip()).name}={a.split(',')[2].strip()}" for a in apps
              if a.count(",") == 2 and int(a.split(",")[2]) > 256]
    return {"free_mib": total - used, "others": others}


class GpuPeak:
    """The most card memory one process held, sampled every 50 ms through nvidia-smi.
    A sampler, so a spike shorter than 50 ms can be missed; a 42 MP render holds its
    textures for far longer than that. Dawn's Vulkan process is listed among the
    compute apps (type C+G with the RTX 3070's driver here), which is what this reads."""

    def __init__(self, pid: int):
        self.pid, self.peak = str(pid), None
        self._proc = None
        if shutil.which("nvidia-smi"):
            self._proc = subprocess.Popen(
                ["nvidia-smi", "--query-compute-apps=pid,used_memory", "--format=csv,noheader,nounits", "-lms", "50"],
                stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True)
            threading.Thread(target=self._read, daemon=True).start()

    def _read(self) -> None:
        for line in self._proc.stdout:
            pid, _, mib = line.partition(",")
            if pid.strip() == self.pid and mib.strip().isdigit():
                self.peak = max(self.peak or 0, int(mib))

    def stop(self) -> float | None:
        if self._proc:
            time.sleep(0.1)
            self._proc.terminate()
            self._proc.wait()
        return self.peak


# ── The page cache ───────────────────────────────────────────────────────────

def evict(files: list[Path]) -> float:
    """Drops the files from the page cache (posix_fadvise DONTNEED, no root needed) and returns
    the fraction still resident by fincore, which is the check that it worked: a clean page
    the kernel was asked to drop is dropped, so anything left is a mapping someone holds."""
    targets = sorted({f.resolve() for f in files})
    for t in targets:
        fd = os.open(t, os.O_RDONLY)
        try:
            os.posix_fadvise(fd, 0, 0, os.POSIX_FADV_DONTNEED)
        finally:
            os.close(fd)
    return resident(targets)


def resident(files: list[Path]) -> float:
    if not shutil.which("fincore") or not files:
        return float("nan")
    out = subprocess.run(["fincore", "--bytes", "--noheadings", "--raw", "-o", "RES,SIZE", *map(str, files)],
                         capture_output=True, text=True).stdout.split()
    res, size = sum(map(int, out[0::2])), sum(map(int, out[1::2]))
    return res / size if size else 0.0


def can_evict() -> bool:
    return hasattr(os, "posix_fadvise")


# ── The programs ─────────────────────────────────────────────────────────────

def run_app(app: Path, scenario: str, photos: list[Path], out: Path, timeout: float = 300) -> dict:
    """One process of `Orion <photos> --bench` running one scenario (shell/Shared/Unattended.Bench.cs).
    The pan is the pan's own bench, `--bench --pan` (Unattended.Pan.cs), read from its `bench pan` lines:
    one driver for the pan, the one its owner keeps."""
    pan = scenario == "pan"
    env = os.environ | {"ORION_BENCH_OUT": str(out), "ORION_BENCH_T0_NS": str(time.time_ns())}
    if not pan:
        env["ORION_BENCH_SCENARIO"] = scenario
    proc = subprocess.Popen([str(app), *map(str, photos), "--bench", *(["--pan"] if pan else [])], env=env,
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    peak = GpuPeak(proc.pid)
    try:
        stdout, stderr = proc.communicate(timeout=timeout)
    except subprocess.TimeoutExpired:
        proc.kill()
        stdout, stderr = proc.communicate()
        peak.stop()
        return {"ok": False, "metrics": {}, "error": f"timed out after {timeout:g} s"}
    gpu = peak.stop()
    if pan:
        metrics = pan_lines(stdout)
        result = {"scenario": "pan", "ok": bool(metrics) and proc.returncode == 0, "metrics": metrics}
        if not result["ok"]:
            result["error"] = _tail(stderr) or f"exit {proc.returncode}, no bench pan lines"
        if gpu is not None:
            result["metrics"]["gpu_peak_mib"] = gpu
        return result
    line = next((l for l in stdout.splitlines() if l.startswith("bench-json ")), None)
    if line is None:
        return {"ok": False, "metrics": {}, "error": _tail(stderr) or f"exit {proc.returncode}, no bench-json line"}
    result = json.loads(line[len("bench-json "):])
    if gpu is not None:
        result["metrics"]["gpu_peak_mib"] = gpu
    said = [l for l in stderr.splitlines() if l.startswith("bench ")]
    if said:
        result["error"] = " | ".join(said)[:600]
    return result


# `bench pan <scenario> <phase> key=value ...` (Unattended.Pan.cs): its keys -> ours. A key named by its
# unit is judged (lower is better); renders, copies, frames and the frame interval are readings only, the
# interval because it includes the headless raster, which is not a product number.
PAN_KEYS = {"ui_median_ms": "ui_ms", "ui_p95_ms": "ui_p95_ms", "ui_max_ms": "ui_max_ms", "move_ms": "move_ms",
            "draw_ms": "draw_ms", "engine_ms": "engine_ms", "copy_ms": "copy_ms", "stalls": "stalls_count",
            "renders": "renders", "copies": "copies", "interval_median_ms": "interval_median"}


def pan_lines(stdout: str) -> dict[str, float]:
    m: dict[str, float] = {}
    for line in stdout.splitlines():
        parts = line.split()
        if parts[:2] != ["bench", "pan"] or len(parts) < 4:
            continue
        for kv in parts[4:]:
            key, _, value = kv.partition("=")
            if key in PAN_KEYS:
                m[f"pan_{parts[2]}_{parts[3]}_{PAN_KEYS[key]}"] = float(value)
    return m


# orion-bench's report, line by line: regex -> the metric names its groups fill.
ENGINE_LINES = [
    (r"^\s+decode\s+([\d.]+) ms", ["engine_decode_ms"]),
    (r"^Pipeline\s+\d+ nodes, (\d+) MiB resident .*pool high-water (\d+) MiB", ["engine_resident_mib", "engine_pool_peak_mib"]),
    (r"^\s+full render\s+([\d.]+) ms", ["engine_full_render_ms"]),
    (r"^\s+best round:\s+min [\d.]+\s+median ([\d.]+)\s+p95 ([\d.]+)", ["engine_exposure_ms", "engine_exposure_p95_ms"]),
    (r"^\s+curve drag\s+([\d.]+) ms median", ["engine_curve_ms"]),
    (r"^\s+-(full|web|print)\.(jpg|tif)\s+([\d.]+) ms", None),   # the export block: one metric per format
]


def run_engine(bench: Path, photo: Path, out: Path, timeout: float = 900) -> dict:
    """One orion-bench run (apps/bench): the engine alone, the shipping graph on a real photo."""
    out.mkdir(parents=True, exist_ok=True)
    proc = subprocess.Popen([str(bench), str(photo), str(out / "orion")], stdout=subprocess.PIPE,
                            stderr=subprocess.PIPE, text=True)
    peak = GpuPeak(proc.pid)
    try:
        stdout, stderr = proc.communicate(timeout=timeout)
    except subprocess.TimeoutExpired:
        proc.kill()
        proc.communicate()
        peak.stop()
        return {"ok": False, "metrics": {}, "error": f"timed out after {timeout:g} s"}
    m: dict[str, float] = {}
    for line in stdout.splitlines():
        for pattern, names in ENGINE_LINES:
            if not (hit := re.match(pattern, line)):
                continue
            if names is None:
                m[f"engine_export_{hit[1]}_{hit[2]}_ms"] = float(hit[3])
            else:
                m.update(zip(names, map(float, hit.groups())))
    m["engine_gate_fail_count"] = 0.0 if re.search(r"M0 gate .*: PASS", stdout) else 1.0
    errors = [l for l in stderr.splitlines() if l.startswith("error:")]
    m["engine_error_count"] = float(len(errors))
    if (gpu := peak.stop()) is not None:
        m["gpu_peak_mib"] = gpu
    shutil.rmtree(out, ignore_errors=True)
    # A section that threw (out of GPU memory, here) ends the report; the numbers before
    # it stand and the error is counted. A process killed by a signal stands for nothing.
    if proc.returncode < 0:
        return {"ok": False, "metrics": {}, "error": f"killed by signal {-proc.returncode}: {_tail(stderr)}"}
    return {"ok": True, "metrics": m, **({"error": errors[0][:600]} if errors else {})}


def _tail(text: str, lines: int = 3) -> str:
    keep = [l for l in text.splitlines() if l.strip() and not l.startswith("Warning:")]
    return " | ".join(keep[-lines:])[:600]
