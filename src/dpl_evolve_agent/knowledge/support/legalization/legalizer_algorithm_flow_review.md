# Legalizer Algorithm Flow Review Card

## Agent Use

- Role: legalization mechanism support. Open after a selected route needs legalizer behavior, liveness, or repair detail.
- Do not optimize legalization-stage HPWL alone; final flow HPWL and DPO recoverability remain decisive.


Purpose: when Teacher reviews a legalizer-evolution iteration, it must connect
stage-wise metrics to a concrete legalizer algorithm family, not only to local
constant tuning.  Students should read this card before planning a mechanism and
should cite the specific paper/card idea used in their plan/report.

## Mandatory Review Discipline

- Name the algorithm role before coding: `primary_legalizer`,
  `guidance_producer`, `repair_or_polish`, or `handoff_consumer`.  Use
  `algorithm_role_liveness_basics.md` if the route mixes Diamond, negotiation,
  LEGALM, and Differential Guidance.
- Path purity is mandatory: LEGALM evolves LEGALM; NEGOTIATION evolves
  negotiation/resource-allocation; DIAMOND evolves Diamond/Tetris/Abacus-style
  legalization.  Do not use another line as fallback, parent, hidden donor, or
  selector branch.
- Validation is full-flow only: detailed placement -> improve placement ->
  optimize mirroring.  Legalize-only results are debug evidence, not acceptance.
- Every student plan should first define the algorithm flow/framework, then code
  the mechanism.  A patch without a flow hypothesis is not a formal iteration.
- Every teacher review should cite at least one relevant knowledge card or paper
  and explain how the paper idea maps to this path without violating path purity.
- If current knowledge is insufficient, Teacher/Student may add a short
  knowledge note with source links before implementing the next mechanism.

## Algorithm Families To Mine

### Tetris / Diamond-Style Ordered Greedy

Core idea: process cells in global-placement order, place into the closest legal
site/row, then repair overlaps with local row compaction.  This is fast and often
DPO-friendly because it preserves a simple local order.

Role: `primary_legalizer`.  Diamond can directly legalize when its local search
and repair route executes.  If it is used only to produce moved cells, dirty
rows, or touched nets for DPO, state that secondary handoff role separately.

Use on DIAMOND path:

- row/site ordering policies;
- bounded nearest-row or nearest-site candidate search;
- local row compaction and same-row reorder windows;
- exact touched-net acceptance gates for small windows.

Do not use on LEGALM/NEGOTIATION as a fallback.  If borrowed, translate only the
abstract principle, e.g. preserve local global-placement order inside a LEGALM
row assignment objective.

### Abacus / Clumping Legalization

Core idea: sort cells by global x-position, try candidate rows, and use row
clusters/clumps to compute the minimum-movement legal position for a row segment.
The important implementation pattern is not brute-force row scans; it is a
bounded row candidate list plus incremental cluster collapse.

Use by path:

- DIAMOND: direct cluster collapse / row candidate scoring.
- LEGALM: use cluster cost as a subproblem inside LEGALM row assignment, not as
  a fallback legalizer.
- NEGOTIATION: use clumps as resource intervals in a native auction/flow model.

### Min-Cost Flow / History-Based Legalization

Core idea: model placement rows/zones/sites as resources and solve assignment to
minimize deviation from global placement; history/conflict costs guide iterative
repair.

Role: `primary_legalizer` when negotiation/resource allocation closes all
violations.  If used after another clean legalizer, first prove it still emits
nonzero moved/conflict/frontier counters; otherwise it may be a no-op producer.

Use on NEGOTIATION path:

- contested resource prices;
- min-cost or auction assignment of overflow cells to rows/zones;
- history penalties for repeated conflicts;
- exact interval packing after row assignment.

Do not stop at local residual cleanup if conflicts persist; design a complete
resource-closure flow first.

### Row-Based / Min-Cut Inspired Legalization

