# Optdp Stage Co-Optimization Evidence

## Agent Use

- Role: inspiration/support only. This card must not directly select a case route or override the core blueprint map.
- First map the case through `case_feature_to_mechanism_route_map.md`; then use this card only to refine a selected mechanism, risk, or failure diagnosis.
- If this card repeats a core idea, treat the core blueprint as authoritative and keep this card as background evidence.


This card summarizes reusable evidence from dense placement evolution runs at
different utilization levels.  The exact design ids are not the lesson; the
transferable lesson is that final HPWL depends on how
legalization and detailed improvement exchange useful local state while keeping
the downstream mirror/evaluation pass callable.

## Headline

Do not bet the whole flow on one stage.  The most useful evidence came from
mechanisms that:

- make legalization emit a bounded, meaningful local frontier,
- make `Optdp` consume that frontier with HPWL-aware candidate generation,
- keep exact legality and exact `DetailedHPWL` acceptance as the final guard,
- preserve downstream evaluation observability without turning mirror into the
  default optimization target.

Legal-stage HPWL improvement alone is not enough.  A legalizer can produce a
clean, low-displacement placement with better legal HPWL and still lose final
HPWL if `improve_placement_evolve` cannot exploit the local order/state it
receives.

## Beneficial Mechanisms

### Source+Target Touched-Net Predictor

Mechanism:

- legalization exports dirty rows / compaction-active rows,
- `improve_placement_evolve` builds a bounded source shortlist from dirty rows,
- candidate moves/swaps touching shortlisted source cells or dirty target rows
  are scored with touched-net bbox HPWL prediction,
- predicted large HPWL-loss trials are skipped before `tryMove` / `trySwap`,
- surviving trials still use the normal exact detailed-placement journal and
  exact `DetailedHPWL` accept/reject path.

Why it helped:

- it is a soft prefilter, not a hard legal target mask,
- it preserves target-row freedom while filtering obviously bad random trials,
- it improves final HPWL while preserving strict legality and good displacement,
- exact HPWL remains the commit guard, so predictor mistakes are bounded.

Observed mechanism pattern:

- clean final HPWL can improve when the predictor filters bad random trials,
- runtime can increase because touched-net scoring is still expensive,
- displacement should remain checked against the canonical baseline.

Future use:

- keep the soft source+target predictor,
- make the predictor cheaper,
- do not replace it with hard source-only or target-bin masks.

### Per-Net Extrema Cache And Boundary Skip

Mechanism:

- cache per-net min/max x/y, second extrema, extrema counts, pin count, and
  cached HPWL inside the improve pass,
- before rescanning a touched net, check whether the moved source/target pins
  can affect a bbox boundary,
- non-boundary touched nets return zero predicted delta without a full scan,
- accepted exact commits refresh only the touched cached boxes.

Why it helped:

- keeps the same exact HPWL guard as the touched-net predictor,
- removes millions of repeated touched-edge score operations,
- gives a small HPWL improvement and a measurable runtime reduction versus the
  previous predictor implementation.

Future use:

- borrow this cache for any deeper `Optdp` candidate search,
- avoid full-net recomputation inside candidate loops,
- use telemetry for cache builds, accepted updates, non-boundary skips, and
  touched-edge score counts.

### Boundary-Primary Source Pool With Fallback

Mechanism:

- legalization exports two source pools:
  - primary: HPWL-boundary or Stage3-active cells per dirty row,
  - fallback: the original dirty-row top-K source pool,
- `Optdp` starts from the primary pool with full dirty target-row coverage,
- if exact accept rate is too low after enough trials, it unions in fallback,
- commit validity remains exact legality plus exact HPWL acceptance.

Why it helped:

- boundary-active sources increased useful exact accepts,
- the fallback pool prevented over-pruning,
- full target-row coverage preserved the useful freedom that target masks lost,
- guarded trials and touched-edge scores dropped without hurting HPWL.

Future use:

- use boundary-primary source class as a scheduler, not as a hard exclusion,
- keep full or broad target coverage unless metrics prove a target mask helps,
- report accept rates by source class.

### Segment-Local Reservation And Compaction

Mechanism:

