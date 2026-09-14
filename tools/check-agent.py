#!/usr/bin/env python3
"""Drive the Orion MCP server end-to-end against a copied sample RAW.

    ./tools/check-agent.py

⚠ **Why this exists.** The eighth gate validates that the MCP server correctly
shells to the `--agent` verbs, that proposed edits round-trip JSON, and that
a human approval commits to the sidecar. It runs against a GPU render and a
real image file, not a stub. Neither `mcp/server.test.ts` (which runs against
`mcp/test/fake-orion.sh` on every change) nor the binary's own unit tests can
catch if the glue between them is wrong.

Seven checks per the spec, Part C:
1. get_stats → width > 0, rating == 0
2. get_proxy maxPx 512 → JPEG (FF D8), long edge ≤ 512
3. propose_edit {exposureEv: 1.0} → proposed file exists with "exposureEv":1
4. get_proxy state proposed → succeeds
5. approve_edit → sidecar exists with exposureEv, proposed file gone
6. set_flag rating 4 → get_stats reports rating 4
7. reject_edit (no proposal) → deleted false, no error
"""

import base64
import json
import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
ORION = ROOT / "build" / "Orion.app" / "Contents" / "MacOS" / "Orion"
SAMPLES = ROOT / "samples"
SAMPLE_RAW = "_PIC8095.ARW"

TIMEOUT = 60
RUN_TIMEOUT = 5 * 60

CHECK_LINE = re.compile(r"^\s+(ok|FAIL)\b", re.M)


def decode_jpeg_dimensions(data: bytes) -> tuple[int, int] | None:
    """Extract width and height from a JPEG by parsing SOF0/SOF2 marker.
    Returns (height, width) or None if parsing fails."""
    # Look for FFD8 (SOI) first
    if len(data) < 2 or data[0] != 0xFF or data[1] != 0xD8:
        return None

    i = 2
    while i < len(data) - 9:
        if data[i] != 0xFF:
            i += 1
            continue
        marker = data[i + 1]
        # SOF0 (0xC0) or SOF2 (0xC2)
        if marker in (0xC0, 0xC2):
            # Skip 2 bytes length and 1 byte precision
            height = struct.unpack(">H", data[i + 5:i + 7])[0]
            width = struct.unpack(">H", data[i + 7:i + 9])[0]
            return (height, width)
        # Skip this segment
        seg_len = struct.unpack(">H", data[i + 2:i + 4])[0]
        i += 2 + seg_len
    return None


def call(proc, method: str, params: dict, req_id: int, timeout: float = TIMEOUT) -> dict:
    """Send a JSON-RPC request and read the response."""
    msg = json.dumps({"jsonrpc": "2.0", "id": req_id, "method": method, "params": params})
    try:
        proc.stdin.write(msg + "\n")
        proc.stdin.flush()
    except BrokenPipeError:
        raise RuntimeError("Server exited unexpectedly")

    # Read lines until we find one with our id
    deadline = None
    while True:
        try:
            if deadline is None:
                deadline = __import__("time").time() + timeout
            remaining = max(0.1, deadline - __import__("time").time())
            # Use select to avoid blocking forever
            import select
            ready, _, _ = select.select([proc.stdout], [], [], remaining)
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


