# Mechanism Reconstruction Roadmap

## Agent Use

This file is the implementation roadmap after Teacher has selected a blueprint
from `case_feature_to_mechanism_route_map.md`.  Read only the selected blueprint
section during normal work.  Each blueprint is a producer-handoff-consumer
mechanism: preserve the mechanism shape, adapt local implementation details to
current source evidence, and prove liveness with counters.

## Shared Contracts

- Source layout is the private `dpl_evolve` tree prepared for the Student.
- Use native OpenROAD state (`Node*`, `Edge*`, segment ids, row ids, journals,
  masks, target arrays) rather than logs, strings, files, or benchmark names as
  handoff payload.
- Exact consumers score with `DetailedHPWL` or a journal/touched-net delta,
  roll back probes, and replay only positive transactions.
- Every route reports: producer count, handoff produced/consumed, exact probes,
  exact accepts, accepted HPWL gain/delta, rejects/rollbacks, pass runtime, and
  final full-flow HPWL/legality/displacement/runtime.

## Shared Source Anchors

- Flow sequence: `src/StudentAlgorithm.cpp`, `Opendp::runStudentAlgorithm`.
- Top-level state and improve schedule: `src/Optdp.cpp`,
  `include/dpl_evolve/Opendp.h`.
- Exact DPO consumers: `src/optimization/detailed_global.{h,cxx}`,
  `src/optimization/detailed_reorder.{h,cxx}`,
  `src/optimization/detailed_vertical.{h,cxx}`,
  `src/optimization/detailed_manager.{h,cxx}`,
  `src/objective/detailed_hpwl.{h,cxx}`.
- LEGALM/current-net producer: `src/LegalmGuidance.cpp`,
  `src/DifferentialGuidance.cpp`, `src/LegalmFullLegalization.cpp`,
  `src/LegalmRowAssignment.cpp`.
- Negotiation producer: `src/EvolveNegotiationRepair.cpp`,
  `src/NegotiationLegalizer.cpp`, `src/NegotiationLegalizerPass.cpp`,
  `src/NegotiationLegalizer.h`.
- Diamond/source-edge producer: `src/Place.cpp`, `src/Opendp.cpp`,
  source-edge hooks in exact global-swap code.


## Blueprint D+A: One-Student Diamond SourceTopK Hot-Frontier Warm-Up

### Use When

Use Blueprint D+A for large row-rich/control/datapath cases with little true
macro evidence, fast clean Diamond legalization, and large improve-placement
recovery potential.  The route is DPO-dominant: Diamond legalization may raise
legal HPWL modestly, then exact global-swap sourceTopK and hot-frontier reorder
create the final HPWL gain.

This is the one-Student warm-up route when the current evidence says the missing
link is exact DPO value from a Diamond basin.  Do not replace it with LEGALM,
negotiation, mirror, guard, or legalizer scoring unless the logs already prove
this exact producer/handoff/consumer chain is live and the remaining failure is
not in DPO consumption.

### Start Basin

- Start from `diamond` for the first reconstruction.
- Preserve Diamond legalization unless logs show clean DPO producer, hot handoff,
  and selected reorder are already live but final HPWL is blocked by legal-basin
  damage or displacement-tail damage.
- Do not use `default_negotiation` unless endpoint/source recoverability is the
  selected payload.  Do not use `framework` unless target-miss/current-net
  anchors are the selected payload.

### Core Mechanism Chain

A Student has implemented D+A when these five links are live in one workspace:

1. `Opendp::improvePlacement` reaches the enhanced exact global-swap path from
   the normal DPO script.  A zero intensity or density score must not silently
   route this blueprint back to legacy `gs`.
2. `DetailedGlobalSwap` scans the broad shuffled single-height source list.  For
   each source, build a compact per-source candidate pool from median/bbox target
   rows, bbox-left/center/right anchors, legal segment feasibility, displacement
   limits, and cheap outside-bbox reduction ranking.
3. Exact-score only a small top-K candidate set per source.  Probe move first,
   use swap only as fallback, roll back every probe, replay only the best positive
   transaction through the normal global-swap accept path, and reject nonpositive
   or illegal replay.
