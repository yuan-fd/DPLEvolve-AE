# Case-Type Quality Mechanism Priority

## Agent Use

- Role: direct Teacher route-insight core.
- Use this file before route assignment to map current case evidence to a small
  set of warm-start mechanism families.
- The routes below are cumulative mechanism stacks.  Do not reduce them to a
  final guard, one cap change, one late polish pass, or a benchmark-name lookup.
- Teacher maps from current metrics and physical features; Student reconstructs
  the selected mechanism in its own workspace from the matching roadmap section.
- After selecting a route, open only the matching section in
  `mechanism_reconstruction_roadmap.md` and put that concrete packet in the
  Student plan.
- Every selected blueprint needs a completeness audit in the Teacher plan:
  start basin, producer payload, handoff lifetime, exact consumer,
  post-consumer closure, liveness counters, which links already exist, and which
  missing link each Student will implement.  A partial route is useful evidence,
  but should not be reported as the full warm-start mechanism.

This card is intentionally a compact route map.  The implementation roadmaps
live in `mechanism_reconstruction_roadmap.md`.

## Teacher Classification Checklist

Before assigning Students, state the current evidence on these axes:

1. Stage tuple: `HPWLg`, `HPWLlg`, `HPWLimprove`, `HPWLfinal`, legality,
   runtime, avg/max displacement.
2. Size/runtime scale: movable count and default-flow runtime.  Long-default
   cases can still spend runtime for quality, but added passes must expose
   accepted gain per runtime.
3. Density/row pressure: utilization, row fragmentation, scarce legal sites,
   and whether the legalizer creates a recoverable or destructive basin.
4. Physical structure: true macro/SRAM/large-blockage evidence from geometry,
   not fixed-instance count alone.  Taps/endcaps/fillers are fixed cells but not
   macro evidence.
5. Stage behavior: whether final HPWL is dominated by legalizer output,
   producer-handoff quality, exact DPO consumption, post-DPO closure, or
   canonical preservation.
6. Live counters: producer cells/frontier, handoff produced/consumed, exact
   probes, exact accepts, accepted delta/gain, replay rejects, pass runtime, and
   accepted gain per runtime.

If several feature classes match, assign different Students to different
reconstruction hypotheses.  It is also valid to assign multiple Students to the
same strong route when they test different start basins, move families,
objectives, or repair hypotheses.  The strongest route must keep chasing final
HPWL; do not freeze it into guard-only, attribution-only, or runtime-only work.

## Feature-Mechanism Map

### Validated Warm-Start Bundles

Teacher should first classify the case by observable features, then map to one
or more of these mechanism bundles.  These are method insights, not donor
names, benchmark lookups, or hard-coded selections.

- Endpoint/source negotiation quality stack: use when a standard-cell,
  control/datapath, wrapper, or large row-rich case has recoverable high-span
  endpoint/source residue after legalization or early DPO.  Start from
  `default_negotiation` for first reconstruction.  Rebuild the whole Blueprint B
  chain: recoverability-ranked negotiation producer, source-edge exact global
  swap, hot-segment reorder, hot micro-pack, accepted-window replay/cascade,
  endpoint/source repair, and pair/small-cluster exact transactions when
  single-cell endpoint repair is live but too local.
- Diamond sourceTopK exact DPO value stack: use when clean Diamond legalization
  is fast/clean but may increase legal HPWL, and improve placement shows a
  broad recoverable basin.  The strong one-shot route starts from `diamond` and
  first makes DPO, not the legalizer, do the large HPWL work: force the enhanced
  exact `gs` path live, scan the full single-height source list, build compact
  per-source bbox/median target rows and left/center/right anchors, exact-score
  only a small top-K with move-before-swap replay, accept through the normal
  global-swap objective, export hot segments from every accepted journal, and
  immediately consume them with selected-segment reorder.  Legal HPWL rising by
  a small amount is acceptable for this signature if the sourceTopK accepted
  delta and final HPWL move strongly.  Do not send the first one-student route
  to `Place.cpp` legalizer repair unless exact-GS dispatch, sourceTopK replay,
  hot-segment handoff, and selected reorder are already live and the remaining
  diagnosis explicitly points to legal-basin damage.
- Low-residual current-net/vertical-frontier stack: use when dense placement
  exposes target-miss/current-net/exact-anchor state that detailed placement can
  consume.  Start from `framework` and reconstruct Blueprint C: LEGALM/full
  guidance as a recoverable producer, stage3 current-net anchors, target
  rows/sites/valid flags, targeted exact global/vertical moves, and
  selected-segment residual reorder.
