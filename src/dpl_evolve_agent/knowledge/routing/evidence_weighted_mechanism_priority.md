# Evidence-Weighted Mechanism Priority

## Agent Use

- Role: inspiration/support only. This card must not directly select a case route or override the core blueprint map.
- First map the case through `case_feature_to_mechanism_route_map.md`; then use this card only to refine a selected mechanism, risk, or failure diagnosis.
- If this card repeats a core idea, treat the core blueprint as authoritative and keep this card as background evidence.


Evidence type: support notes for weighing a selected mechanism by liveness,
final-HPWL evidence, and risk.  It is not a first-route selector.

Use this card after the core blueprint map has produced one or more route
hypotheses.  It helps decide whether a partial mechanism should be continued,
repaired, or demoted.  Pair it with
`runtime_light_final_hpwl_mechanism_priority.md` only to refine runtime control
for the selected route.

## Evidence Gate

A mechanism is `strong` only for a target case after the external evaluator
shows:

- clean legality,
- lower final `metrics.json:hpwl` than the same-case OpenROAD/DPL default-flow
  reference,
- source/binary/log evidence that the changed mechanism executed,
- stage-wise evidence explaining whether the gain came from legalization,
  improve placement, their handoff, or complete-flow endpoint selection.

Legalization-stage HPWL improvement alone is not a strong mechanism.  If
`HPWLlg` improves but `HPWLfinal` does not, classify it as `stage donor` or
`consumer failure`, then route the next work to DPO/handoff repair.
Also check avg/max displacement and DPO exact accepted gain: a lower `HPWLlg`
with larger displacement tails or weaker DPO accepted_delta is a proxy-repair
failure, not a legalizer donor.

## Final-HPWL-Proven Mechanism Patterns

### 1. Producer Frontier Plus Exact Consumer

The strongest repeated final-HPWL pattern is a producer-consumer chain:

- legalization creates a compact residual/frontier signal,
- the signal names cells, nets, rows, windows, or pressure components that are
  still recoverable,
- improve placement consumes that signal with exact HPWL probes,
- accepted moves are committed through a local transaction/rollback guard.

This is stronger evidence than a legalizer-only improvement only when the
current case's final HPWL improves.  If producer counters are live but exact
consumer accepts are absent or final HPWL is worse, preserve it as a stage donor
or passive-handoff failure.

Preferred source-level shape:

- producer writes bounded in-process state,
- consumer remaps that state into current detailed-placement objects,
- exact affected-net or journal HPWL decides commits,
- logs show producer count, consumer probes, exact accepts, exact gain, and
  runtime per pass.

### 2. Residual-Net Grouped Windows

Residual-net grouped windows are high-value when final HPWL improves because a
small set of net boundary cells cannot be repaired by independent single-cell
moves.

Preferred implementation:

- build windows from high-residual or boundary-active nets,
- include only a capped number of boundary/member cells and touched nets,
- generate legal same-row or adjacent-row targets before generic fallback,
- score the whole 2-4 cell bundle with one aggregate exact transaction,
- roll back the entire group unless the exact group delta is positive,
- protect only accepted groups in later stages.

Failure lessons:

- directly publishing many target coordinates to a broad global-swap pass can
  regress final HPWL,
- pack previews can be feasibility-starved; use failed previews as ordering or
  pricing hints rather than hard targets,
- published-but-unaccepted windows should not receive downstream protection.

For hierarchical wrapper-like cases, treat residual grouped windows as a full
producer-consumer route, not as passive handoff.  The strong form is:
legalizer residual cells/nets/components -> DPO focus import -> residual-focus
or diversity replay -> transactional row-window exact replay -> optional
mirror/final protection of accepted replay/window cells.  The alternative
strong form for the same feature class is a stable legal basin plus exact
source-edge or edge-critical DPO, selected-segment reorder, and post-reorder
exact/pair/endpoint polish.  Teacher should route at least one of these strong
forms when wrapper-like cases plateau; otherwise the result is usually a weak
guard or marker-only donor.

### 3. Resource Or Row-Segment Reservation As A Stage Donor

Reservation, interval auction, segment ownership, or scarce-row pricing can
improve legal-stage HPWL and displacement when row pressure is the bottleneck.
Treat this as a legalizer-stage donor until the DPO consumer proves it can use
the changed local order.

Preferred use:

- reserve or price only bounded high-pressure / residual-net components,
- log accepted ownership changes rather than all attempted tags,
- pass accepted reservation tags into reorder, exact move, or frontier consumer
  logic,
- promote only when after-improve and final HPWL improve on the same case.

Failure lessons:

- legal-stage HPWL wins can become final-flow losses if the resulting placement
  is DPO-hostile,