4. Before the accepted journal is cleared, append old/new touched segment ids to
   native `DetailedMgr` hot-segment state.  The frontier is cumulative across
   accepted sourceTopK transactions and is deduplicated after accumulation, not
   overwritten by the last accepted move.
5. `DetailedReorderer` consumes those hot segments first with bounded fallback.
   Log hot-frontier windows/accepts/gain separately from fallback work.

Optional accepted-node or critical-frontier payloads may be exported after this
core works, but the minimal D+A proof is sourceTopK accepts, cumulative hot
segments, and hot-segment reorder consumption.

### Parameter Tuning Rules

Tune the route for search efficiency while preserving the five-link chain.

- Runtime controls: per-source candidate pool size, per-source exact top-K,
  target-row radius, segment pruning, move-before-swap fallback, pass elapsed
  guard, accepted-delta-per-runtime stop, and fallback reorder budget.
- Do not control runtime by shrinking the source traversal to a tiny global
  sampler.  If only a small source pool or a few thousand probes appear on a
  large row-rich case, the producer is underbuilt even if the run is fast.
- If exact `gs` counters are absent or tiny, repair dispatch and source traversal
  first.  Do not tune reorder or legalizer scoring while the producer is not
  live.
- If generated/exact-scored counts are high but replay attempts or accepts are
  low, repair target-row/anchor generation, `alignPos`, exact delta sign, and
  move-before-swap replay semantics.
- If accepts and accepted delta are large but final HPWL is weak, treat it as
  producer-quality or accepted-delta washout.  Tighten ranking toward sources
  outside the median/bbox band, target rows with real segment feasibility,
  bbox-left/center/right anchors that reduce outside-bbox distance, shorter moves
  for equal expected gain, and accepted-delta density.  Do not widen caps first.
- If runtime grows before reorder/final reporting, reduce low-yield exact probes:
  smaller per-source top-K, fewer adjacent target rows, fewer swap fallbacks,
  earlier gain-rate stop, and smaller fallback reorder.  Keep broad source
  traversal and the hot-frontier reorder proof.
- If many accepts produce only one or two hot segments, repair journal capture and
  `DetailedMgr` lifetime.  This is a handoff bug, not a reorder-width problem.
- If selected reorder is live and residual HPWL remains localized, move to
  Blueprint A continuation over the same accepted/hot footprint.

### Not Part Of The Required D+A First Shot

The following are useful only after the D+A core is already live, or when Teacher
selects Blueprint A as a continuation route: critical-row micro-start,
critical-net chain assignment, exact local closure, multi-row residual
transactions, segment-local residual swaps, broad post-DPO basin search, and
legalizer repair.  They should not make the first D+A packet look like it must
implement every downstream idea before build/evaluate.

### Minimum Proof And Counters

Report these in the log and final card:

- `sources_seen`, `generated`, `exact_scored`, `replay_attempts`,
  `replay_failures`, `rollbacks`, `accepts`, `accepted_delta`, elapsed time, and
  accepted delta per runtime for sourceTopK/global-swap.
- `hot_segments` or equivalent cumulative native frontier size.
- selected reorder frontier/windows/accepts/gain split into hot-frontier and
  fallback work.
- `HPWLg`, `HPWLlg`, `HPWLimprove`, `HPWLfinal`, legality, runtime, avg/max
  displacement.

Expected successful signature: legal HPWL may rise modestly; `HPWLimprove` drops
substantially; final HPWL improves; sourceTopK counters are broad and nonzero;
hot segments scale with accepted transactions; selected reorder mostly consumes
hot-frontier windows.

### Reconstruction Sketch

```text
improvePlacement:
  preserve Diamond legalization result
  force enhanced exact gs path reachable from the normal detailed script
  for source in shuffled single-height cells:
    derive median/bbox target rows and legal bbox anchors
    rank a compact per-source candidate pool by expected HPWL reduction
    exact-score top-K with move-before-swap and rollback
    replay best positive transaction through normal accept path
    append old/new hot segments before journal cleanup
  selected_reorder(hot_segments, bounded_fallback)
  report stage HPWL, sourceTopK counters, hot-frontier counters, final legality
```