- build free row/segment intervals,
- reserve local legal intervals around high-pressure or overflow areas,
- compact within reserved segments,
- prioritize auction/reservation-touched cells in later legalization stages,
- optionally pass focus state to improve placement.

Why it helped:

- produced the best legal-stage HPWL signal in the high-utilization run,
- reduced row escapes and kept displacement low,
- showed that local legal order and interval reservation matter.

Limitation:

- final HPWL still lost when `Optdp` did not exploit the better legal state.
  This is a handoff failure, not proof that the legal-stage idea is bad.

Future use:

- keep segment-local compaction as a stage donor,
- pair it with a stronger `Optdp` consumer that directly uses reserved-window
  order, touched nets, and local HPWL deltas.

### Exact Tagged Micro-Pass

Mechanism:

- legalization records tagged high-pressure/micro-seed instances,
- `Optdp` generates capped same-row or near-row candidates around those tags,
- candidates use exact `DetailedHPWL::delta`,
- accepted micro moves/swaps update tag state for downstream consumers.

Why it is useful:

- it proved nonzero exact-gain local moves exist,
- it gives a concrete bridge from legalizer intent to detailed improvement and
  downstream evaluation state.

Limitation:

- running this pass before an unchanged long random tail can still timeout or
  leave the flow dominated by the generic random improver.

Future use:

- make the tagged micro-pass replace or substantially prune the low-ROI random
  tail when it accepts enough moves,
- send accepted tagged instances directly into the improve-placement consumer
  or another explicitly reopened downstream stage.

## Harmful Or Non-Promotable Mechanisms

### Hard Target Masks And Source-Only Pruning

Hard masks saved work but removed useful target rows and reduced HPWL recovery.
Use soft prediction and fallback pools instead.

### Cross-Row Moves Without Local HPWL Proof

Broad row-flow or overflow-credit movement can open net boxes badly.  Cross-row
or cross-segment moves need affected-net HPWL/slack proof and tight conflict
pricing before they change legal targets.

### Anchor Target Rewrites Toward Peer Targets

Rewriting legal targets toward peer/anchor targets raised legal HPWL badly in
the dense run.  This is a negative pattern: preserve useful legal state; do not
blindly pull cells toward donor anchors.

### Passive Tags Without A Strong Consumer

Recording legalizer tags is not enough.  If tags only weakly bias a normal
endpoint or downstream ordering, the generic detailed-improvement path
dominates and the mechanism has little final effect.

### Exact Micro-Pass Plus Full Random Tail

An exact micro-pass can find real gain, but if the full random improver still
runs afterward, runtime and final behavior are dominated by the random tail.
The mechanism should either replace, gate, or sharply prune that tail.

### Stale Or Source-Inactive Metrics

Several attempted mechanisms produced source code with plausible telemetry, but
the evaluator logs showed older pipeline strings or missing source-active
markers.  Treat those results as non-promotable.  Canonical logs and
`metrics.json:hpwl_stages` are the truth; source claims are not enough.

### Public ABI/Layout Changes

Changing public `Opendp` layout or Tcl/SWIG-facing ABI caused clean-link
fragility.  Keep evolved state private to `dpl_evolve`, keyed by internal
objects or private registries.

## Improve Placement Source Optimization Directions

These are source-level directions for `improve_placement_evolve`.  They are
intended to improve quality and runtime together; do not treat them as a
license to repeat unchanged detailed-placement passes.

### High-Priority Runtime Fixes

- `DetailedHPWL::delta()` should read affected edges by const reference, clear
  its local affected-edge list per delta evaluation, and clear the list on
  reject.  The hot path should not copy a `std::set` or carry rejected-edge
  history into later accepts.
- `Journal` should avoid `std::set<Node*>`, `std::set<Edge*>`, and heap
  allocation per move in the inner loop.  Prefer vectors plus node/edge
  timestamp marks, and compact POD move records for the common move-cell path.
- Move generation should add a dry-run legality / HPWL screen before mutating
  grid and segment state.  Commit to `tryMove` / `trySwap` only after a
  candidate has a plausible exact local gain.
- Destination segment lookup should be indexed per row.  Repeated linear scans
  through row segments and repeated `std::find` calls for segment cell indices
  are not acceptable in high-frequency swap/move loops.