- legalizer proxy repairs that reduce `HPWLlg` but increase displacement tails
  or reduce downstream exact accepts should be demoted immediately,
- candidate ordering alone is often too weak; the consumer must use tags in
  scoring, exact probes, or acceptance.

### 4. Endpoint-Level Full-Tail Branch Scoring

When multiple improve-placement branches are plausible, score complete endpoints
from the same legalized snapshot rather than selecting a branch before the tail
and then letting a shared tail overwrite the choice.

Preferred use:

- snapshot once after legalizer closure,
- run each branch through its own accepted tail stages,
- select by canonical full-flow HPWL with legality and displacement gates,
- keep branch-local runtime and legality evidence,
- add early vetoes for branches that create transient legality damage that later
  stages merely hide.

This mechanism is strong only when the selected endpoint reduces final HPWL on
the target case.  It can be expensive, so treat it as a quality donor that may
need runtime repair, not as a default small guard or first-route mechanism.

### 5. Local Exact Closure For Row-Rich Or Locally Recoverable Designs

Target-biased local legalization, assignment repair, and two-row exact closure
are useful when the design has local recoverability and the global legalizer is
not the dominant source.

Preferred use:

- keep Diamond/local behavior as the primary legalizer or guard,
- bias only a bounded set of cells selected by net/residual evidence,
- use exact same-row or adjacent-row closure with legality and HPWL checks,
- pair with a residual/top-K DPO consumer.

This pattern is final-HPWL-effective only when the local closure plus DPO
consumer beats the same-case baseline.  It is often stable and cheap, but it can
plateau quickly.  Once it becomes guard-like, keep one protected route but let
that route spend bounded runtime on a real quality mechanism or runtime rewrite
with counters and rollback.  Move other Students to a different mechanism
family.

## Lower-Priority Or Weak Patterns

The following patterns should be recorded as evidence but should not consume a
full roster unless Teacher has a specific repair hypothesis.

- Telemetry-only or marker-only edits.  They help diagnose liveness but are not
  a mechanism.
- Guard-only continuation of an already retained elite.  Keep at most one guard
  route, and require it to test a bounded quality mechanism or runtime rewrite
  unless it is explicitly a pure-control attribution run. Do not let every
  Student preserve the same elite.
- Threshold, constant, tie-break, or pass-order tweaks without a changed
  candidate generator, scoring function, transaction, producer, or consumer.
- Passive handoff metadata that no downstream consumer probes or accepts.
- Mirror-only or orientation-only priority changes.  Use them only as
  downstream observability or protection for accepted DPO/reorder/handoff
  groups, not as a standalone optimizer or primary route family.
- Broad random-tail or repeated endpoint work without a new source-level
  mechanism, exact accept evidence, caps, and gain-rate stops.
- Directly forcing legalizer-produced targets through a broad global swap
  without exact grouped acceptance.  This can disturb a useful basin.
- Stale-source or missing-marker runs.  Treat them as non-executed evidence
  regardless of apparent HPWL.

## Teacher Routing Rule

For each round, classify the current best evidence:

- `elite guard`: clean best HPWL that needs protected continuation, plus one
  bounded quality or runtime-rewrite attempt unless used only as control;
- `stage donor`: strong legalizer or DPO stage signal, weak final flow;
- `consumer failure`: producer exists, consumer probes or accepts are weak;
- `producer failure`: DPO is live, but legalization destroys recoverability;
- `low-ROI family`: mechanism is live and correct but final movement is tiny.

Then assign routes accordingly:

- keep at most one `elite guard`,
- repair a `stage donor` by adding or rewriting the adjacent consumer,
- repair `consumer failure` with exact grouped-window, residual-net, or
  frontier-ranked DPO,
- repair `producer failure` with legalizer output shaping, local order, or
  resource/row assignment,
- abandon `low-ROI family` after one meaningful repair and switch mechanism
  family.
- when two current-case mechanisms are live and compatible, try a coherent
  source-level stack before another threshold-only or same-basin variant.
  Useful stacks include producer shaping plus DPO recoverability, compact
  handoff plus exact consumer, and source-edge/top-K scoring plus
  selected-segment reorder.

## Student Implementation Rule

A high-value patch should name the actual source mechanism it changes:

- candidate generation,
- legalizer row/resource assignment,
- residual/frontier producer,
- in-process handoff state,
- exact DPO scoring,
- transaction/rollback,
- endpoint selection,
- runtime cache/cap/parallelization.

If the patch only changes logging, thresholds, ordering labels, or protection of
already accepted cells, report it as guard or diagnosis evidence rather than a
new HPWL mechanism.
