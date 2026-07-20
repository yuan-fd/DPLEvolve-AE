# Skill Note: start_diamond_local

Canonical structured fields live in `knowledge/index/skill_cards.jsonl`.
Use `scripts/repo/query_knowledge.py --q start_diamond_local --show-full`
for source handles, metrics, and log signals.

## Extra Guidance

- Use the prepared `diamond` start for local closure, row-nearby repair, or when
  global methods add runtime without stage benefit.
- Diamond is a complete primary legalizer when its route executes. If a patch
  uses Diamond only as a producer of dirty rows, moved cells, or DPO priorities,
  label that handoff role separately and prove the consumer is live.
- Require a source-level mutation in scoring, candidate generation, exact DPO
  activation, or handoff; classic Diamond-equivalent behavior is not a valid
  route.
- Compare against `framework` and `default_negotiation` starts before calling
  local repair the best source parent.

## Common Failure

- Treating Diamond as forbidden or as automatically correct; both block useful
  exploration.