Primary source handles:

- `src/objective/detailed_hpwl.cxx`: `DetailedHPWL::delta`,
  `DetailedHPWL::accept`
- `src/util/journal.h`: `Journal`
- `src/optimization/detailed_manager.cxx`: `tryMove`, `trySwap1`,
  `addToMoveList`, `paintInGrid`

### Quality-Oriented Candidate Generation

- Build a per-net bbox/extrema cache with first/second min/max x/y, owners,
  counts, pin count, and cached HPWL.  Use it for excluding the moved cell in
  O(1), and update only affected nets after accepted moves.
- Replace single target-point global/vertical moves with a small exact-scored
  target set: median-box center, bbox boundaries, nearest legal slots, same-row
  neighbor gaps, and a bounded row band around the target.  Exact local HPWL
  should choose among these targets before any grid mutation.
- Random improvement should consume a damaged-net/cell priority queue rather
  than all movable cells.  Useful seed classes include legalizer-dirty rows,
  high HPWL-growth nets, high displacement cells, boundary-active cells, and
  failed global/vertical neighbors.
- Fixed random windows are weak.  Window size should come from damaged-net
  span, local whitespace, row utilization, and displacement budget; random
  moves should act as bounded escape, not as the main optimizer.

Primary source handles:

- `src/optimization/detailed_global.cxx`: `getRange`,
  `calculateEdgeBB`, `generateWirelengthOptimalMove`
- `src/optimization/detailed_vertical.cxx`: `getRange`,
  `calculateEdgeBB`, `generate`
- `src/optimization/detailed_random.cxx`: `collectCandidates`,
  `RandomGenerator::generate`, `DetailedRandom::go`
- `src/optimization/detailed_mis.cxx`: `getHpwl`, `solveMatch`

### Reorder And Stage Handoff

- Reorder should dry-run permutations using temporary x positions and commit
  only the best legal permutation.  Painting the grid for every permutation is
  too expensive for a pass that should be a high-value local HPWL recovery
  stage.
- Reorder window coverage should be checked carefully.  Because row windows
  are inclusive, the loop must not skip a run whose length exactly equals the
  window size.
- If dry-run reorder becomes cheap, larger windows can be used only on
  high-value windows selected by damaged nets or residual cells.
- `Optdp` should receive a legalization handoff: dirty rows, HPWL-growth nets,
  boundary-active cells, residual/overflow cells, and row-assignment changes.
  Detailed improvement should spend effort on this frontier before falling back
  to broad cleanup.

Primary source handles:

- `src/optimization/detailed_reorder.cxx`: `run`, `reorder`, `cost`
- `src/Optdp.cpp`: `Opendp::improvePlacement`

### Co-Optimization And Runtime Control

- Candidate scoring should focus on legalizer-to-improve-placement handoff:
  move targets, local order, orientation if already represented in the detailed
  placement state, and exact affected-net deltas.
- Stage allocation should use stage-wise attribution: legal HPWL, after-improve
  HPWL, final HPWL, runtime, accepted moves, and touched-net scoring cost.
- Runtime should be spent on exact-scored mechanisms, reusable caches, and
  handoff/frontier-targeted search. Repeated or randomized work is promotable
  only after it becomes a scoped source mechanism with counters, early-stop
  rules, and clear HPWL-stage evidence.
- OpenMP is appropriate for parallel candidate scoring, net-cache build, and
  dry-run evaluation.  Do not parallelize direct grid/segment mutation unless
  the commit protocol is explicitly serialized or conflict-checked.

### GPU-DPO / LSMC Donor Direction

The cached GPU-DPO paper adds a useful DPO-specific donor:

- strong descent should include MIS, global swap, local reorder, and flipping
  with efficient local HPWL deltas,
- a large-step escape can perturb the best legal placement with a bounded ratio
  of legal random swaps, then rerun descent,
- only keep the kicked solution if canonical HPWL improves,
- stop after a bounded number of failed kicks,
- paper-reported defaults to start from are reorder window size `3`, MIS
  problem size `64`, LSMC kick ratio `0.10`, and failure tolerance `5`.

