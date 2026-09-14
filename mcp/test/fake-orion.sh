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
while [ $# -gt 0 ]; do
  case "$1" in
    --out) out="$2"; shift 2 ;;
    --edits) edits="$2"; shift 2 ;;
    --state) shift 2 ;;
    --max) shift 2 ;;
    --rating) shift 2 ;;
    --reject) shift 2 ;;
    *) if [ -z "$raw" ]; then raw="$1"; fi; shift ;;
  esac
done

case "$verb" in
  stats)
    echo '{"path":"'"$raw"'","width":100,"height":80,"camera":"Fake","rating":0,"rejected":false,"clipLow":[0,0,0],"clipHigh":[0,0,0],"mean":[0.5,0.5,0.5]}'
    ;;
  proxy)
    dir=$(dirname "$0")
    cp "$dir/fixture.jpg" "$out"
    bytes=$(wc -c < "$out" | tr -d ' ')
    echo '{"path":"'"$out"'","width":16,"height":12,"bytes":'"$bytes"'}'
    ;;
  apply)
    # Mirrors the real verb's contract (spec Part A): an edit key outside
    # DevelopState is a usage error, exit 2, message lists allowed keys.
    node -e '
      const fs = require("fs");
      const editsPath = process.argv[1];
      const outPath = process.argv[2];
      const allowed = ["exposureEv", "tint"];
      const base = { exposureEv: 1.0, tint: 0 };
      const edits = editsPath ? JSON.parse(fs.readFileSync(editsPath, "utf8")) : {};
      const unknown = Object.keys(edits).filter((k) => !allowed.includes(k));
      if (unknown.length > 0) {
        console.error(`unknown keys ${JSON.stringify(unknown)}; allowed: ${JSON.stringify(allowed)}`);
        process.exit(2);
      }
      const merged = { ...base, ...edits };
      fs.writeFileSync(outPath, JSON.stringify(merged));
      console.log(JSON.stringify({ path: outPath, changed: Object.keys(edits) }));
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
