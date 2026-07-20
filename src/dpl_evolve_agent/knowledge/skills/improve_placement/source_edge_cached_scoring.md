# Skill Note: dpo_source_edge_cached_scoring

Canonical structured fields live in `knowledge/index/skill_cards.jsonl`.
Use `scripts/repo/query_knowledge.py --q dpo_source_edge_cached_scoring --show-full`
for source handles, metrics, and log signals.

## Extra Guidance

- Use source-edge, touched-net, dirty-row, or pressure-window evidence to build
  a compact candidate set.
- Score cheaply first, then run exact affected-net HPWL on only the top-K
  candidates.
- Keep all state implementation-local and add aggregate counters for generated,
  filtered, exact-scored, accepted, and rejected moves.

## Common Failure

- Candidate filtering runs but exact-scored or accepted counters stay zero.
- The cache saves runtime but changes no after-improve HPWL, which means the
  candidate source or objective needs revision.