- Small-control Diamond ordered-pair escape stack: use as a secondary route for
  small low-utilization control/datapath cases when Diamond exact producer
  counters are live but single-cell accepts are too local.  Start from
  `diamond` and add ordered-pair, endpoint-escape, and accepted-footprint
  arbitration over the same exact sourceTopK journals.

| current evidence pattern | primary warm-start method | blueprint | first start basin | first Student packet |
| --- | --- | --- | --- | --- |
| Short/medium standard-cell or control/datapath case with little true macro geometry, visible top-net endpoint/source residue after legal closure or early DPO, and enough runtime budget for a quality lane.  Legal HPWL may be neutral or worse; the useful signal is that endpoint/source cells on high-span nets can be turned into exact accepted moves and preserved through downstream passes. | Negotiation recoverability plus endpoint/source exact consumer stack | `B` | `default_negotiation` | Rebuild the full chain: source-edge exact global swap -> hot-segment reorder -> hot micro-pack -> recoverability-ranked negotiation producer -> accepted-window replay/cascade -> endpoint/source target repair.  If single-cell endpoint repair is live but flat, add bounded pair/small-cluster exact transactions over the same frontier. |
| Large row-rich/control/datapath or runtime-sensitive case with little true macro evidence, nontrivial default runtime, clean Diamond legalization that is fast/clean but may raise legal HPWL, and very large improve-stage recovery.  The decisive first question is whether one Student can turn that Diamond basin into a broad exact-DPO value chain.  If Diamond legalization is much faster than default or negotiation and DPO has room to recover, treat this as a Diamond value-lane signature rather than a negotiation or legalizer-repair signature. | One-student Diamond sourceTopK core chain | `D+A` fused; `B` only as a secondary quality lane when multiple Students are available and endpoint/source residue is explicitly visible | `diamond` for the first Student; do not use `default_negotiation` as the only route for a one-Student first shot | Query `Blueprint D+A` and implement the core chain from `diamond` before optional consumers: force exact sourceTopK/global-swap lane live -> broad full-source pass with ranked per-source bbox/median target rows and bbox-left/center/right anchors -> bounded exact top-K replay with move-before-swap probing -> rollback/replay positive transactions through the normal accept path -> cumulative hot-segment handoff from every accepted journal -> selected-segment reorder over the hot frontier with bounded fallback.  Raw accept count is only liveness; if accepted delta is large but final HPWL regresses, repair producer ranking, accepted-delta washout, handoff lifetime, or selected-reorder consumption before widening consumers. Add critical-row/critical-net/multi-row/residual-swap continuation only after the core builds, evaluates, and exposes usable counters. |
| Large row-rich/control/datapath or runtime-sensitive case where source-topK/global-swap exact accepts are already live with large accepted delta, but final HPWL still has a hot/critical frontier after improve placement.  This is a continuation route after the exact DPO producer is proven live; if sourceTopK is absent, gated, or weak and only one Student is assigned, use Blueprint D+A rather than a downstream-only continuation. | Runtime-balanced critical-frontier exact DPO continuation stack | `A` | `diamond` when the exact DPO producer already runs and the missing work is residual frontier conversion; `framework` only when a recoverable LEGALM/frontier producer is explicitly the missing link | Continue the cumulative exact-consumer chain after the producer is live: accepted-node and hot/critical-segment native handoff -> critical-row micro-start -> critical-net chain assignment -> exact local closure -> multi-row residual transactions -> segment-local residual swaps.  Do not use this continuation route to replace a missing sourceTopK producer. |
| Dense/high-utilization case where legalization exposes low-residual target-miss or current-net anchors, and downstream improve placement can consume target rows/sites, exact anchors, moved cells, row-miss cells, or selected residual segments.  Density alone is not enough; the low-residual handoff must be visible or intentionally rebuilt. | Low-residual current-net / vertical-frontier consumer | `C` | `framework` | Keep LEGALM/full guidance as a recoverable producer, publish target rows/sites/valid flags and stage3 current-net anchors, build a compact row-miss/same-row residual frontier, then consume with targeted exact global/vertical moves and selected-segment reorder. |
| Low-utilization, row-rich, Diamond-friendly, or large-control/datapath case where clean Diamond legalization may be neutral/worse but improve placement has a broad recoverable basin, and sourceTopK/global-swap counters are absent, gated, or too small.  The decisive signal is missing exact-DPO producer/consumer activation, not macro count or legal HPWL alone. | Diamond sourceTopK/source-edge exact DPO activation stack | `D` | `diamond` is the preferred reconstruction basin for the strong route. | Rebuild the exact sourceTopK/global-swap producer first: force the exact lane live, enumerate bounded row/segment/bbox-anchor move probes with swap only as fallback, exact-score top-K with rollback/replay, export accepted nodes plus old/new hot segments, and immediately consume them with selected-segment reorder.  Treat post-DPO closure, endpoint escape, pair repair, or critical-frontier chains as secondary repairs only after sourceTopK counters and accepted delta are large. |
| Hierarchical wrapper / macro- or SRAM-heavy / fragmented-row case where legalizer HPWL rises but improve placement recovers a large fraction. | Assign both a quality lane and a value lane | `B` plus `A`; use `D` only for missing exact-producer activation | `default_negotiation` for the endpoint/source quality lane; `diamond` for the value lane, either with live sourceTopK/global-swap or with producer activation from scratch | Use Blueprint B if endpoint/source residue on top nets is visible.  Use Blueprint A when accepted-node or hot-critical frontier state is already live and needs downstream conversion.  Use Blueprint D only when sourceTopK/global-swap counters are absent, gated, or tiny.  A same-basin late-consumer value route is acceptable for wrapper-like cases only when it expands an already proven broad touched-net footprint with grouped exact replay, not when it replaces the producer.  Do not overfit to fixed-instance count; use geometry and row fragmentation. |

