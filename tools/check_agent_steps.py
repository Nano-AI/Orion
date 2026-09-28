"""The steps `tools/check-agent.py` runs, and the instruments they need.

Split out of the gate when it went past a thousand lines, which CLAUDE.md
forbids outright. `check-agent.py` keeps the docstring saying *why* each step
exists, the sample discovery and `main`; this file is the JSON-RPC plumbing
and one function per step.

**A step is a function `(session, ctx) -> str | None`.** None is a pass; a
string is why it failed, printed under the step's own name; a step wanting a
word in its `ok` line sets `ctx.note`. A call that simply did not work raises
instead, and the gate reports that the same way - which is why there is no
`if resp.get("error")` ladder in any step below, only its assertions.
"""

import base64
import json
import os
import re
import select
import struct
import shutil
import subprocess
import time
from pathlib import Path

TIMEOUT = 60

def decode_jpeg_dimensions(data: bytes) -> tuple[int, int] | None:
    """Width and height out of a JPEG's SOF0/SOF2 marker, as (height, width);
    None when the bytes are not a JPEG at all, which the proxy steps assert."""
    if len(data) < 2 or data[0] != 0xFF or data[1] != 0xD8:
        return None
    i = 2
    while i < len(data) - 9:
        if data[i] != 0xFF:
            i += 1
            continue
        if data[i + 1] in (0xC0, 0xC2):   # SOF0, SOF2
            height = struct.unpack(">H", data[i + 5:i + 7])[0]
            width = struct.unpack(">H", data[i + 7:i + 9])[0]
            return (height, width)
        i += 2 + struct.unpack(">H", data[i + 2:i + 4])[0]
    return None

def call(proc, method: str, params: dict, req_id: int, timeout: float = TIMEOUT) -> dict:
    """One JSON-RPC request, and the reply carrying that id. Reads past
    anything else on stdout rather than assuming the next line is the answer,
    and selects with a deadline so a mute server fails rather than hangs."""
    msg = json.dumps({"jsonrpc": "2.0", "id": req_id, "method": method, "params": params})
    try:
        proc.stdin.write(msg + "\n")
        proc.stdin.flush()
    except BrokenPipeError:
        raise RuntimeError("Server exited unexpectedly")

    deadline = time.time() + timeout
    while True:
        try:
            ready, _, _ = select.select([proc.stdout], [], [],
                                        max(0.1, deadline - time.time()))
            if not ready:
                raise RuntimeError(f"Timeout waiting for response to {method}")
            line = proc.stdout.readline()
            if not line:
                raise RuntimeError("Server closed stdout")
            resp = json.loads(line)
            if resp.get("id") == req_id:
                return resp
        except json.JSONDecodeError:
            continue

def spawn(root: Path, env_extra: dict):
    """`node mcp/server.ts` on stdio, with ORION_BIN and friends."""
    return subprocess.Popen(
        ["node", "--experimental-strip-types", str(root / "mcp" / "server.ts")],
        stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
        text=True, env={**os.environ, **env_extra})

