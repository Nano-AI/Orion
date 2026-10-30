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

# third_party is read-only to a build, so a symlink per entry is right: the
# toolchain is large and nothing writes into it.
#
# ⚠ Per ENTRY, not one link to the whole directory. While SwiftTerm was checked
# in under third_party (#266 to #380), a fresh worktree already had a real
# third_party directory holding it, so the whole-directory link this used to
# make could not be made at all - the script refused every worktree (found
# 2026-10-10). Only the gitignored entries (the Slang toolchain) need linking,
# and `git ls-files` says which those are, so anything vendored later is safe.
src="$main/third_party"
dst="$top/third_party"
if [ ! -d "$src" ]; then
    printf 'worktree-setup: third_party missing in the main repo, skipping\n' >&2
else
    if [ -L "$dst" ]; then
        fail "third_party here is a whole-directory symlink from an older run - remove it and run 'git checkout -- third_party' first"
    fi
    mkdir -p "$dst"
    for entry in "$src"/*/; do
        entry=${entry%/}
        name=$(basename "$entry")
        # A tracked entry came with the checkout and must not be shadowed.
        if git -C "$main" ls-files --error-unmatch "third_party/$name" >/dev/null 2>&1; then
            continue
        fi
        link="$dst/$name"
        if [ -L "$link" ]; then rm "$link"
        elif [ -e "$link" ]; then
            fail "third_party/$name already exists here and is not a symlink - refusing to touch it"
        fi
        ln -s "$entry" "$link"
        printf 'worktree-setup: third_party/%s -> %s\n' "$name" "$entry"
    done
fi

# ⚠ samples is DIFFERENT, and symlinking the directory is a bug.
#
# The gates write into the folder beside the photograph (the retired repro
# scenarios did too, which is how this was found): sidecars, the matte PNGs,
# `PHOTO.orion-snapshots.json`. If every
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
    # A symlink is the whole-directory link an older run made; a directory is
    # this script's own previous run, whose links are refreshed in place so a
    # rerun is harmless. Anything else is not ours and is left alone.
    if [ -L "$dst" ]; then rm "$dst"
    elif [ -e "$dst" ] && [ ! -d "$dst" ]; then
        fail "samples already exists here and is not a directory - refusing to touch it"
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
