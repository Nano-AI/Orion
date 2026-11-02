# Contributing to Orion

Thanks for helping. Orion is source-available, not open source, so read the
licence part first; the rest is how the project works.

## The licence, in one paragraph

[`LICENSE`](../LICENSE) lets you use Orion and change it for yourself. It lets
you fork this repository **only to send pull requests back**. It does not let
you distribute Orion, changed or not, free or paid. **By opening a pull request
you grant the project a perpetual, worldwide, irrevocable, royalty-free licence
to use, change, relicense and distribute your contribution as part of Orion**
(additional permission 2). If you can't agree to that, please don't open one.

## Before you write code

- Read [`AGENTS.md`](../AGENTS.md). Its rules apply to every change: no GPL
  code, every non-trivial filter cited in `research/`, small files, headless tests.
- Work is tracked in [issues](https://github.com/Nano-AI/Orion/issues). Say on
  one that you're taking it, so two people don't build the same thing.
- A new filter needs a published, dated source. A citation is not patent
  clearance; if the only sources are patents, ask on the issue first.

## Making the change

1. Fork, then branch: `git checkout -b <area>/<topic>`.
2. Write the test with the change and prune it:
   [`.claude/skills/testing/SKILL.md`](../.claude/skills/testing/SKILL.md).
3. Build and run the gates listed under **Tests and gates** in `AGENTS.md`.
4. Squash to one commit and write the description as
   [`.claude/skills/pull-request/SKILL.md`](../.claude/skills/pull-request/SKILL.md)
   says; the template has its sections.

One change per pull request; small ones get reviewed fastest.
