# Skills

A skill is the workflow for one recurring task.
`AGENTS.md` holds the rules every task follows; a skill never restates them, it links to them.

| Skill | Use for |
|---|---|
| [`pull-request`](pull-request/SKILL.md) | opening, documenting or squashing a pull request, or deciding which change kind a diff is |
| [`testing`](testing/SKILL.md) | writing, placing or pruning the tests for any code change |

- One skill is one folder holding a `SKILL.md` with `name` and `description` frontmatter; the description says when it applies.
- Keep a skill under 150 lines.
