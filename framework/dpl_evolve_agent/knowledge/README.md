# Knowledge Base

This directory contains compact paper, algorithm, mechanism, and workflow notes
for the constrained `detailed_placement_evolve` framework.

## Route Contract

Knowledge cards are not all equal. Teacher route mapping for case-specific DPL
evolution must start from the current case evidence and the two core
case-evolution files:

1. `routing/case_feature_to_mechanism_route_map.md`
2. `routing/mechanism_reconstruction_roadmap.md`

The generated `case_feature_route_insight_packet.md` is the normal entry point
because it combines current case evidence with the core route-insight tables.
It does not score or preselect a route. Other insight files are inspiration,
mechanism support, evidence archives, or workflow contracts. They may refine a
Teacher-selected route or diagnose a failure, but they must not directly replace
the evidence-driven route decision.

## Default Reading Order

1. `../README.md`
2. `../AGENTS.md`
3. `../docs/KNOWLEDGE_CONTRACT.md`
4. `policies/evidence_policy.md`
5. `../baseline/canonical_lines.yaml`
6. `../family_variants/REFERENCE_INDEX.yaml`
7. `../calibration/source_starts/active_prepared_starts.yaml` when start
   provenance matters
8. `routing/README.md` and `support/README.md` for file roles and insight hygiene
9. generated `case_feature_route_insight_packet.md` in the current Teacher round packet
10. `routing/case_feature_to_mechanism_route_map.md` only when
   the generated packet is insufficient
11. `routing/mechanism_reconstruction_roadmap.md` only for the
   Teacher-selected route section
12. `index/skill_cards.jsonl` and `index/mechanism_stack_cards.jsonl` through
    `../scripts/repo/query_knowledge.py` only to sharpen a selected stage
    mechanism, liveness check, blueprint stack, or failure diagnosis
13. one relevant `support/dpo/`, `support/legalization/`, `support/legalm/`,
    `skills/`, or `algorithms/` file after the route needs it
14. paper PDFs and source notes under `reference/` only if needed

## Insight Hygiene

- Do not put local experiment paths, run-state paths, old program ids, raw
  result-table filenames, or benchmark-specific result rows in default
  knowledge. Translate them into design features, source mechanisms, liveness
  signals, and failure modes.
- Keep case-type routing in the two core files above. If a support card repeats
  a route idea, collapse the mechanism into the core blueprint and leave the
  support card as background evidence.
- A mechanism is strong only when same-case final `metrics.json:hpwl` improves
  with clean legality and source/log liveness. `HPWLlg` is diagnostic, not a
  standalone objective.
- Legalization-side improvements must preserve avg/max displacement, a
  DPO-recoverable local order, useful handoff state, and downstream exact
  accepted gain.
- Prepared start branches (`framework`, `diamond`, `default_negotiation`) are
  source basins, not edit-scope restrictions or universal answers.

## Core Mechanism Families

The core roadmap merges repeated ideas into three warm-start mechanism families
instead of letting many cards describe near-duplicates:

- runtime-balanced critical-frontier exact DPO stack;
- endpoint-source handoff exact consumer stack;
- low-residual current-net / vertical-frontier consumer.

Older source-edge, row-window, residual transaction, mirror, and closure ideas
should be interpreted as submechanisms inside these three families unless
current evidence proves they should become a separate route.

## Support Areas

- `support/dpo/`: DPO consumer, basin, frontier, and handoff mechanism support.
- `support/case_evolution/`: historical route support and case-evidence
  lessons not read by default.
- `support/legalization/`: legalizer role, liveness, and flow support.
- `support/legalm/`: LEGALM paper/code alignment and runtime/reference support.
- `contracts/`: experiment, metric, and Teacher/Student workflow
  contracts.
- `algorithms/`: pseudocode and background references used only when a
  selected route needs algorithm detail.
- `index/`: query-facing JSONL records for skill lookup and blueprint mechanism
  stacks.
- `skills/`: concise stage skill notes discoverable through
  `scripts/repo/query_knowledge.py`.

Use `policies/evidence_policy.md` to distinguish hard contracts from measured
observations, working hypotheses, and reference donors. Current metrics, logs,
legality, source diffs, and final full-flow HPWL override any stale card.