class Session:
    """One MCP server on stdio, and the four ways a step talks to it.

    `ask` and `picture` RAISE when the call fails, because a step that cannot
    make its call has nothing to assert; `refusal` is their mirror, for the
    steps whose whole point is that the server says no.
    """

    def __init__(self, proc):
        self.proc = proc
        self.req_id = 0

    def rpc(self, method: str, params: dict) -> dict:
        self.req_id += 1
        return call(self.proc, method, params, self.req_id, TIMEOUT)

    def handshake(self) -> None:
        """initialize, the initialized notification, tools/list."""
        resp = self.rpc("initialize", {
            "protocolVersion": "2025-03-26",
            "capabilities": {},
            "clientInfo": {"name": "check-agent", "version": "0.1"},
        })
        if resp.get("error"):
            raise RuntimeError(f"initialize failed: {resp['error']}")
        self.proc.stdin.write(json.dumps(
            {"jsonrpc": "2.0", "method": "notifications/initialized", "params": {}}) + "\n")
        self.proc.stdin.flush()
        resp = self.rpc("tools/list", {})
        if resp.get("error"):
            raise RuntimeError(f"tools/list failed: {resp['error']}")

    def tool(self, name: str, **arguments):
        """(result, why). `why` covers a transport error AND isError, a
        distinction no step here has ever wanted to make."""
        resp = self.rpc("tools/call", {"name": name, "arguments": arguments})
        if resp.get("error"):
            return None, resp["error"].get("message", str(resp["error"]))
        result = resp.get("result", {})
        if result.get("isError"):
            return None, result.get("content", [{}])[0].get("text", "")
        return result, None

    def _block(self, name: str, kind: str, arguments: dict):
        result, why = self.tool(name, **arguments)
        if why is not None:
            raise RuntimeError(f"{name}: {why}")
        block = result.get("content", [{}])[0]
        if block.get("type") != kind:
            raise RuntimeError(f"{name}: expected {kind} content, got {block.get('type')}")
        return block

    def ask(self, name: str, **arguments) -> dict:
        """The tool's JSON payload. Raises if the call failed."""
        return json.loads(self._block(name, "text", arguments).get("text", "{}"))

    def picture(self, name: str, **arguments) -> bytes:
        """The tool's image bytes. Raises if the call failed."""
        return base64.b64decode(self._block(name, "image", arguments).get("data", ""))

    def refusal(self, name: str, **arguments) -> str:
        """The error text of a call that was SUPPOSED to be refused. Raises
        when it succeeded instead, which is the failure those steps guard."""
        result, why = self.tool(name, **arguments)
        if why is None:
            raise RuntimeError(f"{name}: expected a rejection, got {result}")
        return why

class Context:
    """The photograph, the copies of it, and the scratch directory."""

    def __init__(self, root: Path, orion: Path, samples: Path, sample: str, tmpdir: Path):
        self.root, self.orion, self.samples = root, orion, samples
        self.sample, self.tmpdir = sample, tmpdir
        self.raw = tmpdir / sample
        self.note: str | None = None
        self._inspect: Path | None = None

    def copy(self, suffix: str, of: Path | None = None) -> Path:
        """A fresh copy, so one step's proposal or sidecar cannot reach the
        next. `of` defaults to the pristine sample."""
        path = self.tmpdir / f"{self.sample[:-4]}_{suffix}.ARW"
        shutil.copy2(of or (self.samples / self.sample), path)
        return path

    @staticmethod
    def proposed(raw: Path) -> Path:
        return raw.with_name(f"{raw.stem}.proposed.json")

# -- The steps, in the order the gate runs them ----------------------------

def get_stats(s, ctx):
    stats = s.ask("get_stats", path=str(ctx.raw))
    if not (stats.get("width", 0) > 0 and stats.get("rating") == 0):
        return f"width={stats.get('width')}, rating={stats.get('rating')}"

def get_proxy(s, ctx):
    data = s.picture("get_proxy", path=str(ctx.raw), maxPx=512)
    if data[:2] != b"\xff\xd8":
        return f"not a valid JPEG (first bytes: {data[:2].hex() or 'empty'})"
    dims = decode_jpeg_dimensions(data)
    if not dims:
        return "failed to parse JPEG dimensions"
    if max(dims) > 512:
        return f"long edge {max(dims)} > 512"

def propose_edit(s, ctx):
    s.ask("propose_edit", path=str(ctx.raw), edits={"exposureEv": 1.0})
    proposed = ctx.proposed(ctx.raw)
    if not proposed.exists():
        return f"proposed file not created at {proposed}"
    got = json.loads(proposed.read_text()).get("exposureEv")
    if got != 1:
        return f"proposed file has exposureEv={got}, expected 1"

