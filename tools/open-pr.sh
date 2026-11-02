#!/bin/bash
# open-pr.sh BODY_FILE [gh pr create args...]: build, run every gate, and open
# the pull request only when all of them pass, with their summary in its body.
#
# ⚠ It exists because the rule "a code PR goes up after the rebuild and every
# gate" (#409) lived only in prose, and #30 went up with its selftest at
# 484 passed, 8 failed. A PR opened through here carries its gate lines as proof.
#
# ORION_GUARD names a wrapper each heavy step runs under (the session's memory
# and load guard); without one they run at nice 10. ORION_SAMPLES as for the gates.
set -euo pipefail

body=${1:?usage: tools/open-pr.sh BODY_FILE [gh pr create args...]}; shift
[[ -f $body ]] || { echo "open-pr: no body file $body" >&2; exit 2; }
cd "$(git rev-parse --show-toplevel)"
# packages.lock.json is rewritten by a build without a runtime id: build noise, not a change.
[[ -z $(git status --porcelain --untracked-files=no -- . ':!shell/packages.lock.json') ]] ||
    { echo "open-pr: uncommitted changes; the gates must run on what the PR holds" >&2; exit 2; }

export DOTNET_ROOT=${DOTNET_ROOT:-/opt/homebrew/opt/dotnet/libexec}
samples=${ORION_SAMPLES:-samples}
raw=
for c in _PIC8095.ARW overexposed-background.ARW _PIC8220.ARW _PIC8148.ARW underexposedsubject.ARW; do
    [[ -f $samples/$c ]] && { raw=$samples/$c; break; }
done
[[ -n $raw ]] || { echo "open-pr: no sample RAW in $samples" >&2; exit 2; }

if [[ -n ${ORION_GUARD:-} ]]; then guard=("$ORION_GUARD"); else guard=(nice -n 10); fi
summary=$(mktemp)
step() {   # step NAME COMMAND...: runs it, keeps its last line, stops the script on failure
    local name=$1; shift
    echo "open-pr: $name" >&2
    local out; out=$(mktemp)
    if "${guard[@]}" "$@" >"$out" 2>&1; then
        # No local paths in a public PR body: the checkout and home directory are trimmed.
        echo "- ✅ $name: $(grep -v '^\s*$' "$out" | tail -1 | sed "s#$PWD/##g; s#$HOME#~#g")" >>"$summary"
    else
        tail -40 "$out" >&2
        echo "open-pr: $name failed; no PR opened" >&2
        exit 1
    fi
}

step "build engine"     cmake --build build
step "build native"     shell/native/build.sh
step "build shell"      dotnet build -c Release shell
step "orion-tests"      ./build/apps/tests/orion-tests
# Held to the same stated skip list as CI (#376): a known failure passes with its reason, any other fails.
step "selftest"         python3 tools/ci-selftest.py macos shell/bin/Release/net10.0/Orion "$raw"
step "present_test"     shell/bin-native/present_test "$raw"
for gate in gestures screens modes wiring agent site; do
    step "check-$gate" "./tools/check-$gate.py"
done
step "mcp tests"        bash -c 'cd mcp && npm test'

full=$(mktemp)
{ cat "$body"; printf '\n### Gates (tools/open-pr.sh, %s)\n' "$(git rev-parse --short HEAD)"; cat "$summary"; } >"$full"
git push -u origin HEAD
gh pr create --body-file "$full" "$@"
