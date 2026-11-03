---
name: pull-request
description: Use when opening, documenting or squashing an Orion pull request, or when deciding whether a diff is a docs, code or site change and so whether it needs a pull request at all.
---

# Pull requests

The rules (change kinds, commit format, branch names) are in `AGENTS.md` under Commits and Pull requests.
This is the workflow and the description format.

## Decide the change kind

Run `git diff --name-only origin/main...HEAD`.

- Every path is Markdown, `planning/` or `research/`: docs only. Squash, push to `main`, no pull request.
- Every path is under `web/`: site. Run `./tools/check-site.py`, look headless at 390 and 1440 px, squash, push to `main`.
- Anything else: code. Follow the rest of this file.

A diff that mixes code with docs is a code change; the docs ride in its pull request.
A diff that mixes two unrelated changes is split into two branches first.

## From branch to merged pull request

1. Branch from an up-to-date `main`: `git fetch origin && git switch -c <area>/<topic> origin/main`.
2. Work and commit as often as you like.
3. Write the test, then prune it (`.claude/skills/testing/SKILL.md`).
4. Squash to one commit:
   ```sh
   git fetch origin
   git reset --soft $(git merge-base origin/main HEAD)
   git commit -s -m "area: what changed"
   ```
5. Write the description to a file outside the repository, in the format below.
6. With the worktree clean (the script refuses uncommitted changes), run `tools/open-pr.sh BODY_FILE --base main --title "area: what changed"`.
   It rebuilds, runs every gate, pushes the branch and opens the pull request with the gate lines appended.
   If a gate fails it opens nothing: fix the cause, amend the commit, run it again.
7. Wait for CI on all three systems. Read a red job before re-running it; a flaky check is fixed in its own change.
8. Merge by squash once CI is green. GitHub deletes the branch.

To update an open pull request, amend the one commit and `git push --force-with-lease`.
A docs-only fix pushed onto an open pull request skips the build.

## The description

The description documents exactly what goes into the pull request, and nothing that does not.
A reader who never opens the diff knows what changed, why, and how it was proven.

```markdown
## What this changes

One paragraph: the behaviour before, the behaviour after, and why.
Link the issue with `Closes #N`, the one place a bare number means an issue.

## Why this way

Only when an alternative was considered and rejected: what it was, and the evidence against it.

## How it was checked

The exact commands run and the last line of each.
Screenshots at 390 and 1440 px for a `web/` change.
Before and after numbers from `tools/bench.py` for a performance change.
The three-line test report from the testing skill.

## Not done

What a reader might expect here that is deliberately absent, and the follow-ups, each with its issue.

- [ ] I agree my contribution is licensed to the project under additional permission 2 of [`LICENSE`](../LICENSE)
```

`tools/open-pr.sh` appends a `### Gates` section with each gate's last line; do not write one by hand.

### Never in a description

- Local paths, home directories, user names or machine names. `open-pr.sh` trims them from gate lines; trim them from yours.
- Personal email addresses.
- Speculation written as measurement. A number appears only with the command that produced it; an untested claim says "untested".
- A narrative of the session, the order things were tried, or a note to the reviewer.
- Anything the diff does not contain.

## Review findings

- Fix each finding, or reject it with evidence: a file and line, a command and its output, or a test that shows the behaviour.
- After a fix, re-run the tests and gates it touches, then the full `open-pr.sh` set before the final push.
- A fix that changes behaviour updates "What this changes"; a rejected alternative goes in "Why this way".
- Amend the one commit; do not stack fix-up commits on the pushed branch.
