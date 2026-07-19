# Runtime-Aware Final-HPWL Mechanism Priority

## Agent Use

- Role: inspiration/support only. This card must not directly select a case route or override the core blueprint map.
- First map the case through `case_feature_to_mechanism_route_map.md`; then use this card only to refine a selected mechanism, risk, or failure diagnosis.
- If this card repeats a core idea, treat the core blueprint as authoritative and keep this card as background evidence.


Evidence type: support notes on runtime cost and gain-rate for already selected
mechanism routes.  This card summarizes how to keep quality mechanisms bounded
without demoting HPWL to a runtime-only objective.

Use this card after Teacher has selected a route from the core blueprint map.
It can refine caps, cache plans, and runtime-spend decisions, but it must not
replace the Teacher-selected route or become the first route selector.

## Promotion Standard

A mechanism is first-priority only when all of the following are true:

- strict legality is clean;
- final `metrics.json:hpwl` improves against the same-case OpenROAD/DPL
  default-flow reference;
- source/log counters prove the changed producer, consumer, transaction, or
  scoring path executed;
- runtime growth is either small relative to the same-case default runtime or
  explained by accepted-gain counters, caches, caps, or deterministic parallel
  work.  Tens of seconds can be a valid quality spend on short/medium cases,
  but if the default flow already takes hundreds of seconds, Teacher may still
  spend more runtime only as a controlled quality investment: require caps,
  gain-rate stops, cache/parallel repair, small-multiplier targets, and
  accepted-gain evidence.

Legalizer-only HPWL, guard-only preservation, telemetry-only summaries, and
passive handoff metadata are not first-priority mechanisms.  They can be useful
evidence, but Teacher should not allocate most Students to them.

## Feature-Conditioned First Routes

Teacher should not use a single global priority order.  First classify the
case through `case_feature_to_mechanism_route_map.md`; then use the notes
below to decide whether the selected mechanism should spend more runtime on
quality search, add caches/caps, or pivot away from a low-gain tail.  If a case
matches multiple feature classes, the core blueprint map decides the route
spread.

### Recoverability-Shaped Legalizer Producer Plus Exact DPO Consumer

Best feature match:

- dense or high-pressure standard-cell placements;
- legalizer output has residual HPWL damage, but DPO can recover when given a
  better frontier;
- stage metrics show legalization is not enough by itself, but after-improve
  and final HPWL can respond strongly.

Reusable mechanism:

- shape legalizer output with bounded low-residual exact refinement;
- bias targets with current-net anchors, target correction, or target release
  only when they keep the legalizer state recoverable;
- expose compact frontier/residual records to improve placement;
- consume the records with exact top-K or affected-net DPO transactions.

Required liveness:

- legalizer refinement moved cells and exact legalizer HPWL gain are nonzero;
- current-net/target-correction counters are nonzero when those submechanisms
  are claimed;
- DPO exact top-K probes and accepts are nonzero;
- final HPWL improves, not only `HPWLlg`.

Why this is high priority for this feature:

This pattern has repeatedly produced large final-HPWL movement without relying
on a broad random tail.  It is a strong support pattern after the core blueprint
map has identified a recoverable frontier and the existing DPO can consume
exact candidates.

### Residual-Basin Objective Repair With Frontier-Aligned Mirror Handoff

Best feature match:

- medium dense placements with a live residual-net basin;
- a residual reorder or basin pass is already active, but its seed ranking is
  disconnected from the legalizer's recoverable frontier;
- final mirroring or final endpoint selection can preserve a better basin if
  the handoff is replaced only after positive evidence.

Reusable mechanism:

- keep a bounded raw residual-HPWL pool;
- re-rank that pool by legalizer frontier cells, touched nets, or accepted-edge
  gain;
- accept the handoff replacement only when a blended exact objective improves:
  local basin delta plus frontier-overlap gain;
- let the final mirror consumer use the new frontier only after the positive
  objective check.

Required liveness:

- residual basin probes and accepts are nonzero;
- frontier overlap cells/nets/gain are reported;
- handoff replacement is conditional and logged;
- final HPWL improves under the hard runtime budget.

Why this is high priority for this feature:

It converts an existing basin donor into a producer-consumer mechanism without
adding unbounded search.  If runtime is slightly high, trim seed rows or exact
slice size before discarding the mechanism.