def get_proxy_proposed(s, ctx):
    s.picture("get_proxy", path=str(ctx.raw), maxPx=512, state="proposed")

def approve_edit(s, ctx):
    s.ask("approve_edit", path=str(ctx.raw))
    if ctx.proposed(ctx.raw).exists():
        return "proposed file still exists after approve"
    sidecar = ctx.raw.with_name(f"{ctx.raw.stem}.xmp")
    if not sidecar.exists():
        return f"sidecar not created at {sidecar}"
    match = re.search(r'orion:Develop="([^"]+)"', sidecar.read_text())
    if not match:
        return "sidecar missing orion:Develop attribute"
    try:
        state = json.loads(base64.b64decode(match.group(1)).decode("utf-8"))
    except (json.JSONDecodeError, ValueError) as e:
        return f"failed to decode base64 state: {e}"
    if state.get("exposureEv") != 1:
        return f"decoded state has exposureEv={state.get('exposureEv')}, expected 1"

def set_flag(s, ctx):
    s.ask("set_flag", path=str(ctx.raw), rating=4)
    rating = s.ask("get_stats", path=str(ctx.raw)).get("rating")
    if rating != 4:
        return f"rating is {rating}, expected 4"

def reject_edit(s, ctx):
    """On a photograph with no proposal at all: `deleted false`, not an error."""
    deleted = s.ask("reject_edit", path=str(ctx.copy("2", of=ctx.raw))).get("deleted")
    if deleted is not False:
        return f"deleted={deleted}, expected false"

def describe_edits(s, ctx):
    entries = s.ask("describe_edits").get("keys", [])
    temp = next((k for k in entries if k.get("name") == "temperatureK"), None)
    lo = temp.get("min") if temp else None
    absolute = temp.get("absolute") if temp else None
    if not (len(entries) >= 30 and (lo is None or lo >= 2000) and absolute is True):
        return (f"num_keys={len(entries)} (need >=30), temperatureK.min={lo} "
                f"(need >=2000), absolute={absolute} (need True)")

def propose_edit_rejected(s, ctx):
    """An absolute 150 K is four hundred kelvin under the coldest the slider
    reaches, and nothing may be written for it."""
    raw = ctx.copy("3", of=ctx.raw)
    why = s.refusal("propose_edit", path=str(raw), edits={"temperatureK": 150})
    if not (why.startswith("REJECTED:") and "temperatureK" in why and "150" in why):
        return f"error text '{why}' does not match expected pattern"
    if ctx.proposed(raw).exists():
        return f"proposed file was created despite rejection at {ctx.proposed(raw)}"

def propose_edit_reset(s, ctx):
    raw = ctx.copy("4", of=ctx.raw)
    s.ask("propose_edit", path=str(raw), edits={"exposureEv": 0.5})
    state = s.ask("propose_edit", path=str(raw),
                  edits={"saturation": 0.5}, reset=True).get("state", {})
    exposure, saturation = state.get("exposureEv"), state.get("saturation")
    if not ((exposure == 0 or exposure is None) and saturation == 0.5):
        return (f"after reset, exposureEv={exposure} (expected 0), "
                f"saturation={saturation} (expected 0.5)")
    s.ask("reject_edit", path=str(raw))

def propose_composite(s, ctx):
    """A curve sent whole, rendered, then withdrawn."""
    raw = ctx.copy("5")
    entry = next((k for k in s.ask("describe_edits").get("keys", [])
                  if k.get("name") == "curve"), None)
    if not (entry and entry.get("example")):
        return "curve not found in describe_edits or missing example"
    curve = dict(entry["example"])
    curve["master"] = [{"x": 0, "y": 0}, {"x": 0.5, "y": 0.6}, {"x": 1, "y": 1}]

    result = s.ask("propose_edit", path=str(raw), edits={"curve": curve})
    points = result.get("state", {}).get("curve", {}).get("master", [])
    if len(points) != 3:
        return f"master has {len(points)} points, expected 3"
    s.picture("get_proxy", path=str(raw), maxPx=512, state="proposed")
    deleted = s.ask("reject_edit", path=str(raw)).get("deleted")
    if deleted is not True:
        return f"deleted={deleted}, expected True"