For this repo, first try a CPU-only version after the current experiments
finish.  The key is to optimize the detailed-placement descent kernels before
adding kicks.  LSMC should be a controlled basin-escape mechanism, not blind
random trial-and-error.

## Teacher Guidance

When reviewing a round:

- do stage-wise attribution first: legal HPWL, after-improve HPWL, final HPWL,
  runtime, and legality,
- preserve legal-stage donors even when final HPWL is weak,
- require at least one route to repair `Optdp` directly when after-improve
  recovery is the failing stage,
- ask for exact function/state insertion points in `Optdp`, `DetailedHPWL`,
  detailed candidate queues, or legalization-to-improve handoff state,
- separate "quality signal" from "efficient donor"; slow random-tail gains are
  only clues until bounded.

## Student Guidance

When editing `Optdp`:

- change candidate generation, ordering, objective, acceptance policy, or local
  caches; do not only change the endpoint script,
- precompute touched nets / local boxes and reuse them,
- keep exact `DetailedHPWL` and legal move/swap checks as commit guards,
- use compact candidate queues and source-class telemetry,
- avoid full-cell/full-net rescans inside inner loops,
- allocate runtime budget to one controlled new mechanism with scope, counters, and
  a clear stop condition; use handoff/frontier signals when they can reduce
  random search.

## Source-Only Improve-Placement Closure Evidence

This evidence comes from high-density standard-cell placements and uses
canonical OpenROAD/DPL pin-based HPWL.  The reusable lesson is about the
detailed-placement handoff ceiling.

### What was source-compliant

- The endpoint script in `Optdp.cpp` stayed unchanged.
- Reorder was changed in source to use the already implemented window-5
  dry-run/offset traversal path by default.
- The reorder net skip threshold was restored to `100`; full-net scoring gave
  only a tiny gain for extra cost and should not be promoted.
- MIS, global swap, vertical swap, reorder, and random kernels now honor the
  existing `-t 0.005` script tolerance instead of silently clamping to `0.01`.
- A guided-random source experiment was removed from default behavior because
  it made final HPWL worse and added runtime.

### Mechanism outcome

| mechanism | final HPWL behavior | runtime behavior | decision |
|---|---:|---:|---|
| old evolve default | weak source baseline | fast | baseline only |
| guided random from median region | worse than no-guided | slower | reject |
| reorder window-5 source default | clear final gain vs evolve default | near Diamond runtime | keep as source donor |
| tolerance honoring | additional small final gain | moderate runtime increase | keep, but not enough |
| prior endpoint portfolio | near-Diamond final HPWL | much slower and script-driven | use as ceiling clue, not as promotion |

Reusable ceiling after this pass:

- Source-compliant changes clearly improved final HPWL versus the old evolve
  default on dense placements.
- The best source-compliant result still trailed the strong local-greedy
  baseline, so legalization quality alone was not the full answer.
- A prior script-portfolio run came closer to the strong local-greedy baseline,
  which says the remaining gap is bounded portfolio behavior inside
  `improve_placement_evolve`, not just LEGALM legalization quality.

### Reusable conclusions

- Do not add environment-variable switches in source to hide experimental
  behavior.  Either make the algorithm correct by default or wire a real Tcl /
  parameter interface for controlled ablations.
- A legal-stage improvement can be real and still fail final HPWL if the DPO
  descent cannot exploit it.  Stage-wise attribution must stay mandatory.
- Reorder coverage, dry-run scoring, and tolerance honoring are worthwhile
  maintenance fixes, but they do not create the 5% class improvement alone.
- The next upper-bound test should implement the useful portfolio behavior as
  bounded source logic: one stronger descent controller, reusable local HPWL
  cache, exact-scored GS/VS/Reorder candidate sets, and a controlled handoff
  from legalization into improve placement.  Do not claim the prior portfolio
  by expanding `dtParams.script`.

## Round Hygiene

- Do not encode local round directories or benchmark ids in default knowledge.
- If several experiments share a similar name, merge them by feature class and
  mechanism rather than by string prefix.
- Treat source+target touched-net predictors, extrema caches, boundary-primary
  source pools, segment-local reservation/compaction, and exact tagged
  micro-passes as mechanism families.  Teacher should ask which feature they
  address and whether strict stage-wise metrics prove the mechanism is live.
