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

- Read [`CLAUDE.md`](../CLAUDE.md). Its hard constraints apply to every change:
  no GPL code, every non-trivial filter cited in `research/`, small files.
- [`planning/DECISIONS.md`](../planning/DECISIONS.md) records what is settled
  and why. Don't reopen a decision in a pull request; open an issue.
- Work is tracked in [issues](https://github.com/Nano-AI/Orion/issues). Say on
  one that you're taking it, so two people don't build the same thing.
- A new filter needs a published, dated source. A citation is not patent
  clearance; if the only sources are patents, ask on the issue first.

## Making the change

1. Fork, then branch: `git checkout -b fix/short-name`
2. Build and run the gates listed under **Tests** in `CLAUDE.md`. They are
   headless; the GPU tests matter most.
3. A settled choice gets a row in `planning/DECISIONS.md` with its reason.
4. Open the pull request against `main` and fill in the template.

Small pull requests get reviewed fastest. One change per pull request.
