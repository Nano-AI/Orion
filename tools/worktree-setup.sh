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

for name in third_party samples; do
    src="$main/$name"
    dst="$top/$name"

    if [ ! -e "$src" ]; then
        printf 'worktree-setup: %s missing in the main repo, skipping\n' "$name" >&2
        continue
    fi

    # Never clobber real content. A symlink may be replaced (it is ours, or it
    # is stale); a directory or file may not.
    if [ -e "$dst" ] || [ -L "$dst" ]; then
        if [ -L "$dst" ]; then
            rm "$dst"
        else
            fail "$name already exists in this worktree and is not a symlink — refusing to touch it"
        fi
    fi

    ln -s "$src" "$dst"
    printf 'worktree-setup: %s -> %s\n' "$name" "$src"
done

printf 'worktree-setup: done. Now: cmake -S . -B build -G Ninja\n'