## Route Map

| blueprint | method name | use first when | use as alternative when | do not use when |
| --- | --- | --- | --- | --- |
| `A` | Runtime-balanced critical-frontier exact DPO continuation stack | source-topK exact accepts are already live and the assignment is explicitly downstream-only; residual hot/critical segments or accepted-node frontier dominate final HPWL | endpoint/source residue exists but negotiation producer evidence is weak, or the case needs a frontier-consumer lane beside a heavier quality lane | one-student first-shot warm-up or absent/gated/tiny sourceTopK; use Blueprint D+A rather than building closure around an empty producer |
| `B` | Negotiation recoverability plus endpoint/source exact consumer stack | endpoint/source residue is visible and a negotiation-like legal basin can produce recoverable top-net payload | row-rich/control/wrapper-like cases can spend quality runtime on this full chain beside Blueprint A | the plan only changes negotiation weights without downstream exact consumers, or the handoff is passive metadata never consumed by DPO/reorder |
| `C` | Low-residual current-net / vertical-frontier consumer | target-miss/current-net/stage3 handoff evidence exists or can be rebuilt in the current `framework` producer | a dense case needs one producer-consumer diagnostic lane while another Student tries Blueprint A | density is high but target-miss/current-net/vertical-frontier evidence is absent |
| `D+A` | One-student Diamond sourceTopK core chain | large row-rich/control/datapath case needs the strong warm-up method from a fast clean Diamond basin and only one Student is assigned; sourceTopK may be absent, gated, or weak, but the Student must implement producer activation, hot-segment handoff, and selected reorder before optional continuation consumers | multi-Student rounds may still assign another lane, but the strongest route should remain this fused chain until counters prove the missing link | endpoint/source recoverability is the named payload (use B), low-residual current-net anchors are the named payload (use C), or the exact D+A chain is already fully live and only a small local repair is needed |
| `D` | Diamond sourceTopK/source-edge exact DPO activation | sourceTopK/source-edge search is absent, gated off, or weak and the task explicitly scopes only producer activation | a low-utilization row-rich/control/datapath case needs an exact DPO lane independent of LEGALM or negotiation; ordered-pair and endpoint-escape are later repairs after single-cell exact accepts are live | one-student warm-up on the large row-rich Diamond/sourceTopK signature; use Blueprint D+A so the Student does not stop before post-DPO consumers |

### Route-Level Parameter Adjustment Map

Use these rules after selecting the route.  They are not hard-coded case
selectors; they tell Teacher and Student which knobs to inspect when the route is
correct but runtime or search efficiency is wrong.

- `D+A`: tune the exact-lane dispatch floor, full-source coverage, per-source
  candidate pool, exact top-K, target-row radius, bbox-left/center/right anchor
  set, move-before-swap fallback, pass elapsed/gain-rate stop, hot-segment
  lifetime, and selected-reorder hot/fallback budgets.  If runtime grows before
  reorder/final reporting, shrink per-source candidates, target-row radius,
  fallback swap probes, and low-yield fallback reorder while keeping broad source
  traversal.  If counters are large but final HPWL is weak, do not widen caps
  first; tighten source ranking toward outside-bbox reduction, legal target-row
  feasibility, accepted-delta density, and consumer preservation.
