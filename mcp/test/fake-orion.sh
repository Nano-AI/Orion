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
while [ $# -gt 0 ]; do
  case "$1" in
    --out) out="$2"; shift 2 ;;
    --edits) edits="$2"; shift 2 ;;
    --state) state="$2"; shift 2 ;;
    --max) max="$2"; shift 2 ;;
    --rating) shift 2 ;;
    --reject) shift 2 ;;
    *) if [ -z "$raw" ]; then raw="$1"; fi; shift ;;
  esac
done

case "$verb" in
  stats)
    echo '{"path":"'"$raw"'","width":100,"height":80,"camera":"Fake","rating":0,"rejected":false,"clipLow":[0,0,0],"clipHigh":[0,0,0],"mean":[0.5,0.5,0.5],"temperatureK":5500,"tint":0}'
    ;;
  keys)
    echo '{"keys":[{"name":"temperatureK","type":"number","unit":"kelvin","min":2000,"max":50000,"default":5500,"absolute":true,"note":"White balance in kelvin. Absolute, not a delta."},{"name":"tint","type":"number","unit":"magenta-green","min":-100,"max":100,"default":0,"absolute":true,"note":"Placeholder range for the fake binary; the real range comes from Orion --agent keys."},{"name":"exposureEv","type":"number","unit":"EV","min":-5,"max":5,"default":0,"absolute":true}]}'
    ;;
  proxy)
    dir=$(dirname "$0")
    cp "$dir/fixture.jpg" "$out"
    # Records the --max this invocation actually received, under the system
    # temp dir (not the ephemeral --out dir, which the caller cleans up, and
    # not the repo tree), so tests can verify the server clamps maxPx before
    # it reaches the binary.
    echo "$max" > "${TMPDIR:-/tmp}/orion-mcp-test-last-maxpx.txt"
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
      const allowed = ["exposureEv", "tint", "temperatureK"];
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
