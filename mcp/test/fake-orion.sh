#!/bin/sh
# Fake `Orion --agent` binary for mcp/server.test.ts and manual smoke tests.
# $1 is "--agent", $2 is the verb. Options are "--flag value" pairs after that.
# Answers each verb with the canned JSON the spec's Part A table describes,
# so the MCP server's wiring can be tested without a GPU or a real build.
set -e
verb="$2"
shift 2

raw=""
out=""
edits=""
max=""
state=""
region=""
kind=""
targets=""
groups=""
mode=""
while [ $# -gt 0 ]; do
  case "$1" in
    --out) out="$2"; shift 2 ;;
    --edits) edits="$2"; shift 2 ;;
    --state) state="$2"; shift 2 ;;
    --max) max="$2"; shift 2 ;;
    --region) region="$2"; shift 2 ;;
    --kind) kind="$2"; shift 2 ;;
    --targets) targets="$2"; shift 2 ;;
    --groups) groups="$2"; shift 2 ;;
    --mode) mode="$2"; shift 2 ;;
    --rating) shift 2 ;;
    --reject) shift 2 ;;
    *) if [ -z "$raw" ]; then raw="$1"; fi; shift ;;
  esac
done

case "$verb" in
  stats)
    # Records the --state and --region this invocation received, under the
    # system temp dir, so tests can verify get_stats passes both through
    # rather than dropping them.
    echo "${state:-<none>}" > "${TMPDIR:-/tmp}/orion-mcp-test-last-stats-state.txt"
    echo "${region:-<none>}" > "${TMPDIR:-/tmp}/orion-mcp-test-last-stats-region.txt"
    regionJSON=""
    if [ -n "$region" ]; then
      regionJSON=',"region":{"luma":0.42,"saturation":0.1,"hue":30,"hueStrength":0.8,"red":0.5,"green":0.4,"blue":0.3,"clippedHigh":0.01,"clippedLow":0,"shading":0.2,"frame":{"centerX":0.6,"centerY":0.25,"radiusX":0.25,"radiusY":0.25}}'
    fi
    echo '{"path":"'"$raw"'","width":100,"height":80,"camera":"Fake","rating":0,"rejected":false,"clipLow":[0,0,0],"clipHigh":[0,0,0],"mean":[0.5,0.5,0.5],"temperatureK":5500,"tint":0'"$regionJSON"'}'
    ;;
  faces)
    # One face, in both spaces, so a test can prove detect_faces carries the
    # shape through. An empty array is the other legal answer and the gate
    # against the real binary covers that half.
    echo "${state:-<none>}" > "${TMPDIR:-/tmp}/orion-mcp-test-last-faces-state.txt"
    echo '{"path":"'"$raw"'","faces":[{"x":0.4,"y":0.2,"w":0.12,"h":0.16,"centerX":0.28,"centerY":0.46,"radiusX":0.08,"radiusY":0.06}]}'
    ;;
  matte)
    # Records the --kind and --state it received, writes the matte PNG beside
    # the RAW the way the real verb does, and answers found for subject and
    # person but not for sky, so both branches are testable.
    tmp="${TMPDIR:-/tmp}"
    echo "${kind:-<none>}" > "$tmp/orion-mcp-test-last-matte-kind.txt"
    echo "${state:-<none>}" > "$tmp/orion-mcp-test-last-matte-state.txt"
    case "$kind" in
      subject|person)
        cp "$(dirname "$0")/fixture.jpg" "${raw%.*}.orion-matte-fake-id.png"
        source="Subject"
        if [ "$kind" = person ]; then source="Person"; fi
        echo '{"path":"'"$raw"'","found":true,"kind":"'"$kind"'","matteId":"fake-id","matteSource":"'"$source"'","width":512,"height":341,"coverage":0.1234,"component":{"kind":4,"matteId":"fake-id","matteSource":"'"$source"'","startsLayer":true,"compose":0,"invert":false,"centerX":0.5,"centerY":0.5,"radiusX":0.25,"radiusY":0.25,"feather":0.5,"angle":0,"length":0.2,"roundness":2}}'
        ;;
      sky)
        echo '{"path":"'"$raw"'","found":false,"kind":"sky","reason":"Nothing was found to select in this photograph."}'
        ;;
      *)
        echo "usage: --kind must be subject, person or sky" >&2
        exit 2
        ;;
    esac
    ;;
  sync)
    # Records --mode, --groups, --state and the targets file's contents (the
    # caller deletes that file), and appends one line per invocation to a log
    # so a test can see the open-photo rule's two calls. Writes a proposal or
    # touches a sidecar per target, like the real verb's two modes.
    tmp="${TMPDIR:-/tmp}"
    echo "${mode:-<none>}" > "$tmp/orion-mcp-test-last-sync-mode.txt"
    echo "${groups:-<none>}" > "$tmp/orion-mcp-test-last-sync-groups.txt"
    echo "${state:-<none>}" > "$tmp/orion-mcp-test-last-sync-state.txt"
    cat "$targets" > "$tmp/orion-mcp-test-last-sync-targets.txt"
    echo "${mode:-propose} $(cat "$targets")" >> "$tmp/orion-mcp-test-sync-calls.txt"
    node -e '
      const fs = require("fs");
      const [source, targetsPath, mode, groups, state] = process.argv.slice(1);
      const targets = JSON.parse(fs.readFileSync(targetsPath, "utf8"));
      if (targets.length === 0) { console.error("usage: --targets is empty"); process.exit(2); }
      const results = targets.map((t) => {
        if (t === source) return { path: t, skipped: "source" };
        const stem = t.replace(/\.[^.]+$/, "");
        if (mode === "commit") {
          fs.writeFileSync(stem + ".xmp", "");
          return { path: t, wrote: "sidecar", file: stem + ".xmp", mattes: [] };
        }
        fs.writeFileSync(stem + ".proposed.json", JSON.stringify({ exposureEv: 1, tint: 0, temperatureK: 5500 }));
        return { path: t, wrote: "proposed", file: stem + ".proposed.json", mattes: [] };
      });
      const count = (f) => results.filter(f).length;
      console.log(JSON.stringify({
        source, sourceState: state ? "proposed" : "sidecar", mode, groups: groups.split(","),
        keys: ["exposureEv"], results,
        written: count((r) => r.wrote), failed: count((r) => r.error), skipped: count((r) => r.skipped),
      }));
    ' "$raw" "$targets" "${mode:-propose}" "${groups:-light,color,curve,effects}" "$state"
    ;;
  state)
    # Records whether this invocation received --state, so tests can verify
    # get_edits passes the proposed file only when asked AND it exists. Writes
    # nothing beside the RAW: the real verb is read-only, and a test asserts
    # no proposed file appears.
    echo "${state:-<none>}" > "${TMPDIR:-/tmp}/orion-mcp-test-last-edits-state.txt"
    source="as-shot"
    exposure=0
    if [ -n "$state" ]; then source="proposed"; exposure=1; fi
    echo '{"path":"'"$raw"'","source":"'"$source"'","state":{"exposureEv":'"$exposure"',"tint":0,"temperatureK":5500,"curve":{"master":[{"x":0,"y":0},{"x":1,"y":1}]}}}'
    ;;
  keys)
    echo '{"keys":[{"name":"temperatureK","type":"number","unit":"kelvin","min":2000,"max":50000,"default":5500,"absolute":true,"note":"White balance in kelvin. Absolute, not a delta."},{"name":"tint","type":"number","unit":"magenta-green","min":-100,"max":100,"default":0,"absolute":true,"note":"Placeholder range for the fake binary; the real range comes from Orion --agent keys."},{"name":"exposureEv","type":"number","unit":"EV","min":-5,"max":5,"default":0,"absolute":true},{"name":"curve","type":"array","absolute":true,"example":[{"x":0,"y":0},{"x":1,"y":1}],"note":"Tone curve control points. Composite: send the whole array, partial elements are rejected."}]}'
    ;;
  proxy)
    dir=$(dirname "$0")
    cp "$dir/fixture.jpg" "$out"
    # Records the --max this invocation actually received, under the system
    # temp dir (not the ephemeral --out dir, which the caller cleans up, and
    # not the repo tree), so tests can verify the server clamps maxPx before
    # it reaches the binary.
    echo "$max" > "${TMPDIR:-/tmp}/orion-mcp-test-last-maxpx.txt"
    echo "${region:-<none>}" > "${TMPDIR:-/tmp}/orion-mcp-test-last-region.txt"
    bytes=$(wc -c < "$out" | tr -d ' ')
    echo '{"path":"'"$out"'","width":16,"height":12,"bytes":'"$bytes"'}'
    ;;
  apply)
    # Records whether this invocation received --state, under the system temp
    # dir (not the ephemeral --out dir), so tests can verify propose_edit
    # accumulates onto an existing proposed file (and that reset:true skips
    # it) before it ever reaches the binary.
    echo "${state:-<none>}" > "${TMPDIR:-/tmp}/orion-mcp-test-last-state.txt"
    # Mirrors the real verb's contract (spec Part A): an edit key outside
    # DevelopState is a usage error, exit 2, message lists allowed keys.
    # temperatureK out of range is the same shape (exit 2), matching the
    # binary fix described in DECISIONS/#agent-mcp: absolute not a delta.
    node -e '
      const fs = require("fs");
      const editsPath = process.argv[1];
      const outPath = process.argv[2];
      const allowed = ["exposureEv", "tint", "temperatureK", "curve"];
      const base = { exposureEv: 1.0, tint: 0, temperatureK: 5500 };
      const edits = editsPath ? JSON.parse(fs.readFileSync(editsPath, "utf8")) : {};
      const unknown = Object.keys(edits).filter((k) => !allowed.includes(k));
      if (unknown.length > 0) {
        console.error(`unknown keys ${JSON.stringify(unknown)}; allowed: ${JSON.stringify(allowed)}`);
        process.exit(2);
      }
      if (typeof edits.temperatureK === "number" && (edits.temperatureK < 2000 || edits.temperatureK > 50000)) {
        console.error(`temperatureK ${edits.temperatureK} is outside 2000…50000 (kelvin, absolute not a delta)`);
        process.exit(2);
      }
      const merged = { ...base, ...edits };
      fs.writeFileSync(outPath, JSON.stringify(merged));
      console.log(JSON.stringify({ path: outPath, changed: Object.keys(edits), state: merged }));
    ' "$edits" "$out"
    ;;
  commit)
    touch "${raw%.*}.xmp"
    echo '{"sidecar":"'"${raw%.*}.xmp"'"}'
    ;;
  flag)
    echo '{"rating":4,"rejected":false}'
    ;;
  *)
    echo "usage: unknown verb '$verb'" >&2
    exit 2
    ;;
esac