### Source-Edge Or Frontier-Ranked Exact Local Consumer

Best feature match:

- locally recoverable placements where Diamond/local repair is near-competitive;
- final HPWL residue appears after improve placement or mirror;
- a small set of source-edge, shifted, dirty-row, or frontier cells explains the
  remaining HPWL.

Reusable mechanism:

- rank a compact source-edge/frontier candidate set with cheap scores;
- exact-score only the top-K candidates with affected-net or journal HPWL;
- keep legality through a local transaction and rollback;
- optionally pass accepted frontier cells to final mirror protection.

Required liveness:

- ranked candidates, exact-scored candidates, and exact accepts are all nonzero;
- accepted HPWL gain is reported separately from ranking-only telemetry;
- mirror/final-stage handoff is only credited when it preserves accepted exact
  moves. Do not route mirror guard as a standalone optimizer.

Why this is high priority for this feature:

This is often the best runtime-light donor.  However, if ranking counters are
nonzero but exact candidate/accept counters are zero, classify the result as
passive hinting or guard evidence, not a live consumer.

### Local Exact Closure For Row-Local Recoverability

Best feature match:

- mostly row-local conflicts;
- the default legalizer is not far from a good solution;
- broad global guidance disturbs the local basin more than it helps.

Reusable mechanism:

- keep Diamond/local closure or negotiation closure as the primary legalizer;
- add bounded exact same-row or adjacent-row repair around dirty rows, residual
  nets, or displacement outliers;
- score complete small windows by exact affected-net HPWL;
- stop after accepted-gain rate falls.

Required liveness:

- local probes, exact accepts, rejected legal candidates, and final HPWL are
  reported;
- the mechanism changes candidate generation or exact acceptance, not only
  thresholds or logs.

Why this is high priority for this feature:

It is cheap and stable.  It can plateau quickly, so keep at most one guard
source.  That guard should still test a bounded quality mechanism or runtime
rewrite when safe: preserve the elite behavior, but use available runtime for a
real HPWL source with caps, counters, and rollback.  Move other Students to a
different HPWL source once gains flatten.

## Medium-Priority Quality Donors

### Node-Anchored Transactional Row-Window Repair

Best feature match:

- large row-rich or wrapper-like placements;
- macro/memory-mixed or hierarchical placements where blockages fragment
  standard-cell logic into several recoverable local basins;
- residual replay or mirror handoff is live;
- row-window, source-edge, macro-edge, or frontier-local accepted transactions
  exist, but selection is fragile or plateaued.

Reusable mechanism examples:

- if residual target-miss or stage3-repair evidence exists, build a compact
  residual payload first, then consume it with residual-focus replay, residual
  diversity replay, and transactional row-window replay;
- if source-edge or endpoint residue dominates, keep a stable Diamond-like
  legal basin and use exact source-edge/global-swap scoring, selected hot
  segments, post-reorder exact polish, pair polish, and endpoint polish;
- anchor windows on high-score residual nodes, touched nets, or boundary-active
  cells rather than sliding uniformly;
- probe bounded row windows with exact transactional move/swap scoring;
- keep accepted-window and rollback counters;
- preserve mirror handoff only when it consumes accepted transaction evidence.

Use this as an evidence-targeted quality-spend family, not as a mandatory
algorithm.  A Student may instead design another local-basin or macro-edge
consumer if source evidence justifies it.  Runtime can be substantial; if the
mechanism moves final HPWL but costs too much, Teacher should ask for an
implementation-efficiency repair.  Possible levers include better candidate
selection, caches, caps, affected-net deltas, deterministic parallel scoring,
or gain-rate stops, but these are examples rather than the only valid fixes.
Do not demote this family merely because it costs tens of seconds on a
short/medium case.  Demote it when exact accepts or final-flow preservation are
missing, or when accepted delta per runtime is poor and not being repaired.

### Critical Row-Window Matching

Best feature match:

- high-pressure row windows contain useful non-adjacent pair swaps;
- local reorder exists but accepts too few windows;
- exact affected-net scoring is already cheap enough to test more pairs.

Reusable mechanism:

- seed bounded high-pressure windows;
- enumerate local pair swaps or small matching candidates;
- dry-run with cached affected-net scoring;
- serially commit exact legal improvements and prune conflicts after accepts.