## Blueprint A: Runtime-Balanced Critical-Frontier Exact DPO Stack

### Use When

Use Blueprint A when sourceTopK/global-swap exact accepts or another producer has
created a recoverable accepted-node/hot-critical frontier, and final HPWL is
DPO/post-DPO-frontier dominated.  For one-student first-shot reconstruction on
large row-rich Diamond-friendly cases, use Blueprint D+A instead of assigning
Blueprint A separately.  This is the continuation/value-consumer stack
for large row-rich/control/datapath and wrapper-fragmented cases.  It is not the
first activation path for a missing sourceTopK producer; use Blueprint D+A when sourceTopK/global-swap counters are absent, gated, or tiny and the Student must reconstruct the full warm-up chain.  Use standalone Blueprint D only when the task is explicitly scoped to producer activation.

### Start Basin

- `diamond` or a current live Diamond donor when sourceTopK/global-swap
  exact DPO and selected reorder are already live.
- `framework` only when LEGALM/differential/row-assignment producer payload is
  explicitly the missing frontier and the exact consumer is planned.
- Avoid negotiation starts unless endpoint/source recoverability is the useful
  payload; that is Blueprint B.

### Mechanism Chain

1. Keep the exact sourceTopK/global-swap or equivalent producer live in the
   normal improve-placement flow.
2. Export accepted moves before journal cleanup: accepted nodes, touched nets if
   available, source/topK cells, old/new hot segments, accepted delta, and
   critical-frontier nodes.
3. Store the payload in `DetailedMgr` using native ids and de-dup masks.
4. Run selected-segment reorder over hot segments.
5. Run the post-DPO critical-frontier stack in `Opendp::improvePlacement`:
   critical-row micro-start -> critical-net chain assignment -> exact local
   closure -> multi-row residual transactions -> segment-local residual swaps.
6. Log HPWL before/after each link and clear hot/critical state only after the
   late links finish.

### Implementation Checklist

- `DetailedGlobalSwap`: while accepted sourceTopK moves are still in the
  journal, collect moved nodes, touched nets if available, old/new segment ids,
  and exact accepted delta.  Sort high-delta moved nodes into a critical
  frontier, set hot segments from old/new segments, and append both to
  `DetailedMgr`.
- `DetailedMgr`: provide clear/get/append APIs for accepted move nodes,
  critical-frontier nodes, and hot segments.  Keep masks for de-duplication and
  preserve lifetime through all post-DPO consumers.
- `runCriticalRowMicroStart`: consume critical frontier and hot segments; choose
  displaced single-height frontier cells; target bbox/median anchors in current
  or nearby rows; exact-score each trial through the placement journal; apply
  only positive adjusted deltas; append affected nodes/segments back into the
  native frontier.
- `criticalNetChainAssignment`: build exact two- or three-step chains as one
  transaction: seed toward bbox/median target, local partner into seed vacancy,
  optional follower into partner vacancy.  Reject nonpositive replay.
- `exactLocalClosure`: merge accepted and critical nodes, generate small reorder
  windows over hot/frontier segments, prioritize displaced and edge-touching
  windows, exact-score with rollback, and attribute gain to base versus
  micro-start/critical segments.
- `multiRowResidualTransactions`: search bounded current/nearby-row
  transactions with local partners and exact transaction replay.
- `segmentLocalResidualSwaps`: try only local neighbors around the accepted or
  critical footprint to preserve and extend earlier DPO gains.

### Parameter Tuning Rules

- Treat frontier breadth and transaction depth as the main quality/runtime
  controls.  Tune critical-frontier node cap, hot-segment cap, micro-start seed
  count, per-seed target candidates, chain partner/follower counts, exact-closure
  window count, multi-row residual probes, and segment-local residual-swap scope.