def main():
    if not ORION.is_file():
        print(f"check-agent: no binary at {ORION}\n"
              f"  Build first: cmake --build build", file=sys.stderr)
        return 2
    if not (SAMPLES / SAMPLE_RAW).is_file():
        print(f"check-agent: no sample at {SAMPLES / SAMPLE_RAW}",
              file=sys.stderr)
        return 2

    problems = []
    checks_passed = 0
    checks_total = 7

    # Copy sample to temp directory
    with tempfile.TemporaryDirectory() as tmpdir:
        tmpdir = Path(tmpdir)
        raw_path = tmpdir / SAMPLE_RAW
        shutil.copy2(SAMPLES / SAMPLE_RAW, raw_path)

        # Copy sidecar if it exists
        sidecar_src = SAMPLES / f"{SAMPLE_RAW[:-4]}.xmp"
        if sidecar_src.exists():
            shutil.copy2(sidecar_src, tmpdir / sidecar_src.name)

        # Start MCP server
        env = {**os.environ, "ORION_BIN": str(ORION)}
        try:
            proc = subprocess.Popen(
                ["node", str(ROOT / "mcp" / "server.ts")],
                stdin=subprocess.PIPE,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True,
                env=env,
            )
        except Exception as e:
            print(f"check-agent: failed to start server: {e}", file=sys.stderr)
            return 2

        try:
            # Initialize
            init_resp = call(proc, "initialize", {
                "protocolVersion": "2025-03-26",
                "capabilities": {},
                "clientInfo": {"name": "check-agent", "version": "0.1"},
            }, 1)
            if init_resp.get("error"):
                problems.append(f"initialize failed: {init_resp['error']}")
                raise RuntimeError("initialize failed")

            # Send initialized notification (no response expected)
            msg = json.dumps({"jsonrpc": "2.0", "method": "notifications/initialized", "params": {}})
            proc.stdin.write(msg + "\n")
            proc.stdin.flush()

            # List tools
            tools_resp = call(proc, "tools/list", {}, 2)
            if tools_resp.get("error"):
                problems.append(f"tools/list failed: {tools_resp['error']}")
                raise RuntimeError("tools/list failed")

            req_id = 3

            # Check 1: get_stats → width > 0, rating == 0
            try:
                stats_resp = call(proc, "tools/call", {
                    "name": "get_stats",
                    "arguments": {"path": str(raw_path)},
                }, req_id, TIMEOUT)
                req_id += 1

                if stats_resp.get("error"):
                    problems.append(f"Check 1 (get_stats): {stats_resp['error']['message']}")
                else:
                    content = stats_resp.get("result", {}).get("content", [{}])[0]
                    if content.get("type") == "text":
                        stats = json.loads(content.get("text", "{}"))
                        if stats.get("width", 0) > 0 and stats.get("rating") == 0:
                            print(f"ok  get_stats")
                            checks_passed += 1
                        else:
                            problems.append(f"Check 1 (get_stats): width={stats.get('width')}, rating={stats.get('rating')}")
                    else:
                        problems.append(f"Check 1 (get_stats): unexpected response format")
            except Exception as e:
                problems.append(f"Check 1 (get_stats): {e}")

            # Check 2: get_proxy maxPx 512 → JPEG (FF D8), long edge ≤ 512
            try:
                proxy_resp = call(proc, "tools/call", {
                    "name": "get_proxy",
                    "arguments": {"path": str(raw_path), "maxPx": 512},
                }, req_id, TIMEOUT)
                req_id += 1

                if proxy_resp.get("error"):
                    problems.append(f"Check 2 (get_proxy 512): {proxy_resp['error']['message']}")
                else:
                    content = proxy_resp.get("result", {}).get("content", [{}])[0]
                    if content.get("type") == "image":
                        data = base64.b64decode(content.get("data", ""))
                        if len(data) >= 2 and data[0] == 0xFF and data[1] == 0xD8:
                            dims = decode_jpeg_dimensions(data)
                            if dims:
                                h, w = dims
                                long_edge = max(h, w)
                                if long_edge <= 512:
                                    print(f"ok  get_proxy")
                                    checks_passed += 1
                                else:
                                    problems.append(f"Check 2: long edge {long_edge} > 512")
                            else:
                                problems.append(f"Check 2: failed to parse JPEG dimensions")
                        else:
                            problems.append(f"Check 2: not a valid JPEG (first bytes: {data[:2].hex() if data else 'empty'})")
                    else:
                        problems.append(f"Check 2: expected image content, got {content.get('type')}")
            except Exception as e:
                problems.append(f"Check 2 (get_proxy 512): {e}")

            # Check 3: propose_edit {exposureEv: 1.0} → proposed file exists with "exposureEv":1
            try:
                propose_resp = call(proc, "tools/call", {
                    "name": "propose_edit",
                    "arguments": {"path": str(raw_path), "edits": {"exposureEv": 1.0}},
                }, req_id, TIMEOUT)
                req_id += 1

                if propose_resp.get("error"):
                    problems.append(f"Check 3 (propose_edit): {propose_resp['error']['message']}")
                else:
                    proposed_path = raw_path.with_name(f"{raw_path.stem}.proposed.json")
                    if proposed_path.exists():
                        with open(proposed_path) as f:
                            proposed_data = json.load(f)
                            if proposed_data.get("exposureEv") == 1.0 or proposed_data.get("exposureEv") == 1:
                                print(f"ok  propose_edit")
                                checks_passed += 1
                            else:
                                problems.append(f"Check 3: proposed file missing exposureEv=1, has {proposed_data.get('exposureEv')}")
                    else:
                        problems.append(f"Check 3: proposed file not created at {proposed_path}")
            except Exception as e:
                problems.append(f"Check 3 (propose_edit): {e}")

            # Check 4: get_proxy state proposed → succeeds
            try:
                proxy_prop_resp = call(proc, "tools/call", {
                    "name": "get_proxy",
                    "arguments": {"path": str(raw_path), "maxPx": 512, "state": "proposed"},
                }, req_id, TIMEOUT)
                req_id += 1

                if proxy_prop_resp.get("error"):
                    problems.append(f"Check 4 (get_proxy proposed): {proxy_prop_resp['error']['message']}")
                else:
                    content = proxy_prop_resp.get("result", {}).get("content", [{}])[0]
                    if content.get("type") == "image":
                        print(f"ok  get_proxy_proposed")
                        checks_passed += 1
                    else:
                        problems.append(f"Check 4: expected image content, got {content.get('type')}")
            except Exception as e:
                problems.append(f"Check 4 (get_proxy proposed): {e}")

            # Check 5: approve_edit → sidecar exists with exposureEv, proposed file gone
            try:
                approve_resp = call(proc, "tools/call", {
                    "name": "approve_edit",
                    "arguments": {"path": str(raw_path)},
                }, req_id, TIMEOUT)
                req_id += 1

                if approve_resp.get("error"):
                    problems.append(f"Check 5 (approve_edit): {approve_resp['error']['message']}")
                else:
                    proposed_path = raw_path.with_name(f"{raw_path.stem}.proposed.json")
                    sidecar_path = raw_path.with_name(f"{raw_path.stem}.xmp")

                    if proposed_path.exists():
                        problems.append(f"Check 5: proposed file still exists after approve")
                    elif not sidecar_path.exists():
                        problems.append(f"Check 5: sidecar not created at {sidecar_path}")
                    else:
                        with open(sidecar_path) as f:
                            sidecar_data = f.read()
                            # Extract base64 from orion:Develop="..." attribute
                            match = re.search(r'orion:Develop="([^"]+)"', sidecar_data)
                            if match:
                                base64_str = match.group(1)
                                try:
                                    decoded = base64.b64decode(base64_str).decode('utf-8')
                                    state = json.loads(decoded)
                                    if state.get("exposureEv") == 1 or state.get("exposureEv") == 1.0:
                                        print(f"ok  approve_edit")
                                        checks_passed += 1
                                    else:
                                        problems.append(f"Check 5: decoded state has exposureEv={state.get('exposureEv')}, expected 1")
                                except (json.JSONDecodeError, ValueError) as e:
                                    problems.append(f"Check 5: failed to decode base64 state: {e}")
                            else:
                                problems.append(f"Check 5: sidecar missing orion:Develop attribute")
            except Exception as e:
                problems.append(f"Check 5 (approve_edit): {e}")

            # Check 6: set_flag rating 4 → get_stats reports rating 4
            try:
                flag_resp = call(proc, "tools/call", {
                    "name": "set_flag",
                    "arguments": {"path": str(raw_path), "rating": 4},
                }, req_id, TIMEOUT)
                req_id += 1

                if flag_resp.get("error"):
                    problems.append(f"Check 6 (set_flag): {flag_resp['error']['message']}")
                else:
                    stats2_resp = call(proc, "tools/call", {
                        "name": "get_stats",
                        "arguments": {"path": str(raw_path)},
                    }, req_id, TIMEOUT)
                    req_id += 1

                    if stats2_resp.get("error"):
                        problems.append(f"Check 6 (get_stats after flag): {stats2_resp['error']['message']}")
                    else:
                        content = stats2_resp.get("result", {}).get("content", [{}])[0]
                        if content.get("type") == "text":
                            stats2 = json.loads(content.get("text", "{}"))
                            if stats2.get("rating") == 4:
                                print(f"ok  set_flag")
                                checks_passed += 1
                            else:
                                problems.append(f"Check 6: rating is {stats2.get('rating')}, expected 4")
                        else:
                            problems.append(f"Check 6: unexpected response format")
            except Exception as e:
                problems.append(f"Check 6 (set_flag): {e}")

            # Check 7: reject_edit on a photo with no proposal → deleted false, no error
            # First create another copy of the raw to test reject on
            raw2_path = tmpdir / f"{SAMPLE_RAW[:-4]}_2.ARW"
            shutil.copy2(raw_path, raw2_path)

            try:
                reject_resp = call(proc, "tools/call", {
                    "name": "reject_edit",
                    "arguments": {"path": str(raw2_path)},
                }, req_id, TIMEOUT)
                req_id += 1

                if reject_resp.get("error"):
                    problems.append(f"Check 7 (reject_edit): {reject_resp['error']['message']}")
                else:
                    content = reject_resp.get("result", {}).get("content", [{}])[0]
                    if content.get("type") == "text":
                        result = json.loads(content.get("text", "{}"))
                        if result.get("deleted") == False:
                            print(f"ok  reject_edit")
                            checks_passed += 1
                        else:
                            problems.append(f"Check 7: deleted={result.get('deleted')}, expected false")
                    else:
                        problems.append(f"Check 7: unexpected response format")
            except Exception as e:
                problems.append(f"Check 7 (reject_edit): {e}")

        finally:
            proc.terminate()
            try:
                proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                proc.kill()

    # Verify samples/ is unchanged
    result = subprocess.run(["git", "status", "--porcelain", str(SAMPLES)],
                          cwd=str(ROOT), capture_output=True, text=True)
    if result.stdout.strip():
        problems.append(f"samples/ modified:\n{result.stdout}")

    if problems:
        print(f"check-agent: {len(problems)} problem(s)\n", file=sys.stderr)
        for p in problems:
            print(f"  {p}\n", file=sys.stderr)
        return 1

    print(f"check-agent: all {checks_passed}/{checks_total} checks passed.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