- `A`: tune accepted/critical frontier size, hot segment selection, micro-start
  seeds, per-seed candidate count, chain partners/followers, exact-closure
  windows, multi-row residual probes, and residual-swap scope.  If the producer
  is live but final HPWL is flat, spend more budget on higher-granularity
  transactions over the same frontier.  If runtime grows without quality, cut
  broad closure windows and low-gain residual swaps before cutting the strongest
  critical-chain consumers.
- `B`: tune negotiation top-net/top-cell breadth, recoverability bonus scale and
  cap, slack halo, displacement damping, negotiation search windows, source-edge
  exact target breadth, hot micro-pack windows, accepted-window replay seed caps,
  replay halo/shift candidates, cascade breadth, and endpoint/source repair
  breadth.  If displacement tails or legal damage grow, lower the bonus or
  strengthen damping rather than removing the endpoint producer.  If the payload
  is too small, widen recoverability top nets/cells or source-edge targets before
  adding unrelated DPO work.
- `C`: tune LEGALM/current-net producer strength, stage3 exact-refinement
  frontier, target-valid/exact-anchor thresholds, row-miss and same-row quotas,
  vertical/global score caps, accept caps, and selected residual-segment windows.
  If producer counters are zero, relax the handoff/frontier criteria and repair
  target/current-net publication.  If runtime grows, cap frontier breadth and
  selected residual windows while preserving exact anchors and target-miss cells.
- `D`: tune exact `gs` reachability, source-edge hot-cell tagging, source
  traversal breadth, per-source row/segment probes, exact top-K, ordered-pair
  search, endpoint-escape targets, and hot-segment export.  If sourceTopK is
  absent or tiny, repair dispatch and source traversal.  If exact accepts are
  live but local, add ordered-pair or endpoint-escape over the accepted footprint
  before switching start basins.

## Blueprint Selection Rules

- Treat each blueprint as a complete mechanism chain.  A plan that names only a
  guard, local polish pass, cap change, or telemetry is not a reconstruction
  route.
- If Teacher assigns Blueprint A/B/C/D or D+A, the plan must name the matching roadmap
  section and tell Student to query that exact section before editing.  The
  Student packet should contain the route summary, but the selected roadmap
  section is the source-level checklist for implementation and self-diagnosis.
- Do not infer negotiation from large `HPWLimprove` alone.  Use Blueprint B when
  endpoint/source/top-net residue and a recoverable negotiation-like basin are
  visible or worth testing.
- When endpoint/source residue is visible on a short/medium or fragmented-row
  case, allocate at least one Blueprint B lane to the full cumulative chain even
  if Blueprint A is also plausible.
- For large row-rich/control/datapath cases where Diamond legalization is fast
  and clean but final HPWL is dominated by improve-placement recovery, the first
  one-Student warm-up route should be Blueprint D+A from `diamond`: the Student
  must reconstruct exact sourceTopK producer activation, export cumulative
  hot-segment/accepted-footprint state, consume it with selected-segment reorder,
  and only then add exact local closure or continuation consumers.  A small
  legal-HPWL rise is not by itself a reason to abandon this route or force a
  `Place.cpp` legalizer rewrite.  Do not choose `default_negotiation` as the
  sole initial route just because negotiation also has improve-stage recovery.
  Negotiation is secondary unless endpoint/source residue is explicitly the
  missing payload.
- For large row-rich/control/datapath cases after sourceTopK/global-swap is live
  but the warm-up method is not complete, keep the same Student or route on
  Blueprint D+A completion rather than splitting responsibility unnecessarily.
  Blueprint A continuation from `diamond` is only a continuation packet
  when the core chain is already proven live: critical-row micro-start,
  critical-net chain assignment, multi-row residual transactions, and
  segment-local residual swaps over the same accepted/hot footprint.  Do not let
  late closure, guard work, or cap retuning crowd out producer-handoff-consumer
  reconstruction.
- For this feature class, local accepted-footprint closure alone is only one
  late consumer link.  It must be attached to the sourceTopK producer and native
  handoff; add heavier critical-frontier consumers only after the core chain
  builds, evaluates, and identifies the residual frontier.
- When DPO recovery is broad but sourceTopK/global-swap counters are absent,
  gated, or tiny, allocate Blueprint D+A for a one-student warm-up so the same
  Student activates the producer and immediately consumes its native frontier.
  Allocate standalone Blueprint A only after exact producer liveness is already
  proven and the route is explicitly downstream-only.
- When the exact global-swap/source-topK lane is blocked by an intensity or
  dispatch fallback, treat the route as not executed.  Repair the gate before
  concluding the mechanism is weak.