- If producer counters are live but final HPWL is flat, increase transaction
  granularity before adding unrelated producers: two/three-step chains,
  multi-row residual transactions, or pair repair over the same
  accepted/critical footprint.
- If micro-start accepts exist but chain/closure adds little, retune ranking
  toward critical-net span, accepted-delta density, displaced cells, and hot
  segments touched by the producer.  Avoid expanding broad windows that are not
  connected to the native frontier.
- If runtime grows without final HPWL, keep the highest-gain exact consumer and
  shrink lower-yield work first: broad exact closure windows, residual swaps far
  from the frontier, and fallback windows with low accepted gain per runtime.
- If a continuation consumer has zero accepts, first verify handoff lifetime and
  native id mapping.  Increasing caps on an empty or stale frontier only wastes
  runtime.

### Counters And Diagnosis

- Required counters: sourceTopK/global-swap generated/exact-scored/replayed/
  accepted/accepted-delta; accepted nodes; hot segments; critical frontier;
  selected reorder windows/accepts/gain; micro-start probes/accepts/gain;
  chain probes/accepts/gain; exact closure windows/accepts/gain; multi-row
  probes/accepts/gain; residual swap probes/accepts/gain.
- If producer counters are absent, repair Blueprint D or the producer first.
- If producer is live but consumer accepts are zero, repair native handoff
  lifetime or candidate generation.
- If accepts are live but final HPWL is flat, repair transaction granularity,
  candidate quality, or canonical replay/preservation rather than widening caps
  only.
- If only sourceTopK, selected reorder, or local closure exists, Blueprint A is
  only partially reconstructed; the full mechanism is the post-DPO
  critical-frontier stack above.

### Reconstruction Sketch

```text
source_topk_or_live_producer:
  exact-score bounded move/swap candidates
  replay only positive transactions
  export accepted nodes, touched nets, hot segments, critical nodes

critical_frontier_stack:
  selected-segment reorder on hot segments
  micro-start displaced frontier cells toward bbox/median anchors
  apply exact multi-step critical-net chains
  close merged frontier with exact local windows
  apply multi-row residual transactions
  finish with local residual swaps around accepted/critical footprint
```

## Blueprint B: Negotiation Recoverability Plus Endpoint/Source Exact Consumer Stack

### Use When

Use Blueprint B when endpoint/source residue, top-net imbalance, or
fragmented/wrapper-style legal pressure indicates that a negotiation-like legal
basin can create recoverable endpoint/source payload.  The mechanism is not a
negotiation-weight tweak: negotiation must produce a payload that exact DPO,
reorder, replay, and endpoint/source repair consume.

### Start Basin

- `default_negotiation` for first reconstruction of the resource/endpoint
  producer.
- Start from `default_negotiation` and rebuild the recoverability producer plus
  exact consumer/replay chain in the current source.
- Keep large row-rich cases eligible when endpoint/source residue is visible;
  size alone should not demote this route.

### Mechanism Chain

1. Run negotiation/resource closure as the legalizer producer.
2. Inside negotiation, rank moderate-degree high-span/top-impact nets and mark
   first/second bbox-extrema endpoint/source cells.
3. Apply a bounded recoverability bonus in target selection, damped by
   displacement growth and local slack; log selected, damped, and reverted
   choices.
4. Run exact source-edge/global-swap after the negotiation legal state.  Rank
   source-edge/boundary cells, enumerate row/segment/bbox/median/swap targets,
   exact-score bounded candidates, rollback probes, replay positives, and export
   accepted nodes, source-edge hot-cell masks, and hot segments.
5. Run hot selected-segment reorder and hot micro-pack over accepted hot
   segments/source-edge cells.
6. Store accepted reorder/micro-pack windows as replay seeds; run residual
   replay, then a cascade pass around accepted seeds.
7. Run post-reorder endpoint/source repair.  Import incoming hot segments and
   source-edge masks before they are cleared, rank top nets touching the
   frontier, derive first/second-extreme endpoint targets, and exact-score
   endpoint/source moves.
8. When single-cell endpoint moves are live but too local, add bounded pair or
   small-cluster exact transactions over the same frontier.

