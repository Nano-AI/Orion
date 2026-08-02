#!/bin/sh
# Link the two gitignored directories a worktree needs in order to build.
#
# ⚠ This script exists because the thing it replaces destroyed the build.
#
# Agent briefs used to carry this line, so a fresh worktree could compile:
#
#     ln -s /Users/grootbeat/Documents/Orion/third_party . && ln -s .../samples .
#
# Correct in a worktree. In the *main* repo the target and the destination are
# the same path, so the entry becomes a symlink to itself and whatever was there
# is gone. On 2026-08-02 an agent ran it with the main repo as its working
# directory and took out `third_party/slang` — the Slang toolchain — and the
# `samples` directory with it. Nothing could compile afterwards.
#
# So the guard is the point of this file, not the linking. It refuses to run
# anywhere except a linked worktree, and it refuses to replace anything that is
# not already a symlink.

set -eu

fail() { printf 'worktree-setup: %s\n' "$1" >&2; exit 1; }

command -v git >/dev/null 2>&1 || fail "git not on PATH"
git rev-parse --is-inside-work-tree >/dev/null 2>&1 || fail "not inside a git work tree"

# A linked worktree has a .git *file* pointing at the main repo's gitdir; the
# main worktree has a .git *directory*. That is the check, and it is the reason
# this cannot repeat the accident: in the main repo the next line exits.
top=$(git rev-parse --show-toplevel)
[ -f "$top/.git" ] || fail "refusing to run in the main repo — this is for linked worktrees only.
  If you meant to repair the main checkout, do it by hand and read the note at
  the top of this file first."

main=$(git rev-parse --path-format=absolute --git-common-dir)
main=$(dirname "$main")
[ -d "$main" ] || fail "cannot locate the main worktree (got '$main')"
[ "$main" != "$top" ] || fail "main worktree and this worktree are the same path — refusing"

# third_party is read-only to a build, so one symlink to the whole directory is
# right: the toolchain is large and nothing writes into it.
src="$main/third_party"
dst="$top/third_party"
if [ ! -e "$src" ]; then
    printf 'worktree-setup: third_party missing in the main repo, skipping\n' >&2
else
    if [ -L "$dst" ]; then rm "$dst"
    elif [ -e "$dst" ]; then
        fail "third_party already exists here and is not a symlink — refusing to touch it"
    fi
    ln -s "$src" "$dst"
    printf 'worktree-setup: third_party -> %s\n' "$src"
fi

# ⚠ samples is DIFFERENT, and symlinking the directory is a bug.
#
# The scenarios write into the folder beside the photograph: `save
# samples/_PIC8220.xmp`, the matte PNGs, `PHOTO.orion-snapshots.json`. If every
# worktree's `samples` is one symlink to the main repo's folder, then every
# agent running `repro/*.txt` at once is writing the *same sidecar* — and on
# 2026-08-02 that is exactly what happened: `grain-survives-a-reopen.txt` failed
# once in a suite of forty, passed alone, and passed twice more in a row, while
# another agent's matte files were landing in that folder seconds apart. A
# flaky gate that is nobody's bug is worse than a red one.
#
# So each worktree gets its own **directory** of symlinks to the raw files. The
# 50 MB originals are still shared and never copied; the sidecars, mattes and
# snapshots each run writes are private to it.
dst="$top/samples"
if [ ! -d "$main/samples" ]; then
    printf 'worktree-setup: samples missing in the main repo, skipping\n' >&2
else
    if [ -L "$dst" ]; then rm "$dst"
    elif [ -e "$dst" ]; then
        fail "samples already exists here and is not a symlink — refusing to touch it"
    fi
    mkdir -p "$dst"
    n=0
    for raw in "$main"/samples/*.ARW; do
        [ -e "$raw" ] || continue
        # Resolve through the main repo's own symlink so this worktree does not
        # depend on that one surviving.
        target=$(cd "$(dirname "$raw")" && readlink "$(basename "$raw")" || true)
        [ -n "$target" ] || target="$raw"
        ln -sf "$target" "$dst/$(basename "$raw")"
        n=$((n + 1))
    done
    printf 'worktree-setup: samples -> %d raw files, private sidecars\n' "$n"
fi

printf 'worktree-setup: done. Now: cmake -S . -B build -G Ninja\n'
