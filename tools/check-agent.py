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
    checks_total = 14

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

            # Check 8: describe_edits → JSON has temperatureK with min ≥ 1000 and absolute true; ≥ 30 keys
            try:
                keys_resp = call(proc, "tools/call", {
                    "name": "describe_edits",
                    "arguments": {},
                }, req_id, TIMEOUT)
                req_id += 1

                if keys_resp.get("error"):
                    problems.append(f"Check 8 (describe_edits): {keys_resp['error']['message']}")
                else:
                    content = keys_resp.get("result", {}).get("content", [{}])[0]
                    if content.get("type") == "text":
                        keys_data = json.loads(content.get("text", "{}"))
                        keys_array = keys_data.get("keys", []) if isinstance(keys_data, dict) else []
                        num_keys = len(keys_array)

                        # Find temperatureK entry
                        temp_entry = None
                        for key_obj in keys_array:
                            if key_obj.get("name") == "temperatureK":
                                temp_entry = key_obj
                                break

                        temp_min = temp_entry.get("min") if temp_entry else None
                        temp_absolute = temp_entry.get("absolute") if temp_entry else None

                        if num_keys >= 30 and (temp_min is None or temp_min >= 2000) and temp_absolute == True:
                            print(f"ok  describe_edits")
                            checks_passed += 1
                        else:
                            problems.append(f"Check 8: num_keys={num_keys} (need ≥30), temperatureK.min={temp_min} (need ≥2000), absolute={temp_absolute} (need True)")
                    else:
                        problems.append(f"Check 8: expected text content, got {content.get('type')}")
            except Exception as e:
                problems.append(f"Check 8 (describe_edits): {e}")

            # Check 9: propose_edit with out-of-range temperatureK → rejected, no proposed file written
            raw3_path = tmpdir / f"{SAMPLE_RAW[:-4]}_3.ARW"
            shutil.copy2(raw_path, raw3_path)
            proposed3_path = raw3_path.with_name(f"{raw3_path.stem}.proposed.json")

            try:
                reject_prop_resp = call(proc, "tools/call", {
                    "name": "propose_edit",
                    "arguments": {"path": str(raw3_path), "edits": {"temperatureK": 150}},
                }, req_id, TIMEOUT)
                req_id += 1

                result = reject_prop_resp.get("result", {})
                if result.get("isError") == True:
                    # This is expected to have an error
                    content = result.get("content", [{}])[0]
                    if content.get("type") == "text":
                        error_text = content.get("text", "")
                        if error_text.startswith("REJECTED:") and "temperatureK" in error_text and "150" in error_text:
                            if not proposed3_path.exists():
                                print(f"ok  propose_edit_rejected")
                                checks_passed += 1
                            else:
                                problems.append(f"Check 9: proposed file was created despite rejection at {proposed3_path}")
                        else:
                            problems.append(f"Check 9: error text '{error_text}' does not match expected pattern")
                    else:
                        problems.append(f"Check 9: expected text error content, got {content.get('type')}")
                else:
                    problems.append(f"Check 9: expected isError=True but got {result}")
            except Exception as e:
                problems.append(f"Check 9 (propose_edit_rejected): {e}")

            # Check 10: propose_edit accumulation and reset
            raw4_path = tmpdir / f"{SAMPLE_RAW[:-4]}_4.ARW"
            shutil.copy2(raw_path, raw4_path)
            proposed4_path = raw4_path.with_name(f"{raw4_path.stem}.proposed.json")

            try:
                # First proposal: exposureEv 0.5
                prop1_resp = call(proc, "tools/call", {
                    "name": "propose_edit",
                    "arguments": {"path": str(raw4_path), "edits": {"exposureEv": 0.5}},
                }, req_id, TIMEOUT)
                req_id += 1

                result1 = prop1_resp.get("result", {})
                if result1.get("isError") == True:
                    problems.append(f"Check 10a (propose_edit exposureEv): {result1.get('content', [{}])[0].get('text', '')}")
                else:
                    # Second proposal with reset: saturation 0.5, reset true
                    prop2_resp = call(proc, "tools/call", {
                        "name": "propose_edit",
                        "arguments": {"path": str(raw4_path), "edits": {"saturation": 0.5}, "reset": True},
                    }, req_id, TIMEOUT)
                    req_id += 1

                    result2 = prop2_resp.get("result", {})
                    if result2.get("isError") == True:
                        problems.append(f"Check 10b (propose_edit reset): {result2.get('content', [{}])[0].get('text', '')}")
                    else:
                        content = result2.get("content", [{}])[0]
                        if content.get("type") == "text":
                            result_obj = json.loads(content.get("text", "{}"))
                            state = result_obj.get("state", {})
                            exp_ev = state.get("exposureEv")
                            sat = state.get("saturation")

                            # After reset, exposureEv should be 0 or base value (not 0.5)
                            # saturation should be 0.5
                            if (exp_ev == 0 or exp_ev is None) and sat == 0.5:
                                print(f"ok  propose_edit_reset")
                                checks_passed += 1
                                # Clean up
                                reject_final_resp = call(proc, "tools/call", {
                                    "name": "reject_edit",
                                    "arguments": {"path": str(raw4_path)},
                                }, req_id, TIMEOUT)
                                req_id += 1
                            else:
                                problems.append(f"Check 10: after reset, exposureEv={exp_ev} (expected 0), saturation={sat} (expected 0.5)")
                        else:
                            problems.append(f"Check 10b: expected text content, got {content.get('type')}")
            except Exception as e:
                problems.append(f"Check 10 (propose_edit_reset): {e}")

            # Check 13: propose_composite — curve with 3 points, get_proxy, reject
            raw5_path = tmpdir / f"{SAMPLE_RAW[:-4]}_5.ARW"
            shutil.copy2(SAMPLES / SAMPLE_RAW, raw5_path)

            try:
                # First get describe_edits to understand curve format
                keys_desc_resp = call(proc, "tools/call", {
                    "name": "describe_edits",
                    "arguments": {},
                }, req_id, TIMEOUT)
                req_id += 1

                if keys_desc_resp.get("error"):
                    problems.append(f"Check 13 (describe_edits): {keys_desc_resp['error']['message']}")
                else:
                    content = keys_desc_resp.get("result", {}).get("content", [{}])[0]
                    if content.get("type") == "text":
                        keys_data = json.loads(content.get("text", "{}"))
                        keys_array = keys_data.get("keys", []) if isinstance(keys_data, dict) else []

                        # Find curve entry's example
                        curve_entry = None
                        for key_obj in keys_array:
                            if key_obj.get("name") == "curve":
                                curve_entry = key_obj
                                break

                        if curve_entry and curve_entry.get("example"):
                            curve_example = curve_entry.get("example")
                            # Modify master curve to 3 points: [[0,0],[0.5,0.6],[1,1]]
                            # Keeping {x,y} object shape
                            modified_curve = {k: v for k, v in curve_example.items()}
                            modified_curve["master"] = [{"x": 0, "y": 0}, {"x": 0.5, "y": 0.6}, {"x": 1, "y": 1}]

                            # propose_edit with the curve
                            curve_propose_resp = call(proc, "tools/call", {
                                "name": "propose_edit",
                                "arguments": {"path": str(raw5_path), "edits": {"curve": modified_curve}},
                            }, req_id, TIMEOUT)
                            req_id += 1

                            if curve_propose_resp.get("error"):
                                problems.append(f"Check 13 (propose_edit curve): {curve_propose_resp['error']['message']}")
                            else:
                                result = curve_propose_resp.get("result", {})
                                if result.get("isError") == True:
                                    problems.append(f"Check 13 (propose_edit): {result.get('content', [{}])[0].get('text', '')}")
                                else:
                                    content_prop = result.get("content", [{}])[0]
                                    if content_prop.get("type") == "text":
                                        prop_result = json.loads(content_prop.get("text", "{}"))
                                        state = prop_result.get("state", {})
                                        state_curve = state.get("curve", {})
                                        master_points = state_curve.get("master", [])

                                        if len(master_points) == 3:
                                            # get_proxy with state proposed
                                            proxy_comp_resp = call(proc, "tools/call", {
                                                "name": "get_proxy",
                                                "arguments": {"path": str(raw5_path), "maxPx": 512, "state": "proposed"},
                                            }, req_id, TIMEOUT)
                                            req_id += 1

                                            if proxy_comp_resp.get("error"):
                                                problems.append(f"Check 13 (get_proxy proposed): {proxy_comp_resp['error']['message']}")
                                            else:
                                                content_img = proxy_comp_resp.get("result", {}).get("content", [{}])[0]
                                                if content_img.get("type") == "image":
                                                    # reject_edit
                                                    reject_comp_resp = call(proc, "tools/call", {
                                                        "name": "reject_edit",
                                                        "arguments": {"path": str(raw5_path)},
                                                    }, req_id, TIMEOUT)
                                                    req_id += 1

                                                    if reject_comp_resp.get("error"):
                                                        problems.append(f"Check 13 (reject_edit): {reject_comp_resp['error']['message']}")
                                                    else:
                                                        result_rej = reject_comp_resp.get("result", {})
                                                        if result_rej.get("isError") == True:
                                                            problems.append(f"Check 13 (reject_edit): got error")
                                                        else:
                                                            content_rej = result_rej.get("content", [{}])[0]
                                                            if content_rej.get("type") == "text":
                                                                rej_result = json.loads(content_rej.get("text", "{}"))
                                                                if rej_result.get("deleted") == True:
                                                                    print(f"ok  propose_composite")
                                                                    checks_passed += 1
                                                                else:
                                                                    problems.append(f"Check 13: deleted={rej_result.get('deleted')}, expected True")
                                                            else:
                                                                problems.append(f"Check 13: expected text reject result, got {content_rej.get('type')}")
                                                else:
                                                    problems.append(f"Check 13 (get_proxy): expected image, got {content_img.get('type')}")
                                        else:
                                            problems.append(f"Check 13: master has {len(master_points)} points, expected 3")
                                    else:
                                        problems.append(f"Check 13 (propose_edit): expected text result, got {content_prop.get('type')}")
                        else:
                            problems.append(f"Check 13: curve not found in describe_edits or missing example")
                    else:
                        problems.append(f"Check 13 (describe_edits): expected text, got {content.get('type')}")
            except Exception as e:
                problems.append(f"Check 13 (propose_composite): {e}")

            # Check 14: propose_composite_rejected — incomplete layers
            raw6_path = tmpdir / f"{SAMPLE_RAW[:-4]}_6.ARW"
            shutil.copy2(SAMPLES / SAMPLE_RAW, raw6_path)

            try:
                # propose_edit with incomplete layers (missing required fields)
                incomplete_layers_resp = call(proc, "tools/call", {
                    "name": "propose_edit",
                    "arguments": {"path": str(raw6_path), "edits": {"layers": [{"exposureEv": 1.0}]}},
                }, req_id, TIMEOUT)
                req_id += 1

                if incomplete_layers_resp.get("error"):
                    problems.append(f"Check 14 (propose_edit layers): {incomplete_layers_resp['error']['message']}")
                else:
                    result = incomplete_layers_resp.get("result", {})
                    if result.get("isError") == True:
                        content_err = result.get("content", [{}])[0]
                        if content_err.get("type") == "text":
                            error_text = content_err.get("text", "")
                            if error_text.startswith("REJECTED:") and "missing" in error_text:
                                print(f"ok  propose_composite_rejected")
                                checks_passed += 1
                            else:
                                problems.append(f"Check 14: error text does not match pattern: '{error_text}'")
                        else:
                            problems.append(f"Check 14: expected text error, got {content_err.get('type')}")
                    else:
                        problems.append(f"Check 14: expected isError=True but got {result}")
            except Exception as e:
                problems.append(f"Check 14 (propose_composite_rejected): {e}")

        finally:
            proc.terminate()
            try:
                proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                proc.kill()

        # Check 11: current_photo_none — ORION_CURRENT points to non-existent file
        # Uses its own server instance with ORION_CURRENT set to a non-existent path
        nonexist_current = tmpdir / "nonexistent_current.json"
        env_none = {**os.environ, "ORION_BIN": str(ORION), "ORION_CURRENT": str(nonexist_current)}
        try:
            proc_none = subprocess.Popen(
                ["node", str(ROOT / "mcp" / "server.ts")],
                stdin=subprocess.PIPE,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True,
                env=env_none,
            )
        except Exception as e:
            problems.append(f"Check 11 (current_photo_none): failed to start server: {e}")
        else:
            try:
                # Initialize
                init_resp = call(proc_none, "initialize", {
                    "protocolVersion": "2025-03-26",
                    "capabilities": {},
                    "clientInfo": {"name": "check-agent", "version": "0.1"},
                }, 1)
                if init_resp.get("error"):
                    problems.append(f"Check 11: initialize failed: {init_resp['error']}")
                else:
                    # Send initialized notification
                    msg = json.dumps({"jsonrpc": "2.0", "method": "notifications/initialized", "params": {}})
                    proc_none.stdin.write(msg + "\n")
                    proc_none.stdin.flush()

                    # Call current_photo with no photo available
                    current_resp = call(proc_none, "tools/call", {
                        "name": "current_photo",
                        "arguments": {},
                    }, 2, TIMEOUT)

                    if current_resp.get("error"):
                        problems.append(f"Check 11 (current_photo): unexpected error: {current_resp['error']}")
                    else:
                        content = current_resp.get("result", {}).get("content", [{}])[0]
                        if content.get("type") == "text":
                            photo_data = json.loads(content.get("text", "{}"))
                            if photo_data.get("photo") is None:
                                # Now test get_stats with no path returns error
                                stats_resp = call(proc_none, "tools/call", {
                                    "name": "get_stats",
                                    "arguments": {},
                                }, 3, TIMEOUT)

                                if stats_resp.get("error"):
                                    problems.append(f"Check 11 (get_stats no path): unexpected error format")
                                else:
                                    result = stats_resp.get("result", {})
                                    if result.get("isError") == True:
                                        content_err = result.get("content", [{}])[0]
                                        if content_err.get("type") == "text":
                                            error_text = content_err.get("text", "")
                                            if "no photo is open" in error_text:
                                                print(f"ok  current_photo_none")
                                                checks_passed += 1
                                            else:
                                                problems.append(f"Check 11: error text '{error_text}' missing 'no photo is open'")
                                        else:
                                            problems.append(f"Check 11: expected text error, got {content_err.get('type')}")
                                    else:
                                        problems.append(f"Check 11: get_stats should error but got {result}")
                            else:
                                problems.append(f"Check 11: current_photo should have photo: null, got {photo_data}")
                        else:
                            problems.append(f"Check 11: expected text content, got {content.get('type')}")
            except Exception as e:
                problems.append(f"Check 11 (current_photo_none): {e}")
            finally:
                proc_none.terminate()
                try:
                    proc_none.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    proc_none.kill()

        # Check 12: current_photo_set — with ORION_CURRENT file present
        # Create a current.json pointing to the sample copy
        current_json_path = tmpdir / "current_test.json"
        with open(current_json_path, "w") as f:
            json.dump({
                "photo": str(raw_path),
                "folder": str(tmpdir),
                "updated": "2026-09-14T00:00:00Z"
            }, f)

        env_set = {**os.environ, "ORION_BIN": str(ORION), "ORION_CURRENT": str(current_json_path)}
        try:
            proc_set = subprocess.Popen(
                ["node", str(ROOT / "mcp" / "server.ts")],
                stdin=subprocess.PIPE,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True,
                env=env_set,
            )
        except Exception as e:
            problems.append(f"Check 12 (current_photo_set): failed to start server: {e}")
        else:
            try:
                # Initialize
                init_resp = call(proc_set, "initialize", {
                    "protocolVersion": "2025-03-26",
                    "capabilities": {},
                    "clientInfo": {"name": "check-agent", "version": "0.1"},
                }, 1)
                if init_resp.get("error"):
                    problems.append(f"Check 12: initialize failed: {init_resp['error']}")
                else:
                    # Send initialized notification
                    msg = json.dumps({"jsonrpc": "2.0", "method": "notifications/initialized", "params": {}})
                    proc_set.stdin.write(msg + "\n")
                    proc_set.stdin.flush()

                    # Test current_photo defaults to this path
                    current_resp = call(proc_set, "tools/call", {
                        "name": "current_photo",
                        "arguments": {},
                    }, 2, TIMEOUT)

                    if current_resp.get("error"):
                        problems.append(f"Check 12 (current_photo): {current_resp['error']}")
                    else:
                        content = current_resp.get("result", {}).get("content", [{}])[0]
                        if content.get("type") == "text":
                            photo_data = json.loads(content.get("text", "{}"))
                            if photo_data.get("photo") == str(raw_path):
                                # Now test get_stats with no path (defaults to current)
                                stats_resp = call(proc_set, "tools/call", {
                                    "name": "get_stats",
                                    "arguments": {},
                                }, 3, TIMEOUT)

                                if stats_resp.get("error"):
                                    problems.append(f"Check 12 (get_stats): {stats_resp['error']['message']}")
                                else:
                                    result = stats_resp.get("result", {})
                                    content_stats = result.get("content", [{}])[0]
                                    if content_stats.get("type") == "text":
                                        stats = json.loads(content_stats.get("text", "{}"))
                                        if stats.get("width", 0) > 0:
                                            print(f"ok  current_photo_set")
                                            checks_passed += 1
                                        else:
                                            problems.append(f"Check 12: width={stats.get('width')}, expected > 0")
                                    else:
                                        problems.append(f"Check 12: expected text stats, got {content_stats.get('type')}")
                            else:
                                problems.append(f"Check 12: current_photo returned {photo_data.get('photo')}, expected {raw_path}")
                        else:
                            problems.append(f"Check 12: expected text content, got {content.get('type')}")
            except Exception as e:
                problems.append(f"Check 12 (current_photo_set): {e}")
            finally:
                proc_set.terminate()
                try:
                    proc_set.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    proc_set.kill()

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
