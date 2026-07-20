# Legalization Paper Pseudocode Index

This file is a compatibility entry point for existing Teacher/Student prompts
and `knowledge/index/skill_cards.jsonl`.  The actual algorithm cards are split
under `legalization/` so agents can inspect only the relevant mechanism family.

Use `rg -n "L7|LEGALM|overflow|lambda" knowledge/algorithms/legalization`
or similar targeted queries instead of reading every card.

## Evidence Tiers

- `checked-source-text`: checked against available source text or committed
  reference.
- `source-handle-only`: source handle is known, but exact paper text was not
  rechecked for this card.  Verify before copying literal algorithm details.
- `derived-from-openroad/context`: maps current OpenROAD behavior or repo
  context rather than a single paper.
- `implementation-hypothesis`: synthesized project mechanism; validate with
  counters and strict full-flow metrics.

## Hard Rules

- Promotion evidence must come from the full flow:
  `detailed_placement_evolve -> improve_placement_evolve -> optimize_mirroring_evolve`.
- Legalize-only metrics are diagnostics only.
- Preserve path purity: LEGALM evolves LEGALM, NEGOTIATION evolves native
  resource allocation, and DIAMOND evolves Diamond/Tetris/Abacus-style local
  legalization.
- A paper idea may be translated as an objective or data structure, but it must
  not become a hidden cross-path fallback.
- Each iteration should cite one mechanism and log counters proving that the
  mechanism ran.

## Card Map

| id | mechanism | card |
|---|---|---|
| L0 | Tetris / Diamond ordered greedy | `legalization/local_order_and_cluster.md` |
| L1 | Abacus / PlaceRow cluster legalization | `legalization/local_order_and_cluster.md` |
| L2 | Jezz incremental legalization | `legalization/local_order_and_cluster.md` |
| L3 | History-based network-flow legalization | `legalization/resource_allocation_and_flow.md` |
| L4 | BonnPlaceLegal / iterative augmentation | `legalization/resource_allocation_and_flow.md` |
| L5 | Darav-style max-movement network flow | `legalization/resource_allocation_and_flow.md` |
| L6 | NBLG / negotiation-based mixed-height legalization | `legalization/resource_allocation_and_flow.md` |
| L7 | LEGALM / linearized augmented Lagrangian legalization | `legalization/alm_and_exact_repair.md` |
| L8 | Fixed-ordering / double-row exact subproblem | `legalization/alm_and_exact_repair.md` |
| L9 | Fence-region / fragmented-row mixed-height legalization | `legalization/constraints_and_parallelism.md` |
| L10 | Parallel region legalization | `legalization/constraints_and_parallelism.md` |
| L11 | Pin-accessible / Ripple-style mixed-cell-height legalization | `legalization/constraints_and_parallelism.md` |
| L12 | Technology/region/VAC/NIMH constraint-aware legalization | `legalization/constraints_and_parallelism.md` |

## Review Checklist

- Does the proposed source diff stay on the intended legalizer path?
- Does it change a real candidate generator, objective, assignment, repair, or
  handoff mechanism instead of only constants?
- Do logs show nonzero mechanism attempts and accepts/rejects?
- Does full-flow `HPWLfinal` improve, or is the result explicitly kept only as
  a diagnostic stage donor?
- Is runtime spent on bounded search, caching, exact repair, or parallel
  evaluation rather than repeated endpoint reruns?
