# Knowledge Indexes

This directory contains machine-readable lookup records for Teacher and Student
agents.  The markdown knowledge files remain the source of detail; index records
are compact routing aids.

## Files

- `skill_cards.jsonl`: stage skills, paper cards, and support notes discoverable
  by stage, bottleneck, metric, source handle, or failure mode.
- `mechanism_stack_cards.jsonl`: compact blueprint stack records for mechanism
  composition.  These records name blueprints, producer/handoff/consumer roles,
  compatible follow-up stacks, failure buckets, handoff payloads, and first patch
  handles.

## Query Contract

Use the repo-local helper:

```bash
"$DPL_EVOLVE_PYTHON" scripts/repo/query_knowledge.py --stage teacher_review --q "Blueprint D+A stack" --limit 3
"$DPL_EVOLVE_PYTHON" scripts/repo/query_knowledge.py --stage improve_placement --q "source-edge top-K exact consumer" --limit 3
"$DPL_EVOLVE_PYTHON" scripts/repo/query_knowledge.py --validate
```

Stage aliases such as `detailed_placement`, `blueprint`, `routing`, `dpo`, and
`legalizer` are accepted for convenience, but prompt templates should prefer the
canonical stages listed in `knowledge/skills/README.md`.

## Knowledge Ladder

Teacher and Student agents should use indexes in this order:

1. Start from current evidence and the generated case feature route packet.
2. For a named Blueprint A/B/C/D/D+A or stack route, query the exact
   mechanism-stack record.
3. Read only the selected roadmap section for implementation order and source
   handles.
4. Query one stage or failure-bucket card only when a producer, handoff,
   consumer, post-consumer, runtime, or liveness link is unclear.
5. Open algorithm/support notes only after the route names the mechanism gap.

The stack record is a composition checklist.  It should help an agent decide
what to build, what to stack, what to prove, and which missing link to repair; it
does not replace source inspection or final full-flow metrics.

Keep stack records short.  If a record starts to carry detailed pseudocode,
source archaeology, or measured result tables, move that material to
`knowledge/routing/`, `knowledge/support/`, or `knowledge/algorithms/` and keep
only the query-facing summary here.