### Implementation Checklist

- `NegotiationLegalizer*`: recoverability producer, endpoint/source target
  identification, local slack, displacement damping, and producer counters.
- `DetailedGlobalSwap`: exact source-edge/global-swap consumer and handoff export
  from the negotiation legal state.
- `DetailedMgr`: accepted nodes, hot segments, source-edge hot-cell masks, and
  incoming handoff lifetime through reorder/replay/repair.
- `DetailedReorderer`: hot-segment reorder, hot micro-pack, accepted-window
  replay, replay cascade, endpoint/source local transactions.
- `Optdp.cpp`: pass order and preservation of incoming handoff through
  source-edge GS, reorder, micro-pack, replay, endpoint repair, and cleanup.

### Parameter Tuning Rules

- The producer knobs are negotiation top-net breadth, candidate-cell breadth,
  recoverability bonus scale/cap, slack halo, displacement-growth guard,
  displacement damping, search-window size, and negotiation phase effort.  Use
  them to create a recoverable endpoint/source payload, not to optimize legal
  HPWL alone.
- If recoverability counters are tiny, widen top-net/top-cell selection or slack
  halo and make sure endpoint/source cells are actually marked.  If preferred
  locations are often reverted, reduce the bonus, strengthen displacement
  damping, or require more local slack.
- The DPO consumer knobs are source-edge target breadth, endpoint target
  construction, exact top-K/probe budget, hot-segment and source-edge hot-cell
  handoff, hot micro-pack windows, accepted-window replay seed cap, replay halo,
  shift-candidate count, cascade breadth, and endpoint/source repair breadth.
- If source-edge exact accepts are zero, repair source-edge candidate generation
  and exact replay before increasing negotiation weights.  If hot reorder or
  micro-pack sees no windows, repair hot-segment/source-edge mask handoff.
- If runtime grows, cap replay seeds, cascade breadth, endpoint top nets, and
  broad micro-pack windows first.  Keep the negotiation producer, source-edge
  exact consumer, and at least one endpoint/source repair pass alive.
- If single-cell endpoint repair is live but final HPWL is flat, do not keep
  retuning the bonus only.  Add pair or small-cluster exact transactions over
  the same top-net endpoint/source frontier.

### Counters And Diagnosis

- Required counters: negotiation recoverability top nets/cells/scored/selected/
  damped/reverted/slack; source-edge GS candidates/exact scores/accepts/delta;
  source-edge hot masks; hot reorder windows/accepts/gain; micro-pack windows/
  accepts/gain; replay seeds/pass1/cascade/pass2 accepts; endpoint targets/
  accepts/gain; pair/cluster candidates/accepts/gain.
- Source-edge accepts zero: repair source-edge candidate generation and exact
  replay before tuning negotiation weights.
- Hot reorder/micro-pack zero: repair hot-segment export/import.
- Producer live but endpoint accepts flat: repair handoff lifetime or endpoint
  target generation.
- Endpoint accepts live but final HPWL flat: use pair/small-cluster exact
  transactions over the same top-net frontier.
- Displacement tail grows sharply: damp recoverability bonus and reject endpoint
  extensions unless exact gain pays for them.

### Reconstruction Sketch

```text
negotiation_legalizer:
  rank high-span moderate-degree nets
  mark first/second extrema endpoint/source cells
  score candidate legal sites with overflow/history + recoverability bonus
  damp or revert bonus when displacement growth outruns local slack
  publish recoverability counters and legal state

improvePlacement:
  run exact source-edge global swap on the negotiation legal state
    prioritize frontier/source-edge cells and endpoint hints
    exact-score bounded move/swap candidates
    replay positives through the normal accept path
    export accepted nodes, source-edge hot cells, hot segments, accepted delta
  selected hot-segment reorder
  hot micro-pack on endpoint/source-edge windows
  accepted-window residual replay and cascade
  endpoint/source exact repair over top nets touching the frontier
  optional pair/small-cluster exact transactions when single-cell repair is live
```