This is stronger than threshold tuning and can improve final HPWL, but it is a
continuation mechanism.  Use it after a light exact consumer is live, not as the
first route for every Student.

### Affected-Net Exact Guard And Cache

Best feature match:

- exact local scoring is the runtime bottleneck;
- a quality mechanism exists but spends too much time on full-network HPWL;
- final HPWL is close to the better-quality parent and needs runtime repair.

Reusable mechanism:

- replace full-network dry-run or commit checks with affected-net journal
  deltas where semantics are equivalent;
- expose affected-edge count and cache hit/miss counters;
- keep deterministic serial acceptance.

This is usually a runtime enabler, not a standalone quality mechanism.  Promote
it as first-priority only when final HPWL is preserved or improved while runtime
falls.

## Heavy Or Secondary Mechanisms

Use these when stage evidence says the target cannot be reached by lighter
mechanisms.  Do not present them as the first default route for every case.  Do
not avoid them merely because they may take tens of seconds on short/medium
cases.  On cases whose default runtime is already very large, deeper search is
still allowed, but it must be controlled and evidence-targeted rather than an
uncapped tail.

- Scoped LSMC-style basin escape.  It is useful only after a deterministic
  descent kernel is live.  The loop must be `descent -> legal kick -> descent ->
  canonical accept/rollback`.  Broad restarts, random tails, uncapped runs on
  already-slow cases, or increasing restart count without accepted basin changes
  are low priority.
- Endpoint-level branch scoring.  It can find a quality ceiling when multiple
  complete improve-placement branches conflict, but it is expensive.  Distill
  the winning branch into a source mechanism with counters instead of stacking
  repeated endpoints.
- Residual diversity replay plus mirror-aware replay.  This can improve large
  row-rich placements, but it must be tied to accepted row-window or exact
  frontier evidence; otherwise it becomes uncontrolled replay.
- Broad global legalizer search.  Global/differential guidance is useful when
  it produces a recoverable frontier.  It is not a default replacement for
  local closure if full-flow final HPWL regresses or DPO cannot recover the
  legalizer output.

## Mechanisms To Demote

Teacher should explicitly demote these unless paired with a concrete repair:

- legalize-only wins that do not improve after-improve or final HPWL;
- guard-only reruns of an elite source without a bounded quality attempt or
  runtime rewrite;
- telemetry-only edits;
- passive handoff records with zero consumer probes or accepts;
- mirror-only ordering without accepted exact moves to protect;
- constant, threshold, or tie-break changes without a changed candidate
  generator, scoring function, transaction, or producer/consumer path;
- broad random search, repeated endpoint-like passes, uncontrolled search on
  already-slow cases, or large logs without caps, counters, and accepted-gain
  evidence.

## Teacher Query And Routing Rule

Use these lookup handles before assigning routes:

```bash
"$DPL_EVOLVE_PYTHON" scripts/repo/query_knowledge.py --stage teacher_review --q "runtime light final HPWL mechanism priority"
"$DPL_EVOLVE_PYTHON" scripts/repo/query_knowledge.py --stage improve_placement --q "source-edge top-K exact consumer affected-net"
"$DPL_EVOLVE_PYTHON" scripts/repo/query_knowledge.py --stage handoff --q "recoverability frontier residual producer consumer"
```

Then route Students as follows:

- one guard only if an elite source must be preserved;
- one or more feature-matched final-HPWL routes when the case matches multiple
  packets;
- one adjacent-stage repair route when stage metrics disagree;
- one larger quality-spend route when lighter routes plateau, when accepted
  gains justify it, or when runtime headroom is clearly available.  A
  tens-of-seconds run is acceptable if final HPWL moves and the work is
  bounded; for cases whose default runtime is already very large, require a
  small runtime multiplier or an explicit controlled-search/runtime-repair
  plan.  Runtime repair can use caches, caps, deltas, parallelism, better
  frontiers, or any source-level efficiency improvement the Student justifies
  from profiling/log evidence.

When a mechanism is live but weak, Teacher should decide whether it is a
producer failure, consumer failure, runtime implementation problem, or low-ROI
family.  Do not keep stacking small variants on a verified low-ROI mechanism.
When multiple same-case mechanisms are live and one improves final HPWL, prefer
coherent stacking or runtime repair of those mechanisms over another
threshold-only or mirror-only route.