def propose_composite_rejected(s, ctx):
    """A layer missing eight of its nine fields is refused, not merged onto
    defaults - which is what would silently zero the other eight."""
    why = s.refusal("propose_edit", path=str(ctx.copy("6")),
                    edits={"layers": [{"exposureEv": 1.0}]})
    if not (why.startswith("REJECTED:") and "missing" in why):
        return f"error text does not match pattern: '{why}'"

# -- The inspection half ---------------------------------------------------

REGION_FIELDS = ("luma", "saturation", "hue", "hueStrength", "red", "green", "blue",
                 "clippedHigh", "clippedLow", "shading")
CENTRE = [0.4, 0.4, 0.2, 0.2]
CORNER = [0.02, 0.02, 0.12, 0.12]

def _inspect_raw(ctx) -> Path:
    """One copy shared by the five inspection steps, so they cost five renders
    rather than five opens and five renders."""
    if ctx._inspect is None:
        ctx._inspect = ctx.copy("7")
    return ctx._inspect

def get_stats_proposed(s, ctx):
    """`--state` has to reach the render, not only the JSON."""
    raw = _inspect_raw(ctx)
    before = s.ask("get_stats", path=str(raw))
    s.ask("propose_edit", path=str(raw), edits={"exposureEv": 2.0})
    after = s.ask("get_stats", path=str(raw), state="proposed")
    if abs(after["mean"][1] - before["mean"][1]) <= 0.01:
        return (f"a +2 EV proposal left the green mean at {after['mean'][1]:.4f} "
                f"against {before['mean'][1]:.4f} - --state never reached the render")

def get_stats_region(s, ctx):
    region = s.ask("get_stats", path=str(_inspect_raw(ctx)),
                   region=[0.3, 0.3, 0.4, 0.4]).get("region", {})
    missing = [f for f in REGION_FIELDS if not isinstance(region.get(f), (int, float))]
    if missing:
        return f"region is missing {missing}"
    if not 0.0 <= region["luma"] <= 1.0:
        return f"luma {region['luma']} is outside 0..1"

def get_proxy_region(s, ctx):
    """A crop at native resolution: its aspect is the region's in PIXELS, not
    in display fractions, and those differ on any frame that is not square."""
    raw = _inspect_raw(ctx)
    data = s.picture("get_proxy", path=str(raw), maxPx=512, region=[0.25, 0.25, 0.2, 0.1])
    dims = decode_jpeg_dimensions(data)
    if not dims:
        return "could not parse JPEG dimensions"
    height, width = dims
    if max(width, height) > 512:
        return f"long edge {max(width, height)} > 512"
    stats = s.ask("get_stats", path=str(raw))
    want = (0.2 * stats["width"]) / (0.1 * stats["height"])
    got = width / height
    if abs(got - want) > 0.05 * want:
        return f"crop aspect {got:.3f} is not the region's {want:.3f}"

def detect_faces(s, ctx):
    """Neither sample holds a face, so an empty array passes; what is checked
    is that every face that IS returned carries both spaces."""
    faces = s.ask("detect_faces", path=str(_inspect_raw(ctx))).get("faces")
    if not isinstance(faces, list):
        return f"faces is {type(faces).__name__}, not a list"
    fields = ("x", "y", "w", "h", "centerX", "centerY", "radiusX", "radiusY")
    bad = [f for f in faces if not all(isinstance(f.get(k), (int, float)) for k in fields)]
    if bad:
        return f"{len(bad)} face(s) missing fields"
    ctx.note = f"{len(faces)} face(s)"