## Blueprint C: Low-Residual Current-Net / Vertical-Frontier Consumer

### Use When

Use Blueprint C for dense/high-utilization cases where LEGALM/full guidance
creates target-miss, current-net, exact-anchor, or stage3 handoff state that
downstream DPO can consume.  Density alone is not enough; require visible or
intentionally rebuilt low-residual target/current-net payload.

### Start Basin

- `framework`, because this route reconstructs the current LEGALM/full-guidance
  producer and its DPO consumer inside the current source.
- Do not select a separate non-current LEGALM start.
- If current-net/target-miss/vertical-frontier payload is absent, either rebuild
  that producer state or use another blueprint.

### Mechanism Chain

1. Run LEGALM/full guidance as a recoverable producer, not as a legalize-only
   objective.
2. Publish target rows, target sites, valid flags, exact-anchor flags,
   target-miss cells, moved/stage3 handoff cells, and current-net anchor state
   in `Opendp`.
3. Add stage3 exact/current-net refinement.  Use target rows/sites, current-net
   anchors, target-miss cells, nearby rows, and displacement-aware legality to
   apply only exact-positive producer moves.
4. In `improvePlacement`, import the target/current-net state and build compact
   row-miss, same-row residual, exact-anchor, dirty-segment, and target-miss
   frontiers.
5. Run targeted exact global and vertical wrappers over that frontier with
   rollback/replay.
6. Rank residual segments by exact-anchor count and residual magnitude; run
   selected-segment exact reorder and optional local transactions only on those
   segments.
7. Clear stale affected-net state before each `DetailedHPWL::delta` probe.

### Implementation Checklist

- `LegalmFullLegalization.cpp`: stage3 exact refinement, target-miss frontier,
  current-net anchor generation, exact scoring, producer counters.
- `Opendp.h`: target rows/sites/valid flags, exact-anchor flags, target-miss
  state, stage3 handoff counts.
- `Optdp.cpp`: imported frontier construction, row-miss and same-row queues,
  targeted global/vertical wrappers, selected-segment reorder schedule, logs.
- `detailed_vertical.{h,cxx}`: targeted vertical candidates and rollback.
- `detailed_global.{h,cxx}` and `detailed_hpwl.{h,cxx}`: targeted exact global
  moves and safe affected-net delta.
- `detailed_reorder.{h,cxx}`: selected-segment exact reorder and anchor-local
  transactions.

### Parameter Tuning Rules

- The producer knobs are LEGALM/full-guidance strength, stage3 exact-refinement
  breadth, target-row/site publication, target-valid/exact-anchor thresholds,
  target-miss marking, current-net anchor scoring, and moved/stage3 handoff cell
  retention.
- If producer counters are zero, relax target-valid or exact-anchor criteria,
  broaden row-miss/same-row quotas, and verify that target rows/sites and
  current-net anchors survive into `Opendp`.
- The consumer knobs are frontier cap, row-miss versus same-row quota,
  vertical/global score cap, accept cap, nearby-row radius, selected
  residual-segment count, reorder window count, and anchor-local transaction
  scope.
- If exact moves score but accept zero, retune target rows/sites and current-net
  anchors before increasing residual reorder.  If legal HPWL improves but final
  HPWL worsens, reduce legalizer-only pressure and improve DPO consumability.
- If runtime grows, shrink frontier breadth, selected residual windows, and
  nearby-row radius first while preserving exact anchors, target-miss cells, and
  one vertical/global exact consumer lane.

### Counters And Diagnosis

- Required counters: stage3 exact refinement enabled/frontier/scored/accepts;
  target misses; target-gain sites; current-net anchor terms/candidates/scored/
  moves; legal/static/displacement rejects; inherited handoff cells; imported
  frontier size; exact-anchor cells; target-miss cells; vertical/global scored
  moves/accepts/gain; selected residual segments/windows/transactions/accepts.
- Producer counters zero: repair target-valid/current-net publication.
- Producer live but frontier consumer sees no cells: repair `Opendp` handoff
  lifetime and id mapping.
