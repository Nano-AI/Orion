#!/usr/bin/env bash
# CI's vcpkg binary cache, kept to what the build uses. build.yml restores the newest
# saved cache, vcpkg builds whatever no archive matches (a compiler update changes a
# package's ABI hash), and this drops each archive the install did not use and says
# whether the cache changed, so a run saves a fresh cache only when it built something.
#
#   tools/ci-vcpkg-cache.sh CACHE BUILD MARK
#
# CACHE is VCPKG_DEFAULT_BINARY_CACHE, BUILD the CMake build directory, MARK a file
# touched before the restore: an archive newer than it was built in this run (the
# restore keeps the archives' own times). Writes changed=true or changed=false to
# $GITHUB_OUTPUT. An install that did not finish leaves the cache whole, so what it
# built before failing is still saved.
set -euo pipefail
cache=$1 build=$2 mark=$3
status=$build/vcpkg_installed/vcpkg/status
log=$build/vcpkg-manifest-install.log
out=${GITHUB_OUTPUT:-/dev/stdout}

built=$(find "$cache" -name '*.zip' -newer "$mark" | wc -l)
dropped=0
if [[ -f $status && -f $log ]] && grep -q 'All requested installations completed successfully' "$log"; then
    # An archive is <first two of the ABI hash>/<ABI hash>.zip; status names each installed package's.
    used=$(sed -n 's/^Abi: //p' "$status")
    while IFS= read -r zip; do
        grep -qxF "$(basename "$zip" .zip)" <<<"$used" || { rm -- "$zip"; dropped=$((dropped + 1)); }
    done < <(find "$cache" -name '*.zip')
else
    echo "vcpkg-cache: the install did not finish; keeping every archive"
fi
echo "vcpkg-cache: $built built, $dropped unused dropped, $(find "$cache" -name '*.zip' | wc -l) kept"
echo "changed=$([[ $built -gt 0 || $dropped -gt 0 ]] && echo true || echo false)" >> "$out"