- When low-residual target-miss/current-net anchors are visible, Blueprint C is
  the producer-consumer route.  Do not use it as a generic dense-case default.
- When Diamond/source-edge hot tags or missing sourceTopK counters are the main
  signal, Blueprint D is the source-topK/source-edge exact route.  Do not
  replace it with LEGALM, negotiation, Blueprint A closure, or guard work unless
  current counters show those producers are active, consumed, and stronger.
- If a route has exact accepts but final HPWL is flat, do not only widen caps.
  Repair candidate quality, native handoff lifetime, transaction granularity,
  or canonical replay/restore.
- If runtime grows and final HPWL is flat, keep one Student on quality expansion
  of the strongest route and assign another to a different move family, payload,
  exact objective, or start basin.  Runtime repair matters only when it frees
  budget for stronger quality search.
- If a route plateaus after one or two rounds, Teacher review must reopen this
  feature map and explicitly decide whether the missing link is producer,
  handoff, consumer, objective, move family, or preservation.
- If a Student claims a blueprint but the result is weak, review the code and
  counters against the selected roadmap checklist.  Continue or repair the route
  only when the missing link is named; otherwise switch start basin or mechanism
  family rather than repeating the same partial implementation.
- If the first or second wave on a validated case type remains far below the
  expected mechanism scale, Teacher must ask whether the Student implemented the
  complete warm-start bundle in one route packet.  Common failures are starting Blueprint A before
  Blueprint D makes the sourceTopK producer live, starting from negotiation but
  omitting the endpoint/source replay stack, and treating accepted counters as a
  win when final HPWL does not move.

## Student Packet Requirements

When Teacher selects a blueprint, the Student plan must include:

- route label and blueprint letter;
- start basin and why current evidence supports it;
- selected stack/roadmap query, e.g. `08_query_knowledge.sh --stage
  teacher_review --q "Blueprint B stack"`;
- complete-chain audit: producer, handoff, exact consumer, post-consumer
  closure, counters, already-live links, missing links, and this iteration's
  repair target;
- full producer-handoff-consumer chain, not one late polish pass;
- for Blueprint D+A, the packet must list the core first-pass links explicitly:
  fast clean Diamond start, broad full-source per-source top-K exact sourceTopK/global-swap producer,
  rollback/replay positive transactions through the normal accept path,
  cumulative native hot-segment/accepted-footprint handoff, and hot-frontier
  selected-segment reorder with bounded fallback.  Exact local closure and
  critical-frontier consumers are follow-on repairs unless the source surface is
  already ready.  The plan must state that exact replay is broad but
  bounded per source: full-source traversal, small ranked candidate pools,
  exact-probe caps, target-segment pruning, and gain-rate stop so the full
  detailed-placement flow reaches final HPWL.  The plan must also state that many exact accepts with a
  one-node/two-segment surviving frontier is a broken handoff lifetime, not a
  validated consumer.  Critical-row,
  critical-net, multi-row, and residual-swap consumers are continuation links;
  assign them only after the core builds/evaluates or when the current source
  already exposes a clean implementation surface;
- for Blueprint A continuation, the packet must state that sourceTopK/global-swap
  accepts and hot/critical state are already live; then it can target
  critical-row micro-start, critical-net chain assignment, multi-row residual
  transaction, and segment-local residual swap over that existing footprint;
- first source files/functions to inspect and edit;
- required counters and log names;
- self-diagnosis rule after build/evaluation;
- one strengthened repair before finalization if the first attempt is weak;
- next-handoff note if the route still fails after the repair.

Student should inspect current source around the assigned handles and may
redesign details, but the final report must prove which route links are live.

## Common Demotion Rules

Downweight these unless current same-case final HPWL and liveness prove value:

- guard-only, attribution-only, telemetry-only, comment-only, or preserve-only
  changes;
- passive handoff metadata that is never consumed by DPO, reorder, vertical, or
  a post-DPO pass;
- legalize-only or `HPWLlg`-only wins that worsen displacement or are erased by
  downstream flow;
- mirror-only priority changes without upstream exact accepted moves;
- repeated endpoint/source-edge cap widening when final HPWL is flat;
- exact candidates scored but never replayed/applied with positive exact
  delta;
- proxy-positive moves accepted without journal/touched-net exact scoring;
- final preservation-only changes treated as the quality mechanism.

## Relationship To Other Cards

- Read this card first for route selection.
- Read `mechanism_reconstruction_roadmap.md` only for the selected blueprint.
- Use other `case_evolution/`, `dpo/`, `legalization/`, and `legalm/` cards as
  support after the route is selected, not as competing route selectors.