- Frontier moves score but accept zero: improve candidate quality or exact
  objective.
- Legal HPWL improves but final worsens: the legalizer produced a DPO-hostile
  basin; repair downstream consumability rather than promoting `HPWLlg`.

### Reconstruction Sketch

```text
LEGALM/full_guidance:
  compute target rows/sites and current-net anchor terms
  mark target-valid, exact-anchor, target-miss, moved/stage3 handoff cells
  run bounded stage3 exact refinement only when exact gain is positive
  publish row-miss, same-row residual, current-net, and exact-anchor counters

improvePlacement:
  import target/current-net state from Opendp
  build compact row-miss and same-row frontier queues
  exact-score targeted global and vertical moves with rollback/replay
  accept only positive moves and append affected residual segments
  selected-segment reorder on exact-anchor/residual segments
  optional anchor-local transactions when residual segments remain concentrated
```

## Blueprint D: Diamond SourceTopK/Source-Edge Exact DPO Activation

### Use When

Use Blueprint D only when the assignment is explicitly scoped to activating the
Diamond-family exact DPO producer.  For one-student warm-up on large row-rich
cases, use Blueprint D+A instead so producer activation and post-DPO consumers
are implemented in one chain.  The typical signature is legal HPWL neutral or
worse, large potential recovery in `HPWLimprove`, and absent/gated/tiny
sourceTopK/global-swap counters.  D is a producer-reconstruction packet; it is
not the complete strong-chain route by itself.

### Start Basin

- `diamond` is the preferred basin when the target feature class is the large
  row-rich/control/datapath value route.  Preserve the fast clean Diamond
  legalizer and activate exact DPO first; a modest legal-HPWL rise is acceptable
  when the DPO producer has recoverable HPWL to consume.
- If exact DPO and selected reorder are already live but final HPWL still fails
  because legalization creates destructive displacement tails, then consider a
  bounded legal-basin repair.  Do not make legalizer scoring the first Blueprint
  D edit merely because `HPWLlg` is above `HPWLg`.
- Do not use `framework` unless a LEGALM/current-net producer is explicitly the
  selected payload.  Do not use negotiation unless endpoint/source
  recoverability is the selected payload.

### Mechanism Chain

1. In `Opendp::improvePlacement`, make the enhanced exact global-swap lane
   reachable from the normal DPO script.
2. Prevent density/intensity dispatch from falling back to weak legacy global
   swap.  A small exact-lane intensity floor is acceptable; random/congestion
   exploration may stay conservative.
3. In `DetailedGlobalSwap`, keep the all-cell global-swap pass over shuffled
   single-height cells.  Bound the per-source target list; do not cap the whole
   pass to a tiny ranked-source pool.
4. For each source cell, derive median/bbox targets with `getRange`, clip by
   displacement, and generate current/target/near-row bbox left/center/right
   anchors on legal segments.
5. Exact-score bounded per-source probes with `DetailedHPWL::delta()` on the
   current journal.  Try a legal move first; try same-width/same-height swap only
   as a fallback for a candidate that cannot be moved directly.  Roll back
   probes, replay only the best positive transaction, re-score the replay, and
   reject nonpositive or illegal replay.
6. Before accepted journal cleanup, export moved nodes, old/new segment ids, and
   accepted delta into `DetailedMgr` as native accepted-node/hot-segment state.
7. Run selected-segment reorder immediately after global swap over those hot
   segments and log frontier/windows/accepts/gain.
8. For large row-rich recoverable cases, do not stop at Blueprint D if one
   Student is responsible for the warm-up method.  Continue immediately into the
   Blueprint D+A post-DPO consumers over the same native footprint: critical-row
   micro-start, critical-net chain, exact closure, multi-row residual
   transactions, and segment-local residual swaps.  These consumers complement
   the producer; they do not replace it.

### Implementation Checklist

- `Optdp.cpp::Opendp::improvePlacement`: DPO script path, enhanced global-swap
  enablement, `DetailedMgr` setup, and handoff lifetime.