def mask_invert(s, ctx):
    """An agent editing two photographs through these tools reported `invert`
    as broken - a radial with it set "selecting nothing". It is not, and
    nothing but a GPU render could have said so."""
    raw = _inspect_raw(ctx)
    examples = {k["name"]: k.get("example") for k in s.ask("describe_edits").get("keys", [])}

    def luma(region, state=None):
        args = {"path": str(raw), "region": region}
        if state:
            args["state"] = state
        return s.ask("get_stats", **args)["region"]["luma"]

    def lumas(invert):
        component = dict(examples["maskComponents"][0])
        component.update(kind=2, centerX=0.5, centerY=0.5, radiusX=0.25, radiusY=0.25,
                         feather=0.3, roundness=2, angle=0.0, compose=0,
                         startsLayer=True, invert=invert)
        layer = dict(examples["layers"][0])
        layer["exposureEv"] = -3.0
        s.ask("propose_edit", path=str(raw), reset=True,
              edits={"maskComponents": [component], "layers": [layer]})
        return [luma(r, "proposed") for r in (CENTRE, CORNER)]

    base = [luma(r) for r in (CENTRE, CORNER)]
    plain, inverted = lumas(False), lumas(True)
    s.ask("reject_edit", path=str(raw))

    # A -3 EV layer through the mask has to more than halve the luma it
    # covers, and leave what it does not cover within an 8-bit code of as shot.
    eps = 1.0 / 255.0
    if (plain[0] < base[0] * 0.5 and abs(plain[1] - base[1]) < eps
            and inverted[1] < base[1] * 0.5 and abs(inverted[0] - base[0]) < eps):
        return None
    return (f"as shot centre/corner {base[0]:.4f}/{base[1]:.4f}; invert false "
            f"{plain[0]:.4f}/{plain[1]:.4f} (want the centre dark, the corner "
            f"unchanged); invert true {inverted[0]:.4f}/{inverted[1]:.4f} "
            f"(want the opposite)")

# -- The two steps that need a server of their own -------------------------

def _solo(ctx, current_json: Path, expect_photo: Path | None):
    """A second server with ORION_CURRENT pointed somewhere, because the
    variable is read at startup. `expect_photo` None is the nothing-is-open
    case, where get_stats with no path must fail by name."""
    proc = spawn(ctx.root, {"ORION_BIN": str(ctx.orion), "ORION_CURRENT": str(current_json)})
    try:
        s = Session(proc)
        s.handshake()
        photo = s.ask("current_photo").get("photo")
        want = None if expect_photo is None else str(expect_photo)
        if photo != want:
            return f"current_photo returned {photo}, expected {want}"
        if expect_photo is None:
            why = s.refusal("get_stats")
            if "no photo is open" not in why:
                return f"'{why}' misses 'no photo is open'"
            return None
        width = s.ask("get_stats").get("width", 0)
        if width <= 0:
            return f"width={width}, expected > 0"
    finally:
        proc.terminate()
        try:
            proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            proc.kill()

def current_photo_none(s, ctx):
    return _solo(ctx, ctx.tmpdir / "nonexistent_current.json", None)

def current_photo_set(s, ctx):
    path = ctx.tmpdir / "current_test.json"
    path.write_text(json.dumps({"photo": str(ctx.raw), "folder": str(ctx.tmpdir),
                                "updated": "2026-09-14T00:00:00Z"}))
    return _solo(ctx, path, ctx.raw)

# The gate's whole list, in order, and the order is load-bearing: the first
# six walk one photograph through propose, approve and flag. Adding a step is
# one name here plus one function above; the `ok` line is its name.
STEPS = [
    get_stats, get_proxy, propose_edit, get_proxy_proposed, approve_edit,
    set_flag, reject_edit, describe_edits, propose_edit_rejected,
    propose_edit_reset, propose_composite, propose_composite_rejected,
    get_stats_proposed, get_stats_region, get_proxy_region, detect_faces,
    mask_invert, current_photo_none, current_photo_set,
]
