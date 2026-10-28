#!/bin/zsh
# The windowed proof, in one command. ⚠ Opens ONE window for about 5 s, then
# exits by itself (hard cap 10 s). Prints per-phase timings and writes two
# window screenshots to shots/. Pass --gl for Avalonia's OpenGL renderer.
# Needs native/build.sh and `dotnet build -c Release` first.
set -euo pipefail
here=${0:A:h}
export DOTNET_ROOT=${DOTNET_ROOT:-/opt/homebrew/opt/dotnet/libexec}
exec "$here/bin/Release/net10.0/DawnAvalonia" --shots "$here/shots" "$@"