- `detailed_global.{h,cxx}`: route selection, intensity/legacy fallback,
  all-cell source iteration, per-source candidate generation, top-K exact
  scoring, rollback/replay, accepted counters, hot-segment export.
- `detailed_hpwl.{h,cxx}`: exact affected-net delta reset per probe.
- `detailed_manager.{h,cxx}`: native accepted-node and hot-segment append/clear
  APIs and lifetime until reorder consumes them.
- `detailed_reorder.{h,cxx}`: selected-segment reorder over hot segments and
  bounded residual windows.
- `detailed.cxx`: confirm the normal DPO script calls the global-swap
  implementation containing this sourceTopK kernel.

### Parameter Tuning Rules

- The producer knobs are exact `gs` dispatch, source-edge/hot-cell marking,
  source traversal breadth, per-source row and segment probes, bbox-anchor set,
  exact top-K, move-before-swap fallback, ordered-pair candidate count,
  endpoint-escape target breadth, pass elapsed guard, and accepted-delta per
  runtime stop.
- If sourceTopK/source-edge counters are absent or tiny, repair dispatch,
  intensity fallback, and source traversal first.  Do not spend the iteration on
  late closure or legalizer scoring while the exact producer is not live.
- If accepts are live but too local, add ordered-pair or endpoint-escape probes
  over the accepted/source-edge footprint.  Keep exact rollback/replay and reject
  nonpositive transactions.
- If runtime grows, reduce per-source probes, ordered-pair breadth, endpoint
  target breadth, swap fallback, and fallback reorder before reducing the source
  pass to a tiny global sample.
- If final HPWL remains weak after producer activation, either extend to
  Blueprint D+A selected-reorder/critical-frontier consumers or switch to
  Blueprint B/C only when the observed payload is endpoint/source recoverability
  or current-net/target-miss state.

### Counters And Diagnosis

- Required stage tuple: `HPWLg`, `HPWLlg`, `HPWLimprove`, `HPWLfinal`, legality,
  runtime, avg/max displacement.
- Required producer counters: cells considered, generated candidates,
  exact-scored probes, replay attempts, replay failures, rollbacks, accepts,
  accepted delta.
- Required handoff/reorder counters: accepted nodes, hot segments generated,
  hot segments consumed, selected reorder frontier/windows/accepts/gain.
- A strong large-row-rich hit shows high-throughput producer liveness: many
  generated candidates, many exact scores, many positive accepts, large accepted
  delta, and most HPWL movement by `HPWLimprove` before mirror/final cleanup.
- No sourceTopK/exact-GS counters: repair pass selection, extra-DPL enablement,
  and intensity/legacy fallback.
- Candidates exist but exact scores are tiny: repair row/segment/bbox candidate
  enumeration and per-source top-K filtering.
- Counters are live but only thousands of generated probes appear on a large
  row-rich case: remove a global ranked-source cap and restore the normal
  all-cell pass.
- Exact scores large but accepts tiny: inspect exact delta reset, displacement
  limits, move-before-swap fallback, candidate target quality, and replay
  legality.
- Accepts live but final HPWL flat: add ordered-pair/source-edge repairs or move
  to Blueprint A residual frontier consumers over the same accepted footprint;
  do not only widen source caps.
- Selected reorder has no hot frontier: repair accepted-journal capture and
  `DetailedMgr` hot-segment lifetime.
- If most code changes are LEGALM, negotiation, mirror, telemetry, guard, or
  cap-only while sourceTopK counters remain absent/small, the implementation has
  drifted away from Blueprint D.

### Reconstruction Sketch

```text
improvePlacement:
  preserve Diamond legal basin
  force enhanced exact gs path reachable from the normal DPO script
  scan broad single-height source list
    derive source-edge and bbox/median target anchors
    exact-score bounded move-before-swap probes
    replay best positive move through normal global-swap acceptance
    export accepted nodes, source-edge hot cells, old/new hot segments
  selected-segment reorder on the accepted hot frontier
  if accepts are local, add ordered-pair or endpoint-escape exact probes over
  the same source-edge footprint
```