Core idea: row-based legalization can preserve ordering and alter row membership
while trying to reduce wirelength / perturbation.  For dense cases, the right
question is which local order and row assignment leaves a DPO-recoverable basin,
not just which legalized HPWL is lower.

Use by path:

- LEGALM: global pressure -> row assignment -> ordered local buckets -> row-band
  repair -> full-flow handoff diagnosis.
- DIAMOND: local row reorder windows around high-pressure endpoints.
- NEGOTIATION: row membership changes become resource moves with prices.

### Differential / LEGALM-Style Guidance

Core idea: compute target fields, pressure, moved-target sets, or high-impact
frontiers before committing a final legal placement.  This is a heuristic
producer unless it is embedded in a full legalizer with explicit legal closure.

Role: usually `guidance_producer`, not `primary_legalizer`.  It must be paired
with Diamond, negotiation, LEGALM full closure, local repair, or DPO.  Good use
requires a stated stop point and nonzero producer/consumer counters.

Use by path:

- DIAMOND: seed local candidate ordering or DPO basin search, then let Diamond
  remain the primary legal closure.
- NEGOTIATION: seed resource prices or conflict candidates, then prove
  negotiation still runs as the primary closure.
- LEGALM: keep the paper-style guidance connected to row assignment,
  no-overflow refinement, and final full-flow handoff.
- DPO: consume residual target misses, dirty rows, or touched-net frontiers as a
  bounded priority/hot set with exact HPWL acceptance.

### Mixed-Height / Zone-Based Optimal Subproblems

Core idea: fixed ordering inside zones or adjacent-row windows can be optimized
with exact or near-optimal displacement objectives.  This is relevant when fixed
macros, cut rows, or macro-tail cases produce hard row segments.

Use by path:

- identify independent row zones split by fixed cells/macros;
- keep ordering fixed where DPO recoverability depends on locality;
- solve small adjacent-row/zone subproblems exactly or with bounded DP;
- reject if a local displacement win worsens full-flow final HPWL.

### Parallel Legalization

Core idea: legalization can be parallelized by independent regions/rows/zones
with boundary reconciliation.  Use this to allocate runtime to wider search without
serial blow-up.

Use by path:

- partition rows into non-overlapping bands for candidate generation/scoring;
- cache touched-net extrema for exact delta HPWL;
- commit accepted moves serially or through deterministic conflict resolution;
- log region count, attempted/accepted windows, and boundary rejects.

## Stage-Wise Diagnosis Patterns

- `HPWLlg improves but HPWLimprove/final worsens`: legalizer made a basin that
  DPO cannot recover.  Next flow should change order/locality/handoff, not chase
  legal HPWL alone.
- `HPWLlg worsens but final improves`: donor may be valuable if DPO recovery is
  strong; preserve the stage donor and explain why it helps downstream.
- `runtime improves but HPWL worsens`: diagnostic only unless it enables a
  larger mechanism in later iterations.
- `case-specific win with other-case regression`: add path-native self-rejection
  or feature gating; do not fallback to another algorithm family.

## Source References

- Knowledge card: OpenROAD DPL command and metric contract.
- Knowledge card: DREAMPlace/Abacus row-assignment and clumping mechanisms.
- Knowledge card: LEGALM and LEGALM 2.0 ALM-style legalization.
- Knowledge card: NBLG negotiation/resource-allocation legalization.
- Knowledge card: LEGALM search-space and HPWL objective alignment.
- Knowledge card: LEGALM paper-faithful integration.
- Source handle: fence-region-aware mixed-height legalization reference.
- External: Abacus, ISPD 2008, DOI `10.1145/1353629.1353640`
- External: History-based VLSI legalization using network flow, DAC 2010, IBM Research
- External: On Legalization of Row-Based Placements, GLSVLSI 2004
- External: A Fast Optimal Double Row Legalization Algorithm, ISPD 2021
- External: Domocus lock-free parallel legalization, 2017
- External: Toward Optimal Legalization for Mixed-Cell-Height Circuit Designs, IEEE CEDA summary
