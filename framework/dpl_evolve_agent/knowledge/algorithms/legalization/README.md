# Legalization Algorithm Cards

Use these cards before assigning or implementing a legalizer change.  The cards
are grouped by mechanism family rather than by experiment result.

Recommended lookup:

- Local greedy / cluster repair: `local_order_and_cluster.md`
- Resource allocation / flow / negotiation: `resource_allocation_and_flow.md`
- Global ALM and exact local repair: `alm_and_exact_repair.md`
- Fragmented rows, constraints, and parallelization: `constraints_and_parallelism.md`

Hard stage rule: promotion requires the full flow
`detailed_placement_evolve -> improve_placement_evolve -> optimize_mirroring_evolve`.
Legalize-only results are diagnostics, not acceptance evidence.
