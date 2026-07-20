# Skill Note: legalizer_local_greedy_hybrid

Canonical structured fields live in `knowledge/index/skill_cards.jsonl`.
Use `scripts/repo/query_knowledge.py --q legalizer_local_greedy_hybrid --show-full`
for source handles, metrics, and log signals.

## Extra Guidance

- Use local greedy logic as an internal stage of a source-level hybrid, not as a
  repeated endpoint call.
- Do not implement this skill as a case/scenario selector among complete
  legalizers.  The intended use is a staged algorithm inside one candidate:
  global/resource guidance first, native legal closure, then local HPWL repair
  or DPO handoff.
- Consume residual/frontier/conflict state from the earlier legalizer so the
  local pass repairs the right cells.
- Bound the search with candidate caps, row/segment caches, and local HPWL
  scoring.
- Good examples are:
  - lightweight global or differential target relaxation followed by local
    interval closure and DPO boundary repair;
  - negotiation conflict prices followed by exact row compaction for the
    unresolved component;
  - Diamond/Abacus local ordering used as a repair objective after global
    guidance, not as a hidden fallback legalizer.

## Common Failure

- Calling unchanged local legalization more times and reporting runtime-spent
  quality as a new mechanism.
- Selecting one legalizer family by design name instead of composing mechanisms
  and proving producer/consumer liveness.
