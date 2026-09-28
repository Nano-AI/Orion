# How an agent should drive Orion - sources

⚠ **Retired surface, 2026-09-20 (#280).** This note designed `orion`, `Orion --serve`
and the agent skill. A bake-off on two real frames judged the MCP surface
(`Orion --agent`, `mcp/server.ts`, proposals approved in the editor, #244-#251)
clearly better, and the socket surface was removed. What survives of this note:
the survey in §3 (no eval verb, ever), the token arithmetic in §2 (return what
changed, never a path to go and read) and the inspection and validation sources in
§4-§5, which the MCP surface's `get_stats`, `get_proxy region` and `detect_faces`
now rest on. §1's format argument is moot: MCP replies are JSON by protocol.

The design behind `orion`, `Orion --serve` and the agent skill. Not a filter,
so the sourcing rule does not strictly bind it, but every choice below was
argued from a measurement somebody else published, and the measurements are
what a later session should re-check before undoing a choice. Researched
2026-09-13; decisions #272-#274.

## 1. The reply format: text lines, not TOON

**Claim.** Line-oriented `key value` text for an agent, compact JSON on
request, and never a compressed notation the model has to write.

- axi.md (Kun Chen, MIT) is a ten-principle design spec - minimal default
  fields, precomputed aggregates, definitive empty states, structured errors,
  exit 0/1/2, no interactive prompts, content-first output, next-step hints -
  plus TOON at stdout. Its own study (425 runs, one repository, self-judged)
  has axi at 100% task success against raw `gh` at 86%, **1.3% fewer input
  tokens** - the 3× gap it advertises is against MCP schema loading, not a
  format effect. https://github.com/kunchenguid/axi
- TOON (Schopplich, MIT, spec 4.1) reports 72.2% vs JSON 71.4% retrieval
  accuracy at 42.6% fewer tokens on its own benchmark, and says itself that
  compact JSON wins on nested data and CSV on flat.
  https://github.com/toon-format/toon
- Independent: "Notation Matters" (arXiv:2605.29676) on four agentic
  benchmarks - TOON saves up to 18% tokens **at a ~9 pp accuracy cost and
  cascades multi-turn parsing failures**. improvingagents.com ranks TOON 9th of
  12 formats on GPT-4.1-nano; Markdown key-value first.
  https://arxiv.org/abs/2605.29676 ·
  https://www.improvingagents.com/blog/toon-benchmarks/

So: the principles, yes; the notation, no. Orion's replies are small and not
tabular, which is where TOON's own benchmark says it loses.

## 2. CLI over a warm socket, MCP as a thin adapter

**Claim.** One core (the scenario grammar), a thin CLI for the loop, MCP later
over the same socket. Return what changed, never a path to go and read.

- Anthropic, programmatic tool calling: 38% fewer input tokens on fan-out; on
  sequential single-call loops "scores unchanged, cost roughly 8% more".
  Orion's edit loop is sequential.
  https://platform.claude.com/docs/en/agents-and-tools/tool-use/programmatic-tool-calling
- Anthropic, "Advanced tool use": deferred tool loading moved MCP-eval
  accuracy 49→74% (Opus 4); worked examples 72→90% on parameters.
  https://www.anthropic.com/engineering/advanced-tool-use
- Microsoft Research, 1,470 MCP servers: accuracy falls with tool count;
  flattening parameters is worth ~47%.
  https://www.microsoft.com/en-us/research/blog/tool-space-interference-in-the-mcp-era-designing-for-agent-compatibility-at-scale/
- Ranger's Playwright measurement: the CLI that returned a *file path* used
  half the tokens and cost 38% more money and 3× the time, because every
  result needed a follow-up read.
  https://outpost.ranger.net/post/the-hidden-cost-of-fewer-tokens/
- Agent Skills: a ~5 KB `SKILL.md` documents a whole tool surface at a
  one-line standing cost.
  https://www.anthropic.com/engineering/equipping-agents-for-the-real-world-with-agent-skills

## 3. No scripting eval

- blender-mcp's one write path is `execute_blender_code`; issue #207 (no
  sandbox) closed as not planned; the README's mitigation is "save your work".
  https://github.com/ahujasid/blender-mcp/issues/207
- darktable-mcp deleted its own `adjust_exposure` because darktable has no
  headless parameter readback - the capability Orion's `state` is.
  https://github.com/w1ne/darktable-mcp
- rawtherapee-mcp works because PP3 is a text document with stackable partial
  profiles; the analogue here is the sidecar plus the grammar.
  https://github.com/lucamarien/rawtherapee-mcp-server
- JarvisArt (arXiv:2506.17612), RetouchIQ (arXiv:2602.17558): the edit is a
  typed, range-declared document; masks are geometry; the critic is separate
  from the actor.

## 4. Later stories, sourced here so the decision rows can point at it

- **Images**: Claude bills ⌈w/28⌉·⌈h/28⌉ visual tokens; a 768 px preview is
  ~530. DePlot (ACL 2023) +24 points from handing the *table* rather than the
  plot; VLM-SubtleBench: composites degrade accuracy in 9 of 10 domains. So
  numbers before pixels, and two labelled images, never a composite.
  https://platform.claude.com/docs/en/build-with-claude/vision ·
  https://arxiv.org/abs/2212.10505 · https://arxiv.org/html/2603.07888v1
- **Validation**: CLIP-based aesthetic scorers encode a fidelity preference
  (arXiv:2608.23593) and an oversaturation bias (CVPR 2026, Hong et al.);
  Q-Bench+ finds pairwise A/B the one regime where a VLM beats its own
  single-image accuracy; ECCV 2024 (arXiv:2403.10854) finds text-plus-numbers
  beats image-only. Lightroom Auto already scores between two human experts
  (PhotoArtAgent, arXiv:2505.23130). Deterministic checks first, deltas
  second, A/B with numbers third, no scorer as an objective.
- **State safety**: PatchBoard (arXiv:2605.29313), validated patches over a
  schema: 84.6% vs 30.8% success at 8× fewer tokens. Claude Code's own
  checkpoints miss any mutation that bypasses the tracked surface - hence one
  journal (`Engine.edit`) for every edit.
