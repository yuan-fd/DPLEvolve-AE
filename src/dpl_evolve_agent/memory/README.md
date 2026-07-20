# Evidence Memory

This directory contains lightweight helpers for evidence-driven evolve loops.
Runtime data is written under `$DPL_EVOLVE_STATE_ROOT/memory`, not into this
repo.

Current stores:

- `run_db.jsonl`: one structured record per Codex/evaluator attempt.
- `observations.jsonl`: factual observations extracted from RunDB records.
- `knowledge_cards.jsonl`: compact prompt fragments with evidence and scope.
- `family_region_stats.json`: posterior counters for strategy families/stages
  and target symbols.  The filename is compatibility storage only; it is not a
  separate default experiment family or prompt source.

The packet builder reads recent RunDB failures before each Codex attempt so the
next prompt can avoid repeating rejected mechanisms. The learning step updates
knowledge cards and stage/region stats after each attempt.  This directory is
part of the durable control-plane infrastructure, not a legacy family archive.
